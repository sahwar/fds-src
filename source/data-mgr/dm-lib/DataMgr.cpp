/*
 * Copyright 2013 Formation Data Systems, Inc.
 */

#include <string>
#include <list>
#include <vector>
#include <fds_timestamp.h>
#include <fdsp_utils.h>
#include <DataMgr.h>
#include <net/net-service.h>
#include <net/net-service-tmpl.hpp>

namespace fds {
extern DataMgr *dataMgr;

Error DataMgr::vol_handler(fds_volid_t vol_uuid,
                           VolumeDesc *desc,
                           fds_vol_notify_t vol_action,
                           fpi::FDSP_NotifyVolFlag vol_flag,
                           fpi::FDSP_ResultType result) {
    Error err(ERR_OK);
    GLOGNORMAL << "Received vol notif from OM for "
               << desc->getName() << ":"
               << std::hex << vol_uuid << std::dec;

    if (vol_action == fds_notify_vol_add) {
        /*
         * TODO: Actually take a volume string name, not
         * just the volume number.
         */
        err = dataMgr->_process_add_vol(dataMgr->getPrefix() +
                                        std::to_string(vol_uuid),
                                        vol_uuid, desc,
                                        (vol_flag == fpi::FDSP_NOTIFY_VOL_WILL_SYNC));
    } else if (vol_action == fds_notify_vol_rm) {
        err = dataMgr->_process_rm_vol(vol_uuid, vol_flag == fpi::FDSP_NOTIFY_VOL_CHECK_ONLY);
    } else if (vol_action == fds_notify_vol_mod) {
        err = dataMgr->_process_mod_vol(vol_uuid, *desc);
    } else {
        assert(0);
    }
    return err;
}

void DataMgr::node_handler(fds_int32_t  node_id,
                           fds_uint32_t node_ip,
                           fds_int32_t  node_st,
                           fds_uint32_t node_port,
                           FDS_ProtocolInterface::FDSP_MgrIdType node_type) {
}

Error
DataMgr::volcat_evt_handler(fds_catalog_action_t catalog_action,
                            const FDS_ProtocolInterface::FDSP_PushMetaPtr& push_meta,
                            const std::string& session_uuid) {
    Error err(ERR_OK);
    OMgrClient* om_client = dataMgr->omClient;
    GLOGNORMAL << "Received Volume Catalog request";
    if (catalog_action == fds_catalog_push_meta) {
        err = dataMgr->catSyncMgr->startCatalogSync(push_meta->metaVol, om_client, session_uuid);
    } else if (catalog_action == fds_catalog_dmt_commit) {
        // thsi will ignore this msg if catalog sync is not in progress
        err = dataMgr->catSyncMgr->startCatalogSyncDelta(session_uuid);
    } else if (catalog_action == fds_catalog_dmt_close) {
        // will finish forwarding when all queued updates are processed
        GLOGNORMAL << "Received DMT Close";
        err = dataMgr->notifyStopForwardUpdates();
    } else {
        fds_assert(!"Unknown catalog command");
    }
    return err;
}

/**
 * Callback from vol meta receiver that receiving vol meta is done
 * for volume 'volid'. This method activates volume's QoS queue
 * so this DM starts processing requests from AM for this volume
 */
void
DataMgr::volmetaRecvd(fds_volid_t volid, const Error& error) {
    LOGDEBUG << "Will unblock qos queue for volume "
             << std::hex << volid << std::dec;
    vol_map_mtx->lock();
    fds_verify(vol_meta_map.count(volid) > 0);
    VolumeMeta *vol_meta = vol_meta_map[volid];
    vol_meta->dmVolQueue->activate();
    vol_map_mtx->unlock();
}

/**
 * Is called on timer to finish forwarding for all volumes that
 * are still forwarding, send DMT close ack, and remove vcat/tcat
 * of volumes that do not longer this DM responsibility
 */
void
DataMgr::finishCloseDMT() {
    std::unordered_map<fds_uint64_t, VolumeMeta*>::iterator vol_it;
    std::unordered_set<fds_volid_t> done_vols;
    fds_bool_t all_finished = false;

    LOGNOTIFY << "Finish handling DMT close and send ack if not sent yet";

    // move all volumes that are in 'finish forwarding' state
    // to not forwarding state, and make a set of these volumes
    // the set is used so that we call catSyncMgr to cleanup/and
    // and send push meta done msg outside of vol_map_mtx lock
    vol_map_mtx->lock();
    for (vol_it = vol_meta_map.begin();
         vol_it != vol_meta_map.end();
         ++vol_it) {
        fds_volid_t volid = vol_it->first;
        VolumeMeta *vol_meta = vol_it->second;
        if (vol_meta->isForwardFinish()) {
            vol_meta->setForwardFinish();
            done_vols.insert(volid);
        }
        // we expect that volume is not forwarding
        fds_verify(!vol_meta->isForwarding());
    }
    vol_map_mtx->unlock();

    for (std::unordered_set<fds_volid_t>::const_iterator it = done_vols.cbegin();
         it != done_vols.cend();
         ++it) {
        // remove the volume from sync_volume list
        LOGNORMAL << "CleanUP: remove Volume " << std::hex << *it << std::dec;
        if (catSyncMgr->finishedForwardVolmeta(*it)) {
            all_finished = true;
        }
    }

    // at this point we expect that all volumes are done
    fds_verify(all_finished || !catSyncMgr->isSyncInProgress());

    // Note that finishedForwardVolmeta sends ack to close DMT when it returns true
    // either in this method or when we called finishedForwardVolume for the last
    // volume to finish forwarding

    // TODO(Anna) remove volume catalog for volumes we finished forwarding
    // we probably want to do this after close, if we will continue forwarding ???
}

/**
 * Note that volId may not necessarily match volume id in ioReq
 * Example when it will not match: if we enqueue request for volumeA
 * to shadow queue, we will pass shadow queue's volId (queue id)
 * but ioReq will contain actual volume id; so maybe we should change
 * this method to pass queue id as first param not volid
 */
Error DataMgr::enqueueMsg(fds_volid_t volId,
                          dmCatReq* ioReq) {
    Error err(ERR_OK);

    switch (ioReq->io_type) {
        case FDS_DM_SNAP_VOLCAT:
        case FDS_DM_SNAPDELTA_VOLCAT:
        case FDS_DM_FWD_CAT_UPD:
            err = qosCtrl->enqueueIO(volId, static_cast<FDS_IOType*>(ioReq));
            break;
        default:
            fds_assert(!"Unknown message");
    };

    if (!err.ok()) {
        LOGERROR << "Failed to enqueue message " << ioReq->log_string()
                 << " to queue "<< std::hex << volId << std::dec << " " << err;
    }
    return err;
}

/*
 * Adds the volume if it doesn't exist already.
 * Note this does NOT return error if the volume exists.
 */
Error DataMgr::_add_if_no_vol(const std::string& vol_name,
                              fds_volid_t vol_uuid, VolumeDesc *desc) {
    Error err(ERR_OK);

    /*
     * Check if we already know about this volume
     */
    vol_map_mtx->lock();
    if (volExistsLocked(vol_uuid) == true) {
        LOGNORMAL << "Received add request for existing vol uuid "
                  << vol_uuid << ", so ignoring.";
        vol_map_mtx->unlock();
        return err;
    }

    vol_map_mtx->unlock();

    err = _add_vol_locked(vol_name, vol_uuid, desc, true);


    return err;
}

/*
 * Meant to be called holding the vol_map_mtx.
 * @param vol_will_sync true if this volume's meta will be synced from
 * other DM first
 */
Error DataMgr::_add_vol_locked(const std::string& vol_name,
                               fds_volid_t vol_uuid,
                               VolumeDesc *vdesc,
                               fds_bool_t vol_will_sync) {
    Error err(ERR_OK);

    // create vol catalogs, etc first
    err = timeVolCat_->addVolume(*vdesc);
    if (err.ok() && !vol_will_sync) {
        // not going to sync this volume, activate volume
        // so that we can do get/put/del cat ops to this volume
        err = timeVolCat_->activateVolume(vol_uuid);
    }
    if (!err.ok()) {
        LOGERROR << "Failed to add volume " << std::hex << vol_uuid << std::dec;
        return err;
    }

    VolumeMeta *volmeta = new(std::nothrow) VolumeMeta(vol_name,
                                                       vol_uuid,
                                                       GetLog(),
                                                       vdesc);
    if (!volmeta) {
        LOGERROR << "Failed to allocate VolumeMeta for volume "
                 << std::hex << vol_uuid << std::dec;
        return ERR_OUT_OF_MEMORY;
    }
    volmeta->dmVolQueue = new(std::nothrow) FDS_VolumeQueue(4096,
                                                            vdesc->iops_max,
                                                            2*vdesc->iops_min,
                                                            vdesc->relativePrio);
    if (!volmeta->dmVolQueue) {
        LOGERROR << "Failed to allocate Qos queue for volume "
                 << std::hex << vol_uuid << std::dec;
        delete volmeta;
        return ERR_OUT_OF_MEMORY;
    }
    volmeta->dmVolQueue->activate();

    LOGDEBUG << "Added vol meta for vol uuid and per Volume queue" << std::hex
              << vol_uuid << std::dec << ", created catalogs? " << !vol_will_sync;

    vol_map_mtx->lock();
    err = dataMgr->qosCtrl->registerVolume(vol_uuid,
                                           static_cast<FDS_VolumeQueue*>(
                                               volmeta->dmVolQueue));
    if (!err.ok()) {
        delete volmeta;
        vol_map_mtx->unlock();
        return err;
    }

    // if we will sync vol meta, block processing IO requests from this volume's
    // qos queue and create a shadow queue for receiving volume meta from another DM
    if (vol_will_sync) {
        // we will use the priority of the volume, but no min iops (otherwise we will
        // need to implement temp deregister of vol queue, so we have enough total iops
        // to admit iops of shadow queue)
        FDS_VolumeQueue* shadowVolQueue =
                new(std::nothrow) FDS_VolumeQueue(4096, 10000, 0, vdesc->relativePrio);
        if (shadowVolQueue) {
            fds_volid_t shadow_volid = catSyncRecv->shadowVolUuid(vol_uuid);
            // block volume's qos queue
            volmeta->dmVolQueue->stopDequeue();
            // register shadow queue
            err = dataMgr->qosCtrl->registerVolume(shadow_volid, shadowVolQueue);
            if (err.ok()) {
                LOGERROR << "Registered shadow volume queue for volume 0x"
                         << std::hex << vol_uuid << " shadow id " << shadow_volid << std::dec;
                // pass ownership of shadow volume queue to volume meta receiver
                catSyncRecv->startRecvVolmeta(vol_uuid, shadowVolQueue);
            } else {
                LOGERROR << "Failed to register shadow volume queue for volume 0x"
                         << std::hex << vol_uuid << " shadow id " << shadow_volid
                         << std::dec << " " << err;
                // cleanup, we will revert volume registration and creation
                delete shadowVolQueue;
                shadowVolQueue = NULL;
            }
        } else {
            LOGERROR << "Failed to allocate shadow volume queue for volume "
                     << std::hex << vol_uuid << std::dec;
            err = ERR_OUT_OF_MEMORY;
        }
    }

    if (err.ok()) {
        // we registered queue and shadow queue if needed
        vol_meta_map[vol_uuid] = volmeta;
    }
    vol_map_mtx->unlock();

    if (!err.ok()) {
        // cleanup volmeta and deregister queue
        LOGERROR << "Cleaning up volume queue and vol meta because of error "
                 << " volid 0x" << std::hex << vol_uuid << std::dec;
        dataMgr->qosCtrl->deregisterVolume(vol_uuid);
        delete volmeta->dmVolQueue;
        delete volmeta;
    }

    return err;
}

Error DataMgr::_process_add_vol(const std::string& vol_name,
                                fds_volid_t vol_uuid,
                                VolumeDesc *desc,
                                fds_bool_t vol_will_sync) {
    Error err(ERR_OK);

    /*
     * Verify that we don't already know about this volume
     */
    vol_map_mtx->lock();
    if (volExistsLocked(vol_uuid) == true) {
        err = Error(ERR_DUPLICATE);
        vol_map_mtx->unlock();
        LOGDEBUG << "Received add request for existing vol uuid "
                 << std::hex << vol_uuid << std::dec;
        return err;
    }
    vol_map_mtx->unlock();

    err = _add_vol_locked(vol_name, vol_uuid, desc, vol_will_sync);
    return err;
}

Error DataMgr::_process_mod_vol(fds_volid_t vol_uuid, const VolumeDesc& voldesc)
{
    Error err(ERR_OK);

    vol_map_mtx->lock();
    /* make sure volume exists */
    if (volExistsLocked(vol_uuid) == false) {
        LOGERROR << "Received modify policy request for "
                 << "non-existant volume [" << vol_uuid
                 << ", " << voldesc.name << "]";
        err = Error(ERR_NOT_FOUND);
        vol_map_mtx->unlock();
        return err;
    }
    VolumeMeta *vm = vol_meta_map[vol_uuid];
    vm->vol_desc->modifyPolicyInfo(2*voldesc.iops_min, voldesc.iops_max, voldesc.relativePrio);
    err = qosCtrl->modifyVolumeQosParams(vol_uuid,
                                         2*voldesc.iops_min,
                                         voldesc.iops_max,
                                         voldesc.relativePrio);
    vol_map_mtx->unlock();

    LOGNOTIFY << "Modify policy for volume "
              << voldesc.name << " RESULT: " << err.GetErrstr();

    return err;
}

Error DataMgr::_process_rm_vol(fds_volid_t vol_uuid, fds_bool_t check_only) {
    Error err(ERR_OK);

    /*
     * Make sure we already know about this volume
     */
    vol_map_mtx->lock();
    if (volExistsLocked(vol_uuid) == false) {
        LOGWARN << "Received Delete request for:"
                << std::hex << vol_uuid << std::dec
                << " that doesn't exist.";
        err = ERR_INVALID_ARG;
        vol_map_mtx->unlock();
        return err;
    }
    vol_map_mtx->unlock();

    fds_bool_t isEmpty = timeVolCat_->queryIface()->isVolumeEmpty(vol_uuid);
    if (isEmpty == false) {
        LOGERROR << "Volume is NOT Empty:"
                 << std::hex << vol_uuid << std::dec;
        err = ERR_VOL_NOT_EMPTY;
        return err;
    }
    // TODO(Andrew): Here we may want to prevent further I/Os
    // to the volume as we're going to remove it but a blob
    // may be written in the mean time.

    // if notify delete asked to only check if deleting volume
    // was ok; so we return with success here; DM will get
    // another notify volume delete with check_only ==false to
    // actually cleanup all other datastructures for this volume
    if (!check_only) {
        // TODO(Andrew): Here we want to delete each blob in the
        // volume and then mark the volume as deleted.
        vol_map_mtx->lock();
        VolumeMeta *vol_meta = vol_meta_map[vol_uuid];
        vol_meta_map.erase(vol_uuid);
        vol_map_mtx->unlock();
        dataMgr->qosCtrl->deregisterVolume(vol_uuid);
        delete vol_meta->dmVolQueue;
        delete vol_meta;
        LOGNORMAL << "Removed vol meta for vol uuid "
                  << vol_uuid;
    } else {
        LOGNORMAL << "Notify volume rm check only, did not "
                  << " remove vol meta for vol " << std::hex
                  << vol_uuid << std::dec;
    }

    vol_map_mtx->unlock();

    return err;
}

/**
 * For all volumes that are in forwarding state, move them to
 * finish forwarding state.
 */
Error DataMgr::notifyStopForwardUpdates() {
    std::unordered_map<fds_uint64_t, VolumeMeta*>::iterator vol_it;
    Error err(ERR_OK);

    if (!catSyncMgr->isSyncInProgress()) {
        err = ERR_CATSYNC_NOT_PROGRESS;
        return err;
    }

    vol_map_mtx->lock();
    for (vol_it = vol_meta_map.begin();
         vol_it != vol_meta_map.end();
         ++vol_it) {
        VolumeMeta *vol_meta = vol_it->second;
        vol_meta->finishForwarding();
    }
    vol_map_mtx->unlock();

    // start timer where we will stop forwarding volumes that are
    // still in 'finishing forwarding' state
    if (!closedmt_timer->schedule(closedmt_timer_task, std::chrono::seconds(2))) {
        // TODO(xxx) how do we handle this?
        fds_panic("Failed to schedule closedmt timer!");
    }
    return err;
}

 /*
 * Returns the maxObjSize in the volume.
 * TODO(Andrew): This should be refactored into a
 * common library since everyone needs it, not just DM
 * TODO(Andrew): Also this shouldn't be hard coded obviously...
 */
Error DataMgr::getVolObjSize(fds_volid_t volId,
                             fds_uint32_t *maxObjSize) {
    Error err(ERR_OK);

    /*
     * Get a local reference to the vol meta.
     */
    vol_map_mtx->lock();
    VolumeMeta *vol_meta = vol_meta_map[volId];
    vol_map_mtx->unlock();

    fds_verify(vol_meta != NULL);
    *maxObjSize = vol_meta->vol_desc->maxObjSizeInBytes;
    return err;
}

DataMgr::DataMgr(int argc, char *argv[], Platform *platform, Module **vec)
        : PlatformProcess(argc, argv, "fds.dm.", "dm.log", platform, vec),
          omConfigPort(0),
          use_om(true),
          numTestVols(10),
          runMode(NORMAL_MODE),
          scheduleRate(4000),
          catSyncRecv(new CatSyncReceiver(this,
                                          std::bind(&DataMgr::volmetaRecvd,
                                                    this,
                                                    std::placeholders::_1,
                                                    std::placeholders::_2))),
                     closedmt_timer(new FdsTimer()),
                     closedmt_timer_task(new CloseDMTTimerTask(*closedmt_timer,
                                                               std::bind(&DataMgr::finishCloseDMT,
                                                                         this))) {
    // If we're in test mode, don't daemonize.
    // TODO(Andrew): We probably want another config field and
    // not to override test_mode
    fds_bool_t noDaemon = conf_helper_.get_abs<bool>("fds.dm.testing.test_mode", false);
    if (noDaemon == false) {
        daemonize();
    }

    // Set testing related members
    testUturnAll       = conf_helper_.get_abs<bool>("fds.dm.testing.uturn_all", false);
    testUturnStartTx = conf_helper_.get_abs<bool>("fds.dm.testing.uturn_starttx", false);
    testUturnUpdateCat = conf_helper_.get_abs<bool>("fds.dm.testing.uturn_updatecat", false);
    testUturnSetMeta   = conf_helper_.get_abs<bool>("fds.dm.testing.uturn_setmeta", false);

    vol_map_mtx = new fds_mutex("Volume map mutex");

    /*
     * Comm with OM will be setup during run()
     */
    omClient = NULL;
    /*
     *  init Data Manager  QOS class.
     */
    qosCtrl = new dmQosCtrl(this, 20, FDS_QoSControl::FDS_DISPATCH_WFQ, GetLog());
    qosCtrl->runScheduler();

    timeVolCat_ = DmTimeVolCatalog::ptr(new
                                        DmTimeVolCatalog("DM Time Volume Catalog",
                                                         *qosCtrl->threadPool));

    LOGNORMAL << "Constructed the Data Manager";
}

void DataMgr::initHandlers() {
    handlers[FDS_LIST_BLOB]   = new dm::GetBucketHandler();
    handlers[FDS_DELETE_BLOB] = new dm::DeleteBlobHandler();
}

DataMgr::~DataMgr()
{
    LOGNORMAL << "Destructing the Data Manager";

    closedmt_timer->destroy();

    for (std::unordered_map<fds_uint64_t, VolumeMeta*>::iterator
                 it = vol_meta_map.begin();
         it != vol_meta_map.end();
         it++) {
        delete it->second;
    }
    vol_meta_map.clear();

    qosCtrl->deregisterVolume(FdsDmSysTaskId);
    delete sysTaskQueue;

    delete omClient;
    delete vol_map_mtx;
    delete qosCtrl;
}

int DataMgr::run()
{
    initHandlers();
    try {
        nstable->listenServer(metadatapath_session);
    }
    catch(...){
        std::cout << "starting server threw an exception" << std::endl;
    }
    return 0;
}

void DataMgr::setup_metadatapath_server(const std::string &ip)
{
    metadatapath_handler.reset(new ReqHandler());

    int myIpInt = netSession::ipString2Addr(ip);
    std::string node_name = "_DM_" + ip;
    // TODO(Andrew): Ideally createServerSession should take a shared pointer
    // for datapath_handler.  Make sure that happens.  Otherwise you
    // end up with a pointer leak.
    // TODO(Andrew): Figure out who cleans up datapath_session_
    metadatapath_session = nstable->\
            createServerSession<netMetaDataPathServerSession>(
                myIpInt,
                plf_mgr->plf_get_my_data_port(),
                node_name,
                FDSP_STOR_HVISOR,
                metadatapath_handler);
}

void DataMgr::proc_pre_startup()
{
    Error err(ERR_OK);
    fds::DmDiskInfo     *info;
    fds::DmDiskQuery     in;
    fds::DmDiskQueryOut  out;
    fds_bool_t      useTestMode = false;

    runMode = NORMAL_MODE;

   /*
    LOGNORMAL << "before running system command rsync ";
    std::system((const char *)("sshpass -p passwd rsync -r /tmp/logs  root@10.1.10.216:/tmp"));
    LOGNORMAL << "After running system command rsync ";
   */

    PlatformProcess::proc_pre_startup();

    // Get config values from that platform lib.
    //
    omConfigPort = plf_mgr->plf_get_om_ctrl_port();
    omIpStr      = *plf_mgr->plf_get_om_ip();

    use_om = !(conf_helper_.get_abs<bool>("fds.dm.no_om", false));
    useTestMode = conf_helper_.get_abs<bool>("fds.dm.testing.test_mode", false);
    if (useTestMode == true) {
        runMode = TEST_MODE;
    }
    LOGNORMAL << "Data Manager using control port " << plf_mgr->plf_get_my_ctrl_port();

    /* Set up FDSP RPC endpoints */
    nstable = boost::shared_ptr<netSessionTbl>(new netSessionTbl(FDSP_DATA_MGR));
    myIp = util::get_local_ip();
    assert(myIp.empty() == false);
    std::string node_name = "_DM_" + myIp;

    LOGNORMAL << "Data Manager using IP:"
              << myIp << " and node name " << node_name;

    setup_metadatapath_server(myIp);

    // Create a queue for system (background) tasks
    sysTaskQueue = new FDS_VolumeQueue(1024, 10000, 20, FdsDmSysTaskPrio);
    sysTaskQueue->activate();
    err = qosCtrl->registerVolume(FdsDmSysTaskId, sysTaskQueue);
    fds_verify(err.ok());
    LOGNORMAL << "Registered System Task Queue";

    if (use_om) {
        LOGDEBUG << " Initialising the OM client ";
        /*
         * Setup communication with OM.
         */
        omClient = new OMgrClient(FDSP_DATA_MGR,
                                  omIpStr,
                                  omConfigPort,
                                  node_name,
                                  GetLog(),
                                  nstable, plf_mgr);
        omClient->initialize();
        omClient->registerEventHandlerForNodeEvents(node_handler);
        omClient->registerEventHandlerForVolEvents(vol_handler);
        omClient->registerCatalogEventHandler(volcat_evt_handler);
        /*
         * Brings up the control path interface.
         * This does not require OM to be running and can
         * be used for testing DM by itself.
         */
        omClient->startAcceptingControlMessages();

        /*
         * Registers the DM with the OM. Uses OM for bootstrapping
         * on start. Requires the OM to be up and running prior.
         */
        omClient->registerNodeWithOM(plf_mgr);
    }

    if (runMode == TEST_MODE) {
        /*
         * Create test volumes.
         */
        std::string testVolName;
        VolumeDesc*  testVdb;
        for (fds_uint32_t testVolId = 1; testVolId < numTestVols + 1; testVolId++) {
            testVolName = "testVol" + std::to_string(testVolId);
            /*
             * We're using the ID as the min/max/priority
             * for the volume QoS.
             */
            testVdb = new VolumeDesc(testVolName,
                                     testVolId,
                                     testVolId,
                                     testVolId * 2,
                                     testVolId);
            fds_assert(testVdb != NULL);
            vol_handler(testVolId,
                        testVdb,
                        fds_notify_vol_add,
                        fpi::FDSP_NOTIFY_VOL_NO_FLAG,
                        FDS_ProtocolInterface::FDSP_ERR_OK);
            delete testVdb;
        }
    }

    setup_metasync_service();

    // finish setting up time volume catalog
    timeVolCat_->mod_startup();

    // register expunge callback
    // TODO(Andrew): Move the expunge work down to the volume
    // catalog so this callback isn't needed
    timeVolCat_->queryIface()->registerExpungeObjectsCb(std::bind(
        &DataMgr::expungeObjectsIfPrimary, this,
        std::placeholders::_1, std::placeholders::_2));
}

void DataMgr::setup_metasync_service()
{
    catSyncMgr.reset(new CatalogSyncMgr(1, this, nstable));
    // TODO(xxx) should we start catalog sync manager when no OM?
    catSyncMgr->mod_startup();
}


void DataMgr::swapMgrId(const FDS_ProtocolInterface::
                        FDSP_MsgHdrTypePtr& fdsp_msg) {
    FDS_ProtocolInterface::FDSP_MgrIdType temp_id;
    temp_id = fdsp_msg->dst_id;
    fdsp_msg->dst_id = fdsp_msg->src_id;
    fdsp_msg->src_id = temp_id;

    fds_uint64_t tmp_addr;
    tmp_addr = fdsp_msg->dst_ip_hi_addr;
    fdsp_msg->dst_ip_hi_addr = fdsp_msg->src_ip_hi_addr;
    fdsp_msg->src_ip_hi_addr = tmp_addr;

    tmp_addr = fdsp_msg->dst_ip_lo_addr;
    fdsp_msg->dst_ip_lo_addr = fdsp_msg->src_ip_lo_addr;
    fdsp_msg->src_ip_lo_addr = tmp_addr;

    fds_uint32_t tmp_port;
    tmp_port = fdsp_msg->dst_port;
    fdsp_msg->dst_port = fdsp_msg->src_port;
    fdsp_msg->src_port = tmp_port;
}

std::string DataMgr::getPrefix() const {
    return stor_prefix;
}

/*
 * Intended to be called while holding the vol_map_mtx.
 */
fds_bool_t DataMgr::volExistsLocked(fds_volid_t vol_uuid) const {
    if (vol_meta_map.count(vol_uuid) != 0) {
        return true;
    }
    return false;
}

fds_bool_t DataMgr::volExists(fds_volid_t vol_uuid) const {
    fds_bool_t result;
    vol_map_mtx->lock();
    result = volExistsLocked(vol_uuid);
    vol_map_mtx->unlock();

    return result;
}

void DataMgr::interrupt_cb(int signum) {
    LOGNORMAL << " Received signal "
              << signum << ". Shutting down communicator";
    catSyncMgr->mod_shutdown();
    timeVolCat_->mod_shutdown();
    exit(0);
}

DataMgr::ReqHandler::ReqHandler() {
}

DataMgr::ReqHandler::~ReqHandler() {
}

void DataMgr::ReqHandler::GetVolumeBlobList(FDSP_MsgHdrTypePtr& msg_hdr,
                                            FDSP_GetVolumeBlobListReqTypePtr& blobListReq) {
    Error err(ERR_OK);
    fds_panic("must not get here");
}

/**
 * Checks the current DMT to determine if this DM primary
 * or not for a given volume.
 */
fds_bool_t
DataMgr::amIPrimary(fds_volid_t volUuid) {
    DmtColumnPtr nodes = omClient->getDMTNodesForVolume(volUuid);
    fds_verify(nodes->getLength() > 0);

    const NodeUuid *mySvcUuid = plf_mgr->plf_get_my_svc_uuid();
    return (*mySvcUuid == nodes->get(0));
}

void DataMgr::startBlobTx(dmCatReq *io)
{
    Error err;
    DmIoStartBlobTx *startBlobReq= static_cast<DmIoStartBlobTx*>(io);

    LOGTRACE << "Will start transaction for blob " << startBlobReq->blob_name <<
            " in tvc; blob mode " << startBlobReq->blob_mode;
    err = timeVolCat_->startBlobTx(startBlobReq->volId,
                                    startBlobReq->blob_name,
                                    startBlobReq->blob_mode,
                                    startBlobReq->ioBlobTxDesc);
    qosCtrl->markIODone(*startBlobReq);
    startBlobReq->dmio_start_blob_tx_resp_cb(err, startBlobReq);
}

void DataMgr::updateCatalog(dmCatReq *io)
{
    Error err;
    DmIoUpdateCat *updCatReq= static_cast<DmIoUpdateCat*>(io);
    err = timeVolCat_->updateBlobTx(updCatReq->volId,
                                    updCatReq->ioBlobTxDesc,
                                    updCatReq->obj_list);
    qosCtrl->markIODone(*updCatReq);
    updCatReq->dmio_updatecat_resp_cb(err, updCatReq);
}

void DataMgr::commitBlobTx(dmCatReq *io)
{
    Error err;
    DmIoCommitBlobTx *commitBlobReq = static_cast<DmIoCommitBlobTx*>(io);

    LOGTRACE << "Will commit blob " << commitBlobReq->blob_name << " to tvc";
    err = timeVolCat_->commitBlobTx(commitBlobReq->volId,
                                    commitBlobReq->blob_name,
                                    commitBlobReq->ioBlobTxDesc,
                                    // TODO(Rao): We should use a static commit callback
                                    std::bind(&DataMgr::commitBlobTxCb, this,
                                              std::placeholders::_1, commitBlobReq));
    if (err != ERR_OK) {
        qosCtrl->markIODone(*commitBlobReq);
        commitBlobReq->dmio_commit_blob_tx_resp_cb(err, commitBlobReq);
    }
}

void DataMgr::commitBlobTxCb(const Error &err, DmIoCommitBlobTx *commitBlobReq)
{
    qosCtrl->markIODone(*commitBlobReq);
    commitBlobReq->dmio_commit_blob_tx_resp_cb(err, commitBlobReq);
}

//
// Handle forwarded (committed) catalog update from another DM
//
void DataMgr::fwdUpdateCatalog(dmCatReq *io)
{
    fds_panic("Not implemented!!!");
}

/*
    // TODO(Anna) DO NOT REMOVE THIS METHOD YET!!!!
    // NEED TO PORT FWD CATALOG!!!

void
DataMgr::updateCatalogBackend(dmCatReq  *updCatReq) {
    Error err(ERR_OK);

    BlobNode *bnode = NULL;
    // err = updateCatalogProcess(updCatReq, &bnode);
    if (err == ERR_OK) {
        fds_verify(bnode != NULL);
    }
    if (updCatReq->io_type == FDS_DM_FWD_CAT_UPD) {
        // we got this update forwarded from other DM
        // so do not reply to AM, but notify cat sync receiver
        // which will do all required post processing
        catSyncRecv->fwdUpdateReqDone(updCatReq, bnode->version, err,
                                      catSyncMgr->respCli(updCatReq->session_uuid));
        qosCtrl->markIODone(*updCatReq);
        delete updCatReq;
        return;
    }

    err = forwardUpdateCatalogRequest(updCatReq);
    if (!err.ok()) {
        // if  the update request  is not forwarded, send the response.
        sendUpdateCatalogResp(updCatReq, bnode);
    }

    // cleanup in any case (forwarded or respond to AM)
    if (bnode != NULL) {
        delete bnode;
    }

    qosCtrl->markIODone(*updCatReq);
    delete updCatReq;
}
*/

Error  DataMgr::forwardUpdateCatalogRequest(dmCatReq  *updCatReq) {
    Error err(ERR_OK);
    /*
     * we have updated the local Meta Db successfully, if the forwarding flag is set, 
     * forward the  request to the respective node 
     */
    if (updCatReq->transOp == fpi::FDS_DMGR_TXN_STATUS_COMMITED) {
        fds_bool_t do_forward = false;
        fds_bool_t do_finish = false;
        vol_map_mtx->lock();
        fds_verify(vol_meta_map.count(updCatReq->volId) > 0);
        VolumeMeta *vol_meta = vol_meta_map[updCatReq->volId];
        do_forward = vol_meta->isForwarding();
        do_finish  = vol_meta->isForwardFinish();
        vol_map_mtx->unlock();

        if ((do_forward) || (vol_meta->dmtclose_time > updCatReq->enqueue_time)) {
            err = catSyncMgr->forwardCatalogUpdate(updCatReq);
        } else {
            err = ERR_DMT_FORWARD;
        }

        // move the state, once we drain  planned queue contents
        LOGNORMAL << "DMT close Time:  " << vol_meta->dmtclose_time
                  << " Enqueue Time: " << updCatReq->enqueue_time;

        if ((vol_meta->dmtclose_time <= updCatReq->enqueue_time) && (do_finish)) {
            vol_meta->setForwardFinish();
            // remove the volume from sync_volume list
            LOGNORMAL << "CleanUP: remove Volume " << updCatReq->volId;
            catSyncMgr->finishedForwardVolmeta(updCatReq->volId);
        }
    } else {
        err = ERR_DMT_FORWARD;
    }

    return err;
}



Error
DataMgr::updateCatalogInternal(FDSP_UpdateCatalogTypePtr updCatReq,
                               fds_volid_t volId, fds_uint32_t srcIp,
                               fds_uint32_t dstIp, fds_uint32_t srcPort,
                               fds_uint32_t dstPort, std::string session_uuid,
                               fds_uint32_t reqCookie, std::string session_cache) {
    fds::Error err(fds::ERR_OK);

    /*
     * allocate a new update cat log  class and  queue  to per volume queue.
     */
    dmCatReq *dmUpdReq = new dmCatReq(volId, updCatReq->blob_name,
                                      updCatReq->dm_transaction_id, updCatReq->dm_operation, srcIp,
                                      dstIp, srcPort, dstPort, session_uuid, reqCookie, FDS_CAT_UPD,
                                      updCatReq);

    dmUpdReq->session_cache = session_cache;
    err = qosCtrl->enqueueIO(volId, static_cast<FDS_IOType*>(dmUpdReq));
    if (err != ERR_OK) {
        LOGERROR << "Unable to enqueue Update Catalog request "
                 << reqCookie << " error " << err.GetErrstr();
        return err;
    } else {
        LOGDEBUG << "Successfully enqueued   update Catalog  request "
                 << reqCookie;
    }

    return err;
}

void
DataMgr::ReqHandler::StartBlobTx(FDS_ProtocolInterface::FDSP_MsgHdrTypePtr& msgHdr,
                                 boost::shared_ptr<std::string> &volumeName,
                                 boost::shared_ptr<std::string> &blobName,
                                 FDS_ProtocolInterface::TxDescriptorPtr &txDesc) {
    GLOGDEBUG << "Received start blob transction request for volume "
              << *volumeName << " and blob " << *blobName;
    fds_panic("must not get here");
}

void
DataMgr::ReqHandler::StatBlob(FDS_ProtocolInterface::FDSP_MsgHdrTypePtr& msgHdr,
                              boost::shared_ptr<std::string> &volumeName,
                              boost::shared_ptr<std::string> &blobName) {
    GLOGDEBUG << "Received stat blob requested for volume "
              << *volumeName << " and blob " << *blobName;
    fds_panic("must not get here");
}

void DataMgr::ReqHandler::UpdateCatalogObject(FDS_ProtocolInterface::
                                              FDSP_MsgHdrTypePtr &msg_hdr,
                                              FDS_ProtocolInterface::
                                              FDSP_UpdateCatalogTypePtr
                                              &update_catalog) {
    Error err(ERR_OK);
    fds_panic("must not get here");
}

void
DataMgr::scheduleAbortBlobTxSvc(void * _io)
{
    Error err(ERR_OK);
    DmIoAbortBlobTx *abortBlobTx = static_cast<DmIoAbortBlobTx*>(_io);

    BlobTxId::const_ptr blobTxId = abortBlobTx->ioBlobTxDesc;
    fds_verify(*blobTxId != blobTxIdInvalid);
    /*
     * TODO(sanjay) we will have  intergrate this with TVC  API's
     */

    qosCtrl->markIODone(*abortBlobTx);
    abortBlobTx->dmio_abort_blob_tx_resp_cb(err, abortBlobTx);
}

void
DataMgr::queryCatalogBackendSvc(void * _io)
{
    Error err(ERR_OK);
    DmIoQueryCat *qryCatReq = static_cast<DmIoQueryCat*>(_io);

    err = timeVolCat_->queryIface()->getBlob(qryCatReq->volId,
                                             qryCatReq->blob_name,
                                             &(qryCatReq->blob_version),
                                             &(qryCatReq->queryMsg->meta_list),
                                             &(qryCatReq->queryMsg->obj_list));
    qosCtrl->markIODone(*qryCatReq);
    // TODO(Andrew): Note the cat request gets freed
    // by the callback
    qryCatReq->dmio_querycat_resp_cb(err, qryCatReq);
}

void DataMgr::ReqHandler::QueryCatalogObject(FDS_ProtocolInterface::
                                             FDSP_MsgHdrTypePtr &msg_hdr,
                                             FDS_ProtocolInterface::
                                             FDSP_QueryCatalogTypePtr
                                             &query_catalog) {
    Error err(ERR_OK);
    fds_panic("must not get here");
}


/**
 * Make snapshot of volume catalog for sync and notify
 * CatalogSync.
 */
void
DataMgr::snapVolCat(dmCatReq *io) {
    Error err(ERR_OK);
    fds_verify(io != NULL);

    DmIoSnapVolCat *snapReq = static_cast<DmIoSnapVolCat*>(io);
    fds_verify(snapReq != NULL);

    LOGDEBUG << "Will do first or second rsync for volume "
             << std::hex << snapReq->volId << " to node "
             << (snapReq->node_uuid).uuid_get_val() << std::dec;

    if (io->io_type == FDS_DM_SNAPDELTA_VOLCAT) {
        // we are doing second rsync
        // TODO(Anna) set state to VFORWARD_STATE_INPROG
    }

    // sync the catalog
    err = timeVolCat_->queryIface()->syncCatalog(snapReq->volId,
                                                 snapReq->node_uuid);

    // notify sync mgr that we did rsync
    snapReq->dmio_snap_vcat_cb(snapReq->volId, err);

    // mark this request as complete
    qosCtrl->markIODone(*snapReq);
    delete snapReq;
}

/**
 * Populates an fdsp message header with stock fields.
 *
 * @param[in] Ptr to msg header to modify
 */
void
DataMgr::initSmMsgHdr(FDSP_MsgHdrTypePtr msgHdr) {
    msgHdr->minor_ver = 0;
    msgHdr->msg_id    = 1;

    msgHdr->major_ver = 0xa5;
    msgHdr->minor_ver = 0x5a;

    msgHdr->num_objects = 1;
    msgHdr->frag_len    = 0;
    msgHdr->frag_num    = 0;

    msgHdr->tennant_id      = 0;
    msgHdr->local_domain_id = 0;

    msgHdr->src_id = FDSP_DATA_MGR;
    msgHdr->dst_id = FDSP_STOR_MGR;

    msgHdr->src_node_name = *(plf_mgr->plf_get_my_name());

    msgHdr->origin_timestamp = fds::get_fds_timestamp_ms();

    msgHdr->err_code = ERR_OK;
    msgHdr->result   = FDSP_ERR_OK;
}

/**
 * Issues delete calls for a set of objects in 'oids' list
 * if DM is primary for volume 'volid'
 */
Error
DataMgr::expungeObjectsIfPrimary(fds_volid_t volid,
                                 const std::vector<ObjectID>& oids) {
    Error err(ERR_OK);
    if (runMode == TEST_MODE) return err;  // no SMs, noone to notify
    if (amIPrimary(volid) == false) return err;  // not primary

    for (std::vector<ObjectID>::const_iterator cit = oids.cbegin();
         cit != oids.cend();
         ++cit) {
        err = expungeObject(volid, *cit);
        fds_verify(err == ERR_OK);
    }
    return err;
}

/**
 * Issues delete calls for an object when it is dereferenced
 * by a blob. Objects should only be expunged whenever a
 * blob's reference to a object is permanently removed.
 *
 * @param[in] The volume in which the obj is being deleted
 * @param[in] The object to expunge
 * return The result of the expunge
 */
Error
DataMgr::expungeObject(fds_volid_t volId, const ObjectID &objId) {
    Error err(ERR_OK);

    // Locate the SMs holding this blob
    DltTokenGroupPtr tokenGroup =
            omClient->getDLTNodesForDoidKey(objId);

    FDSP_MsgHdrTypePtr msgHdr(new FDSP_MsgHdrType);
    initSmMsgHdr(msgHdr);
    msgHdr->msg_code       = FDSP_MSG_DELETE_OBJ_REQ;
    msgHdr->glob_volume_id = volId;
    msgHdr->req_cookie     = 1;
    FDSP_DeleteObjTypePtr delReq(new FDSP_DeleteObjType);
    delReq->data_obj_id.digest.assign((const char *)objId.GetId(),
                                      (size_t)objId.GetLen());
    delReq->dlt_version = omClient->getDltVersion();
    delReq->data_obj_len = 0;  // What is this...?

    // Issue async delete object calls to each SM
    uint errorCount = 0;
    for (fds_uint32_t i = 0; i < tokenGroup->getLength(); i++) {
        try {
            NodeUuid uuid = tokenGroup->get(i);
            // NodeAgent::pointer node = Platform::plf_dm_nodes()->agent_info(uuid);
            NodeAgent::pointer node = plf_mgr->plf_node_inventory()->
                    dc_get_sm_nodes()->agent_info(uuid);
            SmAgent::pointer sm = agt_cast_ptr<SmAgent>(node);
            NodeAgentDpClientPtr smClient = sm->get_sm_client();

            msgHdr->session_uuid = sm->get_sm_sess_id();
            smClient->DeleteObject(msgHdr, delReq);
        } catch(const att::TTransportException& e) {
            errorCount++;
            GLOGERROR << "[" << errorCount << "] error during network call : " << e.what();
        }
    }

    if (errorCount >= static_cast<int>(ceil(tokenGroup->getLength()*0.5))) {
        LOGCRITICAL << "too many network errors ["
                    << errorCount << "/" <<tokenGroup->getLength() <<"]";
        return ERR_NETWORK_TRANSPORT;
    }

    return err;
}

void DataMgr::scheduleGetBlobMetaDataSvc(void *_io) {
    Error err(ERR_OK);
    DmIoGetBlobMetaData *getBlbMeta = static_cast<DmIoGetBlobMetaData*>(_io);

    // TODO(Andrew): We're not using the size...we can remove it
    fds_uint64_t blobSize;
    err = timeVolCat_->queryIface()->getBlobMeta(getBlbMeta->volId,
                                             getBlbMeta->blob_name,
                                             &(getBlbMeta->blob_version),
                                             &(blobSize),
                                             &(getBlbMeta->message->metaDataList));

    getBlbMeta->message->byteCount = blobSize;
    qosCtrl->markIODone(*getBlbMeta);
    // TODO(Andrew): Note the cat request gets freed
    // by the callback
    getBlbMeta->dmio_getmd_resp_cb(err, getBlbMeta);
}

void DataMgr::setBlobMetaDataSvc(void *io) {
    Error err;
    DmIoSetBlobMetaData *setBlbMetaReq = static_cast<DmIoSetBlobMetaData*>(io);
    err = timeVolCat_->updateBlobTx(setBlbMetaReq->volId,
                                    setBlbMetaReq->ioBlobTxDesc,
                                    setBlbMetaReq->md_list);
    qosCtrl->markIODone(*setBlbMetaReq);
    setBlbMetaReq->dmio_setmd_resp_cb(err, setBlbMetaReq);
}

void DataMgr::getVolumeMetaData(dmCatReq *io) {
    Error err(ERR_OK);
    // TODO(xxx) implement using new vol cat methods
    // and new svc layer
    fds_panic("not implemented");
}

void DataMgr::ReqHandler::DeleteCatalogObject(FDS_ProtocolInterface::
                                              FDSP_MsgHdrTypePtr &msg_hdr,
                                              FDS_ProtocolInterface::
                                              FDSP_DeleteCatalogTypePtr
                                              &delete_catalog) {
    Error err(ERR_OK);
    fds_panic("must not get here");
}

void DataMgr::ReqHandler::SetBlobMetaData(boost::shared_ptr<FDSP_MsgHdrType>& msgHeader,
                                          boost::shared_ptr<std::string>& volumeName,
                                          boost::shared_ptr<std::string>& blobName,
                                          boost::shared_ptr<FDSP_MetaDataList>& metaDataList) {
    GLOGDEBUG << " Set metadata for volume:" << *volumeName
              << " blob:" << *blobName;
    fds_panic("must not get here");
}

void DataMgr::ReqHandler::GetBlobMetaData(boost::shared_ptr<FDSP_MsgHdrType>& msgHeader,
                                          boost::shared_ptr<std::string>& volumeName,
                                          boost::shared_ptr<std::string>& blobName) {
    GLOGDEBUG << " volume:" << *volumeName
             << " blob:" << *blobName;
    fds_panic("must not get here");
}

void DataMgr::ReqHandler::GetVolumeMetaData(boost::shared_ptr<FDSP_MsgHdrType>& header,
                                            boost::shared_ptr<std::string>& volumeName) {
    Error err(ERR_OK);
    GLOGDEBUG << " volume:" << *volumeName << " txnid:" << header->req_cookie;

    RequestHeader reqHeader(header);
    dmCatReq* request = new dmCatReq(reqHeader, FDS_GET_VOLUME_METADATA);
    err = dataMgr->qosCtrl->enqueueIO(request->volId, request);
    fds_verify(err == ERR_OK);
}

void DataMgr::InitMsgHdr(const FDSP_MsgHdrTypePtr& msg_hdr)
{
    msg_hdr->minor_ver = 0;
    msg_hdr->msg_code = FDSP_MSG_PUT_OBJ_REQ;
    msg_hdr->msg_id =  1;

    msg_hdr->major_ver = 0xa5;
    msg_hdr->minor_ver = 0x5a;

    msg_hdr->num_objects = 1;
    msg_hdr->frag_len = 0;
    msg_hdr->frag_num = 0;

    msg_hdr->tennant_id = 0;
    msg_hdr->local_domain_id = 0;

    msg_hdr->src_id = FDSP_DATA_MGR;
    msg_hdr->dst_id = FDSP_STOR_MGR;

    msg_hdr->src_node_name = "";

    msg_hdr->err_code = ERR_OK;
    msg_hdr->result = FDSP_ERR_OK;
}

void CloseDMTTimerTask::runTimerTask() {
    timeout_cb();
}

void DataMgr::setResponseError(fpi::FDSP_MsgHdrTypePtr& msg_hdr, const Error& err) {
    if (err.ok()) {
        msg_hdr->result  = fpi::FDSP_ERR_OK;
        msg_hdr->err_msg = "OK";
    } else {
        msg_hdr->result   = fpi::FDSP_ERR_FAILED;
        msg_hdr->err_msg  = "FDSP_ERR_FAILED";
        msg_hdr->err_code = err.GetErrno();
    }
}

}  // namespace fds
