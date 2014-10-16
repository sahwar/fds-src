/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#include <StorMgr.h>
#include <net/net-service-tmpl.hpp>
#include <fdsp_utils.h>
#include <fds_assert.h>
#include <SMSvcHandler.h>
#include <platform/fds_flags.h>
#include <sm-platform.h>
#include <string>
#include <net/SvcRequest.h>
#include <fiu-local.h>

namespace fds {

extern ObjectStorMgr *objStorMgr;

SMSvcHandler::SMSvcHandler()
{
    REGISTER_FDSP_MSG_HANDLER(fpi::GetObjectMsg, getObject);
    REGISTER_FDSP_MSG_HANDLER(fpi::PutObjectMsg, putObject);
    REGISTER_FDSP_MSG_HANDLER(fpi::DeleteObjectMsg, deleteObject);

    REGISTER_FDSP_MSG_HANDLER(fpi::NodeSvcInfo, notifySvcChange);
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlNotifyVolAdd, NotifyAddVol);
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlNotifyVolRemove, NotifyRmVol);
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlNotifyVolMod, NotifyModVol);
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlNotifyScavenger, NotifyScavenger);
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlTierPolicy, TierPolicy);
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlTierPolicyAudit, TierPolicyAudit);

    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlNotifyDLTUpdate, NotifyDLTUpdate);

    REGISTER_FDSP_MSG_HANDLER(fpi::AddObjectRefMsg, addObjectRef);
}

void SMSvcHandler::getObject(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                             boost::shared_ptr<fpi::GetObjectMsg>& getObjMsg)  // NOLINT
{
    DBG(GLOGDEBUG << logString(*asyncHdr) << fds::logString(*getObjMsg));

    DBG(FLAG_CHECK_RETURN_VOID(common_drop_async_resp > 0));
    DBG(FLAG_CHECK_RETURN_VOID(sm_drop_gets > 0));
    fiu_do_on("svc.drop.getobject", return);

    Error err(ERR_OK);
    auto getReq = new SmIoGetObjectReq(getObjMsg);
    getReq->io_type = FDS_SM_GET_OBJECT;
    getReq->setVolId(getObjMsg->volume_id);
    getReq->setObjId(ObjectID(getObjMsg->data_obj_id.digest));
    getReq->obj_data.obj_id = getObjMsg->data_obj_id;
    // perf-trace related data
    getReq->perfNameStr = "volume:" + std::to_string(getObjMsg->volume_id);
    getReq->opReqFailedPerfEventType = SM_GET_OBJ_REQ_ERR;
    getReq->opReqLatencyCtx.type = SM_E2E_GET_OBJ_REQ;
    getReq->opReqLatencyCtx.name = getReq->perfNameStr;
    getReq->opReqLatencyCtx.reset_volid(getObjMsg->volume_id);
    getReq->opLatencyCtx.type = SM_GET_IO;
    getReq->opLatencyCtx.name = getReq->perfNameStr;
    getReq->opLatencyCtx.reset_volid(getObjMsg->volume_id);
    getReq->opQoSWaitCtx.type = SM_GET_QOS_QUEUE_WAIT;
    getReq->opQoSWaitCtx.name = getReq->perfNameStr;
    getReq->opQoSWaitCtx.reset_volid(getObjMsg->volume_id);

    getReq->response_cb = std::bind(
        &SMSvcHandler::getObjectCb, this,
        asyncHdr,
        std::placeholders::_1, std::placeholders::_2);

    // start measuring E2E latency
    PerfTracer::tracePointBegin(getReq->opReqLatencyCtx);

    err = objStorMgr->enqueueMsg(getReq->getVolId(), getReq);
    if (err != fds::ERR_OK) {
        fds_assert(!"Hit an error in enqueing");
        LOGERROR << "Failed to enqueue to SmIoReadObjectMetadata to StorMgr.  Error: "
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
    // TODO(Andrew): This is temp code. For now we want to preseve the
    // legacy IO path. This checks if the legacy IO path filled data or
    // if the new one did. If the new one dide, the network msg already
    // has the data. No need to copy.
    if (getReq->obj_data.data.size() != 0) {
        resp = boost::make_shared<GetObjectResp>();
        resp->data_obj_len = getReq->obj_data.data.size();
        resp->data_obj = getReq->obj_data.data;
    } else {
        resp = getReq->getObjectNetResp;
    }

    // E2E latency end
    PerfTracer::tracePointEnd(getReq->opReqLatencyCtx);
    if (!err.ok()) {
        PerfTracer::incr(getReq->opReqFailedPerfEventType,
                         getReq->getVolId(), getReq->perfNameStr);
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

    DBG(FLAG_CHECK_RETURN_VOID(common_drop_async_resp > 0));
    DBG(FLAG_CHECK_RETURN_VOID(sm_drop_puts > 0));
    fiu_do_on("svc.drop.putobject", return);
    fiu_do_on("svc.uturn.putobject", putObjectCb(asyncHdr, ERR_OK, NULL));

    Error err(ERR_OK);
    auto putReq = new SmIoPutObjectReq(putObjMsg);
    putReq->io_type = FDS_SM_PUT_OBJECT;
    putReq->setVolId(putObjMsg->volume_id);
    putReq->origin_timestamp = putObjMsg->origin_timestamp;
    putReq->setObjId(ObjectID(putObjMsg->data_obj_id.digest));
    // putReq->data_obj = std::move(putObjMsg->data_obj);
    // putObjMsg->data_obj.clear();
    putReq->data_obj = putObjMsg->data_obj;
    // perf-trace related data
    putReq->perfNameStr = "volume:" + std::to_string(putObjMsg->volume_id);
    putReq->opReqFailedPerfEventType = SM_PUT_OBJ_REQ_ERR;
    putReq->opReqLatencyCtx.type = SM_E2E_PUT_OBJ_REQ;
    putReq->opReqLatencyCtx.name = putReq->perfNameStr;
    putReq->opReqLatencyCtx.reset_volid(putObjMsg->volume_id);
    putReq->opLatencyCtx.type = SM_PUT_IO;
    putReq->opLatencyCtx.name = putReq->perfNameStr;
    putReq->opLatencyCtx.reset_volid(putObjMsg->volume_id);
    putReq->opQoSWaitCtx.type = SM_PUT_QOS_QUEUE_WAIT;
    putReq->opQoSWaitCtx.name = putReq->perfNameStr;
    putReq->opQoSWaitCtx.reset_volid(putObjMsg->volume_id);

    putReq->response_cb= std::bind(
        &SMSvcHandler::putObjectCb, this,
        asyncHdr,
        std::placeholders::_1, std::placeholders::_2);

    // start measuring E2E latency
    PerfTracer::tracePointBegin(putReq->opReqLatencyCtx);

    err = objStorMgr->enqueueMsg(putReq->getVolId(), putReq);
    if (err != fds::ERR_OK) {
        fds_assert(!"Hit an error in enqueing");
        LOGERROR << "Failed to enqueue to SmIoPutObjectReq to StorMgr.  Error: "
                 << err;
        putObjectCb(asyncHdr, err, putReq);
    }
}

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
                             putReq->getVolId(), putReq->perfNameStr);
        }
    }

    auto resp = boost::make_shared<fpi::PutObjectRspMsg>();
    asyncHdr->msg_code = static_cast<int32_t>(err.GetErrno());
    sendAsyncResp(*asyncHdr, FDSP_MSG_TYPEID(fpi::PutObjectRspMsg), *resp);

    if ((objStorMgr->testUturnAll == true) ||
        (objStorMgr->testUturnPutObj == true)) {
        fds_verify(putReq == NULL);
        return;
    }

    delete putReq;
}

void SMSvcHandler::deleteObject(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                                boost::shared_ptr<fpi::DeleteObjectMsg>& expObjMsg)
{
    DBG(GLOGDEBUG << fds::logString(*asyncHdr) << fds::logString(*expObjMsg));
    Error err(ERR_OK);

    auto delReq = new SmIoDeleteObjectReq();

    // Set delReq stuffs
    delReq->io_type = FDS_SM_DELETE_OBJECT;

    delReq->setVolId(expObjMsg->volId);
    delReq->origin_timestamp = expObjMsg->origin_timestamp;
    delReq->setObjId(ObjectID(expObjMsg->objId.digest));
    // perf-trace related data
    delReq->perfNameStr = "volume:" + std::to_string(expObjMsg->volId);
    delReq->opReqFailedPerfEventType = SM_DELETE_OBJ_REQ_ERR;
    delReq->opReqLatencyCtx.type = SM_E2E_DELETE_OBJ_REQ;
    delReq->opReqLatencyCtx.name = delReq->perfNameStr;
    delReq->opLatencyCtx.type = SM_DELETE_IO;
    delReq->opLatencyCtx.name = delReq->perfNameStr;
    delReq->opQoSWaitCtx.type =SM_DELETE_QOS_QUEUE_WAIT;
    delReq->opQoSWaitCtx.name = delReq->perfNameStr;

    delReq->response_cb = std::bind(
        &SMSvcHandler::deleteObjectCb, this,
        asyncHdr,
        std::placeholders::_1, std::placeholders::_2);

    // start measuring E2E latency
    PerfTracer::tracePointBegin(delReq->opReqLatencyCtx);

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
    PerfTracer::tracePointEnd(del_req->opReqLatencyCtx);
    if (!err.ok()) {
        PerfTracer::incr(del_req->opReqFailedPerfEventType,
                         del_req->getVolId(), del_req->perfNameStr);
    }

    auto resp = boost::make_shared<fpi::DeleteObjectRspMsg>();
    asyncHdr->msg_code = static_cast<int32_t>(err.GetErrno());
    sendAsyncResp(*asyncHdr, FDSP_MSG_TYPEID(fpi::DeleteObjectRspMsg), *resp);

    delete del_req;
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

        if (err.ok()) {
            objStorMgr->createCache(volumeId, 1024 * 1024 * 8, 1024 * 1024 * 256);
        } else {
            // most likely axceeded min iops
            objStorMgr->deregVol(volumeId);
        }
    }
    if (!err.ok()) {
        GLOGERROR << "Registration failed for vol id " << std::hex << volumeId
                  << std::dec << " error: " << err.GetErrstr();
    }
    hdr->msg_code = err.GetErrno();
    sendAsyncResp(*hdr, FDSP_MSG_TYPEID(fpi::CtrlNotifyVolAdd), *vol_msg);
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
    fds_volid_t volumeId = vol_msg->vol_desc.volUUID;
    VolumeDesc vdbc(vol_msg->vol_desc), * vdb = &vdbc;
    GLOGNOTIFY << "Received modify for vol "
               << "[" << std::hex << volumeId << std::dec << ", "
               << vdb->getName() << "]";

    StorMgrVolume * vol = objStorMgr->getVol(volumeId);
    fds_assert(vol != NULL);
    if (vol->voldesc->mediaPolicy != vdb->mediaPolicy) {
        GLOGWARN << "Modify volume requested to modify media policy "
                 << "- Not supported yet! Not modifying media policy";
    }

    vol->voldesc->modifyPolicyInfo(vdb->iops_min, vdb->iops_max, vdb->relativePrio);
    err = objStorMgr->modVolQos(vol->getVolId(),
                                vdb->iops_min, vdb->iops_max, vdb->relativePrio);
    if ( !err.ok() )  {
        GLOGERROR << "Modify volume policy failed for vol " << vdb->getName()
                  << std::hex << volumeId << std::dec << " error: "
                  << err.GetErrstr();
    }
    hdr->msg_code = err.GetErrno();
    sendAsyncResp(*hdr, FDSP_MSG_TYPEID(fpi::CtrlNotifyVolMod), *vol_msg);
}

// TierPolicy
// ----------
//
void
SMSvcHandler::TierPolicy(boost::shared_ptr<fpi::AsyncHdr>       &hdr,
                         boost::shared_ptr<fpi::CtrlTierPolicy> &msg)
{
    // LOGNOTIFY
    // << "OMClient received tier policy for vol "
    // << tier->tier_vol_uuid;
    fds_verify(objStorMgr->omc_srv_pol != nullptr);
    fdp::FDSP_TierPolicyPtr tp(new FDSP_TierPolicy(msg->tier_policy));
    objStorMgr->omc_srv_pol->serv_recvTierPolicyReq(tp);
    hdr->msg_code = 0;
    sendAsyncResp(*hdr, FDSP_MSG_TYPEID(fpi::CtrlTierPolicy), *msg);
}

// TierPolicyAudit
// ---------------
//
void
SMSvcHandler::TierPolicyAudit(boost::shared_ptr<fpi::AsyncHdr>            &hdr,
                              boost::shared_ptr<fpi::CtrlTierPolicyAudit> &msg)
{
    // LOGNOTIFY
    // << "OMClient received tier audit policy for vol "
    // << audit->tier_vol_uuid;

    fds_verify(objStorMgr->omc_srv_pol != nullptr);
    fdp::FDSP_TierPolicyAuditPtr ta(new FDSP_TierPolicyAudit(msg->tier_audit));
    objStorMgr->omc_srv_pol->serv_recvTierPolicyAuditReq(ta);
    hdr->msg_code = 0;
    sendAsyncResp(*hdr, FDSP_MSG_TYPEID(fpi::CtrlTierPolicyAudit), *msg);
}

void SMSvcHandler::addObjectRef(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                                boost::shared_ptr<fpi::AddObjectRefMsg>& addObjRefMsg) {
    DBG(GLOGDEBUG << fds::logString(*asyncHdr) << " " << fds::logString(*addObjRefMsg));
    Error err(ERR_OK);

    auto addObjRefReq = new SmIoAddObjRefReq(addObjRefMsg);

    // perf-trace related data
    addObjRefReq->perfNameStr = "volume:" + std::to_string(addObjRefMsg->srcVolId);
    addObjRefReq->opReqFailedPerfEventType = SM_ADD_OBJ_REF_REQ_ERR;
    addObjRefReq->opReqLatencyCtx.type = SM_E2E_ADD_OBJ_REF_REQ;
    addObjRefReq->opReqLatencyCtx.name = addObjRefReq->perfNameStr;
    addObjRefReq->opLatencyCtx.type = SM_ADD_OBJ_REF_IO;
    addObjRefReq->opLatencyCtx.name = addObjRefReq->perfNameStr;
    addObjRefReq->opQoSWaitCtx.type = SM_ADD_OBJ_REF_QOS_QUEUE_WAIT;
    addObjRefReq->opQoSWaitCtx.name = addObjRefReq->perfNameStr;

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
                         addObjRefReq->getVolId(), addObjRefReq->perfNameStr);
    }

    auto resp = boost::make_shared<fpi::AddObjectRefRspMsg>();
    asyncHdr->msg_code = static_cast<int32_t>(err.GetErrno());
    sendAsyncResp(*asyncHdr, FDSP_MSG_TYPEID(fpi::AddObjectRefRspMsg), *resp);

    delete addObjRefReq;
}

void
SMSvcHandler::NotifyScavenger(boost::shared_ptr<fpi::AsyncHdr>         &hdr,
                              boost::shared_ptr<fpi::CtrlNotifyScavenger> &msg)
{
    LOGNORMAL << " receive scavenger cmd " << msg->scavenger.cmd;
    switch (msg->scavenger.cmd) {
        case FDS_ProtocolInterface::FDSP_SCAVENGER_ENABLE:
            // objStorMgr->scavenger->enableScavenger();
            break;
        case FDS_ProtocolInterface::FDSP_SCAVENGER_DISABLE:
            // objStorMgr->scavenger->disableScavenger();
            break;
        case FDS_ProtocolInterface::FDSP_SCAVENGER_START:
            // objStorMgr->scavenger->startScavengeProcess();
            break;
        case FDS_ProtocolInterface::FDSP_SCAVENGER_STOP:
            // objStorMgr->scavenger->stopScavengeProcess();
            break;
        default:
            fds_verify(false);  // unknown scavenger command
    };
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
    err = objStorMgr->omClient->updateDlt(dlt->dlt_data.dlt_type, dlt->dlt_data.dlt_data);
    fds_assert(err.ok());
    objStorMgr->handleDltUpdate();

    LOGNOTIFY << "Sending DLT commit response to OM";
    hdr->msg_code = err.GetErrno();
    sendAsyncResp(*hdr, FDSP_MSG_TYPEID(fpi::EmptyMsg), fpi::EmptyMsg());
}

}  // namespace fds
