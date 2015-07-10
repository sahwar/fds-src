/*
 * Copyright 2015 Formation Data Systems, Inc.
 */

#include <DataMgr.h>
#include <DmIoReq.h>
#include <DmMigrationMgr.h>

namespace fds {

DmMigrationMgr::DmMigrationMgr(DmIoReqHandler *DmReqHandle, DataMgr& _dataMgr)
    : DmReqHandler(DmReqHandle), dataManager(_dataMgr), OmStartMigrCb(NULL),
	  maxConcurrency(1), firedMigrations(0), mit(NULL),
	  migrState(MIGR_IDLE), cleanUpInProgress(false)
{
	maxConcurrency = fds_uint32_t(MODULEPROVIDER()->get_fds_config()->
				get<int>("fds.dm..migration.migration_max_concurrency"));

    enableMigrationFeature = bool(MODULEPROVIDER()->get_fds_config()->
                get<bool>("fds.dm.migration.enable_feature"));

    enableResyncFeature = bool(MODULEPROVIDER()->get_fds_config()->
                get<bool>("fds.dm.migration.enable_resync"));
}


DmMigrationMgr::~DmMigrationMgr()
{

}


Error
DmMigrationMgr::createMigrationExecutor(const NodeUuid& srcDmUuid,
										fpi::FDSP_VolumeDescType &vol,
										MigrationType& migrationType,
										const fds_bool_t& autoIncrement)
{
	Error err(ERR_OK);

    SCOPEDWRITE(migrExecutorLock);
	/**
	 * Make sure that this isn't an ongoing operation.
	 * Otherwise, OM bug?
	 */
	auto search = executorMap.find(fds_volid_t(vol.volUUID));
	if (search != executorMap.end()) {
		LOGMIGRATE << "Migration for volume " << vol.vol_name << " is a duplicated request.";
		err = ERR_DUPLICATE;
	} else {
		/**
		 * Create a new instance of migration Executor
		 */
		LOGMIGRATE << "Creating migration instance for volume id=: " << vol.volUUID
                   << " name=" << vol.vol_name;

		executorMap.emplace(fds_volid_t(vol.volUUID),
				            DmMigrationExecutor::unique_ptr(new DmMigrationExecutor(dataManager,
														                            srcDmUuid,
                                                                                    vol,
																					autoIncrement,
														                            std::bind(&DmMigrationMgr::migrationExecutorDoneCb,
																                              this,
                                                                                              std::placeholders::_1,
																                              std::placeholders::_2))));
	}
	return err;
}

Error
DmMigrationMgr::startMigrationExecutor(dmCatReq* dmRequest)
{
	Error err(ERR_OK);

	NodeUuid mySvcUuid(MODULEPROVIDER()->getSvcMgr()->getSelfSvcUuid().svc_uuid);
	NodeUuid destSvcUuid;
    DmIoMigration* typedRequest = static_cast<DmIoMigration*>(dmRequest);
    fpi::CtrlNotifyDMStartMigrationMsgPtr migrationMsg = typedRequest->message;
	OmStartMigrCb = typedRequest->localCb;
	fds_bool_t autoIncrement = false;
	fds_bool_t loopFireNext = true;
	NodeUuid srcDmSvcUuid;

    // Check if the migraion feature is enabled or disabled.
    if (false == enableMigrationFeature) {
        LOGCRITICAL << "DM Migration is disabled! ignoring start migration msg";
        if (OmStartMigrCb) {
	        OmStartMigrCb(ERR_OK);
        }
        return err;
    }

	// TODO(Neil) fix this
	MigrationType localMigrationType(MIGR_DM_ADD_NODE);

	/**
	 * Make sure we're in a state able to dispatch migration.
	 */
	err = activateStateMachine();

	if (err != ERR_OK) {
		return err;
	}

	firedMigrations = 0;
	for (std::vector<fpi::DMVolumeMigrationGroup>::iterator vmg = migrationMsg->migrations.begin();
		 vmg != migrationMsg->migrations.end();
         ++vmg) {

		for (std::vector<fpi::FDSP_VolumeDescType>::iterator vdt = vmg->VolDescriptors.begin();
				vdt != vmg->VolDescriptors.end(); ++vdt) {
			/**
			 * If this is the last executor to be fired, anything from this point on should
			 * have the autoIncrement flag set.
			 */
			fds_verify(maxConcurrency > 0);
			++firedMigrations;
			if (firedMigrations == maxConcurrency) {
				autoIncrement = true;
				// from this point on, all the other executors must have autoIncrement == TRUE
			}
			srcDmSvcUuid.uuid_set_val(vmg->source.svc_uuid);

            LOGMIGRATE << "Pull Volume ID: " << vdt->volUUID
                       << " Name: " << vdt->vol_name
                       << " from SrcDmSvcUuid: " << vmg->source.svc_uuid;

			err = createMigrationExecutor(srcDmSvcUuid,
                                          *vdt,
									      localMigrationType,
										  autoIncrement);
			if (!err.OK()) {
				LOGERROR << "Error creating migrating task.  err=" << err;
				return err;
			}
		}
	}

	/**
	 * For now, execute one at a time. Increase parallelism by changing platform.conf
	 * Note: If we receive more request than max_tokens is allowed, this method will
	 * block. That's ok, because we want to make sure that all the requested migrations
	 * coming from the OM at least have an executor on the src and client on the dest
	 * sides communicate with each other before we allow the call to unblock and returns
	 * a OK response.
	 * TODO(Neil) - the "startMigration()" method is not optimized for multithreading.
	 * We need to revisit this and work with the maxConcurrency concept to start x concurrent
	 * threads.
	 */
	SCOPEDWRITE(migrExecutorLock);
	mit = executorMap.begin();
	while (loopFireNext && (mit != executorMap.end())) {
		loopFireNext = !(mit->second->shouldAutoExecuteNext());
		mit->second->startMigration();
		if (isMigrationAborted()) {
			/**
			 * Abort everything
			 */
			err = ERR_DM_MIGRATION_ABORTED;
			/**
			 * TODO(Neil) -
			 * Right now we only support DM Add node migration that is initiated by the OM.
			 * In the future, when we have multiple ways to do migration, we may have
			 * sub sets of executors. This executorMap blow-away will not work then.
			 * So once that happens, write a new error handling method to handle removing
			 * the correct subset of executors from the executor map.
			 */
			executorMap.clear();
			break;
		} else if (loopFireNext) {
			mit++;
		}
	}
	return err;
}

void
DmMigrationMgr::ackMigrationComplete(const Error &status)
{
	fds_verify(OmStartMigrCb != NULL);
	LOGMIGRATE << "Telling OM migration is done";
	OmStartMigrCb(status);
	executorMap.clear();
}

// See note in header file for design decisions
Error
DmMigrationMgr::startMigrationClient(dmCatReq* dmRequest)
{
	Error err(ERR_OK);
	NodeUuid mySvcUuid(MODULEPROVIDER()->getSvcMgr()->getSelfSvcUuid().svc_uuid);
    DmIoResyncInitialBlob* typedRequest = static_cast<DmIoResyncInitialBlob*>(dmRequest);
	NodeUuid destDmUuid(typedRequest->destNodeUuid);
    fpi::CtrlNotifyInitialBlobFilterSetMsgPtr migReqMsg = typedRequest->message;

	LOGMIGRATE << "received msg for volume " << migReqMsg->volumeId;

	err = createMigrationClient(destDmUuid, mySvcUuid, migReqMsg);

	return err;
}

Error
DmMigrationMgr::createMigrationClient(NodeUuid& destDmUuid,
									  const NodeUuid& mySvcUuid,
									  fpi::CtrlNotifyInitialBlobFilterSetMsgPtr& filterSet)
{
	Error err(ERR_OK);
	/**
	 * Make sure that this isn't an ongoing operation.
	 * Otherwise, DM bug
	 */
	auto fds_volid = fds_volid_t(filterSet->volumeId);
	auto search = clientMap.find(fds_volid);
	DmMigrationClient::shared_ptr client = nullptr;
	if (search != clientMap.end()) {
		LOGMIGRATE << "Client received request for volume " << filterSet->volumeId
				<< " but it already exists";
		err = ERR_DUPLICATE;
	} else {
		/**
		 * Create a new instance of client and start it.
		 */
		migrClientLock.write_lock();
		LOGMIGRATE << "Creating migration client for volume ID# " << fds_volid;
		clientMap.emplace(fds_volid,
							(client = DmMigrationClient::shared_ptr(new DmMigrationClient(DmReqHandler, dataManager,
															mySvcUuid, destDmUuid, filterSet,
															std::bind(&DmMigrationMgr::migrationClientDoneCb,
															this, std::placeholders::_1,
															std::placeholders::_2)))));
		migrClientLock.write_unlock();

		client->handleInitialBlobFilterMsg();
	}

	return err;
}

Error
DmMigrationMgr::activateStateMachine()
{
	Error err(ERR_OK);

	MigrationState expectedState(MIGR_IDLE);

	if (!std::atomic_compare_exchange_strong(&migrState, &expectedState, MIGR_IN_PROGRESS)) {
		// If not ABORTED state, then something wrong with OM FSM sending this migration msg
		fds_verify(isMigrationAborted());
		if (isMigrationAborted()) {
			/**
			 * If migration is in aborted state, don't do anything. Let the cleanup occur.
			 * Once the cleanup is done, the state should go back to MIGR_IDLE.
			 * In the meantime, do not serve anymore migrations.
			 */
			fds_verify(cleanUpInProgress);
			LOGMIGRATE << "DM MigrationMgr cannot receive new requests as it is "
					<< "currently in an error state and being cleaned up.";
			err = ERR_NOT_READY;
		}
	} else {
		/**
		 * Migration should be idle
		 */
		fds_verify(ongoingMigrationCnt() == 0);
	}
	return err;
}

void
DmMigrationMgr::migrationExecutorDoneCb(fds_volid_t volId, const Error &result)
{
	MigrationState expectedState(MIGR_IN_PROGRESS);
	/**
	 * Make sure we can find it
	 */
	DmMigrationExecMap::iterator mapIter = executorMap.find(volId);
	fds_verify(mapIter != executorMap.end());

	/**
	 * TODO(Neil): error handling
	 */
	if (!result.OK()) {
		fds_verify(isMigrationInProgress() || isMigrationAborted());
		fds_verify(std::atomic_compare_exchange_strong(&migrState, &expectedState, MIGR_ABORTED));
		LOGERROR << "Volume=" << volId << " failed migration with error: " << result;
	} else {
		/**
		 * Normal exit. Really doesn't do much as we're waiting for the clients to come back.
		 */
		/**
		 * TODO(Neil):
		 * This will be moved to the callback when source client finishes and
		 * talks to the destination manager.
		 * Also, we're commenting this lock out because this isn't technically supposed to
		 * be here but I'm leaving the lock here to remind ourselves that lock is required.
		 */
		// SCOPEDWRITE(migrExecutorLock);
		if (mit->second->shouldAutoExecuteNext()) {
			++mit;
			if (mit != executorMap.end()) {
				mit->second->startMigration();
			} else {
				ackMigrationComplete(result);
			}
		}
	}
}

void
DmMigrationMgr::migrationClientDoneCb(fds_volid_t uniqueId, const Error &result)
{
	// SCOPEDWRITE(migrClientThrMapLock);
	SCOPEDWRITE(migrClientLock);
	LOGMIGRATE << "Client done with volume " << uniqueId;
	clientMap.erase(fds_volid_t(uniqueId));
}

}  // namespace fds
