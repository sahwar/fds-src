/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#include <StorMgr.h>
#include <fdsp_utils.h>
#include <fds_assert.h>
#include <SMSvcHandler.h>
#include <string>
#include <net/SvcMgr.h>
#include <net/SvcRequest.h>
#include <fiu-local.h>
#include <random>
#include <chrono>
#include <MockSMCallbacks.h>
#include <net/SvcMgr.h>
#include <net/MockSvcHandler.h>
#include <fds_timestamp.h>
#include <OMgrClient.h>

namespace fds {

extern ObjectStorMgr    *objStorMgr;
extern std::string logString(const FDS_ProtocolInterface::AddObjectRefMsg& msg);
extern std::string logString(const FDS_ProtocolInterface::DeleteObjectMsg& msg);
extern std::string logString(const FDS_ProtocolInterface::GetObjectMsg& msg);
extern std::string logString(const FDS_ProtocolInterface::PutObjectMsg& msg);

SMSvcHandler::SMSvcHandler(CommonModuleProviderIf *provider)
    : PlatNetSvcHandler(provider)
{
    /* Data paths messages */
    REGISTER_FDSP_MSG_HANDLER(fpi::GetObjectMsg, getObject);
    REGISTER_FDSP_MSG_HANDLER(fpi::PutObjectMsg, putObject);
    REGISTER_FDSP_MSG_HANDLER(fpi::DeleteObjectMsg, deleteObject);
    REGISTER_FDSP_MSG_HANDLER(fpi::AddObjectRefMsg, addObjectRef);

    /* Service map messages */
    REGISTER_FDSP_MSG_HANDLER(fpi::NodeSvcInfo, notifySvcChange);

    /* volume information and policy messages */
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlNotifyVolAdd, NotifyAddVol);
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlNotifyVolRemove, NotifyRmVol);
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlNotifyVolMod, NotifyModVol);

    /* GC control messages */
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlNotifyScavenger, NotifyScavenger);
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlQueryScavengerStatus, queryScavengerStatus);
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlQueryScavengerProgress, queryScavengerProgress);
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlSetScavengerPolicy, setScavengerPolicy);
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlQueryScavengerPolicy, queryScavengerPolicy);

    /* scrubber, which is part of GC policy, status messages */
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlSetScrubberStatus, setScrubberStatus);
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlQueryScrubberStatus, queryScrubberStatus);

    /* Online smcheck control messages */
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlNotifySMCheck, NotifySMCheck);
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlNotifySMCheckStatus, querySMCheckStatus);

    /* hybrid tiering control messages */
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlStartHybridTierCtrlrMsg, startHybridTierCtrlr);

    /* DLT update messages */
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlNotifyDLTUpdate, NotifyDLTUpdate);
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlNotifyDLTClose, NotifyDLTClose);

    /* SM Service shutdown message */
    REGISTER_FDSP_MSG_HANDLER(fpi::PrepareForShutdownMsg, shutdownSM);

    /* token migration messages */
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlNotifySMStartMigration, migrationInit);
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlNotifySMAbortMigration, migrationAbort);
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlObjectRebalanceFilterSet, initiateObjectSync);
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlObjectRebalanceDeltaSet, syncObjectSet);
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlGetSecondRebalanceDeltaSet, getMoreDelta);
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlFinishClientTokenResyncMsg, finishClientTokenResync);

    /* DMT update messages */
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlNotifyDMTUpdate, NotifyDMTUpdate);
}

int SMSvcHandler::mod_init(SysParams const *const param) {
    mockTimeoutEnabled = MODULEPROVIDER()->get_fds_config()->\
                         get<bool>("fds.sm.testing.enable_mocking");
    mockTimeoutUs = MODULEPROVIDER()->get_fds_config()->\
                    get<uint32_t>("fds.sm.testing.mocktimeout");
    if (true == mockTimeoutEnabled) {
        mockHandler.reset(new MockSvcHandler());
    }
    return 0;
}

void
SMSvcHandler::asyncReqt(boost::shared_ptr<FDS_ProtocolInterface::AsyncHdr>& header,
                        boost::shared_ptr<std::string>& payload) {
    PlatNetSvcHandler::asyncReqt(header, payload);
}

void
SMSvcHandler::migrationInit(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                boost::shared_ptr<fpi::CtrlNotifySMStartMigration>& migrationMsg)
{
    Error err(ERR_OK);
    LOGDEBUG << "Received Start Migration for target DLT "
             << migrationMsg->DLT_version;

    // first disable GC and Tier Migration
    // Note that after disabling GC, GC work in QoS queue will still
    // finish... however, there is no correctness issue if we are running
    // GC job along with token migration, we just want to reduce the amount
    // of system work we do at the same time (for now, we should be able
    // to make it work running fully in parallel in the future).
    SmScavengerActionCmd scavCmd(fpi::FDSP_SCAVENGER_DISABLE,
                                 SM_CMD_INITIATOR_TOKEN_MIGRATION);
    err = objStorMgr->objectStore->scavengerControlCmd(&scavCmd);
    if (!err.ok()) {
        LOGERROR << "Failed to disable Scavenger; failing token migration";
        startMigrationCb(asyncHdr, migrationMsg->DLT_version, err);
        return;
    }

    SmTieringCmd tierCmd(SmTieringCmd::TIERING_DISABLE);
    err = objStorMgr->objectStore->tieringControlCmd(&tierCmd);
    if (!err.ok()) {
        LOGERROR << "Failed to disable Tier Migration; failing token migration";
        startMigrationCb(asyncHdr, migrationMsg->DLT_version, err);
        return;
    }

    // start migration
    const DLT* dlt = objStorMgr->getDLT();
    if (dlt != NULL) {
        err = objStorMgr->migrationMgr->startMigration(migrationMsg,
                                                       std::bind(
                                                           &SMSvcHandler::startMigrationCb, this,
                                                           asyncHdr, migrationMsg->DLT_version,
                                                           std::placeholders::_1),
                                                       objStorMgr->getUuid(),
                                                       dlt->getNumBitsForToken(),
                                                       false); //false because it's not a resync case
    } else {
        LOGERROR << "SM does not have any DLT; make sure that StartMigration is not "
                 << " called on addition of the first set of SMs to the domain";
        startMigrationCb(asyncHdr, migrationMsg->DLT_version, ERR_INVALID_DLT);
    }
}

void
SMSvcHandler::startMigrationCb(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                               fds_uint64_t dltVersion,
                               const Error& err)
{
    DBG(GLOGDEBUG << fds::logString(*asyncHdr));
    asyncHdr->msg_code = static_cast<int32_t>(err.GetErrno());

    fpi::CtrlNotifyMigrationStatusPtr msg(new fpi::CtrlNotifyMigrationStatus());
    msg->status.DLT_version = dltVersion;
    msg->status.context = 0;
    sendAsyncResp(*asyncHdr, FDSP_MSG_TYPEID(fpi::CtrlNotifyMigrationStatus), *msg);
}

void
SMSvcHandler::migrationAbort(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                            CtrlNotifySMAbortMigrationPtr& abortMsg)
{
    Error err(ERR_OK);
    LOGDEBUG << "Received Abort Migration, will revert to previously "
             << " commited DLT version " << abortMsg->DLT_version;

    // tell migration mgr to abort migration
    err = objStorMgr->migrationMgr->abortMigration();

    // revert to DLT version provided in abort message
    if (abortMsg->DLT_version > 0) {
        // will ignore error from setCurrent -- if this SM does not know
        // about DLT with given version, then it did not have a DLT previously..
        objStorMgr->omClient->getDltManager()->setCurrent(abortMsg->DLT_version);
    }

    // send response
    fpi::CtrlNotifySMAbortMigrationPtr msg(new fpi::CtrlNotifySMAbortMigration());
    msg->DLT_version = abortMsg->DLT_version;
    asyncHdr->msg_code = static_cast<int32_t>(err.GetErrno());
    sendAsyncResp(*asyncHdr, FDSP_MSG_TYPEID(fpi::CtrlNotifySMAbortMigration), *msg);
}

/**
 * This is the message from destination SM (SM that asks this SM to migrate objects)
 * with an filter set of object metadata
 */
void
SMSvcHandler::initiateObjectSync(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                                 fpi::CtrlObjectRebalanceFilterSetPtr& filterObjSet)
{
    Error err(ERR_OK);
    LOGDEBUG << "Initiate Object Sync";

    // first disable GC and Tier Migration. If this SM is also a destination and
    // we already disabled GC and Tier Migration, disabling them again is a noop
    // Note that after disabling GC, GC work in QoS queue will still
    // finish... however, there is no correctness issue if we are running
    // GC job along with token migration, we just want to reduce the amount
    // of system work we do at the same time (for now, we should be able
    // to make it work running fully in parallel in the future).
    SmScavengerActionCmd scavCmd(fpi::FDSP_SCAVENGER_DISABLE,
                                 SM_CMD_INITIATOR_TOKEN_MIGRATION);
    err = objStorMgr->objectStore->scavengerControlCmd(&scavCmd);
    SmTieringCmd tierCmd(SmTieringCmd::TIERING_DISABLE);
    err = objStorMgr->objectStore->tieringControlCmd(&tierCmd);

    // tell migration mgr to start object rebalance
    const DLT* dlt = objStorMgr->omClient->getDltManager()->getDLT();
    fds_verify(dlt != NULL);
    err = objStorMgr->migrationMgr->startObjectRebalance(filterObjSet,
                                                         asyncHdr->msg_src_uuid,
                                                         objStorMgr->getUuid(),
                                                         dlt->getNumBitsForToken(), dlt);

    // respond with error code
    asyncHdr->msg_code = err.GetErrno();
    sendAsyncResp(*asyncHdr, FDSP_MSG_TYPEID(fpi::EmptyMsg), fpi::EmptyMsg());
}

void
SMSvcHandler::syncObjectSet(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                            fpi::CtrlObjectRebalanceDeltaSetPtr& deltaObjSet)
{
    Error err(ERR_OK);
    LOGDEBUG << "Received Sync Object Set from SM " << std::hex
             << asyncHdr->msg_src_uuid.svc_uuid << std::dec;
    err = objStorMgr->migrationMgr->recvRebalanceDeltaSet(deltaObjSet);

    // TODO(Anna) respond with error, are we responding on success?
    fds_verify(err.ok() || (err == ERR_SM_TOK_MIGRATION_ABORTED));
}

void
SMSvcHandler::getMoreDelta(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                           fpi::CtrlGetSecondRebalanceDeltaSetPtr& getDeltaSetMsg)
{
    Error err(ERR_OK);
    LOGDEBUG << "Received get second delta set from destination SM "
             << std::hex << asyncHdr->msg_src_uuid.svc_uuid << std::dec
             << " executor ID " << getDeltaSetMsg->executorID;

    // notify migration mgr -- this call is sync
    err = objStorMgr->migrationMgr->startSecondObjectRebalance(getDeltaSetMsg,
                                                               asyncHdr->msg_src_uuid);

    // send response
    fpi::CtrlGetSecondRebalanceDeltaSetRspPtr msg(new fpi::CtrlGetSecondRebalanceDeltaSetRsp());
    asyncHdr->msg_code = static_cast<int32_t>(err.GetErrno());
    sendAsyncResp(*asyncHdr, FDSP_MSG_TYPEID(fpi::CtrlGetSecondRebalanceDeltaSetRsp), *msg);
}

void
SMSvcHandler::finishClientTokenResync(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                                      fpi::CtrlFinishClientTokenResyncMsgPtr& finishClientResyncMsg)
{
    Error err(ERR_OK);
    LOGDEBUG << "Received finish client resync msg from destination SM "
             << std::hex << asyncHdr->msg_src_uuid.svc_uuid << std::dec
             << " executor ID " << finishClientResyncMsg->executorID;

    // notify migration mgr -- this call is sync
    err = objStorMgr->migrationMgr->finishClientResync(finishClientResyncMsg->executorID);

    // send response
    fpi::CtrlFinishClientTokenResyncRspMsgPtr msg(new fpi::CtrlFinishClientTokenResyncRspMsg());
    asyncHdr->msg_code = static_cast<int32_t>(err.GetErrno());
    sendAsyncResp(*asyncHdr, FDSP_MSG_TYPEID(fpi::CtrlFinishClientTokenResyncRspMsg), *msg);
}

void SMSvcHandler::shutdownSM(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
        boost::shared_ptr<fpi::PrepareForShutdownMsg>& shutdownMsg) {
    LOGDEBUG << "Received shutdown message... shutting down...";
    if (!objStorMgr->isShuttingDown()) {
        objStorMgr->mod_disable_service();
        objStorMgr->mod_shutdown();
    }

    // respond to OM
    sendAsyncResp(*asyncHdr, FDSP_MSG_TYPEID(fpi::EmptyMsg), fpi::EmptyMsg());
}

void SMSvcHandler::queryScrubberStatus(boost::shared_ptr<fpi::AsyncHdr> &hdr,
        fpi::CtrlQueryScrubberStatusPtr &scrub_msg) {
    GLOGDEBUG << "Scrubber status called";
    Error err(ERR_OK);
    fpi::CtrlQueryScrubberStatusRespPtr resp(new fpi::CtrlQueryScrubberStatusResp());
    SmScrubberGetStatusCmd scrubCmd(resp);
    err = objStorMgr->objectStore->scavengerControlCmd(&scrubCmd);

    hdr->msg_code = static_cast<int32_t>(err.GetErrno());
    GLOGDEBUG << "Scrubber status = " << resp->scrubber_status << " " << err;
    sendAsyncResp(*hdr, FDSP_MSG_TYPEID(fpi::CtrlQueryScrubberStatusResp), *resp);
}

void SMSvcHandler::setScrubberStatus(boost::shared_ptr<fpi::AsyncHdr> &hdr,
        fpi::CtrlSetScrubberStatusPtr &scrub_msg) {
    Error err(ERR_OK);
    fpi::CtrlSetScrubberStatusRespPtr resp(new fpi::CtrlSetScrubberStatusResp());
    LOGNORMAL << " receive scrubber cmd " << scrub_msg->scrubber_status;
    SmScrubberActionCmd scrubCmd(scrub_msg->scrubber_status);
    err = objStorMgr->objectStore->scavengerControlCmd(&scrubCmd);
    sendAsyncResp(*hdr, FDSP_MSG_TYPEID(fpi::CtrlSetScrubberStatusResp), *resp);
}

void SMSvcHandler::queryScavengerProgress(boost::shared_ptr<fpi::AsyncHdr> &hdr,
        fpi::CtrlQueryScavengerProgressPtr &query_msg) {
    Error err(ERR_OK);
    fpi::CtrlQueryScavengerProgressRespPtr resp(new fpi::CtrlQueryScavengerProgressResp());

    SmScavengerGetProgressCmd scavCmd;
    err = objStorMgr->objectStore->scavengerControlCmd(&scavCmd);
    resp->progress_pct = scavCmd.retProgress;
    hdr->msg_code = static_cast<int32_t>(err.GetErrno());
    GLOGDEBUG << "Response set: " << resp->progress_pct << " " << err;
    sendAsyncResp(*hdr, FDSP_MSG_TYPEID(fpi::CtrlQueryScavengerProgressResp), *resp);
}

void SMSvcHandler::setScavengerPolicy(boost::shared_ptr<fpi::AsyncHdr> &hdr,
                                      fpi::CtrlSetScavengerPolicyPtr &policy_msg) {
    fpi::CtrlSetScavengerPolicyRespPtr resp(new fpi::CtrlSetScavengerPolicyResp());
    Error err(ERR_OK);
    SmScavengerSetPolicyCmd scavCmd(policy_msg);
    err = objStorMgr->objectStore->scavengerControlCmd(&scavCmd);

    hdr->msg_code = static_cast<int32_t>(err.GetErrno());
    GLOGDEBUG << "Setting scavenger policy " << err;
    sendAsyncResp(*hdr, FDSP_MSG_TYPEID(fpi::CtrlSetScavengerPolicyResp), *resp);
}

void SMSvcHandler::queryScavengerPolicy(boost::shared_ptr<fpi::AsyncHdr> &hdr,
                                        fpi::CtrlQueryScavengerPolicyPtr &query_msg) {
    fpi::CtrlQueryScavengerPolicyRespPtr resp(new fpi::CtrlQueryScavengerPolicyResp());
    SmScavengerGetPolicyCmd scavCmd(resp);
    Error err = objStorMgr->objectStore->scavengerControlCmd(&scavCmd);

    hdr->msg_code = static_cast<int32_t>(err.GetErrno());
    GLOGDEBUG << "Got scavenger policy " << resp->dsk_threshold1;
    sendAsyncResp(*hdr, FDSP_MSG_TYPEID(fpi::CtrlQueryScavengerPolicyResp), *resp);
}

void SMSvcHandler::queryScavengerStatus(boost::shared_ptr<fpi::AsyncHdr> &hdr,
                                        fpi::CtrlQueryScavengerStatusPtr &query_msg) {
    Error err(ERR_OK);
    fpi::CtrlQueryScavengerStatusRespPtr resp(new fpi::CtrlQueryScavengerStatusResp());
    SmScavengerGetStatusCmd scavCmd(resp);
    err = objStorMgr->objectStore->scavengerControlCmd(&scavCmd);

    hdr->msg_code = static_cast<int32_t>(err.GetErrno());
    GLOGDEBUG << "Scavenger status = " << resp->status << " " << err;
    sendAsyncResp(*hdr, FDSP_MSG_TYPEID(fpi::CtrlQueryScavengerStatusResp), *resp);
}

void SMSvcHandler::getObject(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                             boost::shared_ptr<fpi::GetObjectMsg>& getObjMsg)  // NOLINT
{
    DBG(GLOGDEBUG << logString(*asyncHdr) << fds::logString(*getObjMsg));

    fiu_do_on("svc.drop.getobject", return);
#if 0
    fiu_do_on("svc.uturn.getobject",
              auto getReq = new SmIoGetObjectReq(getObjMsg); \
              boost::shared_ptr<GetObjectResp> resp = boost::make_shared<GetObjectResp>(); \
              getReq->getObjectNetResp = resp; \
              getObjectCb(asyncHdr, ERR_OK, getReq); return;);
#endif
#if 0
    fiu_do_on("svc.uturn.getobject", \
              mockHandler->schedule(mockTimeoutUs, \
                                    std::bind(&MockSMCallbacks::mockGetCb, this,\
                                              asyncHdr)); \
              return;);
#endif

    Error err(ERR_OK);
    ObjectID objId(getObjMsg->data_obj_id.digest);

    auto getReq = new SmIoGetObjectReq(getObjMsg);
    getReq->io_type = FDS_SM_GET_OBJECT;
    getReq->setVolId(getObjMsg->volume_id);
    getReq->setObjId(objId);
    getReq->obj_data.obj_id = getObjMsg->data_obj_id;
    // perf-trace related data
    getReq->opReqFailedPerfEventType = PerfEventType::SM_GET_OBJ_REQ_ERR;
    getReq->opReqLatencyCtx.type = PerfEventType::SM_E2E_GET_OBJ_REQ;
    getReq->opReqLatencyCtx.reset_volid(getObjMsg->volume_id);
    getReq->opLatencyCtx.type = PerfEventType::SM_GET_IO;
    getReq->opLatencyCtx.reset_volid(getObjMsg->volume_id);
    getReq->opQoSWaitCtx.type = PerfEventType::SM_GET_QOS_QUEUE_WAIT;
    getReq->opQoSWaitCtx.reset_volid(getObjMsg->volume_id);

    getReq->response_cb = std::bind(
        &SMSvcHandler::getObjectCb, this,
        asyncHdr,
        std::placeholders::_1, std::placeholders::_2);

    // start measuring E2E latency
    PerfTracer::tracePointBegin(getReq->opReqLatencyCtx);

    // check if DLT token ready -- we are doing it after creating
    // get request, because callback needs it
    if (!objStorMgr->migrationMgr->isDltTokenReady(objId)) {
        LOGDEBUG << "DLT token not ready, not going to do GET for " << objId;
        getObjectCb(asyncHdr, ERR_TOKEN_NOT_READY, getReq);
        return;
    }

    err = objStorMgr->enqueueMsg(getReq->getVolId(), getReq);
    if (err != fds::ERR_OK) {
        fds_assert(!"Hit an error in enqueing");
        LOGERROR << "Failed to enqueue to SmIoGetObjectReq to StorMgr.  Error: "
                 << err;
        getObjectCb(asyncHdr, err, getReq);
    }
}

void SMSvcHandler::getObjectCb(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                               const Error &err,
                               SmIoGetObjectReq *getReq)
{
    DBG(GLOGDEBUG << fds::logString(*asyncHdr));

    boost::shared_ptr<GetObjectResp> resp;
    asyncHdr->msg_code = static_cast<int32_t>(err.GetErrno());
    resp = getReq->getObjectNetResp;

    // E2E latency end
    PerfTracer::tracePointEnd(getReq->opReqLatencyCtx);
    if (!err.ok()) {
        PerfTracer::incr(getReq->opReqFailedPerfEventType,
                         getReq->getVolId());
    }

    // Independent if error happend, check if this request matches
    // currently closed DLT version. If not, we cannot guarantee that
    // successessful get was in fact successful.
    //
    // Requests to SM are relative to a DLT table that maps which
    // SMs are responsible for which objects. If the DLT version
    // being used by the sender is stale then this may not be the
    // correct SM to contact. We know if other version are stale if
    // our current version has been closed (see OM table propagation
    // protocol). If we don't have a DLT version yet then have SM process
    // the request.
    const DLT *curDlt = objStorMgr->getDLT();
    if ((curDlt) &&
        (curDlt->isClosed()) &&
        (curDlt->getVersion() > (fds_uint64_t)asyncHdr->dlt_version)) {
        // Tell the sender that their DLT version is invalid.
        LOGDEBUG << "Returning DLT mismatch using version "
                 << (fds_uint64_t)asyncHdr->dlt_version << " with current closed version "
                 << curDlt->getVersion();

        asyncHdr->msg_code = ERR_IO_DLT_MISMATCH;
    }

    sendAsyncResp(*asyncHdr, FDSP_MSG_TYPEID(fpi::GetObjectResp), *resp);
    delete getReq;
}

void SMSvcHandler::putObject(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                             boost::shared_ptr<fpi::PutObjectMsg>& putObjMsg)  // NOLINT
{
    if ((objStorMgr->testUturnAll == true) ||
        (objStorMgr->testUturnPutObj == true)) {
        LOGDEBUG << "Uturn testing put object "
                 << fds::logString(*asyncHdr) << fds::logString(*putObjMsg);
        putObjectCb(asyncHdr, ERR_OK, NULL);
        return;
    }

    DBG(GLOGDEBUG << fds::logString(*asyncHdr) << fds::logString(*putObjMsg));

    fiu_do_on("svc.drop.putobject", return);
    // fiu_do_on("svc.uturn.putobject", putObjectCb(asyncHdr, ERR_OK, NULL); return;);
#if 0
    fiu_do_on("svc.uturn.putobject", \
              mockHandler->schedule(mockTimeoutUs, \
                                    std::bind(&MockSMCallbacks::mockPutCb, this,\
                                              asyncHdr)); \
              return;);
#endif

    Error err(ERR_OK);
    ObjectID objId(putObjMsg->data_obj_id.digest);
    auto putReq = new SmIoPutObjectReq(putObjMsg);
    putReq->io_type = FDS_SM_PUT_OBJECT;
    putReq->setVolId(putObjMsg->volume_id);
    putReq->dltVersion = asyncHdr->dlt_version;
    putReq->forwardedReq = putObjMsg->forwardedReq;
    putReq->setObjId(objId);

    // perf-trace related data
    putReq->opReqFailedPerfEventType = PerfEventType::SM_PUT_OBJ_REQ_ERR;
    putReq->opReqLatencyCtx.type = PerfEventType::SM_E2E_PUT_OBJ_REQ;
    putReq->opReqLatencyCtx.reset_volid(putObjMsg->volume_id);
    putReq->opLatencyCtx.type = PerfEventType::SM_PUT_IO;
    putReq->opLatencyCtx.reset_volid(putObjMsg->volume_id);
    putReq->opQoSWaitCtx.type = PerfEventType::SM_PUT_QOS_QUEUE_WAIT;
    putReq->opQoSWaitCtx.reset_volid(putObjMsg->volume_id);

    putReq->response_cb= std::bind(
        &SMSvcHandler::putObjectCb, this,
        asyncHdr,
        std::placeholders::_1, std::placeholders::_2);

    // start measuring E2E latency
    PerfTracer::tracePointBegin(putReq->opReqLatencyCtx);

    // check if DLT token ready -- we are doing it after creating
    // put request, because callback needs it
    if (!objStorMgr->migrationMgr->isDltTokenReady(objId)) {
        LOGDEBUG << "DLT token not ready, not going to do PUT for " << objId;
        putObjectCb(asyncHdr, ERR_TOKEN_NOT_READY, putReq);
        return;
    }

    err = objStorMgr->enqueueMsg(putReq->getVolId(), putReq);
    if (err != fds::ERR_OK) {
        fds_assert(!"Hit an error in enqueing");
        LOGERROR << "Failed to enqueue to SmIoPutObjectReq to StorMgr.  Error: "
                 << err;
        putObjectCb(asyncHdr, err, putReq);
    }
}

#if 0
void SMSvcHandler::mockPutCb(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr)
{
    auto resp = boost::make_shared<fpi::PutObjectRspMsg>();
    asyncHdr->msg_code = ERR_OK;
    sendAsyncResp(*asyncHdr, FDSP_MSG_TYPEID(fpi::PutObjectRspMsg), *resp);
}

void SMSvcHandler::mockGetCb(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr)
{
    auto resp = boost::make_shared<fpi::GetObjectResp>();
    resp->data_obj = std::move(std::string(4096, 'a'));
    asyncHdr->msg_code = ERR_OK;
    sendAsyncResp(*asyncHdr, FDSP_MSG_TYPEID(fpi::GetObjectResp), *resp);
}
#endif

void SMSvcHandler::putObjectCb(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                               const Error &err,
                               SmIoPutObjectReq* putReq)
{
    DBG(GLOGDEBUG << fds::logString(*asyncHdr));

    // E2E latency end
    if (putReq) {
        // check if there was not a uturn test
        PerfTracer::tracePointEnd(putReq->opReqLatencyCtx);
        if (!err.ok()) {
            PerfTracer::incr(putReq->opReqFailedPerfEventType,
                             putReq->getVolId());
        }
    }

    auto resp = boost::make_shared<fpi::PutObjectRspMsg>();
    asyncHdr->msg_code = static_cast<int32_t>(err.GetErrno());

    // Independent if error happend, check if this request matches
    // currently closed DLT version. This should not happen, but if
    // it did, we may end up with incorrect object refcount.
    // TODO(Anna) If we see this error happening, we will have to
    // process DLT close by taking write lock on object store
    // (i.e. object store must not process any IO)
    //
    // Requests to SM are relative to a DLT table that maps which
    // SMs are responsible for which objects. If the DLT version
    // being used by the sender is stale then this may not be the
    // correct SM to contact. We know if other version are stale if
    // our current version has been closed (see OM table propagation
    // protocol). If we don't have a DLT version yet then have SM process
    // the request.
    const DLT *curDlt = objStorMgr->getDLT();
    if ((curDlt) &&
        (curDlt->isClosed()) &&
        (curDlt->getVersion() > (fds_uint64_t)asyncHdr->dlt_version)) {
        // Tell the sender that their DLT version is invalid.
        LOGCRITICAL << "Returning DLT mismatch using version "
                    << (fds_uint64_t)asyncHdr->dlt_version
                    << " with current closed version "
                    << curDlt->getVersion();

        asyncHdr->msg_code = ERR_IO_DLT_MISMATCH;
    }

    sendAsyncResp(*asyncHdr, FDSP_MSG_TYPEID(fpi::PutObjectRspMsg), *resp);

    if ((objStorMgr->testUturnAll == true) ||
        (objStorMgr->testUturnPutObj == true)) {
        fds_verify(putReq == NULL);
        return;
    }

    delete putReq;
}

void SMSvcHandler::deleteObject(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                                boost::shared_ptr<fpi::DeleteObjectMsg>& deleteObjMsg)
{
    if (objStorMgr->testUturnAll == true) {
        LOGDEBUG << "Uturn testing delete object "
                 << fds::logString(*asyncHdr) << fds::logString(*deleteObjMsg);
        deleteObjectCb(asyncHdr, ERR_OK, NULL);
        return;
    }

    DBG(GLOGDEBUG << fds::logString(*asyncHdr) << fds::logString(*deleteObjMsg));
    Error err(ERR_OK);
    ObjectID objId(deleteObjMsg->objId.digest);
    auto delReq = new SmIoDeleteObjectReq(deleteObjMsg);

    // Set delReq stuffs
    delReq->io_type = FDS_SM_DELETE_OBJECT;

    delReq->setVolId(deleteObjMsg->volId);
    delReq->dltVersion = asyncHdr->dlt_version;
    delReq->setObjId(objId);
    delReq->forwardedReq = deleteObjMsg->forwardedReq;

    // perf-trace related data
    delReq->opReqFailedPerfEventType = PerfEventType::SM_DELETE_OBJ_REQ_ERR;
    delReq->opReqLatencyCtx.type = PerfEventType::SM_E2E_DELETE_OBJ_REQ;
    delReq->opLatencyCtx.type = PerfEventType::SM_DELETE_IO;
    delReq->opQoSWaitCtx.type = PerfEventType::SM_DELETE_QOS_QUEUE_WAIT;

    delReq->response_cb = std::bind(
        &SMSvcHandler::deleteObjectCb, this,
        asyncHdr,
        std::placeholders::_1, std::placeholders::_2);

    // start measuring E2E latency
    PerfTracer::tracePointBegin(delReq->opReqLatencyCtx);

    // check if DLT token ready -- we are doing it after creating
    // delete request, because callback needs it
    if (!objStorMgr->migrationMgr->isDltTokenReady(objId)) {
        LOGDEBUG << "DLT token not ready, not going to do DELETE for " << objId;
        deleteObjectCb(asyncHdr, ERR_TOKEN_NOT_READY, delReq);
        return;
    }

    err = objStorMgr->enqueueMsg(delReq->getVolId(), delReq);
    if (err != fds::ERR_OK) {
        fds_assert(!"Hit an error in enqueing");
        LOGERROR << "Failed to enqueue to SmIoDeleteObjectReq to StorMgr.  Error: "
                 << err;
        deleteObjectCb(asyncHdr, err, delReq);
    }
}

void SMSvcHandler::deleteObjectCb(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                                  const Error &err,
                                  SmIoDeleteObjectReq* del_req)
{
    DBG(GLOGDEBUG << fds::logString(*asyncHdr));

    // E2E latency end
    if (del_req) {
        PerfTracer::tracePointEnd(del_req->opReqLatencyCtx);
        if (!err.ok()) {
            PerfTracer::incr(del_req->opReqFailedPerfEventType,
                             del_req->getVolId());
        }
    }

    auto resp = boost::make_shared<fpi::DeleteObjectRspMsg>();
    asyncHdr->msg_code = static_cast<int32_t>(err.GetErrno());

    // Independent if error happend, check if this request matches
    // currently closed DLT version. This should not happen, but if
    // it did, we may end up with incorrect object refcount.
    // TODO(Anna) If we see this error happening, we will have to
    // process DLT close by taking write lock on object store
    // (i.e. object store must not process any IO)
    //
    // Requests to SM are relative to a DLT table that maps which
    // SMs are responsible for which objects. If the DLT version
    // being used by the sender is stale then this may not be the
    // correct SM to contact. We know if other version are stale if
    // our current version has been closed (see OM table propagation
    // protocol). If we don't have a DLT version yet then have SM process
    // the request.
    const DLT *curDlt = objStorMgr->getDLT();
    if ((curDlt) &&
        (curDlt->isClosed()) &&
        (curDlt->getVersion() > (fds_uint64_t)asyncHdr->dlt_version)) {
        // Tell the sender that their DLT version is invalid.
        LOGCRITICAL << "Returning DLT mismatch using version "
                    << (fds_uint64_t)asyncHdr->dlt_version
                    << " with current closed version "
                    << curDlt->getVersion();

        asyncHdr->msg_code = ERR_IO_DLT_MISMATCH;
    }

    sendAsyncResp(*asyncHdr, FDSP_MSG_TYPEID(fpi::DeleteObjectRspMsg), *resp);

    if (del_req) {
        delete del_req;
    }
}
// NotifySvcChange
// ---------------
//
void
SMSvcHandler::notifySvcChange(boost::shared_ptr<fpi::AsyncHdr>    &hdr,
                              boost::shared_ptr<fpi::NodeSvcInfo> &msg)
{
}

// NotifyAddVol
// ------------
//
void
SMSvcHandler::NotifyAddVol(boost::shared_ptr<fpi::AsyncHdr>         &hdr,
                           boost::shared_ptr<fpi::CtrlNotifyVolAdd> &vol_msg)
{
    fds_volid_t volumeId = vol_msg->vol_desc.volUUID;
    VolumeDesc vdb(vol_msg->vol_desc);
    FDSP_NotifyVolFlag vol_flag = vol_msg->vol_flag;
    GLOGNOTIFY << "Received create for vol "
               << "[" << std::hex << volumeId << std::dec << ", "
               << vdb.getName() << "]";

    Error err = objStorMgr->regVol(vdb);
    if (err.ok()) {
        StorMgrVolume * vol = objStorMgr->getVol(volumeId);
        fds_assert(vol != NULL);

        fds_volid_t queueId = vol->getQueue()->getVolUuid();
        if (!objStorMgr->getQueue(queueId)) {
            err = objStorMgr->regVolQos(queueId, static_cast<FDS_VolumeQueue*>(
                vol->getQueue().get()));
        }

        if (!err.ok()) {
            // most likely axceeded min iops
            objStorMgr->deregVol(volumeId);
        }
    }
    if (!err.ok()) {
        GLOGERROR << "Registration failed for vol id " << std::hex << volumeId
                  << std::dec << " " << err;
    }
    hdr->msg_code = err.GetErrno();
    sendAsyncResp(*hdr, FDSP_MSG_TYPEID(fpi::CtrlNotifyVolAdd), *vol_msg);

    // Start the hybrid tier migration on the first volume add call. The volume information
    // is propagated after DLT update, and this can cause missing volume and associated volume
    // policy.
    // Although this is called every time when a volume is added, but hybrid tier migration
    // controller will decided if it need to start tier migration or not.
    SmTieringCmd tierCmd(SmTieringCmd::TIERING_START_HYBRIDCTRLR_AUTO);
    err = objStorMgr->objectStore->tieringControlCmd(&tierCmd);
    if (err != ERR_OK) {
        LOGWARN << "Failed to enable Hybrid Tier Migration";
    }
}

// NotifyRmVol
// -----------
//
void
SMSvcHandler::NotifyRmVol(boost::shared_ptr<fpi::AsyncHdr>            &hdr,
                          boost::shared_ptr<fpi::CtrlNotifyVolRemove> &vol_msg)
{
    fds_volid_t volumeId = vol_msg->vol_desc.volUUID;
    std::string volName  = vol_msg->vol_desc.vol_name;

    if (vol_msg->vol_flag == FDSP_NOTIFY_VOL_CHECK_ONLY) {
        GLOGNOTIFY << "Received del chk for vol "
                   << "[" << volumeId
                   <<  ":" << volName << "]";
    } else  {
        GLOGNOTIFY << "Received delete for vol "
                   << "[" << std::hex << volumeId << std::dec << ", "
                   << volName << "]";

        // wait for queue to be empty.
        fds_uint32_t qSize = objStorMgr->qosCtrl->queueSize(volumeId);
        int count = 0;
        while (qSize > 0) {
            LOGWARN << "wait for delete - q not empty for vol:" << volumeId
                    << " size:" << qSize
                    << " waited for [" << ++count << "] secs";
            usleep(1*1000*1000);
            qSize = objStorMgr->qosCtrl->queueSize(volumeId);
        }

        objStorMgr->quieseceIOsQos(volumeId);
        objStorMgr->deregVolQos(volumeId);
        objStorMgr->deregVol(volumeId);
    }
    hdr->msg_code = 0;
    sendAsyncResp(*hdr, FDSP_MSG_TYPEID(fpi::CtrlNotifyVolRemove), *vol_msg);
}

// NotifyModVol
// ------------
//
void
SMSvcHandler::NotifyModVol(boost::shared_ptr<fpi::AsyncHdr>         &hdr,
                           boost::shared_ptr<fpi::CtrlNotifyVolMod> &vol_msg)
{
    Error err;
    VolumeDesc vdbc(vol_msg->vol_desc), * vdb = &vdbc;
    fds_volid_t volumeId = vol_msg->vol_desc.volUUID;
    GLOGNOTIFY << "Received modify for vol "
               << "[" << std::hex << volumeId << std::dec << ", "
               << vdb->getName() << "]";

    StorMgrVolume * vol = objStorMgr->getVol(volumeId);
    fds_assert(vol != NULL);
    if (vol->voldesc->mediaPolicy != vdb->mediaPolicy) {
        // TODO(Rao): Total hack. This should go through some sort of synchronization.
        // I can't fina a better interface for doing this in the existing code
        vol->voldesc->mediaPolicy = vdb->mediaPolicy;
    }

    vol->voldesc->modifyPolicyInfo(vdb->iops_assured, vdb->iops_throttle, vdb->relativePrio);
    err = objStorMgr->modVolQos(vol->getVolId(),
                                vdb->iops_assured, vdb->iops_throttle, vdb->relativePrio);
    if ( !err.ok() )  {
        GLOGERROR << "Modify volume policy failed for vol " << vdb->getName()
                  << std::hex << volumeId << std::dec << " error: "
                  << err.GetErrstr();
    }
    hdr->msg_code = err.GetErrno();
    sendAsyncResp(*hdr, FDSP_MSG_TYPEID(fpi::CtrlNotifyVolMod), *vol_msg);
}

// CtrlStartHybridTierCtrlrMsg
//
// Manually start Hybrid Tiering.
// ----------------
//
void
SMSvcHandler::startHybridTierCtrlr(boost::shared_ptr<fpi::AsyncHdr> &hdr,
                                   boost::shared_ptr<fpi::CtrlStartHybridTierCtrlrMsg> &hbtMsg)
{
    SmTieringCmd tierCmd(SmTieringCmd::TIERING_START_HYBRIDCTRLR_MANUAL);
    Error err = objStorMgr->objectStore->tieringControlCmd(&tierCmd);
    if (err != ERR_OK) {
        LOGWARN << err;
    }
}

void SMSvcHandler::addObjectRef(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                                boost::shared_ptr<fpi::AddObjectRefMsg>& addObjRefMsg)
{
    DBG(GLOGDEBUG << fds::logString(*asyncHdr) << " " << fds::logString(*addObjRefMsg));
    Error err(ERR_OK);

    auto addObjRefReq = new SmIoAddObjRefReq(addObjRefMsg);

    addObjRefReq->dltVersion = asyncHdr->dlt_version;
    addObjRefReq->forwardedReq = addObjRefMsg->forwardedReq;

    // perf-trace related data
    addObjRefReq->opReqFailedPerfEventType = PerfEventType::SM_ADD_OBJ_REF_REQ_ERR;
    addObjRefReq->opReqLatencyCtx.type = PerfEventType::SM_E2E_ADD_OBJ_REF_REQ;
    addObjRefReq->opLatencyCtx.type = PerfEventType::SM_ADD_OBJ_REF_IO;
    addObjRefReq->opQoSWaitCtx.type = PerfEventType::SM_ADD_OBJ_REF_QOS_QUEUE_WAIT;

    addObjRefReq->response_cb = std::bind(
        &SMSvcHandler::addObjectRefCb, this,
        asyncHdr,
        std::placeholders::_1, std::placeholders::_2);

    // start measuring E2E latency
    PerfTracer::tracePointBegin(addObjRefReq->opReqLatencyCtx);

    err = objStorMgr->enqueueMsg(addObjRefReq->getSrcVolId(), addObjRefReq);
    if (err != fds::ERR_OK) {
        fds_assert(!"Hit an error in enqueing");
        LOGERROR << "Failed to enqueue to SmIoAddObjRefReq to StorMgr.  Error: "
                 << err;
        addObjectRefCb(asyncHdr, err, addObjRefReq);
    }
}

void SMSvcHandler::addObjectRefCb(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                                  const Error &err, SmIoAddObjRefReq* addObjRefReq) {
    DBG(GLOGDEBUG << fds::logString(*asyncHdr));

    // E2E latency end
    PerfTracer::tracePointEnd(addObjRefReq->opReqLatencyCtx);
    if (!err.ok()) {
        PerfTracer::incr(addObjRefReq->opReqFailedPerfEventType,
                         addObjRefReq->getVolId());
    }

    auto resp = boost::make_shared<fpi::AddObjectRefRspMsg>();
    asyncHdr->msg_code = static_cast<int32_t>(err.GetErrno());

    // Independent of error happend, check if this request matches
    // currently closed DLT version. This should not happen, but if
    // it did, we may end up with incorrect object refcount.
    // TODO(Anna) If we see this error happening, we will have to
    // process DLT close by taking write lock on object store
    // (i.e. object store must not process any IO)
    //
    // Requests to SM are relative to a DLT table that maps which
    // SMs are responsible for which objects. If the DLT version
    // being used by the sender is stale then this may not be the
    // correct SM to contact. We know if other version are stale if
    // our current version has been closed (see OM table propagation
    // protocol). If we don't have a DLT version yet then have SM process
    // the request.
    const DLT *curDlt = objStorMgr->getDLT();
    if ((curDlt) &&
        (curDlt->isClosed()) &&
        (curDlt->getVersion() > (fds_uint64_t)asyncHdr->dlt_version)) {
        // Tell the sender that their DLT version is invalid.
        LOGCRITICAL << "Returning DLT mismatch using version "
                    << (fds_uint64_t)asyncHdr->dlt_version
                    << " with current closed version "
                    << curDlt->getVersion();

        asyncHdr->msg_code = ERR_IO_DLT_MISMATCH;
    }

    sendAsyncResp(*asyncHdr, FDSP_MSG_TYPEID(fpi::AddObjectRefRspMsg), *resp);

    delete addObjRefReq;
}

void
SMSvcHandler::NotifyScavenger(boost::shared_ptr<fpi::AsyncHdr>         &hdr,
                              boost::shared_ptr<fpi::CtrlNotifyScavenger> &msg)
{
    Error err(ERR_OK);
    LOGNORMAL << " receive scavenger cmd " << msg->scavenger.cmd;
    SmScavengerActionCmd scavCmd(msg->scavenger.cmd, SM_CMD_INITIATOR_USER);
    err = objStorMgr->objectStore->scavengerControlCmd(&scavCmd);

    hdr->msg_code = 0;
    sendAsyncResp(*hdr, FDSP_MSG_TYPEID(fpi::CtrlNotifyScavenger), *msg);
}

// NotifyDLTUpdate
// ---------------
//
void
SMSvcHandler::NotifyDLTUpdate(boost::shared_ptr<fpi::AsyncHdr>            &hdr,
                              boost::shared_ptr<fpi::CtrlNotifyDLTUpdate> &dlt)
{
    Error err(ERR_OK);
    LOGNOTIFY << "Received new DLT commit version  "
              << dlt->dlt_data.dlt_type;
    err = objStorMgr->omClient->updateDlt(dlt->dlt_data.dlt_type, dlt->dlt_data.dlt_data, nullptr);
    if (err.ok()) {
        err = objStorMgr->handleDltUpdate();
    } else if (err == ERR_DUPLICATE) {
        LOGWARN << "Received duplicate DLT, ignoring";
        err = ERR_OK;
    }

    LOGNOTIFY << "Sending DLT commit response to OM with result " << err;
    hdr->msg_code = err.GetErrno();
    sendAsyncResp(*hdr, FDSP_MSG_TYPEID(fpi::EmptyMsg), fpi::EmptyMsg());
}

// NotifyDLTClose
// ---------------
//
void
SMSvcHandler::NotifyDLTClose(boost::shared_ptr<fpi::AsyncHdr> &hdr,
        boost::shared_ptr<fpi::CtrlNotifyDLTClose> &dlt)
{
    Error err(ERR_OK);
    LOGNOTIFY << "Receiving DLT Close";
    // Set closed flag for the DLT. We use it for garbage collecting
    // DLT tokens that are no longer belong to this SM. We want to make
    // sure we garbage collect only when DLT is closed
    err = objStorMgr->omClient->getDltManager()->setCurrentDltClosed();
    if (err == ERR_NOT_FOUND) {
        LOGERROR << "SM received DLT close without receiving DLT, ok for now, but fix OM!!!";
        // returning OK to OM
        sendAsyncResp(*hdr, FDSP_MSG_TYPEID(fpi::EmptyMsg), fpi::EmptyMsg());
        return;
    }

    // Store the current DLT to the presistent storage to be used
    // by offline smcheck.
    objStorMgr->storeCurrentDLT();

    // tell superblock that DLT is closed, so that it will invalidate
    // appropriate SM tokens
    err = objStorMgr->objectStore->handleDltClose(objStorMgr->getDLT());

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

    // send response
    hdr->msg_code = err.GetErrno();
    sendAsyncResp(*hdr, FDSP_MSG_TYPEID(fpi::EmptyMsg), fpi::EmptyMsg());
}

// NotifyDMTUpdate
// Necessary for streaming stats
// ----------------
//
void
SMSvcHandler::NotifyDMTUpdate(boost::shared_ptr<fpi::AsyncHdr> &hdr,
        boost::shared_ptr<fpi::CtrlNotifyDMTUpdate> &dmt)
{
    Error err(ERR_OK);
    LOGNOTIFY << "OMClient received new DMT commit version  "
                << dmt->dmt_data.dmt_type;
    err = objStorMgr->omClient->updateDmt(dmt->dmt_data.dmt_type, dmt->dmt_data.dmt_data);
    if (err == ERR_DUPLICATE) {
        LOGWARN << "Received duplicate DMT, ignoring";
        err = ERR_OK;
    }
    hdr->msg_code = err.GetErrno();
    sendAsyncResp(*hdr, FDSP_MSG_TYPEID(fpi::CtrlNotifyDMTUpdate), *dmt);
}

void
SMSvcHandler::NotifySMCheck(boost::shared_ptr<fpi::AsyncHdr>& hdr,
                            boost::shared_ptr<fpi::CtrlNotifySMCheck>& msg)
{
    Error err(ERR_OK);

    LOGDEBUG << "Received SMCheck cmd=" << msg->SmCheckCmd << " with target tokens list of len "
             << msg->targetTokens.size();

    std::set<fds_token_id> tgtTokens;
    for (auto token : msg->targetTokens) {
        tgtTokens.insert(token);
    }
    SmCheckActionCmd actionCmd(msg->SmCheckCmd, tgtTokens);
    err = objStorMgr->objectStore->SmCheckControlCmd(&actionCmd);
    hdr->msg_code = static_cast<int32_t>(err.GetErrno());
    sendAsyncResp(*hdr, FDSP_MSG_TYPEID(fpi::CtrlNotifySMCheck), *msg);
}

void
SMSvcHandler::querySMCheckStatus(boost::shared_ptr<fpi::AsyncHdr> &hdr,
                                 boost::shared_ptr<fpi::CtrlNotifySMCheckStatus>& msg)
{
    Error err(ERR_OK);

    LOGDEBUG << "Received SMCheck status query";

    fpi::CtrlNotifySMCheckStatusRespPtr resp(new fpi::CtrlNotifySMCheckStatusResp());
    SmCheckStatusCmd statusCmd(resp);
    err = objStorMgr->objectStore->SmCheckControlCmd(&statusCmd);
    hdr->msg_code = static_cast<int32_t>(err.GetErrno());
    sendAsyncResp(*hdr, FDSP_MSG_TYPEID(fpi::CtrlNotifySMCheckStatusResp), *resp);
}


}  // namespace fds
