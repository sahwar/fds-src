/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#include "./OMgrClient.h"
#include <fds_assert.h>
#include <boost/thread.hpp>
#include <NetSession.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TSimpleServer.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TTransportUtils.h>
#include <thrift/transport/TBufferTransports.h>
#include <dlt.h>

#include <net/net_utils.h>
#include <net/SvcRequestPool.h>
#include "platform/platform_process.h"
#include "platform/platform.h"
#include <fds_typedefs.h>
#include <thread>
#include <string>
using namespace std; // NOLINT
using namespace fds; // NOLINT

namespace fds {
extern const NodeUuid gl_OmUuid;
extern SvcRequestPool *gSvcRequestPool;

OMgrClientRPCI::OMgrClientRPCI(OMgrClient *omc) {
    this->om_client = omc;
}

void OMgrClientRPCI::AttachVol(fpi::FDSP_MsgHdrTypePtr& msg_hdr,
                               fpi::FDSP_AttachVolTypePtr& vol_msg) {
    fds::VolumeDesc *vdb = new fds::VolumeDesc(vol_msg->vol_desc);
}

void OMgrClientRPCI::DetachVol(fpi::FDSP_MsgHdrTypePtr& msg_hdr,
                               fpi::FDSP_AttachVolTypePtr& vol_msg) {
    fds::VolumeDesc *vdb = new fds::VolumeDesc(vol_msg->vol_desc);
}

void OMgrClientRPCI::NotifyNodeAdd(FDSP_MsgHdrTypePtr& msg_hdr,
                                   FDSP_Node_Info_TypePtr& node_info) {
    Platform *plat = PlatformProcess::plf_manager();
    FdspNodeRegPtr reg = FdspNodeRegPtr(new FDSP_RegisterNodeType());
    NodeUuid svc_uuid(node_info->service_uuid);

    reg->node_type      = node_info->node_type;
    reg->node_name      = node_info->node_name;
    reg->domain_id      = 0;  // node_info doesn't have it.
    reg->ip_hi_addr     = node_info->ip_hi_addr;
    reg->ip_lo_addr     = node_info->ip_lo_addr;
    reg->control_port   = node_info->control_port;
    reg->data_port      = node_info->data_port;
    reg->migration_port = node_info->migration_port;
    reg->metasync_port = node_info->metasync_port;
    reg->node_root      = node_info->node_root;
    reg->node_uuid.uuid    = node_info->node_uuid;
    reg->service_uuid.uuid = node_info->service_uuid;

    plat->plf_reg_node_info(svc_uuid, reg);
    om_client->recvNodeEvent(node_info->node_id, node_info->node_type,
                             (unsigned int) node_info->ip_lo_addr,
                             node_info->node_state, node_info);
}

void OMgrClientRPCI::NotifyNodeRmv(FDSP_MsgHdrTypePtr& msg_hdr,
                                   FDSP_Node_Info_TypePtr& node_info) {
    om_client->recvNodeEvent(node_info->node_id, node_info->node_type,
                             (unsigned int) node_info->ip_lo_addr,
                             node_info->node_state, node_info);
}

void OMgrClientRPCI::NotifyDMTUpdate(FDSP_MsgHdrTypePtr& msg_hdr,
                                     FDSP_DMT_TypePtr& dmt_info) {
    #if 0
    Error err(ERR_OK);
    err = om_client->recvDMTUpdate(dmt_info, msg_hdr->session_uuid);
    if (om_client->getNodeType() == fpi::FDSP_DATA_MGR) {
        // if not error, commit ack is async, so only reply here on error
        if (!err.ok()) {
            LOGERROR << "Commit DMT failed, volume meta may not be synced properly";
            // ignore not ready errors
            if (err == ERR_NOT_READY) err = ERR_OK;
            om_client->sendDMTCommitAck(err, msg_hdr->session_uuid);
        }
    } else {
        // DMT commit is sync for all other services, send response now
        om_client->sendDMTCommitAck(err, msg_hdr->session_uuid);
    }
    #endif
}


void OMgrClientRPCI::NotifyDMTClose(FDSP_MsgHdrTypePtr& fdsp_msg,
                                    FDSP_DmtCloseTypePtr& dmt_close) {
#if 0
    om_client->recvDMTClose(dmt_close->DMT_version, fdsp_msg->session_uuid);
#endif
}


void OMgrClientRPCI::PushMetaDMTReq(FDSP_MsgHdrTypePtr& fdsp_msg,
                                    FDSP_PushMetaPtr& push_meta_resp) {
    Error err(ERR_OK);
    LOGNORMAL << "Received Push Meta request";
    if (om_client->getNodeType() == fpi::FDSP_DATA_MGR) {
        err = om_client->recvDMTPushMeta(push_meta_resp, fdsp_msg->session_uuid);
        if (!err.ok()) {
            LOGERROR << "We could not start push meta process, " << err;
            om_client->sendDMTPushMetaAck(err, fdsp_msg->session_uuid);
        }
    } else {
        fds_verify(false);  // should not send push meta to non-DM nodes!
    }
}

OMgrClient::OMgrClient(FDSP_MgrIdType node_type,
                       const std::string& _omIpStr,
                       fds_uint32_t _omPort,
                       const std::string& node_name,
                       fds_log *parent_log,
                       boost::shared_ptr<netSessionTbl> nst,
                       Platform *plf,
                       fds_uint32_t _instanceId)
        : dltMgr(new DLTManager()),
          dmtMgr(new DMTManager(1)),
          instanceId(_instanceId) {
    fds_verify(_omPort != 0);
    my_node_type = node_type;
    omIpStr      = _omIpStr;
    omConfigPort = _omPort;
    my_node_name = node_name;
    node_evt_hdlr = NULL;
    bucket_stats_cmd_hdlr = NULL;
    dltclose_evt_hdlr = NULL;
    catalog_evt_hdlr = NULL;
    if (parent_log) {
        omc_log = parent_log;
    } else {
        omc_log = new fds_log("omc", "logs");
    }
    nst_ = nst;

    clustMap = new LocalClusterMap();
    plf_mgr  = plf;
    fNoNetwork = false;
}

OMgrClient::~OMgrClient()
{
    nst_->endSession(omrpc_handler_session_->getSessionTblKey());
    omrpc_handler_thread_->join();

    delete clustMap;
}

int OMgrClient::initialize() {
    return fds::ERR_OK;
}

int OMgrClient::registerEventHandlerForMigrateEvents(migration_event_handler_t migrate_event_hdlr) {
    this->migrate_evt_hdlr = migrate_event_hdlr;
    return 0;
}

int OMgrClient::registerEventHandlerForDltCloseEvents(dltclose_event_handler_t dltclose_event_hdlr) { //NOLINT
    this->dltclose_evt_hdlr = dltclose_event_hdlr;
    return 0;
}

int OMgrClient::registerEventHandlerForNodeEvents(node_event_handler_t node_event_hdlr) {
    this->node_evt_hdlr = node_event_hdlr;
    return 0;
}

int OMgrClient::registerBucketStatsCmdHandler(bucket_stats_cmd_handler_t cmd_hdlr) {
    bucket_stats_cmd_hdlr = cmd_hdlr;
    return 0;
}


void OMgrClient::registerCatalogEventHandler(catalog_event_handler_t evt_hdlr) {
    catalog_evt_hdlr = evt_hdlr;
}

/**
 * @brief Starts OM RPC handling server.  This function is to be run on a
 * separate thread.  OMgrClient destructor does a join() on this thread
 */
void OMgrClient::start_omrpc_handler()
{
    if (fNoNetwork) return;
    try {
        nst_->listenServer(omrpc_handler_session_);
    } catch(const att::TTransportException& e) {
        LOGERROR << "unable to listen at the given port - check the port";
        LOGERROR << "error during network call : " << e.what();
        fds_panic("Unable to listen on server...");
    }
}

// Call this to setup the (receiving side) endpoint to lister for control path requests from OM.
int OMgrClient::startAcceptingControlMessages() {
    if (fNoNetwork) return 0;
    std::string myIp = fds::net::get_local_ip(
        g_fdsprocess->get_fds_config()->get<std::string>("fds.nic_if"));
    int myIpInt = netSession::ipString2Addr(myIp);
    omrpc_handler_.reset(new OMgrClientRPCI(this));
    // TODO(x): Ideally createServerSession should take a shared pointer
    // for omrpc_handler_.  Make sure that happens.  Otherwise you
    // end up with a pointer leak.
    fds_uint32_t ctrlPort = plf_mgr->plf_get_my_ctrl_port() + instanceId;
    omrpc_handler_session_ =
            nst_->createServerSession<netControlPathServerSession>(myIpInt,
                                                                   ctrlPort,
                                                                   my_node_name,
                                                                   FDSP_ORCH_MGR,
                                                                   omrpc_handler_);
    omrpc_handler_thread_.reset(new boost::thread(&OMgrClient::start_omrpc_handler, this));

    LOGNOTIFY << "OMClient accepting control requests at port "
              << ctrlPort;

    return (0);
}

void OMgrClient::initOMMsgHdr(const FDSP_MsgHdrTypePtr& msg_hdr)
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

    msg_hdr->src_id = my_node_type;
    msg_hdr->dst_id = FDSP_ORCH_MGR;
    msg_hdr->src_node_name = my_node_name;
    msg_hdr->src_service_uuid.uuid = myUuid.uuid_get_val();

    msg_hdr->err_code = ERR_OK;
    msg_hdr->result = FDSP_ERR_OK;
}

// Use this to register the local node with OM as a client.
// Should be called after calling starting subscription endpoint and control path endpoint.
int OMgrClient::registerNodeWithOM(Platform *plat)
{
    if (fNoNetwork) return 0;
    try {
        omclient_prx_session_ = nst_->startSession<netOMControlPathClientSession>(
            omIpStr, omConfigPort, FDSP_ORCH_MGR, 1, /* number of channels */
            boost::shared_ptr<FDSP_OMControlPathRespIf>());
        /* TODO:  pass in response path server pointer */
        // Just return if the om ptr is NULL because
        // FDS-net doesn't throw the exception we're
        // trying to catch. We should probably return
        // an error and let the caller decide what to do.
        // fds_verify(omclient_prx_session_ != nullptr);
        if (omclient_prx_session_ == NULL) {
            LOGCRITICAL << "OMClient unable to register node with OrchMgr. "
                    "Please check if OrchMgr is up and restart.";
            return 0;
        }
        om_client_prx = omclient_prx_session_->getClient();  // NOLINT

        FDSP_MsgHdrTypePtr msg_hdr(new FDSP_MsgHdrType);
        initOMMsgHdr(msg_hdr);

        FDSP_RegisterNodeTypePtr reg_node_msg(new FDSP_RegisterNodeType);
        reg_node_msg->node_type    = plat->plf_get_node_type();
        reg_node_msg->node_name    = *plat->plf_get_my_name();
        reg_node_msg->ip_hi_addr   = 0;
        reg_node_msg->ip_lo_addr   = fds::str_to_ipv4_addr(*plat->plf_get_my_ip());
        reg_node_msg->control_port = plat->plf_get_my_ctrl_port();
        reg_node_msg->data_port    = plat->plf_get_my_data_port();
        reg_node_msg->node_root    = g_fdsprocess->proc_fdsroot()->dir_fdsroot();

        // TODO(Andrew): Move to SM specific
        reg_node_msg->migration_port = plat->plf_get_my_migration_port();
        reg_node_msg->metasync_port  = plat->plf_get_my_metasync_port();

        // TODO(Vy): simple service uuid from node uuid.
        reg_node_msg->node_uuid.uuid    = plat->plf_get_my_node_uuid()->uuid_get_val();
        reg_node_msg->service_uuid.uuid = plat->plf_get_my_svc_uuid()->uuid_get_val();
        myUuid.uuid_set_val(plat->plf_get_my_svc_uuid()->uuid_get_val());

        LOGNOTIFY << "OMClient registering local node "
                  << fds::ipv4_addr_to_str(reg_node_msg->ip_lo_addr)
                  << " control port:" << reg_node_msg->control_port
                  << " data port:" << reg_node_msg->data_port
                  << " with Orchaestration Manager at "
                  << omIpStr << ":" << omConfigPort;

        om_client_prx->RegisterNode(msg_hdr, reg_node_msg);
        LOGDEBUG << "OMClient completed node registration with OM";
    }
    catch(...) {
        LOGCRITICAL << "OMClient unable to register node with OrchMgr. "
                "Please check if OrchMgr is up and restart.";
    }
    return (0);
}

int OMgrClient::testBucket(const std::string& bucket_name,
                           const fpi::FDSP_VolumeDescTypePtr& vol_info,
                           fds_bool_t attach_vol_reqd,
                           const std::string& accessKeyId,
                           const std::string& secretAccessKey)
{
    if (fNoNetwork) return 0;
    try {
        auto req =  gSvcRequestPool->newEPSvcRequest(gl_OmUuid.toSvcUuid());
        fpi::CtrlTestBucketPtr pkt(new fpi::CtrlTestBucket());
        fpi::FDSP_TestBucket * test_buck_msg = & pkt->tbmsg;
        test_buck_msg->bucket_name = bucket_name;
        test_buck_msg->vol_info = *vol_info;
        test_buck_msg->attach_vol_reqd = attach_vol_reqd;
        test_buck_msg->accessKeyId = accessKeyId;
        test_buck_msg->secretAccessKey;
        req->setPayload(FDSP_MSG_TYPEID(fpi::CtrlTestBucket), pkt);
        req->invoke();
        LOGNOTIFY << " sending test bucket request to OM " << bucket_name;
    } catch(...) {
        LOGERROR << "OMClient unable to push test bucket to OM. Check if OM is up and restart.";
    }
    return 0;
}

int OMgrClient::pushCreateBucketToOM(const fpi::FDSP_VolumeDescTypePtr& volInfo)
{
    if (fNoNetwork) return 0;
    try {
        auto req =  gSvcRequestPool->newEPSvcRequest(gl_OmUuid.toSvcUuid());
        fpi::CtrlCreateBucketPtr pkt(new fpi::CtrlCreateBucket());
        FDSP_CreateVolType * volData = & pkt->cv;

        volData->vol_name = volInfo->vol_name;
        volData->vol_info.vol_name = volInfo->vol_name;
        volData->vol_info.tennantId = volInfo->tennantId;
        volData->vol_info.localDomainId = volInfo->localDomainId;

        volData->vol_info.volType = volInfo->volType;
        volData->vol_info.maxObjSizeInBytes = volInfo->maxObjSizeInBytes;
        volData->vol_info.capacity = volInfo->capacity;

        volData->vol_info.volPolicyId = 50;  //  default policy
        volData->vol_info.placementPolicy = volInfo->placementPolicy;
        volData->vol_info.mediaPolicy = volInfo->mediaPolicy;

        req->setPayload(FDSP_MSG_TYPEID(fpi::CtrlCreateBucket), pkt);
        req->invoke();
        LOGNOTIFY << "OMClient sending create bucket request to OM ";
    } catch(...) {
        LOGERROR << "OMClient unable to push the create bucket request to OM."
                 << "Check if OM is up and restart.";
        return -1;
    }

    return 0;
}

int OMgrClient::pushModifyBucketToOM(const std::string& bucket_name,
                                     const fpi::FDSP_VolumeDescTypePtr& vol_desc)
{
    if (fNoNetwork) return 0;
    try {
        auto req =  gSvcRequestPool->newEPSvcRequest(gl_OmUuid.toSvcUuid());
        fpi::CtrlModifyBucketPtr pkt(new fpi::CtrlModifyBucket());
        FDSP_ModifyVolType * mod_vol_msg= &pkt->mv;
        mod_vol_msg->vol_name = bucket_name;
        /* make sure that uuid is not checked, because we don't know it here */
        mod_vol_msg->vol_uuid = 0;
        mod_vol_msg->vol_desc = *vol_desc;
        req->setPayload(FDSP_MSG_TYPEID(fpi::CtrlModifyBucket), pkt);
        req->invoke();
        LOGNOTIFY << "OMClient sending modify bucket request to OM";
    }
    catch(...) {
        LOGERROR << "OMClient unable to send ModifyBucket request to OM"
                 << "Check if OM is up and restart.";
        return -1;
    }

    return 0;
}

int OMgrClient::pushDeleteBucketToOM(const fpi::FDSP_DeleteVolTypePtr& volInfo)
{
    if (fNoNetwork) return 0;
    try {
        auto req =  gSvcRequestPool->newEPSvcRequest(gl_OmUuid.toSvcUuid());
        fpi::CtrlDeleteBucketPtr pkt(new fpi::CtrlDeleteBucket());
        FDSP_DeleteVolType* volData = & pkt->dv;
        volData->vol_name  = volInfo->vol_name;
        volData->domain_id = volInfo->domain_id;
        req->setPayload(FDSP_MSG_TYPEID(fpi::CtrlDeleteBucket), pkt);
        req->invoke();
        LOGNOTIFY << "OMClient sending modify bucket request to OM";
    } catch(...) {
        LOGERROR << "OMClient unable to push perf stats to OM. Check if OM is up and restart.";
    }

    return 0;
}


int OMgrClient::recvMigrationEvent(bool dlt_type)
{
    LOGNOTIFY << "OMClient received Migration event for node " << dlt_type;

    if (this->migrate_evt_hdlr) {
        this->migrate_evt_hdlr(dlt_type);
    }
    return (0);
}

Error OMgrClient::sendDMTPushMetaAck(const Error& op_err,
                                     const std::string& session_uuid) {
    Error err(ERR_OK);
    if (fNoNetwork) return err;

    // send ack back to OM
    boost::shared_ptr<fpi::FDSP_ControlPathRespClient> resp_client_prx =
            omrpc_handler_session_->getRespClient(session_uuid);

    try {
        FDSP_MsgHdrTypePtr msg_hdr(new FDSP_MsgHdrType);
        initOMMsgHdr(msg_hdr);
        FDSP_PushMetaPtr meta_resp(new FDSP_PushMeta());
        // TODO(xxx) should we send the whole PushMeta msg?
        // for now sending empty
        msg_hdr->err_code = op_err.GetErrno();
        if (!op_err.ok()) {
            msg_hdr->result = FDSP_ERR_FAILED;
        }

        resp_client_prx->PushMetaDMTResp(msg_hdr, meta_resp);
        LOGNOTIFY << "OMClient sending PushMeta resp to OM " << err;
    }
    catch(...) {
        LOGERROR << "OMClient unable to send PushMeta response to OM."
                 << " Check if OM is up and restart";
        err = Error(ERR_NETWORK_TRANSPORT);
    }

    return err;
}

Error OMgrClient::sendDMTCommitAck(const Error& op_err,
                                   const std::string& session_uuid) {
    Error err(ERR_OK);

    // send ack back to OM
    boost::shared_ptr<fpi::FDSP_ControlPathRespClient> resp_client_prx =
            omrpc_handler_session_->getRespClient(session_uuid);

    try {
        FDSP_MsgHdrTypePtr msg_hdr(new FDSP_MsgHdrType);
        initOMMsgHdr(msg_hdr);
        msg_hdr->err_code = op_err.GetErrno();
        if (!op_err.ok()) {
            msg_hdr->result = FDSP_ERR_FAILED;
        }
        FDSP_DMT_Resp_TypePtr dmt_resp(new FDSP_DMT_Resp_Type);
        dmt_resp->DMT_version = getDMTVersion();
        resp_client_prx->NotifyDMTUpdateResp(msg_hdr, dmt_resp);
        LOGNOTIFY << "OMClient sent response for DMT update to OM " << op_err;
    } catch(...) {
        LOGERROR << "OMClient failed to send DMT response to OM";
        err = ERR_NETWORK_TRANSPORT;
    }

    return err;
}

int OMgrClient::recvNodeEvent(int node_id,
                              FDSP_MgrIdType node_type,
                              unsigned int node_ip,
                              int node_state,
                              const FDSP_Node_Info_TypePtr& node_info)
{
    omc_lock.write_lock();

    node_info_t node;

    node.node_id = node_info->service_uuid;
    node.node_ip_address = node_ip;
    node.port = node_info->data_port;
    node.node_state = (FDSP_NodeState) node_state;
    node.mig_port = node_info->migration_port;
    node.meta_sync_port = node_info->metasync_port;

    // Update local cluster map
    clustMap->addNode(&node, my_node_type, node_type);

    // TODO(Andrew): Hack to figure out my own node uuid
    // since the OM doesn't reply to my registration yet.
    if ((node.node_ip_address ==
         (uint)netSession::ipString2Addr(*plf_mgr->plf_get_my_ip())) &&
        (node.port == plf_mgr->plf_get_my_data_port()) &&
        (myUuid.uuid_get_val() == 0)) {
        myUuid.uuid_set_val(node_info->node_uuid);
        LOGDEBUG << "Setting my UUID to " << myUuid.uuid_get_val();
    }

    omc_lock.write_unlock();

    LOGNOTIFY << "OMClient received node event for node "
              << node.node_id
              << ", type - " << node_info->node_type
              << " with ip address " << node_ip
              << " and state - " << node_state;

    if (this->node_evt_hdlr) {
        this->node_evt_hdlr(node.node_id,
                            node_ip,
                            node_state,
                            node_info->data_port,
                            node_info->node_type);
    }
    return (0);
}

Error OMgrClient::updateDlt(bool dlt_type, std::string& dlt_data) {
    Error err(ERR_OK);
    LOGNOTIFY << "OMClient received new DLT version  " << dlt_type;

    omc_lock.write_lock();
    err = dltMgr->addSerializedDLT(dlt_data, dlt_type);
    if (err.ok()) {
        dltMgr->dump();
    } else {
        LOGERROR << "Failed to update DLT! check dlt_data was set";
    }
    omc_lock.write_unlock();

    return err;
}

#if 0
/**
 * DMT close event notifies that nodes in the cluster received
 * the commited (new) DMT
 */
void OMgrClient::recvDMTClose(fds_uint64_t dmt_version,
                              const std::string& session_uuid)
{
    Error err(ERR_OK);
    LOGNORMAL << "OMClient received DMT close event for DMT version "
              << dmt_version;

    // TODO(xxx) notify volume sync that we can stop forwarding
    // updates to other DM
    err = this->catalog_evt_hdlr(fds_catalog_dmt_close,
                                 FDSP_PushMetaPtr(),
                                 session_uuid);
    if (!err.ok()) {
        LOGERROR << "DMT Close,  volume meta may not be synced properly";
        // ignore not ready errors
        if (err == ERR_CATSYNC_NOT_PROGRESS)
            err = ERR_OK;
        fpi::FDSP_DmtCloseTypePtr
                dmtCloseAck(new FDSP_DmtCloseType);
        sendDMTCloseAckToOM(dmtCloseAck, session_uuid);
    }
}

int OMgrClient::sendDMTCloseAckToOM(FDSP_DmtCloseTypePtr& dmt_close,
                                    const std::string& session_uuid)
{
    if (fNoNetwork) return 0;
    Error err(ERR_OK);
    LOGDEBUG << "Sending dmt close ack to OM";

    // sending response right away for now...
    boost::shared_ptr<fpi::FDSP_ControlPathRespClient> resp_client_prx =
            omrpc_handler_session_->getRespClient(session_uuid);

    try {
        FDSP_MsgHdrTypePtr msg_hdr(new FDSP_MsgHdrType);
        initOMMsgHdr(msg_hdr);
        msg_hdr->err_code = err.GetErrno();
        if (!err.ok()) {
            msg_hdr->result = FDSP_ERR_FAILED;
        }
        FDSP_DMT_Resp_TypePtr dmt_resp(new FDSP_DMT_Resp_Type);
        dmt_resp->DMT_version = getDMTVersion();
        resp_client_prx->NotifyDMTCloseResp(msg_hdr, dmt_resp);
        LOGNOTIFY << "OMClient sent response for DMT close to OM";
    } catch(...) {
        LOGERROR << "OMClient failed to send DMT close response to OM";
        return -1;
    }

    return (0);
}
#endif

Error OMgrClient::recvDMTPushMeta(FDSP_PushMetaPtr& push_meta,
                                  const std::string& session_uuid) {
    fds_verify(this->catalog_evt_hdlr != NULL);
    return this->catalog_evt_hdlr(fds_catalog_push_meta, push_meta, session_uuid);
}

Error OMgrClient::updateDmt(bool dmt_type, std::string& dmt_data) {
    Error err(ERR_OK);
    LOGNOTIFY << "OMClient received new DMT version  " << dmt_type;

    omc_lock.write_lock();
    err = dmtMgr->addSerializedDMT(dmt_data, DMT_COMMITTED);
    if (!err.ok()) {
        LOGERROR << "Failed to update DMT! check dmt_data was set";
    }
    omc_lock.write_unlock();

    return err;
}
#if 0
Error OMgrClient::recvDMTUpdate(FDSP_DMT_TypePtr& dmt_info,
                                const std::string& session_uuid) {
    Error err(ERR_OK);
    LOGNOTIFY << "OMClient received new DMT version " << dmt_info->dmt_version;

    // before we implement DM sync, any DMT we receive from OM is committed DMT
    // dmtMgr is thread-safe, uses it's internal lock
    err = dmtMgr->addSerializedDMT(dmt_info->dmt_data, DMT_COMMITTED);
    if (!err.ok()) {
        LOGERROR << "Failed to add DMT to DMTManager " << err;
        return err;
    }

    // notify DataMgr
    if (getNodeType() == fpi::FDSP_DATA_MGR) {
        fds_verify(this->catalog_evt_hdlr != NULL);
        err = this->catalog_evt_hdlr(fds_catalog_dmt_commit,
                                     FDSP_PushMetaPtr(),
                                     session_uuid);
    }

    return err;
}
#endif

int
OMgrClient::getNodeInfo(fds_uint64_t node_id,
                        unsigned int *node_ip_addr,
                        fds_uint32_t *node_port,
                        int *node_state) {
    return clustMap->getNodeInfo(node_id,
                                 node_ip_addr,
                                 node_port,
                                 node_state);
}

fds_uint32_t OMgrClient::getLatestDlt(std::string& dlt_data) {
    // TODO(x): Set to a macro'd invalid version
    omc_lock.read_lock();
    Error err = const_cast <DLT* > (dltMgr->getDLT())->getSerialized(dlt_data);
    fds_verify(err.ok());
    omc_lock.read_unlock();

    return 0;
}

const DLT* OMgrClient::getCurrentDLT() {
    omc_lock.read_lock();
    const DLT *dlt = dltMgr->getDLT();
    omc_lock.read_unlock();

    return dlt;
}

void OMgrClient::setCurrentDLTClosed() {
    omc_lock.write_lock();
    dltMgr->setCurrentDltClosed();
    omc_lock.write_unlock();
}

const DLT*
OMgrClient::getPreviousDLT() {
    omc_lock.read_lock();
    fds_uint64_t version = (dltMgr->getDLT()->getVersion()) - 1;
    const DLT *dlt = dltMgr->getDLT(version);
    omc_lock.read_unlock();

    return dlt;
}

fds_uint64_t
OMgrClient::getDltVersion() {
    // TODO(x): Set to a macro'd invalid version
    fds_uint64_t version = DLT_VER_INVALID;
    omc_lock.read_lock();
    version = dltMgr->getDLT()->getVersion();
    omc_lock.read_unlock();

    return version;
}

NodeUuid
OMgrClient::getUuid() const {
    return myUuid;
}

FDSP_MgrIdType
OMgrClient::getNodeType() const {
    return my_node_type;
}

const TokenList&
OMgrClient::getTokensForNode(const NodeUuid &uuid) const {
    return dltMgr->getDLT()->getTokens(uuid);
}

fds_uint32_t
OMgrClient::getNodeMigPort(NodeUuid uuid) {
    return clustMap->getNodeMigPort(uuid);
}

fds_uint32_t
OMgrClient::getNodeMetaSyncPort(NodeUuid uuid) {
    return clustMap->getNodeMetaSyncPort(uuid);
}


NodeMigReqClientPtr
OMgrClient::getMigClient(fds_uint64_t node_id) {
    return clustMap->getMigClient(node_id);
}

DltTokenGroupPtr OMgrClient::getDLTNodesForDoidKey(const ObjectID &objId) {
    return dltMgr->getDLT()->getNodes(objId);
}

DmtColumnPtr OMgrClient::getDMTNodesForVolume(fds_volid_t vol_id) {
    return dmtMgr->getCommittedNodeGroup(vol_id);  // thread-safe, do not hold lock
}

DmtColumnPtr OMgrClient::getDMTNodesForVolume(fds_volid_t vol_id,
                                              fds_uint64_t dmt_version) {
    return dmtMgr->getVersionNodeGroup(vol_id, dmt_version);  // thread-safe, do not hold lock
}

fds_uint64_t OMgrClient::getDMTVersion() const {
    return dmtMgr->getCommittedVersion();
}

fds_bool_t OMgrClient::hasCommittedDMT() const {
    return dmtMgr->hasCommittedDMT();
}

}  //  namespace fds
