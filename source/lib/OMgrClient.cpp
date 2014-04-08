#include "OMgrClient.h"
#include <fds_assert.h>
#include <boost/thread.hpp>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TSimpleServer.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TTransportUtils.h>
#include <thrift/transport/TBufferTransports.h>
#include <dlt.h>

#include <thread>

using namespace std;
using namespace fds;


OMgrClientRPCI::OMgrClientRPCI(OMgrClient *omc) {
    this->om_client = omc;
}

void OMgrClientRPCI::NotifyAddVol(FDS_ProtocolInterface::FDSP_MsgHdrTypePtr& msg_hdr,
                                  FDS_ProtocolInterface::FDSP_NotifyVolTypePtr& vol_msg) {
    assert(vol_msg->type == FDS_ProtocolInterface::FDSP_NOTIFY_ADD_VOL);
    fds_vol_notify_t type = fds_notify_vol_add;
    fds::VolumeDesc *vdb = new fds::VolumeDesc(vol_msg->vol_desc);
    om_client->recvNotifyVol(vdb, type, vol_msg->check_only, msg_hdr->result, msg_hdr->session_uuid);
}

void OMgrClientRPCI::NotifyModVol(FDS_ProtocolInterface::FDSP_MsgHdrTypePtr& msg_hdr,
                                  FDS_ProtocolInterface::FDSP_NotifyVolTypePtr& vol_msg) {
    assert(vol_msg->type == FDS_ProtocolInterface::FDSP_NOTIFY_MOD_VOL);
    fds_vol_notify_t type = fds_notify_vol_mod;
    fds::VolumeDesc *vdb = new fds::VolumeDesc(vol_msg->vol_desc);
    om_client->recvNotifyVol(vdb, type, vol_msg->check_only, msg_hdr->result, msg_hdr->session_uuid);
}

void OMgrClientRPCI::NotifyRmVol(FDS_ProtocolInterface::FDSP_MsgHdrTypePtr& msg_hdr,
                                 FDS_ProtocolInterface::FDSP_NotifyVolTypePtr& vol_msg) {
    assert(vol_msg->type == FDS_ProtocolInterface::FDSP_NOTIFY_RM_VOL);
    fds_vol_notify_t type = fds_notify_vol_rm;
    fds::VolumeDesc *vdb = new fds::VolumeDesc(vol_msg->vol_desc);
    om_client->recvNotifyVol(vdb, type, vol_msg->check_only, msg_hdr->result, msg_hdr->session_uuid);
}
      
void OMgrClientRPCI::AttachVol(FDS_ProtocolInterface::FDSP_MsgHdrTypePtr& msg_hdr,
			       FDS_ProtocolInterface::FDSP_AttachVolTypePtr& vol_msg) {
    fds::VolumeDesc *vdb = new fds::VolumeDesc(vol_msg->vol_desc);
    om_client->recvVolAttachState(vdb, fds_notify_vol_attatch, msg_hdr->result, msg_hdr->session_uuid);
}

void OMgrClientRPCI::DetachVol(FDS_ProtocolInterface::FDSP_MsgHdrTypePtr& msg_hdr,
			       FDS_ProtocolInterface::FDSP_AttachVolTypePtr& vol_msg) {
    fds::VolumeDesc *vdb = new fds::VolumeDesc(vol_msg->vol_desc);
    om_client->recvVolAttachState(vdb, fds_notify_vol_detach, msg_hdr->result, msg_hdr->session_uuid);
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
                             (unsigned int) node_info->ip_lo_addr, node_info->node_state, node_info); 
}

void OMgrClientRPCI::NotifyDLTUpdate(FDSP_MsgHdrTypePtr& msg_hdr,
				     FDSP_DLT_Data_TypePtr& dlt_info) {
    om_client->recvDLTUpdate(dlt_info, msg_hdr->session_uuid);
}

void OMgrClientRPCI::NotifyDLTClose(FDSP_MsgHdrTypePtr& fdsp_msg,
                                    FDSP_DltCloseTypePtr& dlt_close) {
    om_client->recvDLTClose(dlt_close, fdsp_msg->session_uuid);
}

void OMgrClientRPCI::NotifyStartMigration(FDSP_MsgHdrTypePtr& msg_hdr,
                                          FDSP_DLT_Data_TypePtr& dlt_info) {
    // Only SM needs to process these migrations
    if (om_client->getNodeType() == FDS_ProtocolInterface::FDSP_STOR_MGR) {
        om_client->recvDLTStartMigration(dlt_info);
        om_client->recvMigrationEvent(dlt_info->dlt_type); 
    } else {
        om_client->sendMigrationStatusToOM(ERR_OK);
    }
}

void OMgrClientRPCI::NotifyScavengerStart(FDSP_MsgHdrTypePtr& msg_hdr,
                                          FDSP_ScavengerStartTypePtr& gc_info) {
    if (gc_info->all) {
        LOGNOTIFY << "Received Scavenger Start mesage for all tokens";
    } else {
        LOGNOTIFY << "Received Scavenger Start mesage for token "
                  << gc_info->token_id;
    }
}

void OMgrClientRPCI::NotifyDMTUpdate(FDSP_MsgHdrTypePtr& msg_hdr,
				     FDSP_DMT_TypePtr& dmt_info) {
  om_client->recvDMTUpdate(dmt_info->DMT_version, dmt_info->DMT);
}

void OMgrClientRPCI::SetThrottleLevel(FDSP_MsgHdrTypePtr& msg_hdr, 
                                      FDSP_ThrottleMsgTypePtr& throttle_msg) {
  om_client->recvSetThrottleLevel((const float) throttle_msg->throttle_level);
}

void
OMgrClientRPCI::TierPolicy(FDSP_TierPolicyPtr &tier)
{
  FDS_PLOG_SEV(om_client->omc_log, fds::fds_log::notification)
    << "OMClient received tier policy for vol "
    << tier->tier_vol_uuid;

    fds_verify(om_client->omc_srv_pol != nullptr);
    om_client->omc_srv_pol->serv_recvTierPolicyReq(tier);
}

void
OMgrClientRPCI::TierPolicyAudit(FDSP_TierPolicyAuditPtr &audit)
{
  FDS_PLOG_SEV(om_client->omc_log, fds::fds_log::notification)
    << "OMClient received tier audit policy for vol "
    << audit->tier_vol_uuid;

    fds_verify(om_client->omc_srv_pol != nullptr);
    om_client->omc_srv_pol->serv_recvTierPolicyAuditReq(audit);
}

void OMgrClientRPCI::NotifyBucketStats(FDSP_MsgHdrTypePtr& msg_hdr,
				       FDSP_BucketStatsRespTypePtr& buck_stats_msg)
{
  fds_verify(msg_hdr->dst_id == FDS_ProtocolInterface::FDSP_STOR_HVISOR);
  om_client->recvBucketStats(msg_hdr, buck_stats_msg);
}

OMgrClient::OMgrClient(FDSP_MgrIdType node_type,
                       const std::string& _omIpStr,
                       fds_uint32_t _omPort,
                       const std::string& node_name,
                       fds_log *parent_log,
                       boost::shared_ptr<netSessionTbl> nst,
                       Platform *plf) {
  my_node_type = node_type;
  omIpStr      = _omIpStr;
  omConfigPort = _omPort;
  my_node_name = node_name;
  node_evt_hdlr = NULL;
  vol_evt_hdlr = NULL;
  throttle_cmd_hdlr = NULL;
  bucket_stats_cmd_hdlr = NULL;
  dltclose_evt_hdlr = NULL;
  if (parent_log) {
    omc_log = parent_log;
  } else {
    omc_log = new fds_log("omc", "logs");
  }
  nst_ = nst;

  clustMap = new LocalClusterMap();
  plf_mgr  = plf;
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

int OMgrClient::registerEventHandlerForDltCloseEvents(dltclose_event_handler_t dltclose_event_hdlr) {
  this->dltclose_evt_hdlr = dltclose_event_hdlr;
  return 0;
}

int OMgrClient::registerEventHandlerForNodeEvents(node_event_handler_t node_event_hdlr) {
  this->node_evt_hdlr = node_event_hdlr;
  return 0;
}
 

int OMgrClient::registerEventHandlerForVolEvents(volume_event_handler_t vol_event_hdlr) {
  this->vol_evt_hdlr = vol_event_hdlr;
  return 0;
}

int OMgrClient::registerThrottleCmdHandler(throttle_cmd_handler_t throttle_cmd_hdlr) {
  this->throttle_cmd_hdlr = throttle_cmd_hdlr;
  return 0;
}

int OMgrClient::registerBucketStatsCmdHandler(bucket_stats_cmd_handler_t cmd_hdlr) {
  bucket_stats_cmd_hdlr = cmd_hdlr;
  return 0;
}

/**
 * @brief Starts OM RPC handling server.  This function is to be run on a
 * separate thread.  OMgrClient destructor does a join() on this thread
 */
void OMgrClient::start_omrpc_handler()
{
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

  std::string myIp = netSession::getLocalIp();
  int myIpInt = netSession::ipString2Addr(myIp);
  omrpc_handler_.reset(new OMgrClientRPCI(this));
  // TODO: Ideally createServerSession should take a shared pointer
  // for omrpc_handler_.  Make sure that happens.  Otherwise you
  // end up with a pointer leak.
  omrpc_handler_session_ = 
      nst_->createServerSession<netControlPathServerSession>(myIpInt,
                                plf_mgr->plf_get_my_ctrl_port(),
                                my_node_name,
                                FDSP_ORCH_MGR,
                                omrpc_handler_);

  omrpc_handler_thread_.reset(new boost::thread(&OMgrClient::start_omrpc_handler, this));

  FDS_PLOG_SEV(omc_log, fds::fds_log::notification) << "OMClient accepting control requests at port " << plf_mgr->plf_get_my_ctrl_port();

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

// Use this to register the local node with OM as a client. Should be called after calling starting subscription endpoint and control path endpoint.
int OMgrClient::registerNodeWithOM(Platform *plat)
{
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
            FDS_PLOG_SEV(omc_log, fds::fds_log::critical)
                << "OMClient unable to register node with OrchMgr. "
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

        // TODO(Andrew): Move to SM specific
        reg_node_msg->migration_port = plat->plf_get_my_migration_port();

        // TODO(Vy): simple service uuid from node uuid.
        reg_node_msg->node_uuid.uuid    = plat->plf_get_my_uuid()->uuid_get_val();
        reg_node_msg->service_uuid.uuid = plat->plf_get_my_svc_uuid()->uuid_get_val();
        myUuid.uuid_set_val(plat->plf_get_my_svc_uuid()->uuid_get_val());

        FDS_PLOG_SEV(omc_log, fds::fds_log::notification)
            << "OMClient registering local node "
            << fds::ipv4_addr_to_str(reg_node_msg->ip_lo_addr)
            << " control port:" << reg_node_msg->control_port
            << " data port:" << reg_node_msg->data_port
            << " with Orchaestration Manager at "
            << omIpStr << ":" << omConfigPort;

        om_client_prx->RegisterNode(msg_hdr, reg_node_msg);
        FDS_PLOG_SEV(omc_log, fds::fds_log::debug)
            << "OMClient completed node registration with OM";
    }
    catch(...) {
        FDS_PLOG_SEV(omc_log, fds::fds_log::critical)
            << "OMClient unable to register node with OrchMgr. "
               "Please check if OrchMgr is up and restart.";
    }
    return (0);
}

int OMgrClient::pushPerfstatsToOM(const std::string& start_ts,
				  int stat_slot_len,
				  const FDS_ProtocolInterface::FDSP_VolPerfHistListType& hist_list)
{
  try {

    FDSP_MsgHdrTypePtr msg_hdr(new FDSP_MsgHdrType);
    initOMMsgHdr(msg_hdr);
    FDSP_PerfstatsTypePtr perf_stats_msg(new FDSP_PerfstatsType);
    perf_stats_msg->node_type = my_node_type;
    perf_stats_msg->start_timestamp = start_ts;
    perf_stats_msg->slot_len_sec = stat_slot_len;
    perf_stats_msg->vol_hist_list = hist_list;  

    FDS_PLOG_SEV(omc_log, fds::fds_log::normal) << "OMClient pushing perfstats to OM at "
                                                << omIpStr << ":" << omConfigPort
						<< " start ts " << start_ts;

    om_client_prx->NotifyPerfstats(msg_hdr, perf_stats_msg);

  } catch (...) {
    FDS_PLOG_SEV(omc_log, fds::fds_log::error) << "OMClient unable to push perf stats to OM. Check if OM is up and restart.";
  }

  return 0;
}

int OMgrClient::testBucket(const std::string& bucket_name,
			   const FDS_ProtocolInterface::FDSP_VolumeInfoTypePtr& vol_info,
			   fds_bool_t attach_vol_reqd,
			   const std::string& accessKeyId,
			   const std::string& secretAccessKey)
{
  try {

    FDSP_MsgHdrTypePtr msg_hdr(new FDSP_MsgHdrType);
    initOMMsgHdr(msg_hdr);
    FDSP_TestBucketPtr test_buck_msg(new FDSP_TestBucket);
    test_buck_msg->bucket_name = bucket_name;
    test_buck_msg->vol_info = *vol_info;
    test_buck_msg->attach_vol_reqd = attach_vol_reqd;
    test_buck_msg->accessKeyId = accessKeyId;
    test_buck_msg->secretAccessKey;

    FDS_PLOG_SEV(omc_log, fds::fds_log::notification) << "OMClient sending test bucket request to OM at "
                                                      << omIpStr << ":" << omConfigPort;

    om_client_prx->TestBucket(msg_hdr, test_buck_msg);
  }
  catch (...) {
    FDS_PLOG_SEV(omc_log, fds::fds_log::error) << "OMClient unable to send testBucket request to OM. Check if OM is up and restart.";
    return -1;
  }

  return 0;
}

int OMgrClient::pushGetBucketStatsToOM(fds_uint32_t req_cookie)
{
  try {
    FDSP_MsgHdrTypePtr msg_hdr(new FDSP_MsgHdrType);
    initOMMsgHdr(msg_hdr);
    msg_hdr->req_cookie = req_cookie;

    FDSP_GetDomainStatsTypePtr get_stats_msg(new FDSP_GetDomainStatsType());
    get_stats_msg->domain_id = 1; /* this is ignored in OM */

    FDS_PLOG_SEV(omc_log, fds::fds_log::notification) << "OMClient sending get bucket stats request to OM at "
                                                      << omIpStr << ":" << omConfigPort;


    om_client_prx->GetDomainStats(msg_hdr, get_stats_msg);
  }
  catch (...) {
    FDS_PLOG_SEV(omc_log, fds::fds_log::error) << "OMClient unable to send GetBucketStats request to OM. Check if OM is up and restart.";
    return -1;
  }

  return 0;
}

int OMgrClient::pushCreateBucketToOM(const FDS_ProtocolInterface::FDSP_VolumeInfoTypePtr& volInfo)
{
  try {
         FDSP_MsgHdrTypePtr msg_hdr(new FDSP_MsgHdrType);
         initOMMsgHdr(msg_hdr);
  
         FDSP_CreateVolTypePtr volData(new FDSP_CreateVolType());

         volData->vol_name = volInfo->vol_name;
         volData->vol_info.vol_name = volInfo->vol_name;
         volData->vol_info.tennantId = volInfo->tennantId;
         volData->vol_info.localDomainId = volInfo->localDomainId;
         volData->vol_info.globDomainId = volInfo->globDomainId;

         volData->vol_info.capacity = volInfo->capacity;
         volData->vol_info.maxQuota = volInfo->maxQuota;
         volData->vol_info.volType = volInfo->volType;

         volData->vol_info.defReplicaCnt = volInfo->defReplicaCnt;
         volData->vol_info.defWriteQuorum = volInfo->defWriteQuorum;
         volData->vol_info.defReadQuorum = volInfo->defReadQuorum;
         volData->vol_info.defConsisProtocol = volInfo->defConsisProtocol;

         volData->vol_info.volPolicyId = 50; //  default policy 
         volData->vol_info.archivePolicyId = volInfo->archivePolicyId;
         volData->vol_info.placementPolicy = volInfo->placementPolicy;
         volData->vol_info.appWorkload = volInfo->appWorkload;

    	 om_client_prx->CreateBucket(msg_hdr, volData);
  } catch (...) {
    FDS_PLOG_SEV(omc_log, fds::fds_log::error) << "OMClient unable to push  the create bucket request to OM. Check if OM is up and restart.";
    return -1;
  }

  return 0;
}

int OMgrClient::pushModifyBucketToOM(const std::string& bucket_name,
				     const FDS_ProtocolInterface::FDSP_VolumeDescTypePtr& vol_desc)
{
  try {

    FDSP_MsgHdrTypePtr msg_hdr(new FDSP_MsgHdrType);
    initOMMsgHdr(msg_hdr);
    FDSP_ModifyVolTypePtr mod_vol_msg(new FDSP_ModifyVolType());
    mod_vol_msg->vol_name = bucket_name;
    mod_vol_msg->vol_uuid = 0; /* make sure that uuid is not checked, because we don't know it here */
    mod_vol_msg->vol_desc = *vol_desc;

    FDS_PLOG_SEV(omc_log, fds::fds_log::notification) << "OMClient sending modify bucket request to OM at "
                                                      << omIpStr << ":" << omConfigPort;

    om_client_prx->ModifyBucket(msg_hdr, mod_vol_msg);
  }
  catch (...) {
    FDS_PLOG_SEV(omc_log, fds::fds_log::error) << "OMClient unable to send ModifyBucket request to OM. Check if OM is up and restart.";
    return -1;
  }

  return 0;
}

int OMgrClient::pushDeleteBucketToOM(const FDS_ProtocolInterface::FDSP_DeleteVolTypePtr& volInfo)
{
  try {
        FDSP_MsgHdrTypePtr msg_hdr(new FDSP_MsgHdrType);
        initOMMsgHdr(msg_hdr);

 	FDSP_DeleteVolTypePtr volData(new FDSP_DeleteVolType());
   	volData->vol_name  = volInfo->vol_name;
  	volData->domain_id = volInfo->domain_id;
    	om_client_prx->DeleteBucket(msg_hdr, volData);

  } catch (...) {
    FDS_PLOG_SEV(omc_log, fds::fds_log::error) << "OMClient unable to push perf stats to OM. Check if OM is up and restart.";
  }

  return 0;
}


int OMgrClient::recvMigrationEvent(bool dlt_type) 
{

  FDS_PLOG_SEV(omc_log, fds::fds_log::notification) << "OMClient received Migration event for node " << dlt_type; 

  if (this->migrate_evt_hdlr) {
    this->migrate_evt_hdlr(dlt_type);
  }
  return (0);

}

int OMgrClient::sendMigrationStatusToOM(const Error& err) {
    try {
        FDSP_MsgHdrTypePtr msg_hdr(new FDSP_MsgHdrType);
        initOMMsgHdr(msg_hdr);
        FDSP_MigrationStatusTypePtr migr_status_msg(new FDSP_MigrationStatusType());
        migr_status_msg->DLT_version = getDltVersion();
        migr_status_msg->context = 0;
        om_client_prx->NotifyMigrationDone(msg_hdr, migr_status_msg);

        FDS_PLOG_SEV(omc_log, fds::fds_log::notification)
                << "OMClient sending migration done event to OM for DLT version "
                << migr_status_msg->DLT_version;
    }
    catch (...) {
        FDS_PLOG_SEV(omc_log, fds_log::error)
                << "OMClient unable to send migration status to OM."
                << " Check if OM is up and restart";
        return -1;
    }

    return 0;
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

  FDS_PLOG_SEV(omc_log, fds::fds_log::notification) << "OMClient received node event for node "
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

int OMgrClient::recvNotifyVol(VolumeDesc *vdb,
                              fds_vol_notify_t vol_action,
                              fds_bool_t check_only,
			      FDSP_ResultType result,
                              const std::string& session_uuid) {
    Error err(ERR_OK);
    fds_volid_t vol_id = vdb->volUUID;
    FDS_PLOG_SEV(omc_log, fds::fds_log::notification) << "OMClient received volume event for volume 0x"
                                                      << std::hex << vol_id <<std::dec << " action - " << vol_action;
    
    if (this->vol_evt_hdlr) {
        err = this->vol_evt_hdlr(vol_id, vdb, vol_action, check_only, result);
    }

    // send response back to OM
    boost::shared_ptr<FDS_ProtocolInterface::FDSP_ControlPathRespClient> resp_client_prx =
            omrpc_handler_session_->getRespClient(session_uuid);

    try {
        FDSP_MsgHdrTypePtr msg_hdr(new FDSP_MsgHdrType);
        initOMMsgHdr(msg_hdr);
        FDSP_NotifyVolTypePtr vol_resp(new FDSP_NotifyVolType());
        msg_hdr->err_code = err.GetErrno();
        if (!err.ok()) {
            msg_hdr->result = FDSP_ERR_FAILED;
        }
        vol_resp->vol_name = vdb->getName();
        vol_resp->check_only = check_only;
        vol_resp->vol_desc.vol_name = vdb->getName();
        vol_resp->vol_desc.volUUID = vol_id;
        switch (vol_action) {
            case fds_notify_vol_add:
                vol_resp->type = FDSP_NOTIFY_ADD_VOL;
                resp_client_prx->NotifyAddVolResp(msg_hdr, vol_resp);
                break;
            case fds_notify_vol_rm:
                vol_resp->type = FDSP_NOTIFY_RM_VOL;
                resp_client_prx->NotifyRmVolResp(msg_hdr, vol_resp);
                break;
            case fds_notify_vol_mod:
                vol_resp->type = FDSP_NOTIFY_MOD_VOL;
                resp_client_prx->NotifyModVolResp(msg_hdr, vol_resp);
                break;
            default:
                fds_panic("Unknown (corrupt?) volume event");
        }
        FDS_PLOG_SEV(omc_log, fds_log::notification)
                << "OMClient sent response to OM for Volume Notify of type " << vol_action
		<< "; volume " << vdb->getName();
    } catch (...) {
        FDS_PLOG_SEV(omc_log, fds_log::error) << "OMClient failed to send response to OM";
        return -1;
    }
    return 0;
}

int OMgrClient::recvVolAttachState(VolumeDesc *vdb,
                                   fds_vol_notify_t vol_action,
				   FDSP_ResultType result,
                                   const std::string& session_uuid) {
    assert((vol_action == fds_notify_vol_attatch) || (vol_action == fds_notify_vol_detach));
    Error err(ERR_OK);
    fds_volid_t vol_id = vdb->volUUID;

    FDS_PLOG_SEV(omc_log, fds::fds_log::notification)
            << "OMClient received volume attach/detach request for volume 0x"
            << std::hex << vol_id << std::dec << " action - " << vol_action;
    
    if (this->vol_evt_hdlr) {
        err = this->vol_evt_hdlr(vol_id, vdb, vol_action, false, result);
    }
    // send response back to OM
    boost::shared_ptr<FDS_ProtocolInterface::FDSP_ControlPathRespClient> resp_client_prx =
            omrpc_handler_session_->getRespClient(session_uuid);

    try {
        FDSP_MsgHdrTypePtr msg_hdr(new FDSP_MsgHdrType);
        initOMMsgHdr(msg_hdr);
        FDSP_AttachVolTypePtr vol_resp(new FDSP_AttachVolType());
        msg_hdr->err_code = err.GetErrno();
        if (!err.ok()) {
            msg_hdr->result = FDSP_ERR_FAILED;
        }
        vol_resp->vol_name = vdb->getName();
        vol_resp->vol_desc.vol_name = vdb->getName();
        vol_resp->vol_desc.volUUID = vol_id;
        switch (vol_action) {
            case fds_notify_vol_attatch:
                resp_client_prx->AttachVolResp(msg_hdr, vol_resp);
                break;
            case fds_notify_vol_detach:
                resp_client_prx->DetachVolResp(msg_hdr, vol_resp);
                break;
            default:
                fds_panic("Unknown (corrupt?) volume event");
        }
        FDS_PLOG_SEV(omc_log, fds_log::notification)
                << "OMClient sent response to OM for Volume Attach/detach (type " << vol_action
		<< "); volume " << vdb->getName();
    } catch (...) {
        FDS_PLOG_SEV(omc_log, fds_log::error) << "OMClient failed to send response to OM";
        return -1;
    }

    return 0;
}

int OMgrClient::updateDlt(bool dlt_type, std::string& dlt_data) {

  FDS_PLOG_SEV(omc_log, fds::fds_log::notification) << "OMClient received new DLT version  " << dlt_type;

  omc_lock.write_lock();
  dltMgr.addSerializedDLT(dlt_data,dlt_type);
  dltMgr.dump();
  omc_lock.write_unlock();

  return (0);
}

int OMgrClient::recvDLTUpdate(FDSP_DLT_Data_TypePtr& dlt_info,
                              const std::string& session_uuid) {
    FDS_PLOG_SEV(omc_log, fds::fds_log::notification)
            << "OMClient received new DLT commit version  "
            << dlt_info->dlt_type;

    omc_lock.write_lock();
    dltMgr.addSerializedDLT(dlt_info->dlt_data, dlt_info->dlt_type);
    dltMgr.dump();
    omc_lock.write_unlock();

    // send ack back to OM
    boost::shared_ptr<FDS_ProtocolInterface::FDSP_ControlPathRespClient> resp_client_prx =
            omrpc_handler_session_->getRespClient(session_uuid);

    try {
        FDSP_MsgHdrTypePtr msg_hdr(new FDSP_MsgHdrType);
        initOMMsgHdr(msg_hdr);
        FDSP_DLT_Resp_TypePtr dlt_resp(new FDSP_DLT_Resp_Type);
        dlt_resp->DLT_version = getDltVersion();
        resp_client_prx->NotifyDLTUpdateResp(msg_hdr, dlt_resp);
        FDS_PLOG_SEV(omc_log, fds_log::notification)
                << "OMClient sent response for DLT update to OM";
    } catch (...) {
        FDS_PLOG_SEV(omc_log, fds_log::error) << "OMClient failed to send response to OM";
        return -1;
    }

    return (0);
}

/**
 * DLT close event notifies that nodes in the cluster received
 * the commited (new) DLT
 */
int OMgrClient::recvDLTClose(FDSP_DltCloseTypePtr& dlt_close,
                             const std::string& session_uuid)
{
    LOGNORMAL << "OMClient received DLT close event for DLT version "
            << dlt_close->DLT_version;

    if (this->dltclose_evt_hdlr) {
        this->dltclose_evt_hdlr(dlt_close, session_uuid);
    }
    return (0);
}

/**
 * Acking back dlt close notification to OM
 */
int OMgrClient::sendDLTCloseAckToOM(FDSP_DltCloseTypePtr& dlt_close,
        const std::string& session_uuid)
{
    LOGDEBUG << "Sending dlt close ack to OM";
    int err = 0;

    // send ack back to OM
    boost::shared_ptr<FDS_ProtocolInterface::FDSP_ControlPathRespClient> resp_client_prx =
            omrpc_handler_session_->getRespClient(session_uuid);

    try {
        FDSP_MsgHdrTypePtr msg_hdr(new FDSP_MsgHdrType);
        initOMMsgHdr(msg_hdr);
        FDSP_DLT_Resp_TypePtr dlt_resp(new FDSP_DLT_Resp_Type);
        dlt_resp->DLT_version = dlt_close->DLT_version;
        resp_client_prx->NotifyDLTCloseResp(msg_hdr, dlt_resp);
        FDS_PLOG_SEV(omc_log, fds_log::notification)
                << "OMClient sent response for DLT close to OM";
    } catch (...) {
        LOGERROR << "OMClient failed to send response to OM";
        err = -1;
    }

    return err;
}

int OMgrClient::recvDLTStartMigration(FDSP_DLT_Data_TypePtr& dlt_info) {
  FDS_PLOG_SEV(omc_log, fds::fds_log::notification)
          << "OMClient received new Migration DLT version  " << dlt_info->dlt_type;

  omc_lock.write_lock();
  dltMgr.addSerializedDLT(dlt_info->dlt_data, dlt_info->dlt_type);
  dltMgr.dump();  
  omc_lock.write_unlock();

  return (0);
}

int OMgrClient::recvDMTUpdate(int dmt_vrsn, const Node_Table_Type& dmt_table) {

  FDS_PLOG_SEV(omc_log, fds::fds_log::notification) << "OMClient received new DMT version  " << dmt_vrsn;

  omc_lock.write_lock();

  this->dmt_version = dmt_vrsn;
  this->dmt = dmt_table;

  omc_lock.write_unlock();

  int row = 0;
  for (auto it = dmt_table.cbegin(); it != dmt_table.cend(); it++) {
    for (auto jt = (*it).cbegin(); jt != (*it).cend(); jt++) {
        FDS_PLOG_SEV(omc_log, fds::fds_log::notification)
                << "[Col " << row << std::hex << "] " << *jt << std::dec;
    }
    row++;
  }
  return (0);
}

int OMgrClient::recvSetThrottleLevel(const float throttle_level) {

  FDS_PLOG_SEV(omc_log, fds::fds_log::notification) << "OMClient received new throttle level  " << throttle_level;

  this->current_throttle_level = throttle_level;

  if (this->throttle_cmd_hdlr) {
    this->throttle_cmd_hdlr(throttle_level);
  }
  return (0);

}

int OMgrClient::recvBucketStats(const FDSP_MsgHdrTypePtr& msg_hdr, 
				const FDSP_BucketStatsRespTypePtr& stats_msg)
{
  FDS_PLOG_SEV(omc_log, fds::fds_log::notification) << "OMClient received buckets' stats with timestamp  " 
						    << stats_msg->timestamp;

  if (bucket_stats_cmd_hdlr) {
    bucket_stats_cmd_hdlr(msg_hdr, stats_msg);
  }

  return 0;
}

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

fds_uint32_t
OMgrClient::getLatestDlt(std::string& dlt_data) {
    // TODO: Set to a macro'd invalid version
    omc_lock.read_lock();
    const_cast <DLT* > (dltMgr.getDLT())->getSerialized(dlt_data);
    omc_lock.read_unlock();

    return 0;
}

const DLT* OMgrClient::getCurrentDLT() {
    omc_lock.read_lock();
    const DLT *dlt = dltMgr.getDLT();
    omc_lock.read_unlock();

    return dlt;
}

const DLT*
OMgrClient::getPreviousDLT() {
    omc_lock.read_lock();
    fds_uint64_t version = (dltMgr.getDLT()->getVersion()) - 1;
    const DLT *dlt = dltMgr.getDLT(version);
    omc_lock.read_unlock();

    return dlt;
}

fds_uint64_t
OMgrClient::getDltVersion() {
    // TODO: Set to a macro'd invalid version
    fds_uint64_t version = DLT_VER_INVALID;
    omc_lock.read_lock();
    version = dltMgr.getDLT()->getVersion();
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
    return dltMgr.getDLT()->getTokens(uuid);
}

fds_uint32_t
OMgrClient::getNodeMigPort(NodeUuid uuid) {
    return clustMap->getNodeMigPort(uuid);
}

NodeMigReqClientPtr
OMgrClient::getMigClient(fds_uint64_t node_id) {
    return clustMap->getMigClient(node_id);
}

DltTokenGroupPtr OMgrClient::getDLTNodesForDoidKey(const ObjectID &objId) {
 return dltMgr.getDLT()->getNodes(objId);

}

int OMgrClient::getDMTNodesForVolume(fds_volid_t vol_id, fds_uint64_t *node_ids, int *n_nodes) {

  omc_lock.read_lock();

  int total_shards = this->dmt.size();
  int lookup_key = ((fds_uint64_t)vol_id) % total_shards;
  int total_nodes = this->dmt[lookup_key].size();
  *n_nodes = (total_nodes < *n_nodes)? total_nodes:*n_nodes;
  int i;
  
  for (i = 0; i < *n_nodes; i++) {
    node_ids[i] = this->dmt[lookup_key][i];
  } 

  omc_lock.read_unlock();

  return 0;
}
