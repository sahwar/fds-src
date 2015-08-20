/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

#include <functional>
#include <dmhandler.h>
#include <util/Log.h>
#include <fds_error.h>
#include <DMSvcHandler.h>  // This shouldn't be necessary, included because of
                           // incomplete type errors in PlatNetSvcHandler.h
#include <net/PlatNetSvcHandler.h>
#include <PerfTrace.h>
#include <VolumeMeta.h>
#include <DmIoReq.h>

namespace fds {
namespace dm {

/**
 * DmMigrationHandler
 */
DmMigrationHandler::DmMigrationHandler(DataMgr& dataManager)
    : Handler(dataManager)
{
    if (!dataManager.features.isTestModeEnabled()) {
        REGISTER_DM_MSG_HANDLER(fpi::CtrlNotifyDMStartMigrationMsg, handleRequest);
    }
}

void DmMigrationHandler::handleRequest(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                                        boost::shared_ptr<fpi::CtrlNotifyDMStartMigrationMsg>& message) {
    LOGMIGRATE << logString(*asyncHdr) << logString(*message);

    auto dmReq = new DmIoMigration(FdsDmSysTaskId, message);
    dmReq->cb = BIND_MSG_CALLBACK(DmMigrationHandler::handleResponse, asyncHdr, message);
    dmReq->localCb = std::bind(&DmMigrationHandler::handleResponseReal,
                               this,
                               asyncHdr,
                               message->DMT_version,
                               std::placeholders::_1);

    fds_verify(dmReq->io_vol_id == FdsDmSysTaskId);
    fds_verify(dmReq->io_type == FDS_DM_MIGRATION);

    addToQueue(dmReq);

}

void DmMigrationHandler::handleQueueItem(DmRequest* dmRequest) {
    // Queue helper should mark IO done.
    QueueHelper helper(dataManager, dmRequest);
    DmIoMigration* typedRequest = static_cast<DmIoMigration*>(dmRequest);

    // Talk to migration handler.
    dataManager.dmMigrationMgr->startMigrationExecutor(dmRequest);
}

void DmMigrationHandler::handleResponse(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                                         boost::shared_ptr<fpi::CtrlNotifyDMStartMigrationMsg>& message,
                                         Error const& e, DmRequest* dmRequest)
{
    // Don't need to send the response to this message.  We will a message
    // back to OM when the migration is complete, but not as part of
    // this response handler (see handleResponseReal()).

    // Remove the dm request, since we don't need it any more
    delete dmRequest;
}


void DmMigrationHandler::handleResponseReal(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                                            uint64_t dmtVersion,
                                            const Error& e)
{
    LOGMIGRATE << logString(*asyncHdr);

    fpi::CtrlNotifyDMStartMigrationRspMsgPtr msg(new fpi::CtrlNotifyDMStartMigrationRspMsg());
    msg->DMT_version = dmtVersion;
    asyncHdr->msg_code = e.GetErrno();
    DM_SEND_ASYNC_RESP(*asyncHdr, fpi::CtrlNotifyDMStartMigrationRspMsgTypeId, *msg);


}

/**
 * DmMigrationBlobFilterHandler
 */

DmMigrationBlobFilterHandler::DmMigrationBlobFilterHandler(DataMgr& dataManager)
	: Handler(dataManager)

{
    if (!dataManager.features.isTestModeEnabled()) {
        REGISTER_DM_MSG_HANDLER(fpi::CtrlNotifyInitialBlobFilterSetMsg, handleRequest);
    }
}

void DmMigrationBlobFilterHandler::handleRequest(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                                        boost::shared_ptr<fpi::CtrlNotifyInitialBlobFilterSetMsg>& message) {
    LOGMIGRATE << logString(*asyncHdr) << logString(*message);

    NodeUuid tmpUuid;
    tmpUuid.uuid_set_val(asyncHdr->msg_src_uuid.svc_uuid);
    auto dmReq = new DmIoResyncInitialBlob(FdsDmSysTaskId, message, tmpUuid);
    dmReq->cb = BIND_MSG_CALLBACK(DmMigrationBlobFilterHandler::handleResponse, asyncHdr, message);

    fds_verify(dmReq->io_vol_id == FdsDmSysTaskId);
    fds_verify(dmReq->io_type == FDS_DM_RESYNC_INIT_BLOB);

    addToQueue(dmReq);

}

void DmMigrationBlobFilterHandler::handleQueueItem(DmRequest* dmRequest) {
    // Queue helper should mark IO done.
    QueueHelper helper(dataManager, dmRequest);
    DmIoResyncInitialBlob* typedRequest = static_cast<DmIoResyncInitialBlob*>(dmRequest);

    dataManager.dmMigrationMgr->startMigrationClient(dmRequest);
}

void DmMigrationBlobFilterHandler::handleResponse(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
			boost::shared_ptr<fpi::CtrlNotifyInitialBlobFilterSetMsg>& message,
			Error const& e, DmRequest* dmRequest) {

    LOGMIGRATE << logString(*asyncHdr) << logString(*message);

    // Do nothing for now.  Filter set was sent by the destination SM as a fire/forget message.
    // If we need to responde for some reason in the future, we can send back a message, but
    // no need to send back a entire message back again -- just need an empty message with
    // error code stuffed in the async header.

    delete dmRequest;
}

DmMigrationDeltaBlobDescHandler::DmMigrationDeltaBlobDescHandler(DataMgr& dataManager)
	: Handler(dataManager)
{
    REGISTER_DM_MSG_HANDLER(fpi::CtrlNotifyDeltaBlobDescMsg, handleRequest);
}

void DmMigrationDeltaBlobDescHandler::handleRequest(fpi::AsyncHdrPtr& asyncHdr,
                                        fpi::CtrlNotifyDeltaBlobDescMsgPtr& message) {
    LOGMIGRATE << logString(*asyncHdr) << logString(*message);

    auto dmReq = new DmIoMigrationDeltaBlobDesc(message);

    fds_verify(dmReq->io_vol_id == FdsDmSysTaskId);
    fds_verify(dmReq->io_type == FDS_DM_MIG_DELTA_BLOBDESC);

    addToQueue(dmReq);
}

void DmMigrationDeltaBlobDescHandler::handleQueueItem(DmRequest* dmRequest) {
    QueueHelper helper(dataManager, dmRequest);
    helper.skipImplicitCb = true;
    DmIoMigrationDeltaBlobDesc* typedRequest = static_cast<DmIoMigrationDeltaBlobDesc*>(dmRequest);
    dataManager.dmMigrationMgr->applyDeltaBlobDescs(typedRequest);
}

DmMigrationDeltaBlobHandler::DmMigrationDeltaBlobHandler(DataMgr& dataManager)
    : Handler(dataManager)
{
    if (!dataManager.features.isTestModeEnabled()) {
        REGISTER_DM_MSG_HANDLER(fpi::CtrlNotifyDeltaBlobsMsg, handleRequest);
    }
}

void DmMigrationDeltaBlobHandler::handleRequest(
        boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
        boost::shared_ptr<fpi::CtrlNotifyDeltaBlobsMsg>& message) {
    auto dmReq = new DmIoMigrationDeltaBlobs(message);

    dmReq->cb = BIND_MSG_CALLBACK(DmMigrationDeltaBlobHandler::handleResponse, asyncHdr, message);
    fds_verify(dmReq->io_type == FDS_DM_MIG_DELTA_BLOB);

    LOGMIGRATE << "Enqueued delta blob migration  request " << logString(*asyncHdr)
        << " " << *reinterpret_cast<DmIoMigrationDeltaBlobs*>(dmReq);

    addToQueue(dmReq);

}

void DmMigrationDeltaBlobHandler::handleQueueItem(DmRequest* dmRequest) {
    QueueHelper helper(dataManager, dmRequest);
    DmIoMigrationDeltaBlobs* typedRequest = static_cast<DmIoMigrationDeltaBlobs*>(dmRequest);

    LOGMIGRATE << "Sending the delta blob migration dequest to migration Mgr " << *typedRequest;
    /*
     *  revisit this - Migration Migration executor integration
     */
    dataManager.dmMigrationMgr->applyDeltaBlobs(typedRequest);
}

void DmMigrationDeltaBlobHandler::handleResponse(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                        boost::shared_ptr<fpi::CtrlNotifyDeltaBlobsMsg>& message,
                        Error const& e, DmRequest* dmRequest) {
	LOGMIGRATE << "Finished deleting request for volume " << message->volume_id;
	delete dmRequest;
}

DmMigrationFinishVolResyncHandler::DmMigrationFinishVolResyncHandler(DataMgr& dataManager)
	: Handler(dataManager)
{
    REGISTER_DM_MSG_HANDLER(fpi::CtrlNotifyFinishVolResyncMsg, handleRequest);
}

void DmMigrationFinishVolResyncHandler::handleRequest(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                       boost::shared_ptr<fpi::CtrlNotifyFinishVolResyncMsg>& message) {
    LOGMIGRATE << logString(*asyncHdr) << logString(*message);

    auto dmReq = new DmIoMigrationFinishVolResync(message);

    fds_verify(dmReq->io_vol_id == FdsDmSysTaskId);
    fds_verify(dmReq->io_type == FDS_DM_MIG_FINISH_VOL_RESYNC);

    addToQueue(dmReq);
}

void DmMigrationFinishVolResyncHandler::handleQueueItem(DmRequest* dmRequest) {
    QueueHelper helper(dataManager, dmRequest);
    DmIoMigrationFinishVolResync* typedRequest = static_cast<DmIoMigrationFinishVolResync*>(dmRequest);

    // dataManager.dmMigrationMgr->applyDeltaBlobDescs(typedRequest);
}

void DmMigrationFinishVolResyncHandler::handleResponse(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                        boost::shared_ptr<fpi::CtrlNotifyFinishVolResyncMsg>& message,
                        Error const& e, DmRequest* dmRequest) {
	LOGMIGRATE << "Finished deleting request for volume " << message->volume_id;
	delete dmRequest;
}

DmMigrationTxStateHandler::DmMigrationTxStateHandler(DataMgr& dataManager)
    : Handler(dataManager)
{
    if (!dataManager.features.isTestModeEnabled()) {
        REGISTER_DM_MSG_HANDLER(fpi::CtrlNotifyTxStateMsg, handleRequest);
    }
}

void DmMigrationTxStateHandler::handleRequest(fpi::AsyncHdrPtr& asyncHdr,
                                        fpi::CtrlNotifyTxStateMsgPtr& message) {
    auto dmReq = new DmIoMigrationTxState(message);

    dmReq->cb = BIND_MSG_CALLBACK(DmMigrationTxStateHandler::handleResponse, asyncHdr, message);

    fds_verify(dmReq->io_vol_id == FdsDmSysTaskId);
    fds_verify(dmReq->io_type == FDS_DM_MIG_TX_STATE);

    LOGMIGRATE << "Enqueued TxState migration request " << logString(*asyncHdr)
               << " " << *reinterpret_cast<DmIoMigrationTxState*>(dmReq);

    addToQueue(dmReq);
}

void DmMigrationTxStateHandler::handleQueueItem(DmRequest* dmRequest) {
    QueueHelper helper(dataManager, dmRequest);
    DmIoMigrationTxState* typedRequest = static_cast<DmIoMigrationTxState*>(dmRequest);
    helper.err = dataManager.dmMigrationMgr->applyTxState(typedRequest);
}

void DmMigrationTxStateHandler::handleResponse(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                        boost::shared_ptr<fpi::CtrlNotifyTxStateMsg>& message,
                        Error const& e, DmRequest* dmRequest) {
    asyncHdr->msg_code = e.GetErrno();

    DM_SEND_ASYNC_RESP(*asyncHdr, FDSP_MSG_TYPEID(fpi::CtrlNotifyTxStateRspMsg),
                       fpi::CtrlNotifyTxStateRspMsg());

	delete dmRequest;
}

}  // namespace dm
}  // namespace fds
