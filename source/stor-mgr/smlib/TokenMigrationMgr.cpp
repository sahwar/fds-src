/*
 * Copyright 2013-2014 Formation Data Systems, Inc.
 */

#include <vector>
#include <object-store/SmDiskMap.h>
#include <TokenMigrationMgr.h>

namespace fds {

SmTokenMigrationMgr::SmTokenMigrationMgr(SmIoReqHandler *dataStore)
        : smReqHandler(dataStore),
          clientLock("Migration Client Map Lock") {
    migrState = ATOMIC_VAR_INIT(MIGR_IDLE);
    nextExecutorId = ATOMIC_VAR_INIT(0);

    snapshotRequest.io_type = FDS_SM_SNAPSHOT_TOKEN;
    snapshotRequest.smio_snap_resp_cb = std::bind(&SmTokenMigrationMgr::smTokenMetadataSnapshotCb,
                                                  this,
                                                  std::placeholders::_1,
                                                  std::placeholders::_2,
                                                  std::placeholders::_3,
                                                  std::placeholders::_4);
}

SmTokenMigrationMgr::~SmTokenMigrationMgr() {
}

/**
 * Handles start migration message from OM.
 * Creates MigrationExecutor object for each <SM token, source SM> pair
 * which initiate token migration
 */
Error
SmTokenMigrationMgr::startMigration(fpi::CtrlNotifySMStartMigrationPtr& migrationMsg,
                                    fds_uint32_t bitsPerDltToken) {
    Error err(ERR_OK);
    // it's strange to receive empty message from OM, but ok we just ignore that
    if (migrationMsg->migrations.size() == 0) {
        LOGWARN << "We received empty migrations message from OM, nothing to do";
        return err;
    }

    // TODO(Sean:)  Need to disable GarbageCollection here, before
    //              we can proceed with migration.

    // we need to do migration, switch to 'in progress' state
    MigrationState expectState = MIGR_IDLE;
    if (!std::atomic_compare_exchange_strong(&migrState, &expectState, MIGR_IN_PROGRESS)) {
        LOGNOTIFY << "startMigration called in non-idle state " << migrState;
        return ERR_NOT_READY;
    }

    // create migration executors for each <SM token, source SM> pair
    for (auto migrGroup : migrationMsg->migrations) {
        // migrGroup is <source SM, set of DLT tokens> pair
        NodeUuid srcSmUuid(migrGroup.source);
        LOGNORMAL << "Will migrate tokens from source SM " << std::hex
                  << srcSmUuid.uuid_get_val() << std::dec;
        // iterate over DLT tokens for this source SM
        for (auto dltTok : migrGroup.tokens) {
            fds_token_id smTok = SmDiskMap::smTokenId(dltTok);
            LOGNORMAL << "Source SM " << std::hex << srcSmUuid.uuid_get_val() << std::dec
                      << " DLT token " << dltTok << " SM token " << smTok;
            // if we don't know about this SM token and source SM, create migration executor
            if ((migrExecutors.count(smTok) == 0) ||
                (migrExecutors.count(smTok) > 0 && migrExecutors[smTok].count(srcSmUuid) == 0)) {
                LOGNORMAL << "Will create migration executor class";
                fds_uint64_t eId = std::atomic_fetch_add(&nextExecutorId, (fds_uint64_t)1);
                migrExecutors[smTok][srcSmUuid] = MigrationExecutor::unique_ptr(
                    new MigrationExecutor(smReqHandler,
                                          bitsPerDltToken,
                                          srcSmUuid,
                                          smTok, eId,
                                          std::bind(
                                              &SmTokenMigrationMgr::migrationExecutorDoneCb, this,
                                              std::placeholders::_1, std::placeholders::_2,
                                              std::placeholders::_3)));
            }
            // tell migration executor that it is responsible for this DLT token
            migrExecutors[smTok][srcSmUuid]->addDltToken(dltTok);
        }
    }

    // for now we will do one SM token at a time
    // take first SM token from the migrExecutors map and queue qos msg to create
    // a snapshot of SM token metadata
    MigrExecutorMap::const_iterator cit = migrExecutors.cbegin();
    fds_verify(cit != migrExecutors.cend());
    startSmTokenMigration(cit->first);
    return err;
}

void
SmTokenMigrationMgr::startSmTokenMigration(fds_token_id smToken) {
    smTokenInProgress = smToken;
    LOGNORMAL << "Starting migration for SM token " << smToken;

    // enqueue snapshot work
    snapshotRequest.token_id = smTokenInProgress;
    Error err = smReqHandler->enqueueMsg(FdsSysTaskQueueId, &snapshotRequest);
    if (!err.ok()) {
        LOGERROR << "Failed to enqueue index db snapshot message ;" << err;
        // for now, we are failing the whole migration on any error
        abortMigration(err);
    }
}

/**
 * Callback whith SM token snapshot
 */
void
SmTokenMigrationMgr::smTokenMetadataSnapshotCb(const Error& error,
                                               SmIoSnapshotObjectDB* snapRequest,
                                               leveldb::ReadOptions& options,
                                               leveldb::DB *db) {
    Error err(ERR_OK);

    // must match sm token id that is currently in progress
    fds_verify(snapRequest->token_id == smTokenInProgress);
    fds_verify(migrExecutors.count(smTokenInProgress) > 0);

    // on error, we just stop the whole migration process
    if (!error.ok()) {
        LOGERROR << "Failed to get index db snapshot for SM token " << smTokenInProgress;
        abortMigration(error);
        return;
    }

    // pass this snapshot to all migration executors that are responsible for
    // migrating DLT tokens that belong to this SM token
    for (SrcSmExecutorMap::const_iterator cit = migrExecutors[smTokenInProgress].cbegin();
         cit != migrExecutors[smTokenInProgress].cend();
         ++cit) {
        err = cit->second->startObjectRebalance(options, db);
        if (!err.ok()) {
            LOGERROR << "Failed to start object rebalance for SM token "
                     << smTokenInProgress << ", source SM " << std::hex
                     << (cit->first).uuid_get_val() << std::dec << " " << err;
            break;  // we are going to abort migration
        }
    }

    // done with the snapshot
    db->ReleaseSnapshot(options.snapshot);

    if (!err.ok()) {
        abortMigration(err);
    }

    // TODO(Anna) start snapshot of the next SM token when we finish
    // migration of all DLT tokens belonging to this SM token
}

/**
 * Handle start object rebalance from destination SM
 */
Error
SmTokenMigrationMgr::startObjectRebalance(fpi::CtrlObjectRebalanceFilterSetPtr& rebalSetMsg,
                                          const fpi::SvcUuid &executorSmUuid,
                                          fds_uint32_t bitsPerDltToken) {
    Error err(ERR_OK);
    LOGDEBUG << "Object Rebalance Initial Set executor SM Id " << std::hex
             << executorSmUuid.svc_uuid << std::dec << " executor ID "
             << rebalSetMsg->executorID << " seqNum " << rebalSetMsg->seqNum
             << " last " << rebalSetMsg->lastFilterSet;

    MigrationClient::shared_ptr migrClient;
    int64_t executorId = rebalSetMsg->executorID;
    {
        fds_mutex::scoped_lock l(clientLock);
        if (migrClients.count(executorId) == 0) {
            // first time we see a message for this executor ID
            NodeUuid executorNodeUuid(executorSmUuid);
            migrClient = MigrationClient::shared_ptr(new MigrationClient(smReqHandler,
                                                                         executorNodeUuid,
                                                                         bitsPerDltToken));
            migrClients[executorId] = migrClient;
        } else {
            migrClient = migrClients[executorId];
        }
    }
    // message contains DLTToken + {<objects + refcnt>} + seqNum + lastSetFlag.
    migrClient->migClientAddObjectSet(rebalSetMsg);
    return err;
}

/**
 * Ack from source SM when it receives the whole initial set of
 * objects.
 */
Error
SmTokenMigrationMgr::startObjectRebalanceResp() {
    Error err(ERR_OK);
    LOGDEBUG << "";
    return err;
}

/**
 * Handle rebalance delta set at destination from the source
 */
Error
SmTokenMigrationMgr::recvRebalanceDeltaSet(fpi::CtrlObjectRebalanceDeltaSetPtr& deltaSet) {
    Error err(ERR_OK);
    fds_uint64_t executorId = deltaSet->executorID;

    // since we are doing one SM token at a time, search for executor in deltaSet
    fds_bool_t found = false;
    for (SrcSmExecutorMap::const_iterator cit = migrExecutors[smTokenInProgress].cbegin();
         cit != migrExecutors[smTokenInProgress].cend();
         ++cit) {
        if (cit->second->getId() == executorId) {
            found = true;
            err = cit->second->applyRebalanceDeltaSet(deltaSet);
        }
    }
    fds_verify(found);   // did we start doing more than one SM token at a time?
    return err;
}

/**
 * Ack from destination for rebalance delta set message
 */
Error
SmTokenMigrationMgr::rebalanceDeltaSetResp() {
    Error err(ERR_OK);
    LOGDEBUG << "";
    return err;
}

void
SmTokenMigrationMgr::migrationExecutorDoneCb(fds_uint64_t executorId,
                                             fds_token_id smToken,
                                             const Error& error) {
    LOGNORMAL << "Migration executor " << executorId << " finished migration "
              << error;

    fds_verify(smToken == smTokenInProgress);

    MigrationState curState = atomic_load(&migrState);
    if (curState == MIGR_ABORTED) {
        // migration already stopped, don't do anything..
        return;
    }
    fds_verify(curState == MIGR_IN_PROGRESS);

    // beta2: stop the whole migration process on any error
    if (!error.ok()) {
        abortMigration(error);
        return;
    }

    // check if we there are other executors that need to start migration
    MigrExecutorMap::const_iterator it = migrExecutors.find(smTokenInProgress);
    fds_verify(it != migrExecutors.end());
    // if we are done migration for all executors migrating current SM token,
    // start executors for the next SM token (if any)
    fds_bool_t finished = true;
    for (SrcSmExecutorMap::const_iterator cit = migrExecutors[smTokenInProgress].cbegin();
         cit != migrExecutors[smTokenInProgress].cend();
         ++cit) {
        if (!cit->second->isDone()) {
            finished = false;
            break;
        }
    }

    // migrate next SM token or we are done
    if (finished) {
        ++it;
        if (it != migrExecutors.end()) {
            // we have more SM tokens to migrate
            startSmTokenMigration(it->first);
        } else {
            /// we are done migrating, reply to start migration msg from OM
            // TODO(Anna) revisit this when doing active IO
            MigrationState expectState = MIGR_IN_PROGRESS;
            if (!std::atomic_compare_exchange_strong(&migrState, &expectState, MIGR_DONE)) {
                fds_panic("Unexpected migration executor state!");
            }
        }
    }
}

/**
 * Handles DLT close event. At this point IO should not arrive
 * with old DLT. Once we reply, we are done with token migration.
 * Assumes we are receiving DLT close event for the correct version,
 * caller should check this
 */
Error
SmTokenMigrationMgr::handleDltClose() {
    Error err(ERR_OK);
    // for now, to make sure we can handle another migration...
    MigrationState expectState = MIGR_IN_PROGRESS;
    if (!std::atomic_compare_exchange_strong(&migrState, &expectState, MIGR_IDLE)) {
        LOGNOTIFY << "startMigration called in non- in progress state " << migrState;
        return ERR_OK;  // this is ok
    }
    LOGDEBUG << "";
    return err;
}

void
SmTokenMigrationMgr::abortMigration(const Error& error) {
    LOGNOTIFY << "Aborting token migration " << error;
    if (atomic_load(&migrState) == MIGR_ABORTED) {
        LOGNOTIFY << "Migration already aborted, nothing else to do";
        return;
    }

    // set state to 'aborted'
    MigrationState newState = MIGR_ABORTED;
    atomic_store(&migrState, newState);

    // TODO(Anna) depending on current state, send ack with error to OM
    // and do any necessary cleanup here...

    migrExecutors.clear();
}

}  // namespace fds
