
/*
 * Copyright 2013 Formation Data Systems, Inc.
 */
#include <map>
#include <string>
#include <set>
#include <list>
#include <iostream>
#include <thread>
#include <functional>

#include <fiu-control.h>
#include <fiu-local.h>
#include <PerfTrace.h>
#include <ObjMeta.h>
#include <StorMgr.h>
#include <fds_timestamp.h>
#include <net/net_utils.h>
#include <net/SvcRequestPool.h>
#include <net/SvcMgr.h>
#include <SMSvcHandler.h>
#include "PerfTypes.h"

using diskio::DataTier;

namespace fds {

#define FDS_XPORT_PROTO_TCP 1
#define FDS_XPORT_PROTO_UDP 2
#define FDSP_MAX_MSG_LEN (4*(4*1024 + 128))

ObjectStorMgr *objStorMgr;

/**
 * Storage manager member functions
 *
 * TODO: The number of test vols, the
 * totalRate, and number of qos threads
 * are being hard coded in the initializer
 * list below.
 */
ObjectStorMgr::ObjectStorMgr(CommonModuleProviderIf *modProvider)
    : Module("sm"),
      modProvider_(modProvider),
      totalRate(6000),  // will be over-written using node capability
      qosThrds(100),  // will be over-written from config
      qosOutNum(10),
      volTbl(nullptr),
      qosCtrl(nullptr),
      shuttingDown(false)
{
    // NOTE: Don't put much stuff in the constuctor.  Move any construction
    // into mod_init()
}

void ObjectStorMgr::setModProvider(CommonModuleProviderIf *modProvider) {
    modProvider_ = modProvider;
}

ObjectStorMgr::~ObjectStorMgr() {
    LOGDEBUG << " Destructing  the Storage  manager";
    if (qosCtrl) {
        delete qosCtrl;
        qosCtrl = nullptr;
        LOGDEBUG << "qosCtrl destructed...";
    }
    if (volTbl) {
        delete volTbl;
        volTbl = NULL;
        LOGDEBUG << "volTbl destructed...";
    }

    objectStore.reset();
    LOGDEBUG << "Destructed Object Store";
}

int
ObjectStorMgr::mod_init(SysParams const *const param) {
    shuttingDown = false;

    // Init  the log infra
    GetLog()->setSeverityFilter(fds_log::getLevelFromName(
        modProvider_->get_fds_config()->get<std::string>("fds.sm.log_severity")));

    testStandalone = modProvider_->get_fds_config()->get<bool>("fds.sm.testing.standalone");

    modProvider_->proc_fdsroot()->\
        fds_mkdir(modProvider_->proc_fdsroot()->dir_user_repo_objs().c_str());
    std::string obj_dir = modProvider_->proc_fdsroot()->dir_user_repo_objs();

    /*
     * Create local volume table.
     */
    volTbl = new StorMgrVolumeTable(this, GetLog());

    // create stats dir before creating ObjectStore (that sets up tier engine, etc)
    FdsRootDir::fds_mkdir(modProvider_->proc_fdsroot()->dir_fds_var_stats().c_str());

    // Init the object store
    // TODO(Andrew): The object store should be executed as part
    // of the module initialization since it needs to discover
    // any prior data and possibly perform recovery before allowing
    // another module layer to come up above it.
    objectStore = ObjectStore::unique_ptr(new ObjectStore("SM Object Store Module",
                                                          this,
                                                          volTbl));

    static Module *smDepMods[] = {
        objectStore.get(),
        NULL
    };
    mod_intern = smDepMods;
    Module::mod_init(param);
    return 0;
}

void ObjectStorMgr::mod_startup()
{
    // Init token migration manager
    migrationMgr = MigrationMgr::unique_ptr(new MigrationMgr(this));

    // qos defaults
    qosThrds = modProvider_->get_fds_config()->get<int>(
        "fds.sm.qos.default_qos_threads");

    // the default value is for minimum number; if we have more disks
    // then number of outstanding should be greater than number of disks
    qosOutNum = modProvider_->get_fds_config()->get<int>(
        "fds.sm.qos.default_outstanding_io");
    fds_uint32_t minOutstanding = objectStore->getDiskCount() + 2;
    if (minOutstanding > qosOutNum) {
        qosOutNum = minOutstanding;
    }
    // we should also have enough QoS threads to serve outstanding IO
    if (qosThrds <= qosOutNum) {
        qosThrds = qosOutNum + 1;   // one is used for dispatcher
    }

    testUturnAll    = modProvider_->get_fds_config()->get<bool>("fds.sm.testing.uturn_all");
    testUturnPutObj = modProvider_->get_fds_config()->get<bool>("fds.sm.testing.uturn_putobj");

    Module::mod_startup();
}

//
// Finishing initialization -- at this point we can access platform service
// and get info like node capabilities we need for properly configuring QoS
// control. So here we initialize everything depending on QoS control
//
void ObjectStorMgr::mod_enable_service()
{
    fiu_do_on("sm.exit.on.bringup", exit(1));

    if (modProvider_->get_fds_config()->get<bool>("fds.sm.testing.standalone") == false) {
        // note that qos dispatcher in SM/DM uses total rate just to assign
        // guaranteed slots, it still will dispatch more IOs if there is more
        // perf capacity available (based on how fast IOs return). So setting
        // totalRate to node_iops_min does not actually restrict the SM from
        // servicing more IO if there is more capacity (eg.. because we have
        // cache and SSDs)
        auto svcmgr = MODULEPROVIDER()->getSvcMgr();
        totalRate = svcmgr->getSvcProperty<fds_uint32_t>(
                modProvider_->getSvcMgr()->getMappedSelfPlatformUuid(), "node_iops_min");
    }

    /*
     * Setup QoS related members.
     */
    qosCtrl = new SmQosCtrl(this,
                            qosThrds,
                            FDS_QoSControl::FDS_DISPATCH_WFQ,
                            GetLog());
    qosCtrl->runScheduler();

    // create queue for system tasks
    sysTaskQueue = new SmVolQueue(FdsSysTaskQueueId,
                                  256,
                                  getSysTaskIopsMax(),
                                  getSysTaskIopsMin(),
                                  getSysTaskPri());

    qosCtrl->registerVolume(FdsSysTaskQueueId,
                            sysTaskQueue);

    if (modProvider_->get_fds_config()->get<bool>("fds.sm.testing.standalone") == false) {
        gSvcRequestPool->setDltManager(MODULEPROVIDER()->getSvcMgr()->getDltManager());

        /**
         * This is post service registration. Check if object store passed the initial
         * superblock validation.
         */
        if (objectStore->isUnavailable()) {
            LOGCRITICAL << "First phase of object store initialization failed; "
                        << "SM service is marked unavailable";
            // TODO(Anna) we should at some point try to recover from this
            // for now SM service will remain unavailable, until someone restarts it
        } else {
            LOGNOTIFY << "SM services successfully finished first-phase of starting up;"
                      << " starting the second phase";
            // get DMT from OM if DMT already exist
            MODULEPROVIDER()->getSvcMgr()->getDMT();
            // get DLT from OM
            Error err = MODULEPROVIDER()->getSvcMgr()->getDLT();
            if (err.ok()) {
                // got a DLT, ignore it if SM is not in it
                const DLT* curDlt = MODULEPROVIDER()->getSvcMgr()->getCurrentDLT();
                if (curDlt->getTokens(objStorMgr->getUuid()).empty()) {
                    LOGDEBUG << "First DLT received does not contain this SM, ignoring...";
                } else {
                    // if second phase of start up failes, object store will set the state
                    // during validating token ownership in the superblock
                    handleDltUpdate();
                }
            }
            // else ok if no DLT yet
        }

        // Enable stats collection in SM for stats streaming
        StatsCollector::singleton()->setSvcMgr(MODULEPROVIDER()->getSvcMgr());
        StatsCollector::singleton()->startStreaming(
            std::bind(&ObjectStorMgr::sampleSMStats, this, std::placeholders::_1), NULL);
    }

    /*
     * Create local variables for test mode
     */
    if (modProvider_->get_fds_config()->get<bool>("fds.sm.testing.test_mode") == true) {
        /*
         * Create test volumes.
         */
        VolumeDesc*  testVdb;
        std::string testVolName;
        int numTestVols = modProvider_->get_fds_config()->\
                          get<int>("fds.sm.testing.test_volume_cnt");
        for (fds_int32_t testVolId = 1; testVolId < numTestVols + 1; testVolId++) {
            testVolName = "testVol" + std::to_string(testVolId);
            /*
             * We're using the ID as the min/max/priority
             * for the volume QoS.
             */
            /* high max iops so that unit tests does not take forever to finish */
            testVdb = new VolumeDesc(testVolName,
                                     testVolId,
                                     8+ testVolId,
                                     10000,
                                     testVolId);
            fds_assert(testVdb != NULL);
            testVdb->volType = FDSP_VOL_BLKDEV_TYPE;
            if ( (testVolId % 4) == 0)
                testVdb->mediaPolicy = FDSP_MEDIA_POLICY_HDD;
            else if ( (testVolId % 3) == 1)
                testVdb->mediaPolicy = FDSP_MEDIA_POLICY_SSD;
            else if ( (testVolId % 3) == 2)
                testVdb->mediaPolicy = FDSP_MEDIA_POLICY_HYBRID;
            else
                testVdb->mediaPolicy = FDSP_MEDIA_POLICY_HYBRID_PREFCAP;

            registerVolume(testVolId,
                           testVdb,
                           FDS_ProtocolInterface::FDSP_NOTIFY_VOL_NO_FLAG,
                           FDS_ProtocolInterface::FDSP_ERR_OK);

            delete testVdb;
        }
    }

    if (modProvider_->get_fds_config()->get<bool>("fds.sm.testing.standalone") == false) {
        gSvcRequestPool->setDltManager(MODULEPROVIDER()->getSvcMgr()->getDltManager());
    }

    Module::mod_enable_service();
}

void ObjectStorMgr::mod_disable_service() {
    LOGDEBUG << "Disable (shutdown) service is called on SM";
    // Setting shuttingDown will cause any IO going to the QOS queue
    // to return ERR_NOT_READY
    fds_bool_t expectShuttingDown = false;
    if (!shuttingDown.compare_exchange_strong(expectShuttingDown, true)) {
        LOGNOTIFY << "SM already started shutting down, nothing to do";
        return;
    }

    // Stop scavenger
    SmScavengerCmd *shutdownCmd = new SmScavengerCmd();
    shutdownCmd->command = SmScavengerCmd::SCAV_STOP;
    objectStore->scavengerControlCmd(shutdownCmd);
    LOGDEBUG << "Scavenger is now shut down.";

    // Disable tier migration
    SmTieringCmd tierCmd(SmTieringCmd::TIERING_DISABLE);
    objectStore->tieringControlCmd(&tierCmd);

    /*
     * Clean up the QoS system. Need to wait for I/Os to
     * complete;
     * TODO: We should prevent further volume registration
     */
    if (volTbl) {
        std::list<fds_volid_t> volIds = volTbl->getVolList();
        for (std::list<fds_volid_t>::iterator vit = volIds.begin();
             vit != volIds.end();
             vit++) {
            qosCtrl->quieseceIOs((*vit));
        }
    }
    // once we are here, enqueue msg to qos queues will return
    // not ready error == requests will be rejected
    LOGDEBUG << "Started draining volume QoS queues...";
}

void ObjectStorMgr::mod_shutdown()
{
    LOGDEBUG << "Mod shutdown called on ObjectStorMgr";
    fds_verify(shuttingDown == true);

    Module::mod_shutdown();
}

int ObjectStorMgr::run()
{
    while (true) {
        sleep(1);
    }
    return 0;
}


NodeUuid
ObjectStorMgr::getUuid() const {
    NodeUuid mySvcUuid(MODULEPROVIDER()->getSvcMgr()->getSelfSvcUuid());
    return mySvcUuid;
}

const DLT* ObjectStorMgr::getDLT() {
    if (testStandalone == true) {
        return standaloneTestDlt;
    }
    return MODULEPROVIDER()->getSvcMgr()->getCurrentDLT();
}

fds_bool_t ObjectStorMgr::amIPrimary(const ObjectID& objId) {
    if (testStandalone == true) {
        return true;  // TODO(Anna) add test DLT and use my svc uuid = 1
    }
    DltTokenGroupPtr nodes = MODULEPROVIDER()->getSvcMgr()->getDLTNodesForDoidKey(objId);
    fds_verify(nodes->getLength() > 0);
    return (MODULEPROVIDER()->getSvcMgr()->getSelfSvcUuid() == nodes->get(0).toSvcUuid());
}

Error ObjectStorMgr::handleDltUpdate() {
    // until we start getting dlt from platform, we need to path dlt
    // width to object store, so that we can correctly map object ids
    // to SM tokens
    const DLT* curDlt = MODULEPROVIDER()->getSvcMgr()->getCurrentDLT();
    Error err = objStorMgr->objectStore->handleNewDlt(curDlt);
    if (err == ERR_SM_NOERR_NEED_RESYNC) {
        LOGNORMAL << "SM received first DLT after restart, which matched "
                  << "its persistent state, will start full resync of DLT tokens";

        // Start the resync process
        if (g_fdsprocess->get_fds_config()->get<bool>("fds.sm.migration.enable_resync")) {
            err = objStorMgr->migrationMgr->startResync(curDlt,
                                                        objStorMgr->getUuid(),
                                                        curDlt->getNumBitsForToken());
        } else {
            // not doing resync, making all DLT tokens ready
            objStorMgr->migrationMgr->notifyDltUpdate(curDlt->getNumBitsForToken());
            // pretend we successfully started resync, return success
            err = ERR_OK;
        }
    } else if (err.ok()) {
        objStorMgr->migrationMgr->notifyDltUpdate(curDlt->getNumBitsForToken());
    }

    return err;
}

/*
 * Note this method register volumes outside of SMSvc
 */
Error
ObjectStorMgr::registerVolume(fds_volid_t  volumeId,
                              VolumeDesc  *vdb,
                              FDSP_NotifyVolFlag vol_flag,
                              FDSP_ResultType result) {
    StorMgrVolume* vol = NULL;
    Error err(ERR_OK);
    fds_assert(vdb != NULL);

    GLOGNOTIFY << "Received create for vol "
               << "[" << std::hex << volumeId << std::dec << ", "
               << vdb->getName() << "]";
    /*
     * Needs to reference the global SM object
     * since this is a static function.
     */
    err = objStorMgr->regVol(*vdb);
    if (err.ok()) {
        vol = objStorMgr->getVol(volumeId);
        fds_assert(vol != NULL);
        err = objStorMgr->regVolQos(vdb->isSnapshot() ?
                vdb->qosQueueId : vol->getVolId(),
                static_cast<FDS_VolumeQueue*>(vol->getQueue().get()));
        if (!err.ok()) {
            // most likely axceeded min iops
            objStorMgr->deregVol(volumeId);
        }
    }
    if (!err.ok()) {
        GLOGERROR << "Registration failed for vol id " << std::hex << volumeId
                  << std::dec << " error: " << err.GetErrstr();
    }

    return err;
}

//
// sample SM-specific stats to the collector
//
void ObjectStorMgr::sampleSMStats(fds_uint64_t timestamp) {
    LOGDEBUG << "Sampling SM stats";
    std::map<std::string, int64_t> counter_map;
    std::list<fds_volid_t> volIds = volTbl->getVolList();
    for (std::list<fds_volid_t>::iterator vit = volIds.begin();
         vit != volIds.end();
         vit++) {
        if (volTbl->isSnapshot(*vit)) {
            continue;
        }
        fds_uint64_t dedup_bytes = volTbl->getDedupBytes(*vit);
        LOGDEBUG << "Volume " << std::hex << *vit << std::dec
                 << " deduped bytes " << dedup_bytes;
        StatsCollector::singleton()->recordEvent(*vit,
                                                 timestamp,
                                                 STAT_SM_CUR_DEDUP_BYTES,
                                                 dedup_bytes);
    }
}

/* Initialize an instance specific vector of locks to cover the entire
 * range of potential sm tokens.
 *
 * Take a read (or optionally write lock) on a sm token and return
 * a structure that will automatically call the correct unlock when
 * it goes out of scope.
 */
ObjectStorMgr::always_call
ObjectStorMgr::getTokenLock(fds_token_id const& id, bool exclusive) {
    using lock_array_type = std::vector<fds_rwlock*>;
    static lock_array_type token_locks;
    static std::once_flag f;

    fds_uint32_t b_p_t = getDLT()->getNumBitsForToken();

    // Once, resize the vector appropriately on the bits in the token
    std::call_once(f,
                   [](fds_uint32_t size) {
                       token_locks.resize(0x01<<size);
                       token_locks.shrink_to_fit();
                       for (auto& p : token_locks)
                       { p = new fds_rwlock(); }
                   },
                   b_p_t);

    auto lock = token_locks[id];
    exclusive ? lock->write_lock() : lock->read_lock();
    return always_call([lock, exclusive] { exclusive ? lock->write_unlock() : lock->read_unlock(); });
}


/*------------------------------------------------------------------------- ------------
 * FDSP Protocol internal processing
 -------------------------------------------------------------------------------------*/

Error
ObjectStorMgr::putObjectInternal(SmIoPutObjectReq *putReq)
{
    Error err(ERR_OK);
    const ObjectID&  objId    = putReq->getObjId();
    fds_volid_t volId         = putReq->getVolId();

    fds_assert(volId != 0);
    fds_assert(objId != NullObjectID);

    {  // token lock
        auto token_lock = getTokenLock(objId);

        // latency of ObjectStore layer
        PerfTracer::tracePointBegin(putReq->opLatencyCtx);

        // TODO(Andrew): Remove this copy. The network should allocated
        // a shared ptr structure so that we can directly store that, even
        // after the network message is freed.
        err = objectStore->putObject(volId,
                                     objId,
                                     boost::make_shared<std::string>(putReq->putObjectNetReq->data_obj),
                                     putReq->forwardedReq);

        qosCtrl->markIODone(*putReq);

        // end of ObjectStore layer latency
        PerfTracer::tracePointEnd(putReq->opLatencyCtx);

        // forward this IO to destination SM if needed, but only if we succeeded locally
        // and was not already a forwarded request.
        // The assumption is that forward will not take  multiple hops.  It will be only from
        // one source to one destionation SM.
        if (err.ok() && (false == putReq->forwardedReq)) {
            bool forwarded = migrationMgr->forwardReqIfNeeded(objId, putReq->dltVersion, putReq);
            if (forwarded) {
                // we forwarded request, however we will ack to AM right away, and if
                // forwarded request fails, we will fail the migration
                LOGMIGRATE << "Forwarded PUT " << objId << " to destination SM ";
            }
        }
    }

    putReq->response_cb(err, putReq);
    return err;
}

Error
ObjectStorMgr::deleteObjectInternal(SmIoDeleteObjectReq* delReq)
{
    Error err(ERR_OK);
    const ObjectID&  objId    = delReq->getObjId();
    fds_volid_t volId         = delReq->getVolId();

    fds_assert(volId != 0);
    fds_assert(objId != NullObjectID);

    LOGDEBUG << "Volume " << std::hex << volId << std::dec
             << " " << objId;

    {  // token lock
        auto token_lock = getTokenLock(objId);

        // start of ObjectStore layer latency
        PerfTracer::tracePointBegin(delReq->opLatencyCtx);

        err = objectStore->deleteObject(volId,
                                        objId,
                                        delReq->forwardedReq);


        qosCtrl->markIODone(*delReq);

        // end of ObjectStore layer latency
        PerfTracer::tracePointEnd(delReq->opLatencyCtx);

        // forward this IO to destination SM if needed, but only if we succeeded locally
        // and was not already a forwarded request.
        // The assumption is that forward will not take multiple hops.  It will be only from
        // one source to one destionation SM.
        if (err.ok() && (false == delReq->forwardedReq)) {
            bool forwarded = migrationMgr->forwardReqIfNeeded(objId, delReq->dltVersion, delReq);
            if (forwarded) {
                // we forwarded request, however we will ack to AM right away, and if
                // forwarded request fails, we will fail the migration
                LOGMIGRATE << "Forwarded DELETE " << objId << " to destination SM ";
            }
        }
    }

    delReq->response_cb(err, delReq);
    return err;
}

Error
ObjectStorMgr::addObjectRefInternal(SmIoAddObjRefReq* addObjRefReq)
{
    fds_assert(0 != addObjRefReq);
    fds_assert(0 != addObjRefReq->getSrcVolId());
    fds_assert(0 != addObjRefReq->getDestVolId());

    Error rc = ERR_OK;

    if (addObjRefReq->objIds().empty()) {
        return rc;
    }

    uint64_t origNumObjIds = addObjRefReq->objIds().size();

    // start of ObjectStore layer latency
    PerfTracer::tracePointBegin(addObjRefReq->opLatencyCtx);

    for (std::vector<fpi::FDS_ObjectIdType>::iterator it = addObjRefReq->objIds().begin();
         it != addObjRefReq->objIds().end();
         ++it) {
        ObjectID oid = ObjectID(it->digest);

        // token lock
        auto token_lock = getTokenLock(oid);

        rc = objectStore->copyAssociation(addObjRefReq->getSrcVolId(),
                                          addObjRefReq->getDestVolId(),
                                              oid);
        if (!rc.ok()) {
            LOGERROR << "Failed to add association entry for object " << oid
                     << "in to odb with err " << rc;
            // Remove from the list before forwarding, if failed.
            // Will forward only the successfully applied volume association.
            // Also, only remove, if it's going to be forwarded, since we only
            // forward successful request.
            if (false == addObjRefReq->forwardedReq) {
                addObjRefReq->objIds().erase(it);
            }
        }
    }

    qosCtrl->markIODone(*addObjRefReq);
    // end of ObjectStore layer latency
    PerfTracer::tracePointEnd(addObjRefReq->opLatencyCtx);

    // TODO(Sean): not sure how to handle the error case above, since there is no
    // roll back mechsnim.  It's hard to tell what it means if there is an error
    // for this operation.  Who ever designed this need to think about this one.
    // For now, just dealing


    // forward this IO to destination SM if needed, but only if we succeeded locally
    // and was not already a forwarded request.
    // The assumption is that forward will not take multiple hops.  It will be only from
    // one source to one destionation SM.
    if ((addObjRefReq->objIds().size() > 0) && (false == addObjRefReq->forwardedReq)) {
        bool forwarded = migrationMgr->forwardReqIfNeeded(NullObjectID,
                                                          addObjRefReq->dltVersion,
                                                          addObjRefReq);
        if (forwarded) {
            // we forwarded request, however we will ack to AM right away, and if
            // forwarded request fails, we will fail the migration
            LOGMIGRATE << "Forwarded addObjRefReq: "
                       << " Original ObjIds=" << origNumObjIds
                       << " number of Objects=" << addObjRefReq->objIds().size()
                       << " to destination SM ";
        }
    }

    addObjRefReq->response_cb(rc, addObjRefReq);

    return rc;
}

Error
ObjectStorMgr::getObjectInternal(SmIoGetObjectReq *getReq)
{
    Error err(ERR_OK);
    const ObjectID&  objId    = getReq->getObjId();
    fds_volid_t volId         = getReq->getVolId();
    diskio::DataTier tierUsed = diskio::maxTier;

    fds_assert(volId != 0);
    fds_assert(objId != NullObjectID);

    LOGDEBUG << "volume " << std::hex << volId << std::dec
             << " " << objId;

    // start of ObjectStore layer latency
    PerfTracer::tracePointBegin(getReq->opLatencyCtx);

    boost::shared_ptr<const std::string> objData =
            objectStore->getObject(volId,
                                   objId,
                                   tierUsed,
                                   err);
    if (err.ok()) {
        // TODO(Andrew): Remove this copy. The network should allocated
        // a shared ptr structure so that we can directly store that, even
        // after the network message is freed.
        getReq->getObjectNetResp->data_obj = *objData;
    }

    qosCtrl->markIODone(*getReq, tierUsed, amIPrimary(objId));

    // end of ObjectStore layer latency
    PerfTracer::tracePointEnd(getReq->opLatencyCtx);

    getReq->response_cb(err, getReq);
    return err;
}

/**
 * @brief Enqueues a message into storage manager for processing
 *
 * @param volId
 * @param ioReq
 *
 * @return
 */
Error ObjectStorMgr::enqueueMsg(fds_volid_t volId, SmIoReq* ioReq)
{
    Error err(ERR_OK);
    ObjectID objectId;
    ioReq->setVolId(volId);

    switch (ioReq->io_type) {
        case FDS_SM_COMPACT_OBJECTS:
        case FDS_SM_TIER_WRITEBACK_OBJECTS:
        case FDS_SM_TIER_PROMOTE_OBJECTS:
        case FDS_SM_APPLY_DELTA_SET:
        case FDS_SM_READ_DELTA_SET:
        case FDS_SM_SNAPSHOT_TOKEN:
        case FDS_SM_MIGRATION_ABORT:
        case FDS_SM_NOTIFY_DLT_CLOSE:
        {
            err = qosCtrl->enqueueIO(volId, static_cast<FDS_IOType*>(ioReq));
            break;
        }
        case FDS_SM_GET_OBJECT:
            {
            StorMgrVolume* smVol = volTbl->getVolume(ioReq->getVolId());

            // It's possible that the volume information on this SM may not have
            // all volume information propagated when IO is enabled.  If the
            // volume is not found, then the object lookup should fail.
            if (NULL == smVol) {
                err = fds::ERR_VOL_NOT_FOUND;
                break;
            }
            err = qosCtrl->enqueueIO(smVol->getQueue()->getVolUuid(),
                                     static_cast<FDS_IOType*>(ioReq));
            break;
            }
        case FDS_SM_PUT_OBJECT:
            // Volume association resolution is handled in object store layer
            // for putObject.
            err = qosCtrl->enqueueIO(volId, static_cast<FDS_IOType*>(ioReq));
            break;
        case FDS_SM_ADD_OBJECT_REF:
            err = qosCtrl->enqueueIO(volId, static_cast<FDS_IOType*>(ioReq));
            break;
        case FDS_SM_DELETE_OBJECT:
            // Volume association resolution is handled in object store layer
            // for deleteObject.
            err = qosCtrl->enqueueIO(volId, static_cast<FDS_IOType*>(ioReq));
            break;
        default:
            LOGERROR << "Unknown message: " << ioReq->io_type;
            fds_panic("Unknown message");
    }

    if (err != fds::ERR_OK) {
        LOGERROR << "Failed to enqueue msg: " << ioReq->log_string() << " " << err;
    }

    return err;
}

/**
 * Takes snapshot of sm object metadata db identifed by
 * token
 * @param ioReq
 */
void
ObjectStorMgr::snapshotTokenInternal(SmIoReq* ioReq)
{
    Error err(ERR_OK);
    SmIoSnapshotObjectDB *snapReq = static_cast<SmIoSnapshotObjectDB*>(ioReq);
    LOGDEBUG << *snapReq;

    // When this lock is held, any put/delete request in that
    // object id range will block
    auto token_lock = getTokenLock(snapReq->token_id, true);

    // if this is the second snapshot for migration, start forwarding puts and deletes
    // for this migration client (which is addressed by executorID on destination side)
    migrationMgr->startForwarding(snapReq->executorId, snapReq->token_id);

    if (snapReq->isPersistent) {
        objectStore->snapshotMetadata(snapReq->token_id,
                                      snapReq->smio_persist_snap_resp_cb,
                                      snapReq);
    } else {
        objectStore->snapshotMetadata(snapReq->token_id,
                                      snapReq->smio_snap_resp_cb,
                                      snapReq);
    }
    /* Mark the request as complete */
    qosCtrl->markIODone(*snapReq,
                        diskio::diskTier);
}

void
ObjectStorMgr::compactObjectsInternal(SmIoReq* ioReq)
{
    Error err(ERR_OK);
    SmIoCompactObjects *cobjs_req =  static_cast<SmIoCompactObjects*>(ioReq);
    fds_verify(cobjs_req != NULL);
    const DLT* curDlt = getDLT();
    NodeUuid myUuid = getUuid();

    for (fds_uint32_t i = 0; i < (cobjs_req->oid_list).size(); ++i) {
        const ObjectID& obj_id = (cobjs_req->oid_list)[i];
        fds_bool_t objOwned = true;

        // we will garbage collect an object even if its refct > 0
        // if it belongs to DLT token that this SM is not responsible anymore
        // However, migration should be idle and DLT must be closed (not just
        // committed) -- if that does not hold, we will garbage collect this
        // object next time.. correctness holds.
        if (migrationMgr->isMigrationIdle() && curDlt->isClosed()) {
            DltTokenGroupPtr nodes = curDlt->getNodes(obj_id);
            fds_bool_t found = false;
            for (uint i = 0; i < nodes->getLength(); ++i) {
                if (nodes->get(i) == myUuid) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                objOwned = false;
                LOGTRACE << "Will remove " << obj_id << " even if refct > 0 "
                         << " because the object no longer owned by this SM";
            }
        }

        LOGDEBUG << "Compaction is working on object " << obj_id
                 << " on tier " << cobjs_req->tier << " verify data?"
                 << cobjs_req->verifyData << " object owned? "
                 << objOwned;

        // copy this object if not garbage, otherwise rm object db entry
        err = objectStore->copyObjectToNewLocation(obj_id, cobjs_req->tier,
                                                   cobjs_req->verifyData,
                                                   objOwned);
        if (!err.ok()) {
            LOGERROR << "Failed to compact object " << obj_id
                     << ", error " << err;
            break;
        }
    }

    /* Mark the request as complete */
    qosCtrl->markIODone(*cobjs_req, diskio::diskTier);

    cobjs_req->smio_compactobj_resp_cb(err, cobjs_req);

    /** TODO(Sean)
     *  Originally cobjs_req was deleted here, but valgrind detected is as memory leak.
     *  However, moving the delete call into the smio_compactobj_resp_cb() make valgrind
     *  happy.  Need to investigate why this is.  Could be that 1% false positive valgrind
     *  can report.
     */
}

void
ObjectStorMgr::applyRebalanceDeltaSet(SmIoReq* ioReq)
{
    Error err(ERR_OK);
    SmIoApplyObjRebalDeltaSet* rebalReq = static_cast<SmIoApplyObjRebalDeltaSet*>(ioReq);
    fds_verify(rebalReq != NULL);

    LOGMIGRATE << "Applying Delta Set:"
               << " executorID=" << rebalReq->executorId
               << " seqNum=" << rebalReq->seqNum
               << " lastSet=" << rebalReq->lastSet
               << " qosSeqNum=" << rebalReq->qosSeqNum
               << " qosLastSet=" << rebalReq->qosLastSet
               << " delta set size=" << rebalReq->deltaSet.size();

    for (fds_uint32_t i = 0; i < (rebalReq->deltaSet).size(); ++i) {
        const fpi::CtrlObjectMetaDataPropagate& objDataMeta = (rebalReq->deltaSet)[i];
        ObjectID objId(ObjectID(objDataMeta.objectID.digest));

        LOGMIGRATE << "Applying DeltaSet element: " << objId;

        err = objectStore->applyObjectMetadataData(objId, objDataMeta);

        if (!err.ok()) {
            // we will stop applying object metadata/data and report error to migr mgr
            LOGERROR << "Failed to apply object metadata/data " << objId
                     << ", " << err;
            break;
        }
    }

    // mark request as complete
    qosCtrl->markIODone(*rebalReq, diskio::diskTier);

    // notify migration executor we are done with this request
    rebalReq->smioObjdeltaRespCb(err, rebalReq);

    delete rebalReq;
}

void
ObjectStorMgr::readObjDeltaSet(SmIoReq *ioReq)
{
    Error err(ERR_OK);

    fiu_do_on("sm.exit.sending.delta.set", exit(1));

    SmIoReadObjDeltaSetReq *readDeltaSetReq = static_cast<SmIoReadObjDeltaSetReq *>(ioReq);
    fds_verify(NULL != readDeltaSetReq);

    LOGMIGRATE << "Filling DeltaSet:"
               << " destinationSmId " << readDeltaSetReq->destinationSmId
               << " executorID=" << std::hex << readDeltaSetReq->executorId << std::dec
               << " seqNum=" << readDeltaSetReq->seqNum
               << " lastSet=" << readDeltaSetReq->lastSet
               << " delta set size=" << readDeltaSetReq->deltaSet.size();

    PerfContext tmp_pctx(PerfEventType::SM_READ_OBJ_DELTA_SET, 0);
    SCOPED_PERF_TRACEPOINT_CTX(tmp_pctx);

    fpi::CtrlObjectRebalanceDeltaSetPtr objDeltaSet(new fpi::CtrlObjectRebalanceDeltaSet());
    NodeUuid destSmId = readDeltaSetReq->destinationSmId;
    objDeltaSet->executorID = readDeltaSetReq->executorId;
    objDeltaSet->seqNum = readDeltaSetReq->seqNum;
    objDeltaSet->lastDeltaSet = readDeltaSetReq->lastSet;

    // PerfTracer::incr(PerfEventType::SM_READ_OBJ_DELTA_SET_LOOP_SIZE, 0, (readDeltaSetReq->deltaSet).size(), 1);

    for (fds_uint32_t i = 0; i < (readDeltaSetReq->deltaSet).size(); ++i) {
        ObjMetaData::ptr objMetaDataPtr = (readDeltaSetReq->deltaSet)[i].first;
        fpi::ObjectMetaDataReconcileFlags reconcileFlag = (readDeltaSetReq->deltaSet)[i].second;

        const ObjectID objID(objMetaDataPtr->obj_map.obj_id.metaDigest);

        fpi::CtrlObjectMetaDataPropagate objMetaDataPropagate;

        /* copy metadata to object propagation message. */
        objMetaDataPtr->propagateObjectMetaData(objMetaDataPropagate,
                                                reconcileFlag);

        /* Read object data, if NO_RECONCILE or OVERWRITE */
        if (fpi::OBJ_METADATA_NO_RECONCILE == reconcileFlag) {

            /* get the object from metadata information. */
            boost::shared_ptr<const std::string> dataPtr =
                    objectStore->getObjectData(invalid_vol_id,
                                               objID,
                                               objMetaDataPtr,
                                               err);
            /* TODO(sean): For now, just panic. Need to know why
             * object read failed.
             */
            fds_verify(err.ok());

            /* Copy the object data */
            objMetaDataPropagate.objectData = *dataPtr;
        }

        /* Add metadata and data to the delta set */
        objDeltaSet->objectToPropagate.push_back(objMetaDataPropagate);

        LOGMIGRATE << "Adding DeltaSet element: " << objID
                   << " reconcileFlag=" << reconcileFlag;
    }

    // Inject fault (if set) that source silently failed to send delta set msg
    fds_bool_t failToSendDeltaSet = false;
    fiu_do_on("sm.faults.senddeltaset", failToSendDeltaSet = true; );

    if (!failToSendDeltaSet) {
        auto asyncDeltaSetReq = gSvcRequestPool->newEPSvcRequest(destSmId.toSvcUuid());
        asyncDeltaSetReq->setPayload(FDSP_MSG_TYPEID(fpi::CtrlObjectRebalanceDeltaSet),
                                     objDeltaSet);
        asyncDeltaSetReq->setTimeoutMs(0);
        asyncDeltaSetReq->invoke();
    } else {
        LOGNOTIFY << "Injected fault: SM silently failed to send delta set to destination";
        fiu_disable("sm.faults.senddeltaset");
    }

    // mark request as complete
    qosCtrl->markIODone(*readDeltaSetReq, diskio::diskTier);

    // notify migration executor we are done with this request
    readDeltaSetReq->smioReadObjDeltaSetReqCb(err, readDeltaSetReq);

    /* Delete the delta set request */
    delete readDeltaSetReq;
}


void
ObjectStorMgr::abortMigration(SmIoReq *ioReq)
{
    Error err(ERR_OK);

    SmIoAbortMigration *abortMigrationReq = static_cast<SmIoAbortMigration *>(ioReq);
    fds_verify(abortMigrationReq != NULL);

    LOGDEBUG << "XXX: migrationAbort";

    // tell migration mgr to abort migration
    err = objStorMgr->migrationMgr->abortMigration();

    // revert to DLT version provided in abort message
    if (abortMigrationReq->abortMigrationDLTVersion > 0) {
        // will ignore error from setCurrent -- if this SM does not know
        // about DLT with given version, then it did not have a DLT previously..
        MODULEPROVIDER()->getSvcMgr()->getDltManager()->setCurrent(abortMigrationReq->abortMigrationDLTVersion);
    }

    qosCtrl->markIODone(*abortMigrationReq);

    if (abortMigrationReq->abortMigrationCb) {
        abortMigrationReq->abortMigrationCb(err, abortMigrationReq);
    }

    delete abortMigrationReq;
}

void
ObjectStorMgr::notifyDLTClose(SmIoReq *ioReq)
{
    Error err(ERR_OK);

    SmIoNotifyDLTClose *closeDLTReq = static_cast<SmIoNotifyDLTClose *>(ioReq);
    fds_verify(closeDLTReq != NULL);

    LOGDEBUG << "XXX: executing dlt close";


    // Store the current DLT to the presistent storage to be used
    // by offline smcheck.
    objStorMgr->storeCurrentDLT();

    // tell superblock that DLT is closed, so that it will invalidate
    // appropriate SM tokens -- however, it is possible that this SM
    // got DLT update and close for the DLT version this SM does not belong
    // to, that should only happen on initial start; but for not notifying
    // superblock about DLT this SM does not belong to
    const DLT *curDlt = objStorMgr->getDLT();
    if (!curDlt->getTokens(objStorMgr->getUuid()).empty()) {
        err = objStorMgr->objectStore->handleDltClose(objStorMgr->getDLT());
    }

    // re-enable GC and Tier Migration
    // If this SM did not receive start migration or rebalance
    // message and GC and TierMigration were not disabled, this operation
    // will be a noop
    SmScavengerActionCmd scavCmd(fpi::FDSP_SCAVENGER_ENABLE,
                                 SM_CMD_INITIATOR_TOKEN_MIGRATION);
    err = objStorMgr->objectStore->scavengerControlCmd(&scavCmd);
    SmTieringCmd tierCmd(SmTieringCmd::TIERING_ENABLE);
    err = objStorMgr->objectStore->tieringControlCmd(&tierCmd);

    // Update the DLT information for the SM checker when migration
    // is complete.
    // Strangely, compilation has some issues when trying to acquire the latest
    // DLT in the SMCheckOnline class.  So, decided to update the DLT
    // here.
    // TODO(Sean):  This is a bit of a hack.  Access to DLT from SMCheck is
    //              causing compilation issues, so make couple layers of
    //              indirect call to update the
    objStorMgr->objectStore->SmCheckUpdateDLT(objStorMgr->getDLT());

    // notify token migration manager
    err = objStorMgr->migrationMgr->handleDltClose();

    qosCtrl->markIODone(*closeDLTReq);

    if (closeDLTReq->closeDLTCb) {
        closeDLTReq->closeDLTCb(err, closeDLTReq);
    }

    delete closeDLTReq;
}

void
ObjectStorMgr::moveTierObjectsInternal(SmIoReq* ioReq)
{
    Error err(ERR_OK);
    SmIoMoveObjsToTier *moveReq = static_cast<SmIoMoveObjsToTier*>(ioReq);
    fds_verify(moveReq != NULL);

    LOGDEBUG << "Will move " << (moveReq->oidList).size() << " objs from tier "
             << moveReq->fromTier << " to tier " << moveReq->fromTier
             << " relocate? " << moveReq->relocate;

    moveReq->movedCnt = 0;
    for (fds_uint32_t i = 0; i < (moveReq->oidList).size(); ++i) {
        const ObjectID& objId = (moveReq->oidList)[i];
        err = objectStore->moveObjectToTier(objId, moveReq->fromTier,
                                            moveReq->toTier, moveReq->relocate);
        if (!err.ok()) {
            if (err != ERR_SM_ZERO_REFCNT_OBJECT &&
                err != ERR_SM_TIER_HYBRIDMOVE_ON_FLASH_VOLUME &&
                err != ERR_SM_TIER_WRITEBACK_NOT_DONE) {
                LOGERROR << "Failed to move " << objId << " from tier "
                    << moveReq->fromTier << " to tier " << moveReq->fromTier
                    << " relocate? " << moveReq->relocate << " " << err;
                // we will just continue to move other objects; ok for promotions
                // demotion should not assume that object was written back to HDD
                // anyway, because writeback is eventual
            }
        } else {
            moveReq->movedCnt++;
        }
    }

    /* Mark the request as complete */
    qosCtrl->markIODone(*moveReq);

    if (moveReq->moveObjsRespCb) {
        moveReq->moveObjsRespCb(err, moveReq);
    }
    delete moveReq;
}

void
ObjectStorMgr::storeCurrentDLT()
{
    // Store current DLT to a file in log directory, so offline smcheck can use it to
    // verify dlt ownership.
    //
    // TODO(Sean):  cleanup when going moving to online smcheck
    DLT *currentDLT = const_cast<DLT *>(MODULEPROVIDER()->getSvcMgr()->getCurrentDLT());
    std::string dltPath = g_fdsprocess->proc_fdsroot()->dir_fds_logs() + DLTFileName;

    // Must remove pre-existing file.  Otherwise, it will append a new version to the
    // end of the file.  When de-serializing, it read from the beginning of the file,
    // thus getting the oldest copy of DLT, if not removed.
    remove(dltPath.c_str());
    currentDLT->storeToFile(dltPath);

    // To validate the token ownership by SM, we also need UUID of the SM to compare it
    // against DLT
    NodeUuid myUuid = getUuid();
    std::string uuidPath = g_fdsprocess->proc_fdsroot()->dir_fds_logs() + UUIDFileName;
    std::ofstream uuidFile(uuidPath);
    uuidFile <<  myUuid.uuid_get_val();
}


Error ObjectStorMgr::SmQosCtrl::processIO(FDS_IOType* _io) {
    Error err(ERR_OK);
    SmIoReq *io = static_cast<SmIoReq*>(_io);

    PerfTracer::tracePointEnd(io->opQoSWaitCtx);

    switch (io->io_type) {
        case FDS_IO_READ:
        case FDS_IO_WRITE:
        {
            fds_panic("must not get here!");
            break;
        }
        case FDS_SM_DELETE_OBJECT:
        {
            LOGDEBUG << "Processing a Delete request";
                threadPool->schedule(&ObjectStorMgr::deleteObjectInternal,
                                     objStorMgr,
                                     static_cast<SmIoDeleteObjectReq *>(io));
            break;
        }
        case FDS_SM_GET_OBJECT:
        {
            LOGDEBUG << "Processing a get request";
            threadPool->schedule(&ObjectStorMgr::getObjectInternal,
                                 objStorMgr,
                                 static_cast<SmIoGetObjectReq *>(io));
            break;
        }
        case FDS_SM_PUT_OBJECT:
        {
            LOGDEBUG << "Processing a put request";
            threadPool->schedule(&ObjectStorMgr::putObjectInternal,
                                 objStorMgr,
                                 static_cast<SmIoPutObjectReq *>(io));
            break;
        }
        case FDS_SM_ADD_OBJECT_REF:
        {
            LOGDEBUG << "Processing and add object reference request";
            threadPool->schedule(&ObjectStorMgr::addObjectRefInternal,
                                 objStorMgr,
                                 static_cast<SmIoAddObjRefReq *>(io));
            break;
        }
        case FDS_SM_SNAPSHOT_TOKEN:
        {
            threadPool->schedule(&ObjectStorMgr::snapshotTokenInternal, objStorMgr, io);
            break;
        }
        case FDS_SM_COMPACT_OBJECTS:
        {
            LOGDEBUG << "Processing sync apply metadata";
            threadPool->schedule(&ObjectStorMgr::compactObjectsInternal, objStorMgr, io);
            break;
        }
        case FDS_SM_TIER_WRITEBACK_OBJECTS:
        case FDS_SM_TIER_PROMOTE_OBJECTS:
        {
            threadPool->schedule(&ObjectStorMgr::moveTierObjectsInternal, objStorMgr, io);
            break;
        }
        case FDS_SM_APPLY_DELTA_SET:
        {
            threadPool->schedule(&ObjectStorMgr::applyRebalanceDeltaSet, objStorMgr, io);
            break;
        }
        case FDS_SM_READ_DELTA_SET:
        {
            threadPool->schedule(&ObjectStorMgr::readObjDeltaSet, objStorMgr, io);
            break;
        }
        case FDS_SM_MIGRATION_ABORT:
        {
            LOGDEBUG << "XXX: MIGRATION ABORT";
            threadPool->schedule(&ObjectStorMgr::abortMigration, objStorMgr, io);
            break;
        }
        case FDS_SM_NOTIFY_DLT_CLOSE:
        {
            LOGDEBUG << "XXX: NOTIFY DLT CLOSE";
            threadPool->schedule(&ObjectStorMgr::notifyDLTClose, objStorMgr, io);
            break;
        }
        default:
            fds_assert(!"Unknown message");
            break;
    }

    return err;
}


}  // namespace fds
