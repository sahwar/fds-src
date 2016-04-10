/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#include <DataMgr.h>
#include <fdsp_utils.h>
#include <util/path.h>
#include <DMSvcHandler.h>
#include <StatStreamAggregator.h>
#include <fdsp/dm_api_types.h>
#include <err.h>

namespace fds {
DMSvcHandler::DMSvcHandler(CommonModuleProviderIf *provider, DataMgr& dataManager)
        : PlatNetSvcHandler(provider),
          dataManager_(dataManager)
{
    REGISTER_FDSP_MSG_HANDLER(fpi::StatStreamRegistrationMsg, registerStreaming);
    REGISTER_FDSP_MSG_HANDLER(fpi::StatStreamDeregistrationMsg, deregisterStreaming);

    /* DM to DM service messages */
    REGISTER_FDSP_MSG_HANDLER(fpi::VolSyncStateMsg, volSyncState);

    /* OM to DM snapshot messages */
    REGISTER_FDSP_MSG_HANDLER(fpi::CreateSnapshotMsg, createSnapshot);
    REGISTER_FDSP_MSG_HANDLER(fpi::DeleteSnapshotMsg, deleteSnapshot);
    REGISTER_FDSP_MSG_HANDLER(fpi::CreateVolumeCloneMsg, createVolumeClone);
    REGISTER_FDSP_MSG_HANDLER(fpi::NodeSvcInfo, notifySvcChange);
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlNotifyVolAdd, NotifyAddVol);
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlNotifyVolRemove, NotifyRmVol);
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlNotifyVolMod, NotifyModVol);
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlNotifyDMTClose, NotifyDMTClose);
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlNotifyDMTUpdate, NotifyDMTUpdate);
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlNotifyDLTUpdate, NotifyDLTUpdate);
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlDMMigrateMeta, StartDMMetaMigration);
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlNotifyDMAbortMigration, NotifyDMAbortMigration);
    REGISTER_FDSP_MSG_HANDLER(fpi::PrepareForShutdownMsg, shutdownDM);

    /* DM Debug messages */
    REGISTER_FDSP_MSG_HANDLER(fpi::DbgForceVolumeSyncMsg, handleDbgForceVolumeSyncMsg);
    REGISTER_FDSP_MSG_HANDLER(fpi::DbgOfflineVolumeGroupMsg, handleDbgOfflineVolumeGroupMsg);
    REGISTER_FDSP_MSG_HANDLER(fpi::CopyVolumeMsg, handleCopyVolume);
    REGISTER_FDSP_MSG_HANDLER(fpi::ArchiveMsg, handleArchive);
    REGISTER_FDSP_MSG_HANDLER(fpi::ArchiveRespMsg, handleArchiveResp);

    /* Volume Group messages */
    registerDmVolumeReqHandler<DmIoVolumegroupUpdate>();
    registerDmVolumeReqHandler<DmIoFinishStaticMigration>();

    /* Volume Checker messages */
    registerDmVolumeReqHandler<DmIoVolumeCheck>();
}

// notifySvcChange
// ---------------
//
void
DMSvcHandler::notifySvcChange(boost::shared_ptr<fpi::AsyncHdr>    &hdr,
                              boost::shared_ptr<fpi::NodeSvcInfo> &msg)
{
#if 0
    DomainAgent::pointer self;

    self = NetPlatform::nplat_singleton()->nplat_self();
    fds_verify(self != NULL);

    auto list = msg->node_svc_list;
    for (auto it = list.cbegin(); it != list.cend(); it++) {
        auto rec = *it;
        if (rec.svc_type == fpi::FDSP_DATA_MGR) {
            self->agent_fsm_input(msg, rec.svc_type,
                                  rec.svc_runtime_state, rec.svc_deployment_state);
        }
    }
#endif
}

// NotifyAddVol
// ------------
//
void
DMSvcHandler::NotifyAddVol(boost::shared_ptr<fpi::AsyncHdr>         &hdr,
                           boost::shared_ptr<fpi::CtrlNotifyVolAdd> &msg)
{
    DBG(GLOGDEBUG << logString(*hdr) << "Vol Add");  // logString(*msg));
    fds_verify(msg->__isset.vol_desc);
    auto func = [this, hdr, msg]() {
        fds_volid_t vol_uuid (msg->vol_desc.volUUID);
        Error err(ERR_OK);
        VolumeDesc desc(msg->vol_desc);

        GLOGNOTIFY << "rcvd create for vol:"
        << msg->vol_desc.volUUID
        << " name:" << desc.getName();

        err = this->dataManager_.addVolume(dataManager_.getPrefix() + std::to_string(vol_uuid.get()),
                                     vol_uuid,
                                     &desc);

        hdr->msg_code = err.GetErrno();
        sendAsyncResp(*hdr, FDSP_MSG_TYPEID(fpi::CtrlNotifyVolAdd), *msg);
    };

    dataManager_.addToQueue(func);

}

// NotifyRmVol
// -----------
//
void
DMSvcHandler::NotifyRmVol(boost::shared_ptr<fpi::AsyncHdr>            &hdr,
                          boost::shared_ptr<fpi::CtrlNotifyVolRemove> &msg)
{
    DBG(GLOGDEBUG << logString(*hdr) << "Vol Remove");  // logString(*msg));
    fds_verify(msg->__isset.vol_desc);
    auto func = [this, hdr, msg]() {
        fds_volid_t vol_uuid (msg->vol_desc.volUUID);
        Error err(ERR_OK);
        bool fCheck = (msg->vol_flag == fpi::FDSP_NOTIFY_VOL_CHECK_ONLY);
        auto volDesc = this->dataManager_.getVolumeDesc(vol_uuid);
        if (!volDesc) {
            LOGERROR << "Volume NOT found vol:" << vol_uuid;
            err = ERR_VOL_NOT_FOUND;
        } else {
            LOGNOTIFY << "will delete volume:" << vol_uuid << ":" << volDesc->name;
            if (msg->vol_desc.fSnapshot) {
                if (fCheck) {
                    err = ERR_OK;
                } else {
                    err = this->dataManager_.timelineMgr->deleteSnapshot(volDesc->srcVolumeId, volDesc->volUUID);
                }
            } else {
                if (fCheck) {
                    err = this->dataManager_.removeVolume(vol_uuid, true);
                } else {
                    err = this->dataManager_.removeVolume(vol_uuid, false);
                }
            }
        }
        hdr->msg_code = err.GetErrno();
        sendAsyncResp(*hdr, FDSP_MSG_TYPEID(fpi::CtrlNotifyVolRemove), *msg);
    };

    dataManager_.addToQueue(func, fds_volid_t(msg->vol_desc.volUUID));
}

// ------------
//
void
DMSvcHandler::NotifyModVol(boost::shared_ptr<fpi::AsyncHdr>         &hdr,
                           boost::shared_ptr<fpi::CtrlNotifyVolMod> &vol_msg)
{
    DBG(GLOGDEBUG << logString(*hdr) << "vol modify");  //  logString(*vol_msg));
    fds_verify(vol_msg->__isset.vol_desc);
    fds_volid_t vol_uuid (vol_msg->vol_desc.volUUID);
    Error err(ERR_OK);
    VolumeDesc desc(vol_msg->vol_desc);
    err = dataManager_._process_mod_vol(vol_uuid, desc);
    hdr->msg_code = err.GetErrno();
    sendAsyncResp(*hdr, FDSP_MSG_TYPEID(fpi::CtrlNotifyVolMod), *vol_msg);
}



void DMSvcHandler::createSnapshot(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                                  boost::shared_ptr<fpi::CreateSnapshotMsg>& createSnapshot)
{
    Error err(ERR_OK);
    fds_assert(createSnapshot);

    /*
     * get the snapshot manager instanace
     * invoke the createSnapshot DM function
     */
    // TODO(Sanjay) revisit  this when we enable the server layer interface
    // err = dataMgr->createSnapshot(createSnapshot->snapshot);

    asyncHdr->msg_code = static_cast<int32_t>(err.GetErrno());
    fpi::CreateSnapshotRespMsg createSnapshotResp;
    /*
     * init the response message with  snapshot id
     */
    sendAsyncResp(*asyncHdr, FDSP_MSG_TYPEID(fpi::CreateSnapshotRespMsg), createSnapshotResp);
}

void DMSvcHandler::deleteSnapshot(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                                  boost::shared_ptr<fpi::DeleteSnapshotMsg>& deleteSnapshot)
{
    Error err(ERR_OK);
    fds_assert(deleteSnapshot);

    /*
     * get the snapshot manager instanace
     * invoke the deleteSnapshot DM function
     */
    fds_volid_t vol_uuid (deleteSnapshot->snapshotId);
    err = dataManager_.removeVolume(vol_uuid, true);
    if (err.ok()) {
        err = dataManager_.removeVolume(vol_uuid, false);
    }

    asyncHdr->msg_code = static_cast<int32_t>(err.GetErrno());
    fpi::DeleteSnapshotRespMsg deleteSnapshotResp;
    /*
     * init the response message with  snapshot id
     */
    sendAsyncResp(*asyncHdr, FDSP_MSG_TYPEID(fpi::DeleteSnapshotMsg), deleteSnapshotResp);
}

void DMSvcHandler::createVolumeClone(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                                     boost::shared_ptr<fpi::CreateVolumeCloneMsg>& createClone)
{
    Error err(ERR_OK);
    fds_assert(createClone);

    /*
     * get the snapshot manager instanace
     * invoke the createClone DM function
     */
    asyncHdr->msg_code = static_cast<int32_t>(err.GetErrno());
    fpi::CreateVolumeCloneRespMsg createVolumeCloneResp;
    /*
     * init the response message with  Clone id
     */
    sendAsyncResp(*asyncHdr, FDSP_MSG_TYPEID(fpi::CreateVolumeCloneRespMsg),
                  createVolumeCloneResp);
}

/**
 * Destination handler for receiving a VolsyncStateMsg (rsync has finished).
 *
 * @param[in] asyncHdr the async header sent with svc layer request
 * @param[in] fwdMsg the VolSyncState message
 *
 */
void DMSvcHandler::volSyncState(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                                boost::shared_ptr<fpi::VolSyncStateMsg>& syncStateMsg)
{
    Error err(ERR_OK);

    // synchronous call to process the volume sync state
    err = dataManager_.processVolSyncState(fds_volid_t(syncStateMsg->volume_id),
                                           syncStateMsg->forward_complete);

    asyncHdr->msg_code = err.GetErrno();
    fpi::VolSyncStateRspMsg volSyncStateRspMsg;
    // TODO(Brian): send a response here, make sure we've set the cb properly in the caller first
    // sendAsyncResp(*asyncHdr, FDSP_MSG_TYPEID(VolSyncStateRspMsg), VolSyncStateRspMsg);
}

void
DMSvcHandler::registerStreaming(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                                boost::shared_ptr<fpi::StatStreamRegistrationMsg>& streamRegstrMsg) { //NOLINT
    StatStreamAggregator::ptr statAggr = dataManager_.statStreamAggregator();
    if (!statAggr) {
        LOGWARN << "statStreamAggregator is not initialised";
        return;
    }
    fds_assert(statAggr);
    fds_assert(streamRegstrMsg);

    Error err = statAggr->registerStream(streamRegstrMsg);

    asyncHdr->msg_code = static_cast<int32_t>(err.GetErrno());
    fpi::StatStreamRegistrationRspMsg resp;
    // since OM did not implement response yet, just not send response for now..
    // sendAsyncResp(*asyncHdr, FDSP_MSG_TYPEID(StatStreamRegistrationRspMsg), resp);
}

void
DMSvcHandler::deregisterStreaming(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                                  boost::shared_ptr<fpi::StatStreamDeregistrationMsg>& streamDeregstrMsg) { //NOLINT
    StatStreamAggregator::ptr statAggr = dataManager_.statStreamAggregator();
    fds_assert(statAggr);
    fds_assert(streamDeregstrMsg);

    Error err = statAggr->deregisterStream(streamDeregstrMsg->id);

    asyncHdr->msg_code = static_cast<int32_t>(err.GetErrno());
    fpi::StatStreamDeregistrationRspMsg resp;
    sendAsyncResp(*asyncHdr, FDSP_MSG_TYPEID(fpi::StatStreamDeregistrationRspMsg), resp);
}

void
DMSvcHandler::NotifyDLTUpdate(boost::shared_ptr<fpi::AsyncHdr>            &hdr,
                              boost::shared_ptr<fpi::CtrlNotifyDLTUpdate> &dlt)
{
    Error err(ERR_OK);
    LOGNOTIFY << "OMClient received new DLT commit version  "
              << dlt->dlt_data.dlt_type;

    DLTManagerPtr dltMgr = MODULEPROVIDER()->getSvcMgr()->getDltManager();
    err = dltMgr->addSerializedDLT(dlt->dlt_data.dlt_data,
                                   std::bind(
                                       &DMSvcHandler::NotifyDLTUpdateCb,
                                       this, hdr, dlt,
                                       std::placeholders::_1),
                                   dlt->dlt_data.dlt_type);
    if (err.ok() || (err == ERR_IO_PENDING)) {
        // added DLT
        dltMgr->dump();
    } else if (err == ERR_DUPLICATE) {
        LOGWARN << "Received duplicate DLT version, ignoring";
        err = ERR_OK;
    } else {
        LOGERROR << "Failed to update DLT! Check dlt_data was set " << err;
    }

    // send response right away on error or if there is no IO pending for
    // the previous DLT
    if (err != ERR_IO_PENDING) {
        NotifyDLTUpdateCb(hdr, dlt, err);
    }
    // else we will get a callback from DLT manager when there are no more
    // IO pending for the previous DLT, and then we will send response
}

void
DMSvcHandler::NotifyDLTUpdateCb(boost::shared_ptr<fpi::AsyncHdr>            &hdr,
                                boost::shared_ptr<fpi::CtrlNotifyDLTUpdate> &dlt,
                                const Error                                 &err) {
    LOGDEBUG << "Sending response for DLT version " << dlt->dlt_data.dlt_type
             << " "  << err;
    hdr->msg_code = static_cast<int32_t>(err.GetErrno());
    sendAsyncResp(*hdr, FDSP_MSG_TYPEID(fpi::CtrlNotifyDLTUpdate), *dlt);
}

void
DMSvcHandler::StartDMMetaMigration(boost::shared_ptr<fpi::AsyncHdr>            &hdr,
                                   boost::shared_ptr<fpi::CtrlDMMigrateMeta>   &migrMsg)
{
    Error err(ERR_OK);
    LOGNOTIFY << "Will start meta migration";

    // TODO(Anna) DM migration needs to be re-written, returning success right away
    // without doing actual migration so DMT state machine can move forward
    // IMPLEMENT DM MIGRATION
    LOGWARN << "DM migration not implemented, not migrating meta!";
    StartDMMetaMigrationCb(hdr, err);
    return;

    // see if DM sync feature is enabled
    if (dataManager_.features.isCatSyncEnabled()) {
        err = dataManager_
                .catSyncMgr
                ->startCatalogSync(migrMsg->metaVol, std::bind(&DMSvcHandler::StartDMMetaMigrationCb,
                                                               this,
                                                               hdr,
                                                               std::placeholders::_1));
    } else {
        LOGWARN << "catalog sync feature NOT enabled -- not going to migrate volume meta";
        // ok we just respond...
        StartDMMetaMigrationCb(hdr, err);
        return;
    }
    if (!err.ok()) {
        StartDMMetaMigrationCb(hdr, err);
    }
}

void DMSvcHandler::StartDMMetaMigrationCb(boost::shared_ptr<fpi::AsyncHdr> &hdr,
                                          const Error &err)
{
    LOGDEBUG << "Sending async DM meta migration ack " << err;
    hdr->msg_code = err.GetErrno();
    sendAsyncResp(*hdr, FDSP_MSG_TYPEID(fpi::EmptyMsg), fpi::EmptyMsg());
}

void
DMSvcHandler::NotifyDMTUpdate(boost::shared_ptr<fpi::AsyncHdr>            &hdr,
                              boost::shared_ptr<fpi::CtrlNotifyDMTUpdate> &dmt)
{
    Error err(ERR_OK);
    LOGNOTIFY << "DMSvcHandler received new DMT commit version  "
              << dmt->dmt_version;
    err = MODULEPROVIDER()->getSvcMgr()->updateDmt(dmt->dmt_data.dmt_type, dmt->dmt_data.dmt_data, nullptr);
    if (err == ERR_DUPLICATE) {
        LOGWARN << "Received duplicate DMT (version " << dmt->dmt_version
                << "), ignoring...";
        NotifyDMTUpdateCb(hdr, ERR_OK);
        return;
    } else if (!err.ok()) {
        LOGERROR << "Failed to update DMT for version " << dmt->dmt_version << " " << err;
        NotifyDMTUpdateCb(hdr, err);
        return;
    }

    NotifyDMTUpdateCb(hdr, err);

    // get all volumes descriptors after every DMT update
    if (dataManager_.features.isVolumegroupingEnabled()) {
        auto lambda = [this] () {
            LOGNORMAL << "pulling all volumes after DMT update";
            dataManager_.getAllVolumeDescriptors();
        };
        dataManager_.addToQueue(lambda);
    }
}

void DMSvcHandler::NotifyDMTUpdateCb(boost::shared_ptr<fpi::AsyncHdr> &hdr,
                                     const Error &err)
{
    LOGDEBUG << "Sending async DMT update ack " << err;
    hdr->msg_code = err.GetErrno();
    fpi::CtrlNotifyDMTUpdatePtr msg(new fpi::CtrlNotifyDMTUpdate());
    sendAsyncResp(*hdr, FDSP_MSG_TYPEID(fpi::CtrlNotifyDMTUpdate), *msg);
}

void
DMSvcHandler::NotifyDMTClose(boost::shared_ptr<fpi::AsyncHdr>            &hdr,
                             boost::shared_ptr<fpi::CtrlNotifyDMTClose> &dmtClose) {
    LOGNOTIFY << "DMSvcHandler received DMT close: DMTVersion=" << dmtClose->dmt_close.DMT_version;
    Error err(ERR_OK);

    // TODO(xxx) notify volume sync that we can stop forwarding
    // updates to other DM

    dataManager_.sendDmtCloseCb = std::bind(&DMSvcHandler::NotifyDMTCloseCb,
                                            this,
                                            hdr,
                                            dmtClose,
                                            std::placeholders::_1);
    // will finish forwarding when all queued updates are processed
    err = dataManager_.notifyDMTClose(dmtClose->dmt_close.DMT_version);

    if (!err.ok()) {
        LOGERROR << "DMT Close, volume meta may not be synced properly";
        hdr->msg_code = err.GetErrno();
        sendAsyncResp(*hdr, FDSP_MSG_TYPEID(fpi::CtrlNotifyDMTClose), *dmtClose);
    }
}

void DMSvcHandler::NotifyDMTCloseCb(boost::shared_ptr<fpi::AsyncHdr> &hdr,
                                    boost::shared_ptr<fpi::CtrlNotifyDMTClose>& dmtClose,
                                    Error &err)
{
    LOGNOTIFY << "DMT close callback: DMTversion=" << dmtClose->dmt_close.DMT_version;

    // When DMT is closed, then delete unowned volumes iff DM Migration is active
    if (dataManager_.dmMigrationMgr->isMigrationEnabled()) {
        // TODO(james) : revisit this to see if this is needed.
        // dataManager_.deleteUnownedVolumes();
    } else {
        LOGNOTIFY << "DM Migration feature is disabled. Skipping removing unowned volumes.";
    }

    hdr->msg_code = err.GetErrno();
    sendAsyncResp(*hdr, FDSP_MSG_TYPEID(fpi::CtrlNotifyDMTClose), *dmtClose);
}

void DMSvcHandler::shutdownDM(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                              boost::shared_ptr<fpi::PrepareForShutdownMsg>& shutdownMsg) {
    LOGDEBUG << "Received shutdown message DM ... flush IOs..";
    dataManager_.shutdown();

    // respond to OM
    sendAsyncResp(*asyncHdr, FDSP_MSG_TYPEID(fpi::EmptyMsg), fpi::EmptyMsg());
}

void DMSvcHandler::NotifyDMAbortMigration(boost::shared_ptr<fpi::AsyncHdr>& hdr,
                                          boost::shared_ptr<fpi::CtrlNotifyDMAbortMigration>& abortMsg)
{
    Error err(ERR_OK);
    fds_uint64_t dmtVersion = abortMsg->DMT_version;
    LOGDEBUG << "Got abort migration, reverting to DMT version" << dmtVersion;

    // revert to DMT version provided in abort message
    if (abortMsg->DMT_version > 0) {
        err = MODULEPROVIDER()->getSvcMgr()->getDmtManager()->commitDMT(dmtVersion);
        if (err == ERR_NOT_FOUND) {
            LOGNOTIFY << "We did not revert to previous DMT, because DM did not receive it."
                      << " DM will not have any DMT, which is ok";
            err = ERR_OK;
        }
    }

    // Tell the DM Migration Mgr
    dataManager_.dmMigrationMgr->abortMigration();

    LOGNOTIFY << "DM Migration Manager aborted and cleaned up";

    // TODO(xxx): make abort cb
    fpi::CtrlNotifyDMAbortMigrationPtr msg(new fpi::CtrlNotifyDMAbortMigration());
    msg->DMT_version = abortMsg->DMT_version;
    hdr->msg_code = static_cast<int32_t>(err.GetErrno());
    sendAsyncResp(*hdr, FDSP_MSG_TYPEID(fpi::CtrlNotifyDMAbortMigration), *msg);
}

/**
* Debug only command
* Only inovke this call when you are certain volume doesn't have
* any oustanding jobs/operations running.
*
* @param hdr
* @param queryMsg
*/
void
DMSvcHandler::handleDbgForceVolumeSyncMsg(SHPTR<fpi::AsyncHdr>& hdr,
                                          SHPTR<fpi::DbgForceVolumeSyncMsg> &queryMsg)
{
    auto volMeta = dataManager_.getVolumeMeta(fds_volid_t(queryMsg->volId));
    auto cb = [this, hdr](const Error &e) {
        hdr->msg_code = e.GetErrno();
        sendAsyncResp(*hdr, FDSP_MSG_TYPEID(fpi::EmptyMsg), fpi::EmptyMsg());
    };

    if (volMeta == nullptr) {
        LOGWARN << "Failed to debug force volume sync.  volid: "
                << queryMsg->volId << " not found";
        cb(ERR_VOL_NOT_FOUND);
        return;
    }

    /* Execute under synchronized context */
    auto func = volMeta->makeSynchronized([volMeta, cb]() {
            if (!volMeta->isCoordinatorSet()) {
                LOGWARN << "Force sync failed.  Volume coordinator is not set."
                << volMeta->logString();
                cb(ERR_INVALID);
                return;
            } else if (volMeta->isInitializerInProgress()) {
                LOGWARN << "Force sync failed. Volume sync is in progress" << volMeta->logString();
                cb(ERR_INVALID);
                return;
            }

            LOGNORMAL << "Doing a force sync volume.  Current state: " << volMeta->logString();

            volMeta->setState(fpi::Offline, "force sync message");
            volMeta->scheduleInitializer(true);
            cb(ERR_OK);
        });
    func();
}

void
DMSvcHandler::handleDbgOfflineVolumeGroupMsg(SHPTR<fpi::AsyncHdr>& hdr,
                                          SHPTR<fpi::DbgOfflineVolumeGroupMsg> &offlineMsg)
{
    auto volMeta = dataManager_.getVolumeMeta(fds_volid_t(offlineMsg->volId));
    auto cb = [this, hdr](const Error &e) {
        hdr->msg_code = e.GetErrno();
        sendAsyncResp(*hdr, FDSP_MSG_TYPEID(fpi::EmptyMsg), fpi::EmptyMsg());
    };

    if (volMeta == nullptr) {
        LOGWARN << "Failed to debug offline volume group.  volid: "
                << offlineMsg->volId << " not found";
        cb(ERR_VOL_NOT_FOUND);
        return;
    }

    /* Execute under synchronized context */
    auto func = volMeta->makeSynchronized([volMeta, cb]() {
            if (volMeta->isInitializerInProgress()) {
                LOGWARN << "Offline volume group failed.  Volume sync is in progress"
                    << volMeta->logString();
                cb(ERR_INVALID);
                return;
            }

            LOGNORMAL << "Offlining volume group.  Current state: " << volMeta->logString();

            fpi::VolumeGroupCoordinatorInfo coordinator;
            coordinator.id.svc_uuid = 0;
            coordinator.version = 0;

            volMeta->setState(fpi::Offline, "offline volume group");
            volMeta->setCoordinator(coordinator);
            cb(ERR_OK);
        });
    func();
}
void
DMSvcHandler::handleCopyVolume(SHPTR<fpi::AsyncHdr> &hdr, SHPTR<fpi::CopyVolumeMsg> &copyMsg) {
    Error err;

    auto volId = fds_volid_t(copyMsg->volId);
    fpi::SvcUuid svcId;
    svcId.__set_svc_uuid(copyMsg->destDmUuid);
    err = dataManager_.copyVolumeToTargetDM(svcId,volId, copyMsg->archivePolicy);

    hdr->msg_code = static_cast<int32_t>(err.GetErrno());
    hdr->msg_type_id = fpi::EmptyMsgTypeId;
    sendAsyncResp(*hdr,fpi::EmptyMsgTypeId, fpi::EmptyMsg());
}

void
DMSvcHandler::handleArchive(SHPTR<fpi::AsyncHdr> &hdr, SHPTR<fpi::ArchiveMsg> &archiveMsg) {
    Error err;
    auto volId = fds_volid_t(archiveMsg->volId);

    err = dataManager_.archiveTargetVolume(volId);

    hdr->msg_code = static_cast<int32_t>(err.GetErrno());
    hdr->msg_type_id = fpi::ArchiveRespMsgTypeId;
    sendAsyncResp(*hdr,fpi::ArchiveRespMsgTypeId,fpi::ArchiveRespMsg());
}


void DMSvcHandler::handleArchiveResp(SHPTR<fpi::AsyncHdr> &hdr, SHPTR<fpi::ArchiveRespMsg> &archiveMsg) {
}

}  // namespace fds
