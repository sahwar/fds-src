/*
 * Copyright 2014 by Formation Data Systems, Inc.
 */
#include <stdlib.h>
#include <stdio.h>
#include <vector>
#include <string>
#include <fds_error.h>
#include <OmVolume.h>
#include <OmClusterMap.h>
#include <OmResources.h>
#include <OmConstants.h>
#include <OmAdminCtrl.h>
#include <OmDeploy.h>
#include <omutils.h>
#include <orchMgr.h>
#include <OmVolumePlacement.h>
#include <orch-mgr/om-service.h>
#include <OmDmtDeploy.h>
#include <fdsp/am_api_types.h>
#include <fdsp/dm_api_types.h>
#include <fdsp/sm_api_types.h>
#include <fdsp/pm_service_types.h>
#include <fdsp/PlatNetSvc.h>
#include <net/SvcRequestPool.h>
#include <net/SvcMgr.h>

namespace fds {

// ---------------------------------------------------------------------------------
// OM SM NodeAgent
// ---------------------------------------------------------------------------------
OM_NodeAgent::~OM_NodeAgent() {}
OM_NodeAgent::OM_NodeAgent(const NodeUuid &uuid, fpi::FDSP_MgrIdType type)
    : NodeAgent(uuid)
{
    node_svc_type = type;
}

int
OM_NodeAgent::node_calc_stor_weight()
{
    TRACEFUNC;
    return 0;
}

// om_send_myinfo
// --------------
// Send a node event taken from the new node agent to a peer agent.
//
void
OM_NodeAgent::om_send_myinfo(NodeAgent::pointer peer)
{
    TRACEFUNC;
    // TODO(Andrew): This function is deprecated and should not
    // be called. It is not in any main code path but has a long
    // deprecated call chain that can be removed when OM is re-factored.
    LOGWARN << "You're calling a dead function";
}

// ----------------
// TODO(Vy): define messages in inheritance tree.
//
void
OM_NodeAgent::om_send_node_throttle_lvl(fpi::FDSP_ThrottleMsgTypePtr throttle)
{
    TRACEFUNC;
    auto req =  gSvcRequestPool->newEPSvcRequest(rs_get_uuid().toSvcUuid());
    req->setPayload(FDSP_MSG_TYPEID(fpi::CtrlNotifyThrottle), throttle);
    req->invoke();
    LOGNORMAL << " send throttle level " << throttle->throttle_level
        << " to am node " << rs_get_uuid();
}

// om_send_vol_cmd
// ---------------
// TODO(Vy): have 2 separate APIs, 1 to format the packet and 1 to send it.
Error
OM_NodeAgent::om_send_vol_cmd(VolumeInfo::pointer vol,
                              fpi::FDSPMsgTypeId      cmd_type,
                              fpi::FDSP_NotifyVolFlag vol_flag)
{
    TRACEFUNC;
    return om_send_vol_cmd(vol, NULL, cmd_type, vol_flag);
}

void OM_NodeAgent::om_send_vol_cmd_resp(VolumeInfo::pointer     vol,
                      fpi::FDSPMsgTypeId      cmd_type,
                      EPSvcRequest* req,
                      const Error& error,
                      boost::shared_ptr<std::string> payload) {
    TRACEFUNC;
    if (vol == NULL || vol->rs_get_uuid() == 0) {
        
        /*
         * TODO Tinius 02/11/2015
         * 
         * FS-936 -- AM and OM continuously log errors with
         * "invalid bucket SYSTEM_VOLUME_0"
         * 
         * Not sure if this is expected behavior? Once re-written we will
         * handle this correctly. But for now remove the logging noise
         * 
         * LOGWARN << "response received for invalid volume . ignored.";
         */

        return;
    }
    
    LOGDEBUG << "received vol cmd response " << vol->vol_get_name();
    
    OM_NodeContainer * local = OM_NodeDomainMod::om_loc_domain_ctrl();
    VolumeContainer::pointer volumes = local->om_vol_mgr();
    volumes->om_vol_cmd_resp(vol, cmd_type, error, rs_get_uuid());
}

Error
OM_NodeAgent::om_send_vol_cmd(VolumeInfo::pointer     vol,
                              std::string            *vname,
                              fpi::FDSPMsgTypeId      cmd_type,
                              fpi::FDSP_NotifyVolFlag vol_flag)
{
    TRACEFUNC;

    if (node_state() == fpi::FDS_Node_Down) {
        LOGNORMAL << "Will not send vol command to service we know is down"
                  << get_node_name();
        return ERR_NOT_FOUND;
    }

    const char       *log;
    const VolumeDesc *desc;

    desc = NULL;
    if (vol != NULL) {
        desc = vol->vol_get_properties();
    }
    auto req =  gSvcRequestPool->newEPSvcRequest(rs_get_uuid().toSvcUuid());
    switch (cmd_type) {
    case fpi::CtrlNotifyVolAddTypeId: {
        fpi::CtrlNotifyVolAddPtr pkt(new fpi::CtrlNotifyVolAdd());

        log = "Send notify add volume ";
        if (vol != NULL) {
            vol->vol_fmt_desc_pkt(&pkt->vol_desc);
        } else {
            /* TODO(Vy): why we need to send dummy data? */
            pkt->vol_desc.vol_name  = *vname;
            pkt->vol_desc.volUUID   = 0;
            pkt->vol_desc.tennantId = 0;
            pkt->vol_desc.localDomainId = 0;
            pkt->vol_desc.capacity = 1000;
            pkt->vol_desc.volType  = fpi::FDSP_VOL_S3_TYPE;
        }
        pkt->vol_flag = vol_flag;
        req->setPayload(FDSP_MSG_TYPEID(fpi::CtrlNotifyVolAdd), pkt);
        break;
    }
    case fpi::CtrlNotifySnapVolTypeId: {
        fpi::CtrlNotifySnapVolPtr pkt(new fpi::CtrlNotifySnapVol());

        fds_assert(vol != NULL);
        log = "Send snap volume ";
        vol->vol_fmt_desc_pkt(&pkt->vol_desc);
        pkt->vol_flag = vol_flag;
        req->setPayload(FDSP_MSG_TYPEID(fpi::CtrlNotifySnapVol), pkt);
        break;
    }
    case fpi::CtrlNotifyVolRemoveTypeId: {
        fpi::CtrlNotifyVolRemovePtr pkt(new fpi::CtrlNotifyVolRemove());

        fds_assert(vol != NULL);
        log = "Send remove volume ";
        vol->vol_fmt_desc_pkt(&pkt->vol_desc);
        pkt->vol_flag = vol_flag;
        req->setPayload(FDSP_MSG_TYPEID(fpi::CtrlNotifyVolRemove), pkt);
        break;
    }
    case fpi::CtrlNotifyVolModTypeId: {
        fpi::CtrlNotifyVolModPtr pkt(new fpi::CtrlNotifyVolMod());

        fds_assert(vol != NULL);
        log = "Send modify volume ";
        vol->vol_fmt_desc_pkt(&pkt->vol_desc);
        pkt->vol_flag = vol_flag;
        req->setPayload(FDSP_MSG_TYPEID(fpi::CtrlNotifyVolMod), pkt);
        break;
    }
    default:
        fds_panic("Unknown vol cmd type");
    }
    if (!vol) {
        vol = VolumeInfo::pointer(new VolumeInfo(0));  // create dummy to avoid issues
    }
    EPSvcRequestRespCb cb = std::bind( &OM_NodeAgent::om_send_vol_cmd_resp, 
                                       this, 
                                       vol, 
                                       cmd_type,
                                       std::placeholders::_1, 
                                       std::placeholders::_2,
                                       std::placeholders::_3 );
    req->onResponseCb(cb);
    req->invoke();
    if (desc != NULL) {
        LOGNORMAL << log << desc->volUUID << " " << desc->name
                  << " to node " << get_node_name() << std::hex
                  << ", uuid " << get_uuid().uuid_get_val() << std::dec;
    } else {
        
        /*
         * TODO Tinius 02/11/2015
         * 
         * FS-936 -- AM and OM continously log errors with "invalid bucket SYSTEM_VOLUME_0"
            
         * Not sure if this is expected behavior? Once re-written we will
         * handle this correctly. But for now remove the logging noise
         * 
         * LOGNORMAL << log << ", no vol to node " << get_node_name();
         */
    }
    return Error(ERR_OK);
}


Error
OM_NodeAgent::om_send_sm_abort_migration(fds_uint64_t dltVersion) {
    TRACEFUNC;
    Error err(ERR_OK);
    auto om_req =  gSvcRequestPool->newEPSvcRequest(rs_get_uuid().toSvcUuid());
    fpi::CtrlNotifySMAbortMigrationPtr msg(new fpi::CtrlNotifySMAbortMigration());
    msg->DLT_version = dltVersion;

    // send request
    om_req->setPayload(FDSP_MSG_TYPEID(fpi::CtrlNotifySMAbortMigration), msg);
    om_req->onResponseCb(std::bind(&OM_NodeAgent::om_send_abort_sm_migration_resp, this, msg,
                                   std::placeholders::_1, std::placeholders::_2,
                                   std::placeholders::_3));
    om_req->setTimeoutMs(2000);  // huge, but need to handle timeouts in resp
    om_req->invoke();

    LOGNORMAL << "OM: Send abort migration (DLT version " << dltVersion
              << ") to " << get_node_name() << " uuid 0x"
              << std::hex << (get_uuid()).uuid_get_val() << std::dec;

    return err;
}

void
OM_NodeAgent::om_send_abort_sm_migration_resp(fpi::CtrlNotifySMAbortMigrationPtr msg,
                                           EPSvcRequest* req,
                                           const Error& error,
                                           boost::shared_ptr<std::string> payload)
{
    TRACEFUNC;
    LOGNOTIFY << "OM received response for SM Abort Migration from node "
              << std::hex << req->getPeerEpId().svc_uuid << std::dec
              << " with version " << msg->DLT_version
              << " " << error;

    // notify DLT state machine
    NodeUuid node_uuid(req->getPeerEpId().svc_uuid);
    OM_Module *om = OM_Module::om_singleton();
    OM_DLTMod *dltMod = om->om_dlt_mod();
    dltMod->dlt_deploy_event(DltRecoverAckEvt(true, node_uuid, error));
}

Error
OM_NodeAgent::om_send_dm_abort_migration(fds_uint64_t dmtVersion) {
    TRACEFUNC;
    Error err(ERR_OK);
    auto om_req =  gSvcRequestPool->newEPSvcRequest(rs_get_uuid().toSvcUuid());
    fpi::CtrlNotifyDMAbortMigrationPtr msg(new fpi::CtrlNotifyDMAbortMigration());
    msg->DMT_version = dmtVersion;

    // send request
    om_req->setPayload(FDSP_MSG_TYPEID(fpi::CtrlNotifyDMAbortMigration), msg);
    om_req->onResponseCb(std::bind(&OM_NodeAgent::om_send_abort_dm_migration_resp, this, msg,
            std::placeholders::_1, std::placeholders::_2,
            std::placeholders::_3));
    om_req->setTimeoutMs(2000);  // huge, but need to handle timeouts in resp
    om_req->invoke();

    LOGNORMAL << "OM: Send abort DM migration (DMT version " << dmtVersion
                << ") to " << get_node_name() << " uuid 0x"
                << std::hex << (get_uuid()).uuid_get_val() << std::dec;

    return err;
}

void
OM_NodeAgent::om_send_abort_dm_migration_resp(fpi::CtrlNotifyDMAbortMigrationPtr msg,
        EPSvcRequest* req,
        const Error& error,
        boost::shared_ptr<std::string> payload)
{
    TRACEFUNC;
    LOGNOTIFY << "OM received response for DM Abort Migration from node "
                << std::hex << req->getPeerEpId().svc_uuid << std::dec
                << " with version " << msg->DMT_version
                << " " << error;

    // notify DLT state machine
    NodeUuid node_uuid(req->getPeerEpId().svc_uuid);
    OM_Module *om = OM_Module::om_singleton();
    OM_DMTMod *dmtMod = om->om_dmt_mod();
    dmtMod->dmt_deploy_event(DmtRecoveryEvt(true, node_uuid, error));
}


Error
OM_NodeAgent::om_send_dlt(const DLT *curDlt) {
    TRACEFUNC;
    Error err(ERR_OK);
    if (curDlt == NULL) {
        LOGNORMAL << "No current DLT to send to " << get_node_name();
        return Error(ERR_NOT_FOUND);
    }
    if (node_state() == fpi::FDS_Node_Down) {
        LOGNORMAL << "Will not send dlt to node we know is down... "
                  << get_node_name();
        return ERR_NOT_FOUND;
    }

    auto om_req =  gSvcRequestPool->newEPSvcRequest(rs_get_uuid().toSvcUuid());
    fpi::CtrlNotifyDLTUpdatePtr msg(new fpi::CtrlNotifyDLTUpdate());
    auto d_msg = &msg->dlt_data;

    d_msg->dlt_type        = true;
    err = const_cast<DLT*>(curDlt)->getSerialized(d_msg->dlt_data);
    msg->dlt_version = curDlt->getVersion();
    if (!err.ok()) {
        LOGERROR << "Failed to fill in dlt_data, not sending DLT";
        return err;
    }
    om_req->setPayload(FDSP_MSG_TYPEID(fpi::CtrlNotifyDLTUpdate), msg);
    om_req->onResponseCb(std::bind(&OM_NodeAgent::om_send_dlt_resp, this, msg,
                                   std::placeholders::_1, std::placeholders::_2,
                                   std::placeholders::_3));
    om_req->setTimeoutMs(300000);  // huge, but need to handle timeouts in resp
    om_req->invoke();

    curDlt->dump();
    LOGNORMAL << "OM: Send dlt info (version " << curDlt->getVersion()
              << ") to " << get_node_name() << " uuid 0x"
              << std::hex << (get_uuid()).uuid_get_val() << std::dec;

    return err;
}

Error
OM_NodeAgent::om_send_dlt_close(fds_uint64_t cur_dlt_version) {
    TRACEFUNC;
    Error err(ERR_OK);
    if (node_state() == fpi::FDS_Node_Down) {
        LOGNORMAL << "Will not send dlt close to service we know is down... "
                  << get_node_name();
        return ERR_NOT_FOUND;
    }

    auto om_req = gSvcRequestPool->newEPSvcRequest(rs_get_uuid().toSvcUuid());
    fpi::CtrlNotifyDLTClosePtr msg(new fpi::CtrlNotifyDLTClose());
    msg->dlt_close.DLT_version = cur_dlt_version;

    om_req->setPayload(FDSP_MSG_TYPEID(fpi::CtrlNotifyDLTClose), msg);
    om_req->onResponseCb(std::bind(&OM_NodeAgent::om_send_dlt_close_resp, this, msg,
            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    om_req->setTimeoutMs(5000);
    om_req->invoke();

    LOGNORMAL << "OM: send dlt close (version " << cur_dlt_version
                << ") to " << get_node_name() << " uuid 0x"
                << std::hex << (get_uuid()).uuid_get_val() << std::dec;

    return err;
}

void
OM_NodeAgent::om_send_dlt_close_resp(fpi::CtrlNotifyDLTClosePtr msg,
        EPSvcRequest* req,
        const Error& error,
        boost::shared_ptr<std::string> payload)
{
    TRACEFUNC;
    LOGDEBUG << "OM received response for NotifyDltClose from node "
             << std::hex << req->getPeerEpId().svc_uuid << std::dec
             << " with version " << msg->dlt_close.DLT_version
             << " " << error;

    // notify DLT state machine
    OM_NodeDomainMod* domain = OM_NodeDomainMod::om_local_domain();
    NodeUuid node_uuid(req->getPeerEpId().svc_uuid);
    domain->om_recv_dlt_close_resp(node_uuid, msg->dlt_close.DLT_version, error);
}

void
OM_NodeAgent::om_send_dlt_resp(fpi::CtrlNotifyDLTUpdatePtr msg, EPSvcRequest* req,
                               const Error& error,
                               boost::shared_ptr<std::string> payload)
{
    LOGNOTIFY << "OM received response for NotifyDltUpdate from node "
              << std::hex << req->getPeerEpId().svc_uuid << std::dec
              << " node type " << rs_get_uuid().uuid_get_type()
              << " with DLT version " << msg->dlt_version << " " << error;

    // notify DLT state machine
    OM_NodeDomainMod* domain = OM_NodeDomainMod::om_local_domain();
    NodeUuid node_uuid(rs_get_uuid());
    FdspNodeType node_type = rs_get_uuid().uuid_get_type();
    domain->om_recv_dlt_commit_resp(node_type, node_uuid, msg->dlt_version, error);
}

Error
OM_NodeAgent::om_send_dmt(const DMTPtr& curDmt) {
    Error err(ERR_OK);

    fds_verify(curDmt->getVersion() != DMT_VER_INVALID);
    if (node_state() == fpi::FDS_Node_Down) {
        LOGNORMAL << "Will not send DMT to node we know is down... "
                  << get_node_name();
        return ERR_NOT_FOUND;
    }

    auto om_req =  gSvcRequestPool->newEPSvcRequest(rs_get_uuid().toSvcUuid());
    fpi::CtrlNotifyDMTUpdatePtr msg(new fpi::CtrlNotifyDMTUpdate());
    auto dmt_msg = &msg->dmt_data;
    err = curDmt->getSerialized(dmt_msg->dmt_data);
    msg->dmt_version = curDmt->getVersion();
    if (!err.ok()) {
        LOGERROR << "Failed to fill in dmt_data, not sending DMT";
        return err;
    }
    om_req->setPayload(FDSP_MSG_TYPEID(fpi::CtrlNotifyDMTUpdate), msg);
    om_req->onResponseCb(std::bind(&OM_NodeAgent::om_send_dmt_resp, this, msg,
                                   std::placeholders::_1, std::placeholders::_2,
                                   std::placeholders::_3));
    om_req->setTimeoutMs(20000);
    om_req->invoke();
    LOGNORMAL << "OM: Send dmt info (version " << curDmt->getVersion()
              << ") to " << get_node_name() << " uuid 0x"
              << std::hex << (get_uuid()).uuid_get_val() << std::dec;
    return err;
}

void
OM_NodeAgent::om_send_dmt_resp(fpi::CtrlNotifyDMTUpdatePtr msg, EPSvcRequest* req,
                               const Error& error,
                               boost::shared_ptr<std::string> payload)
{
    LOGNOTIFY << "OM received response for NotifyDmtUpdate from node "
              << std::hex << req->getPeerEpId().svc_uuid << std::dec
              << " with version " << msg->dmt_version << " " << error;

    Error respError(error);
    // ok to receive ERR_CATSYNC_NOT_PROGRESS error
    if (respError == ERR_CATSYNC_NOT_PROGRESS) {
        respError = ERR_OK;
    }

    // notify DLT state machine
    OM_NodeDomainMod* domain = OM_NodeDomainMod::om_local_domain();
    NodeUuid node_uuid(rs_get_uuid());
    FdspNodeType node_type = rs_get_uuid().uuid_get_type();
    domain->om_recv_dmt_commit_resp(node_type, node_uuid, msg->dmt_version, respError);
}

//
// Currently sends scavenger start message
// TODO(xxx) extend to other scavenger commands (pass cmd type)
//
Error
OM_NodeAgent::om_send_scavenger_cmd(fpi::FDSP_ScavengerCmd cmd) {
    fpi::CtrlNotifyScavengerPtr msg(new fpi::CtrlNotifyScavenger());
    fpi::FDSP_ScavengerType *gc_msg = &msg->scavenger;
    gc_msg->cmd = cmd;
    auto req =  gSvcRequestPool->newEPSvcRequest(rs_get_uuid().toSvcUuid());
    req->setPayload(FDSP_MSG_TYPEID(fpi::CtrlNotifyScavenger), msg);
    req->invoke();
    LOGNORMAL << "OM: send scavenger command: " << cmd;
    return Error(ERR_OK);
}

Error
OM_NodeAgent::om_send_qosinfo(fds_uint64_t total_rate) {
    TRACEFUNC;
    fpi::CtrlNotifyQoSControlPtr qos_msg(new fpi::CtrlNotifyQoSControl());
    fpi::FDSP_QoSControlMsgType *qosctrl = &qos_msg->qosctrl;
    qosctrl->total_rate = total_rate;
    auto req =  gSvcRequestPool->newEPSvcRequest(rs_get_uuid().toSvcUuid());
    req->setPayload(FDSP_MSG_TYPEID(fpi::CtrlNotifyQoSControl), qos_msg);
    req->invoke();
    LOGNORMAL << "OM: send total rate to AM: " << total_rate;
    return Error(ERR_OK);
}

Error
OM_NodeAgent::om_send_stream_reg_cmd(fds_int32_t regId,
                                     fds_bool_t bAll) {
    Error err(ERR_OK);

    // get UUID of any AM
    OM_NodeContainer* local = OM_NodeDomainMod::om_loc_domain_ctrl();
    OM_AmContainer::pointer amNodes = local->om_am_nodes();
    NodeAgent::pointer agent = amNodes->agent_info(0);
    if (!agent) {
        LOGERROR << "There are no AMs, cannot broadcast stream registration "
                 << " to DMs, will do when at least one AM joins";
        return ERR_NOT_READY;
    }
    NodeUuid am_uuid = agent->get_uuid();

    kvstore::ConfigDB* configDB = gl_orch_mgr->getConfigDB();
    if (bAll == true) {
        // send all known registrations
        std::vector<apis::StreamingRegistrationMsg> reg_vec;
        fds_bool_t bret = configDB->getStreamRegistrations(reg_vec);
        if (!bret) {
            LOGERROR << "Failed to get stream registrations from configDB";
            return ERR_NOT_READY;
        }
        LOGDEBUG << "Got " << reg_vec.size() << " stream registrations "
                 << " from configDB";
        for (fds_uint32_t i = 0; i < reg_vec.size(); ++i) {
            om_send_one_stream_reg_cmd(reg_vec[i], am_uuid);
        }
    } else {
        apis::StreamingRegistrationMsg reg_msg;
        fds_bool_t bret = configDB->getStreamRegistration(regId, reg_msg);
        if (!bret) {
            LOGERROR << "Failed to get stream registration "
                     << regId << " from configDB";
            return ERR_NOT_READY;
        }
        om_send_one_stream_reg_cmd(reg_msg, am_uuid);
    }

    return err;
}

void
OM_NodeAgent::om_send_one_stream_reg_cmd(const apis::StreamingRegistrationMsg& reg,
                                         const NodeUuid& stream_dest_uuid) {
    fpi::StatStreamRegistrationMsgPtr reg_msg(new fpi::StatStreamRegistrationMsg());
    reg_msg->id = reg.id;
    reg_msg->url = reg.url;
    reg_msg->method = reg.http_method;
    (reg_msg->dest).svc_uuid = stream_dest_uuid.uuid_get_val();
    reg_msg->sample_freq_seconds = reg.sample_freq_seconds;
    reg_msg->duration_seconds = reg.duration_seconds;

    (reg_msg->volumes).reserve((reg.volume_names).size());
    for (uint i = 0; i < (reg.volume_names).size(); i++) {
        std::string volname = (reg.volume_names).at(i);
        OM_NodeContainer * local = OM_NodeDomainMod::om_loc_domain_ctrl();
        VolumeContainer::pointer volumes = local->om_vol_mgr();
        VolumeInfo::pointer vol = volumes->get_volume(volname);
        if (vol == NULL) {
            LOGDEBUG << "Volume " << volname << " not found.  Skipping stream registration";
        } else {
          (reg_msg->volumes).push_back(vol->rs_get_uuid().uuid_get_val());
        }
    }

    LOGDEBUG << "Will send StatStreamRegistration with id " << reg.id
             << " to DM " << std::hex << rs_uuid.uuid_get_val() << ", AM uuid is "
             << std::hex << stream_dest_uuid.uuid_get_val() << std::dec;

    auto asyncStreamRegReq = gSvcRequestPool->newEPSvcRequest(rs_uuid.toSvcUuid());
    asyncStreamRegReq->setPayload(FDSP_MSG_TYPEID(fpi::StatStreamRegistrationMsg), reg_msg);
    // HACK
    asyncStreamRegReq->setTimeoutMs(0);
    asyncStreamRegReq->invoke();
}

Error
OM_NodeAgent::om_send_pullmeta(fpi::CtrlNotifyDMStartMigrationMsgPtr& meta_msg)
{
    Error err(ERR_OK);

    auto om_req = gSvcRequestPool->newEPSvcRequest(rs_get_uuid().toSvcUuid());
    om_req->setPayload(FDSP_MSG_TYPEID(fpi::CtrlNotifyDMStartMigrationMsg), meta_msg);
    om_req->onResponseCb(std::bind(&OM_NodeAgent::om_pullmeta_resp, this,
            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    om_req->setTimeoutMs(60000);
    om_req->invoke();

    LOGNORMAL << "OM: send CtrlNotifyDMStartMigrationMsg to " << get_node_name() << " uuid 0x"
              << std::hex << (get_uuid()).uuid_get_val() << std::dec;
    return err;
}

void
OM_NodeAgent::om_pullmeta_resp(EPSvcRequest* req,
                               const Error& error,
                               boost::shared_ptr<std::string> payload)
{
    LOGDEBUG << "OM received response for CtrlNotifyDMStartMigrationMsg from node "
             << std::hex << req->getPeerEpId().svc_uuid << std::dec
             << " " << error;


    OM_NodeDomainMod* domain = OM_NodeDomainMod::om_local_domain();
    NodeUuid node_uuid(req->getPeerEpId().svc_uuid);
    /**
     * For now override error code to OK. Removde this hard code once
     * FS-2214 is fixed.
     */
    domain->om_recv_pull_meta_resp(node_uuid, ERR_OK);
    // domain->om_recv_pull_meta_resp(node_uuid, error);
}

Error
OM_NodeAgent::om_send_dmt_close(fds_uint64_t cur_dmt_version) {
    Error err(ERR_OK);
    if (node_state() == fpi::FDS_Node_Down) {
        LOGNORMAL << "Will not send DMT close to service we know is down... "
                  << get_node_name();
        return ERR_NOT_FOUND;
    }

    auto om_req = gSvcRequestPool->newEPSvcRequest(rs_get_uuid().toSvcUuid());
    fpi::CtrlNotifyDMTClosePtr msg(new fpi::CtrlNotifyDMTClose());
    msg->dmt_close.DMT_version = cur_dmt_version;

    om_req->setPayload(FDSP_MSG_TYPEID(fpi::CtrlNotifyDMTClose), msg);
    om_req->onResponseCb(std::bind(&OM_NodeAgent::om_send_dmt_close_resp, this, msg,
            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    om_req->setTimeoutMs(20000);
    om_req->invoke();

    LOGNORMAL << "OM: send dmt close (version " << cur_dmt_version
                << ") to " << get_node_name() << " uuid 0x"
                << std::hex << (get_uuid()).uuid_get_val() << std::dec;

    return err;
}

void
OM_NodeAgent::om_send_dmt_close_resp(fpi::CtrlNotifyDMTClosePtr msg,
        EPSvcRequest* req,
        const Error& error,
        boost::shared_ptr<std::string> payload)
{
    LOGDEBUG << "OM received response for NotifyDmtClose from node "
             << std::hex << req->getPeerEpId().svc_uuid << std::dec
             << " with version " << msg->dmt_close.DMT_version
             << " " << error;

    // notify DMT state machine
    OM_NodeDomainMod* domain = OM_NodeDomainMod::om_local_domain();
    NodeUuid node_uuid(req->getPeerEpId().svc_uuid);
    domain->om_recv_dmt_close_resp(node_uuid, msg->dmt_close.DMT_version, error);
}

Error
OM_NodeAgent::om_send_shutdown() {
    Error err(ERR_OK);

    auto om_req = gSvcRequestPool->newEPSvcRequest(rs_get_uuid().toSvcUuid());
    fpi::PrepareForShutdownMsgPtr msg(new fpi::PrepareForShutdownMsg());

    om_req->setPayload(FDSP_MSG_TYPEID(fpi::PrepareForShutdownMsg), msg);
    om_req->onResponseCb(std::bind(&OM_NodeAgent::om_send_shutdown_resp, this,
                                   std::placeholders::_1, std::placeholders::_2,
                                   std::placeholders::_3));
    om_req->setTimeoutMs(20000);
    om_req->invoke();

    LOGNOTIFY << "OM: send shutdown message to " << get_node_name() << " uuid 0x"
              << std::hex << (get_uuid()).uuid_get_val() << std::dec;
    return err;
}

void
OM_NodeAgent::om_send_shutdown_resp(EPSvcRequest* req,
                                    const Error& error,
                                    boost::shared_ptr<std::string> payload)
{
    LOGDEBUG << "OM received response for Prepare For Shutdown msg from node "
             << std::hex << req->getPeerEpId().svc_uuid << std::dec
             << " " << error;

    // Notify domain state machine
    OM_NodeDomainMod* domain = OM_NodeDomainMod::om_local_domain();
    domain->local_domain_event(ShutAckEvt(node_svc_type, error));
}

void
OM_NodeAgent::init_msg_hdr(fpi::FDSP_MsgHdrTypePtr msgHdr) const
{
    TRACEFUNC;
    NodeInventory::init_msg_hdr(msgHdr);

    msgHdr->src_id       = FDS_ProtocolInterface::FDSP_ORCH_MGR;
    msgHdr->dst_id       = ndMyServId;
    msgHdr->session_uuid = ndSessionId;
}

// ---------------------------------------------------------------------------------
// OM PM NodeAgent
// ---------------------------------------------------------------------------------
OM_PmAgent::~OM_PmAgent() {}
OM_PmAgent::OM_PmAgent(const NodeUuid &uuid)
        : OM_NodeAgent(uuid, fpi::FDSP_PLATFORM), dbNodeInfoLock("Config DB Node Info lock") {}

void
OM_PmAgent::init_msg_hdr(fpi::FDSP_MsgHdrTypePtr msgHdr) const
{
    TRACEFUNC;
    NodeInventory::init_msg_hdr(msgHdr);
    msgHdr->src_id = FDS_ProtocolInterface::FDSP_ORCH_MGR;
    msgHdr->dst_id = FDS_ProtocolInterface::FDSP_PLATFORM;
    msgHdr->session_uuid = ndSessionId;
}


// service_exists
// --------------
//
fds_bool_t
OM_PmAgent::service_exists(FDS_ProtocolInterface::FDSP_MgrIdType svc_type) const
{
    TRACEFUNC;
    switch (svc_type) {
        case FDS_ProtocolInterface::FDSP_STOR_MGR:
            if (activeSmAgent != NULL)
                return true;
            break;
        case FDS_ProtocolInterface::FDSP_DATA_MGR:
            if (activeDmAgent != NULL)
                return true;
            break;
        case FDS_ProtocolInterface::FDSP_ACCESS_MGR:
            if (activeAmAgent != NULL)
                return true;
            break;
        default:
            break;
    };
    return false;
}

fds_bool_t OM_PmAgent::hasRegistered(const FdspNodeRegPtr  msg) {
    OM_NodeAgent::pointer nodeAgent;
    switch (msg->node_type) {
        case FDS_ProtocolInterface::FDSP_STOR_MGR:
            nodeAgent = activeSmAgent;
            break;
        case FDS_ProtocolInterface::FDSP_DATA_MGR:
            nodeAgent = activeDmAgent;
            break;
        case FDS_ProtocolInterface::FDSP_ACCESS_MGR:
            nodeAgent  = activeAmAgent;
            break;
        default:
            break;
    };

    if (nodeAgent == NULL) return false;
    if (msg->node_name != nodeAgent->get_node_name()) return false;
    if (nodeAgent->get_uuid() != msg->service_uuid.uuid) return false;

    return true;

}

// register_service
// ----------------
//
Error
OM_PmAgent::handle_register_service(FDS_ProtocolInterface::FDSP_MgrIdType svc_type,
                                    NodeAgent::pointer svc_agent)
{
    TRACEFUNC;
    // we cannot register more than one service of the same type
    // with the same node (platform)
    if (service_exists(svc_type)) {
        LOGWARN << "Cannot register more than one service of the same type "
                << svc_type;
        return Error(ERR_DUPLICATE);
    }

    // update configDB with which services this platform has
    kvstore::ConfigDB* configDB = gl_orch_mgr->getConfigDB();
    NodeServices services;

    // Here we are reading the node info from DB, modifying a service info
    // within the node info, and storing it. Have to do it under the lock
    // since multiple threads can be modifying same node info (e.g. adding
    // different services to it).
    fds_mutex::scoped_lock l(dbNodeInfoLock);
    if (configDB && !configDB->getNodeServices(get_uuid(), services)) {
        // just in case reset services to 0
        services.reset();
    }

    switch (svc_type) {
        case FDS_ProtocolInterface::FDSP_STOR_MGR:
            activeSmAgent = OM_SmAgent::agt_cast_ptr(svc_agent);
            services.sm = svc_agent->get_uuid();
            LOGDEBUG << " sm: " <<  std::hex << services.sm.uuid_get_val() << std::dec;
            break;
        case FDS_ProtocolInterface::FDSP_DATA_MGR:
            activeDmAgent = OM_DmAgent::agt_cast_ptr(svc_agent);
            services.dm = svc_agent->get_uuid();
            LOGDEBUG << " dm: " <<  std::hex << services.dm.uuid_get_val() << std::dec;
            break;
        case FDS_ProtocolInterface::FDSP_ACCESS_MGR:
            activeAmAgent = OM_AmAgent::agt_cast_ptr(svc_agent);
            services.am = svc_agent->get_uuid();
            LOGDEBUG << " am: " <<  std::hex << services.am.uuid_get_val() << std::dec;
            break;
        default:
            fds_verify(false);
    };

    // actually update config DB
    if (configDB) {
        configDB->setNodeServices(get_uuid(), services);
    }

    return Error(ERR_OK);
}

// unregister_service
// ------------------
//
NodeUuid
OM_PmAgent::handle_unregister_service(FDS_ProtocolInterface::FDSP_MgrIdType svc_type)
{
    TRACEFUNC;
    // update configDB -- remove the service
    kvstore::ConfigDB* configDB = gl_orch_mgr->getConfigDB();
    NodeServices services;

    // Here we are reading the node info from DB, modifying a service info
    // within the node info, and storing it. Have to do it under the lock
    // since multiple threads can be modifying same node info (e.g. removing
    // different services to it).
    fds_mutex::scoped_lock l(dbNodeInfoLock);
    fds_bool_t found_services = configDB ?
            configDB->getNodeServices(get_uuid(), services) : false;

    NodeUuid svc_uuid;
    switch (svc_type) {
        case FDS_ProtocolInterface::FDSP_STOR_MGR:
            activeSmAgent = NULL;
            svc_uuid = services.sm;
            services.sm.uuid_set_val(0);
            break;
        case FDS_ProtocolInterface::FDSP_DATA_MGR:
            activeDmAgent = NULL;
            svc_uuid = services.dm;
            services.dm.uuid_set_val(0);
            break;
        case FDS_ProtocolInterface::FDSP_ACCESS_MGR:
            activeAmAgent = NULL;
            svc_uuid = services.am;
            services.am.uuid_set_val(0);
            break;
        default:
            fds_verify(false);
    };

    if (found_services) {
        configDB->setNodeServices(get_uuid(), services);
    } else {
        LOGWARN << "Node info " << std::hex << get_uuid().uuid_get_val() << std::dec
                << " not found to persist removal of service from this node";
    }

    return svc_uuid;
}

void
OM_PmAgent::handle_unregister_service(const NodeUuid& uuid)
{
    TRACEFUNC;
    if (activeSmAgent->get_uuid() == uuid) {
        handle_unregister_service(FDS_ProtocolInterface::FDSP_STOR_MGR);
    } else if (activeDmAgent->get_uuid() == uuid) {
        handle_unregister_service(FDS_ProtocolInterface::FDSP_DATA_MGR);
    } else if (activeAmAgent->get_uuid() == uuid) {
        handle_unregister_service(FDS_ProtocolInterface::FDSP_ACCESS_MGR);
    }
}

// unregister_service
// ------------------
//
void
OM_PmAgent::handle_deactivate_service(const FDS_ProtocolInterface::FDSP_MgrIdType svc_type)
{
    LOGDEBUG << "Will deactivate service " << svc_type;
    
    kvstore::ConfigDB* configDB = gl_orch_mgr->getConfigDB();
    
    // we are just deactivating the service during this run, so no
    // need to update configDB

    // Here we are reading the node info from DB, modifying a service info
    // within the node info, and storing it. Have to do it under the lock
    // since multiple threads can be modifying same node info (e.g. removing
    // different services to it).
    fds_mutex::scoped_lock l(dbNodeInfoLock);
    switch (svc_type) {
        case FDS_ProtocolInterface::FDSP_STOR_MGR:
            if ( activeSmAgent ) 
            {
                LOGDEBUG << "Will deactivate SM service " 
                         << std::hex
                         << ( activeSmAgent->get_uuid() ).uuid_get_val() 
                         << std::dec;
                
                change_service_state( configDB, 
                                      ( activeSmAgent->get_uuid() ).uuid_get_val(),
                                      fpi::SVC_STATUS_INACTIVE );
                
                activeSmAgent = nullptr;
            } 
            else 
            {
                LOGDEBUG << "SM service already not active on platform " 
                         << std::hex << get_uuid().uuid_get_val() << std::dec;
            }
            break;
        case FDS_ProtocolInterface::FDSP_DATA_MGR:
            if ( activeDmAgent ) 
            {
                LOGDEBUG << "Will deactivate DM service " 
                         << std::hex
                         << ( activeDmAgent->get_uuid() ).uuid_get_val() 
                         << std::dec;
                
                change_service_state( configDB, 
                                      ( activeDmAgent->get_uuid() ).uuid_get_val(),
                                      fpi::SVC_STATUS_INACTIVE );
                
                activeDmAgent = nullptr;
            }
            else
            {
                LOGDEBUG << "DM service already not active on platform " 
                         << std::hex << get_uuid().uuid_get_val() << std::dec;
            }
            break;
        case FDS_ProtocolInterface::FDSP_ACCESS_MGR:
            if ( activeAmAgent ) 
            {
                LOGDEBUG << "Will deactivate AM service " 
                         << std::hex
                         << ( activeAmAgent->get_uuid() ).uuid_get_val() 
                         << std::dec;
                
                change_service_state( configDB, 
                                      ( activeAmAgent->get_uuid() ).uuid_get_val(),
                                      fpi::SVC_STATUS_INACTIVE );
                  
                activeAmAgent = nullptr;
            }
            else
            {
                LOGDEBUG << "AM service already not active on platform " 
                         << std::hex << get_uuid().uuid_get_val() << std::dec;
            }
            break;
        default:
            LOGWARN << "Unknown service type " << svc_type << ". Did we add a new"
                    << " service type? If so, update this method";
    };
}

// send_activate_services
// -----------------------
//
Error
OM_PmAgent::send_activate_services(fds_bool_t activate_sm,
                                   fds_bool_t activate_dm,
                                   fds_bool_t activate_am)
{
    TRACEFUNC;
    Error err(ERR_OK);
    fds_bool_t do_activate_sm = activate_sm;
    fds_bool_t do_activate_dm = activate_dm;
    fds_bool_t do_activate_am = activate_am;

    // we only activate services from 'discovered' state or
    // 'node up' state
    if ((node_state() != FDS_ProtocolInterface::FDS_Node_Discovered) &&
        (node_state() != FDS_ProtocolInterface::FDS_Node_Up)) {
        LOGERROR << "Invalid state";
        return Error(ERR_INVALID_ARG);
    }
    // WARNING: DMTMigration test depends on log parsing of the following message.
    LOGNORMAL << "OM_PmAgent: will send node activate message to " << get_node_name()
              << "; activate sm: " << activate_sm << "; activate dm: "<< activate_dm
              << "; activate am: " << activate_am;

    // we are ok to activate a service after we already activate another
    // services on this node, but check if the requested services are already
    // running
    if (node_state() == FDS_ProtocolInterface::FDS_Node_Up) {
        if (activate_sm && service_exists(FDS_ProtocolInterface::FDSP_STOR_MGR)) {
            LOGNOTIFY << "OM_PmAgent: SM service already running, "
                      << "not going to restart...";
            do_activate_sm = false;
        }
        if (activate_dm && service_exists(FDS_ProtocolInterface::FDSP_DATA_MGR)) {
            LOGNOTIFY << "OM_PmAgent: DM service already running, "
                      << "not going to restart...";
            do_activate_dm = false;
        }
        if (activate_am && service_exists(FDS_ProtocolInterface::FDSP_ACCESS_MGR)) {
            LOGNOTIFY << "OM_PmAgent: AM service already running. Allowing another "
                      << "AM instance...";
            // TODO(Andrew): Re-enable this if we want to prevent multiple AM
            // instances per node.
            // do_activate_am = false;
        }

        //  if all requested services already active, nothing to do
        if (!do_activate_sm && !do_activate_dm && !do_activate_am) {
            LOGNOTIFY << "All services already running, nothing to activate"
                      << " on node " << get_node_name();
            return err;
        }
    } else {
        // TODO(anna) we should set node active state when we get a response
        // for node activate + update config DB with node info
        // but for now assume always success and set active state here
        set_node_state(FDS_ProtocolInterface::FDS_Node_Up);
        kvstore::ConfigDB* configDB = gl_orch_mgr->getConfigDB();
        fds_mutex::scoped_lock l(dbNodeInfoLock);
        if (!configDB->nodeExists(get_uuid())) {
            // for now store only if the node was not known to DB
            configDB->addNode(*getNodeInfo());
            switch ( node_state() )
            {
                case fpi::FDS_Node_Discovered:
                case fpi::FDS_Node_Up:
                    fds::change_service_state( configDB, 
                                               get_uuid().uuid_get_val(),
                                               fpi::SVC_STATUS_ACTIVE );
                    break;
                case fpi::FDS_Start_Migration: 
                    fds::change_service_state( configDB, 
                                               get_uuid().uuid_get_val(), 
                                               fpi::SVC_STATUS_INVALID );
                    break;
                case fpi::FDS_Node_Down:    
                case fpi::FDS_Node_Rmvd:
                    fds::change_service_state( configDB, 
                                               get_uuid().uuid_get_val(),
                                               fpi::SVC_STATUS_INACTIVE );
                    break;    
            }
            
            LOGNOTIFY << "Adding node info for " << get_node_name() << ":"
                << std::hex << get_uuid().uuid_get_val() << std::dec
                << " in configDB";
        }
    }

    /**
    * Update or add Node Services accordingly.
    *
    * You might think we should do this at this point. I did. However,
    * handle_register_service() at this point causes om_reg_node_info()
    * to fail because it sets active[X]mAgent which is not expected. It must
    * be the case that Services are expected to be activated and then
    * registered, or something along those lines.
    */
//    if (activate_sm) {
//        handle_register_service(FDS_ProtocolInterface::FDSP_STOR_MGR, this);
//    }
//    if (activate_dm) {
//        handle_register_service(FDS_ProtocolInterface::FDSP_DATA_MGR, this);
//    }
//    if (activate_am) {
//        handle_register_service(FDS_ProtocolInterface::FDSP_ACCESS_MGR, this);
//    }

    fpi::ActivateServicesMsgPtr activateMsg = boost::make_shared<fpi::ActivateServicesMsg>();
    fpi::FDSP_ActivateNodeType& activateInfo = activateMsg->info;

    (activateInfo.node_uuid).uuid = static_cast<int64_t>(get_uuid().uuid_get_val());
    activateInfo.node_name = get_node_name();
    activateInfo.has_sm_service = activate_sm;
    activateInfo.has_dm_service = activate_dm;
    activateInfo.has_am_service = activate_am;
    activateInfo.has_om_service = false;

    auto req =  gSvcRequestPool->newEPSvcRequest(rs_get_uuid().toSvcUuid());
    req->setPayload(FDSP_MSG_TYPEID(fpi::ActivateServicesMsg), activateMsg);
    req->invoke();

    return err;
}

/**
 * Execute "remove services" message for the specified services.
 *
 * @param remove_XX - true or false indicating whether the Service is to be removed or not.
 *
 * @return Error
 */
Error
OM_PmAgent::send_remove_services(fds_bool_t remove_sm,
                                 fds_bool_t remove_dm,
                                 fds_bool_t remove_am)
{
    TRACEFUNC;
    Error err(ERR_OK);
    try {
        LOGNORMAL << "Received remove services for node" << get_node_name()
                        << " UUID " << std::hex << get_uuid().uuid_get_val() << std::dec
                        << " remove am ? " << remove_am
                        << " remove sm ? " << remove_sm
                        << " remove dm ? " << remove_dm;

        /*
         * Currently (3/21/2015) we only have support for one Local Domain.
         * At some point we should be able to look up the DomainContainer based on this PM Agent.
         */

        OM_NodeDomainMod *domain = OM_NodeDomainMod::om_local_domain();

        err = domain->om_del_services(get_uuid().uuid_get_val(),
                                      get_node_name(),
                                      remove_sm,
                                      remove_dm,
                                      remove_am);

        if (!err.ok()) {
            LOGERROR << "RemoveServices: Failed to remove services for node "
                            << get_node_name() << ", uuid "
                            << std::hex << get_uuid().uuid_get_val()
                            << std::dec << ", result: " << err.GetErrstr();
        }
    }
    catch(...) {
        LOGERROR << "Orch Mgr encountered exception while "
                        << "processing rmv node";
        err = Error(ERR_NOT_FOUND);
    }

    return err;
}

/**
 * Execute "deactivate services" message for the specified services.
 *
 * @param deactivate_XX - true or false indicating whether the Service is to be deactivated or not.
 *
 * @return Error
 */
Error
OM_PmAgent::send_deactivate_services(fds_bool_t deactivate_sm,
                                     fds_bool_t deactivate_dm,
                                     fds_bool_t deactivate_am)
{
    Error err(ERR_OK);
    LOGNORMAL << "Received deactivate services for node" << get_node_name()
              << " UUID " << std::hex << get_uuid().uuid_get_val() << std::dec
              << " deactivate am ? " << deactivate_am
              << " deactivate sm ? " << deactivate_sm
              << " deactivate dm ? " << deactivate_dm;

    fpi::DeactivateServicesMsgPtr deactivateMsg = boost::make_shared<fpi::DeactivateServicesMsg>();
    (deactivateMsg->node_uuid).uuid = static_cast<int64_t>(get_uuid().uuid_get_val());
    deactivateMsg->node_name = get_node_name();
    deactivateMsg->deactivate_sm_svc = deactivate_sm;
    deactivateMsg->deactivate_dm_svc = deactivate_dm;
    deactivateMsg->deactivate_am_svc = deactivate_am;
    deactivateMsg->deactivate_om_svc = false;

    auto req =  gSvcRequestPool->newEPSvcRequest(rs_get_uuid().toSvcUuid());
    req->setPayload(FDSP_MSG_TYPEID(fpi::DeactivateServicesMsg), deactivateMsg);
    req->onResponseCb(std::bind(&OM_PmAgent::send_deactivate_services_resp, this,
                                deactivate_sm, deactivate_dm, deactivate_am,
                                std::placeholders::_1, std::placeholders::_2,
                                std::placeholders::_3));
    req->setTimeoutMs(10000);
    req->invoke();

    return err;
}

void
OM_PmAgent::send_deactivate_services_resp(fds_bool_t deactivate_sm,
                                          fds_bool_t deactivate_dm,
                                          fds_bool_t deactivate_am,
                                          EPSvcRequest* req,
                                          const Error& error,
                                          boost::shared_ptr<std::string> payload) {
    LOGNORMAL << "ACK for deactivate services for node" << get_node_name()
              << " UUID " << std::hex << get_uuid().uuid_get_val() << std::dec
              << " deactivate am ? " << deactivate_am
              << " deactivate sm ? " << deactivate_sm
              << " deactivate dm ? " << deactivate_dm
              << " " << error;
    if (error.ok()) {
        // deactivate services on platform agent
        if (deactivate_sm) {
            handle_deactivate_service(FDS_ProtocolInterface::FDSP_STOR_MGR);
        }
        if (deactivate_dm) {
            handle_deactivate_service(FDS_ProtocolInterface::FDSP_DATA_MGR);
        }
        if (deactivate_am) {
            handle_deactivate_service(FDS_ProtocolInterface::FDSP_ACCESS_MGR);
        }
    } else {
        LOGERROR << "Failed to deactivate services on node " << get_node_name()
                 << " UUID " << std::hex << get_uuid().uuid_get_val() << std::dec
                 << " not updating local state of PM agent .... " << error;
    }

    // notify domain state machine
    OM_NodeDomainMod* domain = OM_NodeDomainMod::om_local_domain();
    domain->local_domain_event(DeactAckEvt(error));
}


// ---------------------------------------------------------------------------------
// Common OM Service Container
// ---------------------------------------------------------------------------------
OM_AgentContainer::OM_AgentContainer(FdspNodeType id) : AgentContainer(id)
{
    TRACEFUNC;
}

// agent_register
// --------------
//
Error
OM_AgentContainer::agent_register(const NodeUuid       &uuid,
                                  const FdspNodeRegPtr  msg,
                                  NodeAgent::pointer   *out,
                                  bool                  activate)
{
    TRACEFUNC;
    Error err = AgentContainer::agent_register(uuid, msg, out, false);

    if (OM_NodeDomainMod::om_in_test_mode() || (err != ERR_OK)) {
        return err;
    }
    OM_NodeAgent::pointer agent = OM_NodeAgent::agt_cast_ptr(*out);

    if (agent->node_get_svc_type() == fpi::FDSP_PLATFORM) {
        agent->node_agent_up();
        agent_activate(agent);
        return err;
    }

    agent_activate(agent);
    return err;
}

// agent_unregister
// ----------------
//
Error
OM_AgentContainer::agent_unregister(const NodeUuid &uuid, const std::string &name)
{
    Error err = AgentContainer::agent_unregister(uuid, name);

    return err;
}

// populate_nodes_in_container
// -----------------------------
//
const Error
OM_AgentContainer::populate_nodes_in_container(std::list<NodeSvcEntity> &container_nodes)
{
	NodeUuid nd_uuid;
	NodeAgent::pointer agent;
	FDS_ProtocolInterface::FDSP_MgrIdType type = container_type();

	if (rs_available_elm() == 0) {
		return (ERR_NOT_FOUND);
	}

	fds_verify((type == FDS_ProtocolInterface::FDSP_MgrIdType::FDSP_ACCESS_MGR) ||
			(type == FDS_ProtocolInterface::FDSP_MgrIdType::FDSP_STOR_MGR) ||
			(type == FDS_ProtocolInterface::FDSP_MgrIdType::FDSP_PLATFORM) ||
			(type == FDS_ProtocolInterface::FDSP_MgrIdType::FDSP_DATA_MGR));

	container_nodes.clear();

	for (fds_uint32_t i = 0; i < rs_available_elm(); i++) {
        agent = agent_info(i);
        if (agent->node_get_svc_type() == type) {
    	    container_nodes.emplace_back(agent->get_node_name(),
    	                                 agent->get_uuid(),
    	                                 agent->node_get_svc_type());
        }
	}

	return (ERR_OK);
}

// ---------------------------------------------------------------------------------
// OM Platform NodeAgent Container
// ---------------------------------------------------------------------------------
OM_PmContainer::OM_PmContainer() : OM_AgentContainer(fpi::FDSP_PLATFORM) {}

// agent_register
// --------------
//
Error
OM_PmContainer::agent_register(const NodeUuid       &uuid,
                               const FdspNodeRegPtr  msg,
                               NodeAgent::pointer   *out,
                               bool                  activate)
{
    TRACEFUNC;
    // check if this is a known Node
    bool        known;
    fpi::FDSP_RegisterNodeType nodeInfo;
    kvstore::ConfigDB* configDB = gl_orch_mgr->getConfigDB();

    if (configDB->getNode(uuid, nodeInfo)) {
        // this is a known node
        known = true;
        msg->node_name = nodeInfo.node_name;
    } else {
        // we are ignoring name that platform sends us
        known = false;
        if (msg->node_name.empty() || msg->node_name == "auto") {
            uint cfgNameCounter = configDB->getNodeNameCounter();
            if (cfgNameCounter > 0) {
                nodeNameCounter = cfgNameCounter;
            } else {
                nodeNameCounter++;
            }
            msg->node_name.clear();
            char buf[20];
            snprintf(buf, sizeof(buf), "Node-%d", nodeNameCounter);
            msg->node_name.append(buf);
            LOGNORMAL << "auto named node : " << msg->node_name;
        } else {
            LOGNOTIFY << "Using user provided name: " << msg->node_name;
        }
    }
    Error err = OM_AgentContainer::agent_register(uuid, msg, out, activate);
    if (!err.ok()) {
        return err;
    }

    fds_verify(out != NULL);
    OM_PmAgent::pointer agent = OM_PmAgent::agt_cast_ptr(*out);

    /* Cache the node information */
    agent->setNodeInfo(msg);

    if (known == true) {

        // start services that were running on that node
        NodeServices services;
        fds_bool_t has_am = false;
        fds_bool_t has_sm = false;
        fds_bool_t has_dm = false;
        if (configDB->getNodeServices(uuid, services)) {
            if (services.am.uuid_get_val() != 0) {
                has_am = true;
            }
            if (services.sm.uuid_get_val() != 0) {
                has_sm = true;
            }
            if (services.dm.uuid_get_val() != 0) {
                has_dm = true;
            }
        }
        LOGNORMAL << "Known node uuid " << agent->get_uuid().uuid_get_val()
                  << ", name " << agent->get_node_name() << ", start services:"
                  << " am:" << has_am << " sm:" << has_sm << " dm:" << has_dm;

        if (has_am || has_sm || has_dm) {
            agent->send_activate_services(has_sm, has_dm, has_am);
        }
    }
    return err;
}

// check_new_service
// -----------------
//
fds_bool_t
OM_PmContainer::check_new_service(const NodeUuid &pm_uuid,
                                  FDS_ProtocolInterface::FDSP_MgrIdType svc_role) {
    TRACEFUNC;
    fds_bool_t bret = false;
    NodeAgent::pointer agent = agent_info(pm_uuid);
    if (agent == NULL) {
        LOGWARN << "agent for PM node does not exit";
        return false;  // we must have pm node
    } else if (agent->node_state() != FDS_ProtocolInterface::FDS_Node_Up) {
        // TODO(anna) for now using NodeUp state as active, review states
        LOGWARN << "PM agent not in Node_Up state";
        return false;  // must be in active state
    }

    bret = (OM_PmAgent::agt_cast_ptr(agent)->service_exists(svc_role) == false);
    LOGDEBUG << "Service of type " << svc_role << " on node " << std::hex
             << pm_uuid.uuid_get_val() << std::dec << " exists? " << bret;
    return bret;
}

fds_bool_t OM_PmContainer::hasRegistered(const FdspNodeRegPtr  msg) {
    NodeAgent::pointer agent = agent_info(NodeUuid(msg->node_uuid.uuid));
    if (NULL == agent) return false;
    return OM_PmAgent::agt_cast_ptr(agent)->hasRegistered(msg);
}

// handle_register_service
// -----------------------
//
Error
OM_PmContainer::handle_register_service(const NodeUuid &pm_uuid,
                                        FDS_ProtocolInterface::FDSP_MgrIdType svc_role,
                                        NodeAgent::pointer svc_agent)
{
    TRACEFUNC;
    Error err(ERR_OK);
    NodeAgent::pointer pm_agt = agent_info(pm_uuid);

    if (pm_agt == NULL) {
        return Error(ERR_NODE_NOT_ACTIVE);
    }
    return OM_PmAgent::agt_cast_ptr(pm_agt)->handle_register_service(svc_role, svc_agent);
}

// handle_unregister_service
// -------------------------
//
NodeUuid
OM_PmContainer::handle_unregister_service(const NodeUuid& node_uuid,
                                          const std::string& node_name,
                                          FDS_ProtocolInterface::FDSP_MgrIdType svc_type)
{
    TRACEFUNC;
    NodeUuid svc_uuid;
    NodeAgent::pointer agent;
    if (node_uuid.uuid_get_val() == 0) {
        NodeUuid nd_uuid;
        for (fds_uint32_t i = 0; i < rs_available_elm(); ++i) {
            agent = agent_info(i);
            if (node_name.compare(agent->get_node_name()) == 0) {
                // we found node agent!
                if (nd_uuid.uuid_get_val() != 0) {
                    LOGWARN << "Found more than one node with the same name "
                            << " -- ambiguous, will not unregister service";
                    return svc_uuid;
                }
                // first node that we found
                nd_uuid = agent->get_uuid();
            }
        }
        if (nd_uuid.uuid_get_val() == 0) {
            LOGWARN << "Could not find platform agent for node " << node_name;
            return svc_uuid;
        }
        agent = agent_info(nd_uuid);
        svc_uuid = OM_PmAgent::agt_cast_ptr(
            agent)->handle_unregister_service(svc_type);
    } else {
        agent = agent_info(node_uuid);
        if (agent) {
            svc_uuid = OM_PmAgent::agt_cast_ptr(
                agent)->handle_unregister_service(svc_type);
        } else {
            LOGWARN << "Could not find platform agent for node uuid "
                    << std::hex << node_uuid.uuid_get_val() << std::dec;
        }
    }
    return svc_uuid;
}

// populate_nodes_in_container
// -----------------------------
//
const Error
OM_PmContainer::populate_nodes_in_container(std::list<NodeSvcEntity> &container_nodes)
{
	return (OM_AgentContainer::populate_nodes_in_container(container_nodes));
}
// ---------------------------------------------------------------------------------
// OM SM NodeAgent Container
// ---------------------------------------------------------------------------------
OM_SmContainer::OM_SmContainer() : OM_AgentContainer(fpi::FDSP_STOR_MGR) {}
// populate_nodes_in_container
// -----------------------------
//
const Error
OM_SmContainer::populate_nodes_in_container(std::list<NodeSvcEntity> &container_nodes)
{
	return (OM_AgentContainer::populate_nodes_in_container(container_nodes));
}
// agent_activate
// --------------
//
void
OM_AgentContainer::agent_activate(NodeAgent::pointer agent)
{
    TRACEFUNC;
    LOGNORMAL << "Activate node uuid " << std::hex
              << "0x" << agent->get_uuid().uuid_get_val() << std::dec;

    rs_mtx.lock();
    rs_register_mtx(agent);
    node_up_pend.push_back(OM_NodeAgent::agt_cast_ptr(agent));
    rs_mtx.unlock();
}

// agent_deactivate
// ----------------
//
void
OM_AgentContainer::agent_deactivate(NodeAgent::pointer agent)
{
    TRACEFUNC;
    LOGNORMAL << "Deactivate node uuid " << std::hex
              << "0x" << agent->get_uuid().uuid_get_val() << std::dec;

    rs_mtx.lock();
    rs_unregister_mtx(agent);
    node_down_pend.push_back(OM_NodeAgent::agt_cast_ptr(agent));
    rs_mtx.unlock();
}

// om_splice_nodes_pend
// --------------------
//
void
OM_AgentContainer::om_splice_nodes_pend(NodeList *addNodes, NodeList *rmNodes)
{
    TRACEFUNC;
    rs_mtx.lock();
    addNodes->splice(addNodes->begin(), node_up_pend);
    rmNodes->splice(rmNodes->begin(), node_down_pend);
    rs_mtx.unlock();
}

void
OM_AgentContainer::om_splice_nodes_pend(NodeList *addNodes,
                                        NodeList *rmNodes,
                                        const NodeUuidSet& filter_nodes)
{
    TRACEFUNC;
    rs_mtx.lock();
    for (NodeUuidSet::const_iterator cit = filter_nodes.cbegin();
         cit != filter_nodes.cend();
         ++cit) {
        NodeList::iterator it = node_up_pend.begin();
        while (it != node_up_pend.end()) {
            if ((*it)->get_uuid() == (*cit)) {
                addNodes->splice(addNodes->begin(), node_up_pend, it);
                break;
            }
            ++it;
        }
        it = node_down_pend.begin();
        while (it != node_down_pend.end()) {
            if ((*it)->get_uuid() == (*cit)) {
                rmNodes->splice(rmNodes->begin(), node_down_pend, it);
                break;
            }
            ++it;
        }
    }
    rs_mtx.unlock();
}

// --------------------------------------------------------------------------------------
// OM DM NodeAgent Container
// --------------------------------------------------------------------------------------
OM_DmContainer::OM_DmContainer() : OM_AgentContainer(fpi::FDSP_DATA_MGR) {}

// populate_nodes_in_container
// -----------------------------
//
const Error
OM_DmContainer::populate_nodes_in_container(std::list<NodeSvcEntity> &container_nodes)
{
	return (OM_AgentContainer::populate_nodes_in_container(container_nodes));
}
// -------------------------------------------------------------------------------------
// OM AM NodeAgent Container
// -------------------------------------------------------------------------------------
OM_AmContainer::OM_AmContainer() : OM_AgentContainer(fpi::FDSP_ACCESS_MGR) {}

// populate_nodes_in_container
// -----------------------------
//
const Error
OM_AmContainer::populate_nodes_in_container(std::list<NodeSvcEntity> &container_nodes)
{
	return (OM_AgentContainer::populate_nodes_in_container(container_nodes));
}
// --------------------------------------------------------------------------------------
// OM Node Container
// --------------------------------------------------------------------------------------
OM_NodeContainer::OM_NodeContainer()
    : DomainContainer("OM-Domain",
                      NULL,
                      new OM_SmContainer(),
                      new OM_DmContainer(),
                      new OM_AmContainer(),
                      new OM_PmContainer(),
                      new OmContainer(fpi::FDSP_ORCH_MGR))
{
    TRACEFUNC;
    om_volumes    = new VolumeContainer();
}

OM_NodeContainer::~OM_NodeContainer()
{
    delete om_admin_ctrl;
}

// om_init_domain
// --------------
//
void
OM_NodeContainer::om_init_domain()
{
    TRACEFUNC;
    om_admin_ctrl = new FdsAdminCtrl(OrchMgr::om_stor_prefix());

    // TODO(Anna) PerfStats class is replaced, not sure yet if we
    // are also pushing stats to OM, so commenting this out for now
    // to remember to port to new stats class yet
    /*
    am_stats = new PerfStats(OrchMgr::om_stor_prefix() + "OM_from_AM",
                             5 * FDS_STAT_DEFAULT_HIST_SLOTS);
    if (am_stats != NULL) {
        am_stats->enable();
    }
    */
}

// om_send_qos_info
// -----------------------
//
static void
om_send_qos_info(fds_uint64_t total_rate, NodeAgent::pointer node)
{
    OM_SmAgent::agt_cast_ptr(node)->om_send_qosinfo(total_rate);
}

// om_update_capacity
// ------------------
//
void
OM_NodeContainer::om_update_capacity(OM_PmAgent::pointer pm_agent,
                                     fds_bool_t b_add)
{
    TRACEFUNC;
    fds_uint64_t old_max_iopc = om_admin_ctrl->getMaxIOPC();
    if (b_add) {
        om_admin_ctrl->addDiskCapacity(pm_agent->getDiskCapabilities());
    } else {
        om_admin_ctrl->removeDiskCapacity(pm_agent->getDiskCapabilities());
    }

    // if perf capability changed, notify AMs to modify QoS
    // control params accordingly
    fds_uint64_t new_max_iopc = om_admin_ctrl->getMaxIOPC();
    if ((new_max_iopc != 0) &&
        (new_max_iopc != old_max_iopc)) {
        dc_am_nodes->agent_foreach<fds_uint64_t>(new_max_iopc, om_send_qos_info);
    }
}

void
OM_NodeContainer::om_send_me_qosinfo(NodeAgent::pointer me) {
    TRACEFUNC;
    OM_AmAgent::pointer agent = OM_AmAgent::agt_cast_ptr(me);

    // for now we are just sending total rate to AM
    if (agent->node_get_svc_type() != fpi::FDSP_ACCESS_MGR) return;

    fds_uint64_t max_iopc = om_admin_ctrl->getMaxIOPC();
    if (max_iopc != 0) {
        agent->om_send_qosinfo(max_iopc);
    }
}

// om_send_my_info_to_peer
// -----------------------
//
static void
om_send_my_info_to_peer(NodeAgent::pointer me, NodeAgent::pointer peer)
{
    TRACEFUNC;
    OM_SmAgent::agt_cast_ptr(me)->om_send_myinfo(peer);
}

// om_bcast_new_node
// -----------------
//
void
OM_NodeContainer::om_bcast_new_node(NodeAgent::pointer node, const FdspNodeRegPtr ref)
{
    TRACEFUNC;
    if (ref->node_type == fpi::FDSP_ACCESS_MGR) {
        return;
    }
    dc_sm_nodes->agent_foreach<NodeAgent::pointer>(node, om_send_my_info_to_peer);
    dc_dm_nodes->agent_foreach<NodeAgent::pointer>(node, om_send_my_info_to_peer);
    dc_am_nodes->agent_foreach<NodeAgent::pointer>(node, om_send_my_info_to_peer);
}

// om_send_peer_info_to_me
// -----------------------
//
static void
om_send_peer_info_to_me(NodeAgent::pointer me, NodeAgent::pointer peer)
{
    TRACEFUNC;
    OM_SmAgent::agt_cast_ptr(peer)->om_send_myinfo(me);
}

// om_update_node_list
// -------------------
//
void
OM_NodeContainer::om_update_node_list(NodeAgent::pointer node, const FdspNodeRegPtr ref)
{
    TRACEFUNC;
    dc_sm_nodes->agent_foreach<NodeAgent::pointer>(node, om_send_peer_info_to_me);
    dc_dm_nodes->agent_foreach<NodeAgent::pointer>(node, om_send_peer_info_to_me);
    dc_am_nodes->agent_foreach<NodeAgent::pointer>(node, om_send_peer_info_to_me);
}

// om_activate_service
// -------------------
//
static void
om_activate_services(fds_bool_t activate_sm,
                     fds_bool_t activate_dm,
                     fds_bool_t activate_am,
                     NodeAgent::pointer node)
{
    TRACEFUNC;

    if (!activate_sm && !activate_dm & !activate_am) {
        /**
         * We are being asked to activate all Services defined for the Node.
         * Which services are defined for this Node?
         */
        NodeServices services;
        kvstore::ConfigDB* configDB = gl_orch_mgr->getConfigDB();

        if (configDB->getNodeServices(node->get_uuid(), services)) {
            if (services.am.uuid_get_val() != 0) {
                activate_am = true;
            }
            if (services.sm.uuid_get_val() != 0) {
                activate_sm = true;
            }
            if (services.dm.uuid_get_val() != 0) {
                activate_dm = true;
            }
        } else {
            /**
            * We interpret no Services information in ConfigDB
            * as the Node is configured for all Services - default
            * for a new Node.
            */
            activate_am = true;
            activate_sm = true;
            activate_dm = true;
        }
    }

    OM_PmAgent::agt_cast_ptr(node)->send_activate_services(activate_sm,
                                                           activate_dm,
                                                           activate_am);
}

// om_activate_services
//
//
void
OM_NodeContainer::om_cond_bcast_activate_services(fds_bool_t activate_sm,
                                                  fds_bool_t activate_dm,
                                                  fds_bool_t activate_am)
{
    TRACEFUNC;
    dc_pm_nodes->agent_foreach<fds_bool_t, fds_bool_t, fds_bool_t>
            (activate_sm, activate_dm, activate_am, om_activate_services);
}

// om_activate_service
//
//
Error
OM_NodeContainer::om_activate_node_services(const NodeUuid& node_uuid,
                                            fds_bool_t activate_sm,
                                            fds_bool_t activate_dm,
                                            fds_bool_t activate_am) {
    TRACEFUNC;
    OM_PmAgent::pointer agent = om_pm_agent(node_uuid);
    if (agent == NULL) {
        LOGERROR << "activate node services: platform service is not "
                 << "running (or node uuid is not correct) on node "
                 << std::hex << node_uuid.uuid_get_val() << std::dec;
        return Error(ERR_NOT_FOUND);
    }

    return agent->send_activate_services(activate_sm,
                                         activate_dm,
                                         activate_am);
}

/**
 * Remove all defined Services on the specified Node.
 */
static void
om_remove_services(fds_bool_t remove_sm,
                   fds_bool_t remove_dm,
                   fds_bool_t remove_am,
                   NodeAgent::pointer node)
{
    TRACEFUNC;

    if (!remove_sm && !remove_dm && !remove_am) {
        /**
         * We are being asked to remove all defined Services from the given Node.
         * Which services are defined for this Node?
         */
        NodeServices services;
        kvstore::ConfigDB *configDB = gl_orch_mgr->getConfigDB();

        if (configDB->getNodeServices(node->get_uuid(), services)) {
            if (services.am.uuid_get_val() != 0) {
                remove_am = true;
            }
            if (services.sm.uuid_get_val() != 0) {
                remove_sm = true;
            }
            if (services.dm.uuid_get_val() != 0) {
                remove_dm = true;
            }
        } else {
            /**
            * Nothing defined, so nothing to remove.
            */
            return;
        }
    }

    OM_PmAgent::agt_cast_ptr(node)->send_remove_services(remove_sm, remove_dm, remove_am);
}

/**
 * Remove specified Services on all Nodes defined in the Local Domain.
 */
void
OM_NodeContainer::om_cond_bcast_remove_services(fds_bool_t remove_sm,
                                                fds_bool_t remove_dm,
                                                fds_bool_t remove_am)
{
    TRACEFUNC;
    dc_pm_nodes->agent_foreach(remove_sm, remove_dm, remove_am, om_remove_services);
}

/**
 * Deactivate all defined Services on the specified Node.
 * if all deactivate_sm && deactivate_dm && deactivate_am are false, then
 * will deactivate all services on the specified Node
 */
static Error
om_deactivate_services(fds_bool_t deactivate_sm,
                       fds_bool_t deactivate_dm,
                       fds_bool_t deactivate_am,
                       NodeAgent::pointer node)
{
    LOGDEBUG << "deactivate_sm " << deactivate_sm << ", deactivate_dm "
             << deactivate_dm << ", deactivate_am " << deactivate_am;

    if (!deactivate_sm && !deactivate_dm && !deactivate_am) {
        /**
         * We are being asked to deactivate all defined Services from the given Node.
         * Which services are defined for this Node?
         */
        NodeServices services;
        kvstore::ConfigDB *configDB = gl_orch_mgr->getConfigDB();

        if (configDB->getNodeServices(node->get_uuid(), services)) {
            if (services.am.uuid_get_val() != 0) {
                deactivate_am = true;
            }
            if (services.sm.uuid_get_val() != 0) {
                deactivate_sm = true;
            }
            if (services.dm.uuid_get_val() != 0) {
                deactivate_dm = true;
            }
        } else {
            /**
            * Nothing defined, so nothing to deactivate.
            * However, we will still send a deactivate services msg to
            * PM so it can choose to check/deactivate unregistered/unknown
            * services
            */
            LOGNOTIFY << "No services defined for node" << std::hex
                      << node->get_uuid().uuid_get_val() << std::dec
                      << ", but we will send deactivate services msg to PM"
                      << " anyway so it can check if there are any uknown"
                      << " or unregistered services";
        }
    }

    Error err = OM_PmAgent::agt_cast_ptr(node)->send_deactivate_services(deactivate_sm,
                                                                         deactivate_dm,
                                                                         deactivate_am);
    return err;
}


/**
 * Deactivate specified Services on all Nodes defined in the Local Domain.
 * This is different from remove services -- remove a service will deregister
 * the service and remove it from cluster map, deactivate is just a message
 * to PM to kill the corresponding processes
 */
fds_uint32_t
OM_NodeContainer::om_cond_bcast_deactivate_services(fds_bool_t deactivate_sm,
                                                    fds_bool_t deactivate_dm,
                                                    fds_bool_t deactivate_am)
{
    TRACEFUNC;
    fds_uint32_t errok_count = dc_pm_nodes->agent_ret_foreach(deactivate_sm, deactivate_dm, deactivate_am,
                                                              om_deactivate_services);
    return errok_count;
}

// om_send_vol_info
// ----------------
//
static void
om_send_vol_info(NodeAgent::pointer me, fds_uint32_t *cnt, VolumeInfo::pointer vol)
{
    TRACEFUNC;
    /*
     * Only send if not deleted or marked to be deleted.
     */
    if (vol->isDeletePending() || vol->isStateDeleted()) {
        LOGDEBUG << "Dmt not sending Volume to Node :" << vol->vol_get_name()
                 << "; state " << vol->getStateName();
        return;
    }

    (*cnt)++;
    OM_Module* om = OM_Module::om_singleton();
    VolumePlacement* vp = om->om_volplace_mod();
    fpi::FDSP_NotifyVolFlag vol_flag = fpi::FDSP_NOTIFY_VOL_NO_FLAG;
    // TODO(Anna) Since DM migration is disabled and we are going to re-implement it
    // do not set "volume will sync" flag; otherwise it has unexpected effect of
    // volume queues being not active in DMs
    /*
    if (vp->hasCommittedDMT()) {
      vol_flag = fpi::FDSP_NOTIFY_VOL_WILL_SYNC;
    }
    */
    LOGDEBUG << "Dmt Send Volume to Node :" << vol->vol_get_name()
             << "; will sync flag " << vp->hasCommittedDMT();
    OM_NodeAgent::agt_cast_ptr(me)->om_send_vol_cmd(vol,
                                                    fpi::CtrlNotifyVolAddTypeId,
                                                    vol_flag);
}

// om_bcast_vol_list
// -----------------
//
fds_uint32_t
    OM_NodeContainer::om_bcast_vol_list(NodeAgent::pointer node)
{
    fds_uint32_t cnt = 0;
    om_volumes->vol_foreach<NodeAgent::pointer, fds_uint32_t *>
                              (node, &cnt, om_send_vol_info);
    LOGDEBUG << "Dmt bcast Volume list :" << cnt;
    return cnt;
}

static void
om_bcast_volumes(VolumeContainer::pointer om_volumes, NodeAgent::pointer node) {
    fds_uint32_t cnt = 0;
    om_volumes->vol_foreach<NodeAgent::pointer, fds_uint32_t *>
                              (node, &cnt, om_send_vol_info);
}

void
OM_NodeContainer::om_bcast_vol_list_to_services(fpi::FDSP_MgrIdType svc_type) {
    if (svc_type == fpi::FDSP_DATA_MGR) {
        dc_dm_nodes->agent_foreach<VolumeContainer::pointer>(om_volumes, om_bcast_volumes);
        LOGDEBUG << "Sent Volume List to DM services successfully";
    } else if (svc_type == fpi::FDSP_STOR_MGR) {
        dc_sm_nodes->agent_foreach<VolumeContainer::pointer>(om_volumes, om_bcast_volumes);
        LOGDEBUG << "Sent Volume List to SM services successfully";
    } else if (svc_type == fpi::FDSP_ACCESS_MGR) {
        // this method must only be called for either DM, SM or AM!
        fds_verify(svc_type == fpi::FDSP_ACCESS_MGR);
        dc_am_nodes->agent_foreach<VolumeContainer::pointer>(om_volumes, om_bcast_volumes);
        LOGDEBUG << "Sent Volume List to AM services successfully";
    } else {
        LOGERROR << "Received request to bcast Volume List to invalid svc type.";
    }

}

void
OM_NodeContainer::om_bcast_stream_reg_list(NodeAgent::pointer node) {
    OM_DmAgent::agt_cast_ptr(node)->om_send_stream_reg_cmd(0, true);
}

// om_send_vol_command
// -------------------
// Send the volume command to the node represented by the agent.
//
static Error
om_send_vol_command(fpi::FDSPMsgTypeId cmd_type,
                    fpi::FDSP_NotifyVolFlag vol_flag,
                    VolumeInfo::pointer   vol,
                    NodeAgent::pointer    agent)
{
    return OM_SmAgent::agt_cast_ptr(agent)->om_send_vol_cmd(vol, cmd_type, vol_flag);
}

// om_bcast_vol_create
// -------------------
//
fds_uint32_t
OM_NodeContainer::om_bcast_vol_create(VolumeInfo::pointer vol)
{
    fds_uint32_t errok_sm_count = 0;
    errok_sm_count = dc_sm_nodes->agent_ret_foreach<
        fpi::FDSPMsgTypeId,
        fpi::FDSP_NotifyVolFlag,
        VolumeInfo::pointer>(fpi::CtrlNotifyVolAddTypeId,
                             fpi::FDSP_NOTIFY_VOL_NO_FLAG,
                             vol,
                             om_send_vol_command);

    // for DMs, we should send volume create with WillSync flag set for
    // DMs that are in the middle of addition, and for others no flag
    Error err(ERR_OK);
    RsArray dm_nodes;
    fds_uint32_t dm_count = dc_dm_nodes->rs_container_snapshot(&dm_nodes);
    OM_Module* om = OM_Module::om_singleton();
    ClusterMap* cm = om->om_clusmap_mod();
    NodeUuidSet addedDms = cm->getAddedServices(fpi::FDSP_DATA_MGR);

    fds_uint32_t errok_dm_count = 0;
    for (RsContainer::const_iterator it = dm_nodes.cbegin();
         it != dm_nodes.cend();
         ++it) {
        NodeAgent::pointer cur = agt_cast_ptr<NodeAgent>(*it);
        if ((cur != NULL) &&
            (dc_dm_nodes->rs_get_resource(cur->get_uuid()))) {
            fpi::FDSP_NotifyVolFlag flag = fpi::FDSP_NOTIFY_VOL_NO_FLAG;
            if (addedDms.count(cur->get_uuid()) > 0) {
                flag = fpi::FDSP_NOTIFY_VOL_WILL_SYNC;
            }
            err = om_send_vol_command(fpi::CtrlNotifyVolAddTypeId,
                                      flag, vol, cur);
            if (err.ok()) {
                ++errok_dm_count;
            }
        }
    }

    return (errok_sm_count + errok_dm_count);
}

// om_bcast_vol_modify
// -------------------
//
void
OM_NodeContainer::om_bcast_vol_modify(VolumeInfo::pointer vol)
{
    dc_sm_nodes->agent_ret_foreach<fpi::FDSPMsgTypeId,
                                   fpi::FDSP_NotifyVolFlag,
                                   VolumeInfo::pointer>(fpi::CtrlNotifyVolModTypeId,
                                                    fpi::FDSP_NOTIFY_VOL_NO_FLAG,
                                                    vol, om_send_vol_command);

    dc_dm_nodes->agent_ret_foreach<fpi::FDSPMsgTypeId,
                                   fpi::FDSP_NotifyVolFlag,
                                   VolumeInfo::pointer>(fpi::CtrlNotifyVolModTypeId,
                                                    fpi::FDSP_NOTIFY_VOL_NO_FLAG,
                                                    vol, om_send_vol_command);

    dc_am_nodes->agent_ret_foreach<fpi::FDSPMsgTypeId,
                                   fpi::FDSP_NotifyVolFlag,
                                   VolumeInfo::pointer>(fpi::CtrlNotifyVolModTypeId,
                                                    fpi::FDSP_NOTIFY_VOL_NO_FLAG,
                                                    vol, om_send_vol_command);
}

// om_bcast_vol_snap
// -------------------
//
fds_uint32_t
OM_NodeContainer::om_bcast_vol_snap(VolumeInfo::pointer vol)
{
    fds_uint32_t errok_dm_nodes = 0;
    errok_dm_nodes = dc_dm_nodes->agent_ret_foreach<
        fpi::FDSPMsgTypeId,
        fpi::FDSP_NotifyVolFlag,
        VolumeInfo::pointer>(fpi::CtrlNotifySnapVolTypeId,
                             fpi::FDSP_NOTIFY_VOL_NO_FLAG,
                             vol, om_send_vol_command);
    return errok_dm_nodes;
}

// om_bcast_vol_detach
// -------------------
//
fds_uint32_t
OM_NodeContainer::om_bcast_vol_detach(VolumeInfo::pointer vol)
{
    return dc_am_nodes->agent_ret_foreach<fpi::FDSPMsgTypeId,
                                          fpi::FDSP_NotifyVolFlag,
                                          VolumeInfo::pointer>(fpi::CtrlNotifyVolRemoveTypeId,
                                                            fpi::FDSP_NOTIFY_VOL_NO_FLAG,
                                                            vol, om_send_vol_command);
}


// om_bcast_vol_delete
// -------------------
//
fds_uint32_t
OM_NodeContainer::om_bcast_vol_delete(VolumeInfo::pointer vol, fds_bool_t check_only)
{
    fds_uint32_t count = 0;
    fpi::FDSP_NotifyVolFlag vol_flag = fpi::FDSP_NOTIFY_VOL_NO_FLAG;
    if (check_only) {
        vol_flag = fpi::FDSP_NOTIFY_VOL_CHECK_ONLY;
    }

    if (!check_only) {
        dc_sm_nodes->agent_ret_foreach<fpi::FDSPMsgTypeId,
                                       fpi::FDSP_NotifyVolFlag,
                                       VolumeInfo::pointer>(fpi::CtrlNotifyVolRemoveTypeId,
                                                            vol_flag, vol,
                                                            om_send_vol_command);
        count += dc_sm_nodes->rs_available_elm();
    }

    dc_dm_nodes->agent_ret_foreach<fpi::FDSPMsgTypeId,
                                   fpi::FDSP_NotifyVolFlag,
                                   VolumeInfo::pointer>(fpi::CtrlNotifyVolRemoveTypeId,
                                                        vol_flag, vol,
                                                        om_send_vol_command);
    count += dc_dm_nodes->rs_available_elm();

    return count;
}

// om_send_node_command
// --------------------
// Plugin to send a generic node command to all nodes.  Plugin to node iterator.
//
static void
om_send_node_throttle_lvl(fpi::FDSP_ThrottleMsgTypePtr msg,
                        fds_uint32_t *cnt, NodeAgent::pointer node)
{
    (*cnt)++;
    OM_SmAgent::agt_cast_ptr(node)->om_send_node_throttle_lvl(msg);
}

// om_bcast_throttle_lvl
// ---------------------
//
void
OM_NodeContainer::om_bcast_throttle_lvl(float throttle_level)
{
    fds_uint32_t count = 0;
    fpi::FDSP_ThrottleMsgTypePtr throttle(new fpi::FDSP_ThrottleMsgType);

    throttle->domain_id      = DEFAULT_LOC_DOMAIN_ID;
    throttle->throttle_level = throttle_level;

    dc_am_nodes->agent_foreach<fpi::FDSP_ThrottleMsgTypePtr, \
            fds_uint32_t *>(throttle, &count, om_send_node_throttle_lvl);
}

// om_set_throttle_lvl
// -------------------
//
void
OM_NodeContainer::om_set_throttle_lvl(float level)
{
    om_cur_throttle_level = level;

    LOGNOTIFY << "Setting throttle level for local domain at " << level << std::endl;

    om_bcast_throttle_lvl(level);
}

// om_send_dlt
// -----------------------
//
static Error
om_send_dmt(const DMTPtr& curDmt, NodeAgent::pointer agent)
{
    return OM_NodeAgent::agt_cast_ptr(agent)->om_send_dmt(curDmt);
}

// om_bcast_dmt
// -------------
//
fds_uint32_t
OM_NodeContainer::om_bcast_dmt(fpi::FDSP_MgrIdType svc_type,
                               const DMTPtr& curDmt)
{
    TRACEFUNC;
    fds_uint32_t count = 0;
    // WARNING: DMTMigration test depends on the following messages.
    if (svc_type == fpi::FDSP_DATA_MGR) {
        count += dc_dm_nodes->agent_ret_foreach<const DMTPtr&>(curDmt, om_send_dmt);
        LOGDEBUG << "Sent DMT to " << count << " DM services successfully";
    } else if (svc_type == fpi::FDSP_STOR_MGR) {
        count += dc_sm_nodes->agent_ret_foreach<const DMTPtr&>(curDmt, om_send_dmt);
        LOGDEBUG << "Sent DMT to " << count << " SM services successfully";
    } else if (svc_type == fpi::FDSP_ACCESS_MGR) {
        // this method must only be called for either DM, SM or AM!
        fds_verify(svc_type == fpi::FDSP_ACCESS_MGR);
        count += dc_am_nodes->agent_ret_foreach<const DMTPtr&>(curDmt, om_send_dmt);
        LOGDEBUG << "Sent DMT to " << count << " AM services successfully";
    } else {
        LOGERROR << "Received request to bcast DMT to invalid svc type.";
    }
    return count;
}

// om_send_dmt_close
// -----------------------
//
static Error
om_send_dmt_close(fds_uint64_t dmt_version, NodeAgent::pointer agent)
{
    TRACEFUNC;
    return OM_DmAgent::agt_cast_ptr(agent)->om_send_dmt_close(dmt_version);
}

// om_bcast_dmt_close
// ------------------
// Broadcasts to DMs that are already in cluster map! Just added DMs that
// are not included in DMT must not get DMT close message (which may arrive
// without DMT update if DM was added right in the middle)
// @return number of nodes we sent the message to (and
// we are waiting for that many responses)
//
fds_uint32_t
OM_NodeContainer::om_bcast_dmt_close(fds_uint64_t dmt_version)
{
    Error err(ERR_OK);
    OM_Module* om = OM_Module::om_singleton();
    OM_NodeContainer* loc_domain = OM_NodeDomainMod::om_loc_domain_ctrl();
    ClusterMap* cm = om->om_clusmap_mod();
    fds_uint32_t count = 0;

    // iterate over DMs that are in cluster map, and send dmt close
    // mesage to each of them
    for (ClusterMap::const_dm_iterator cit = cm->cbegin_dm();
         cit != cm->cend_dm();
         ++cit) {
        OM_DmAgent::pointer dm_agent = loc_domain->om_dm_agent(cit->first);
        err = dm_agent->om_send_dmt_close(dmt_version);
        if (err.ok()) ++count;
    }
    LOGDEBUG << "Send DMT close to " << count << " DMs successfully";
    return count;
}


// om_send_dlt
// -----------------------
//
static Error
om_send_dlt(const DLT* curDlt, NodeAgent::pointer agent)
{
    return OM_SmAgent::agt_cast_ptr(agent)->om_send_dlt(curDlt);
}

// om_bcast_dlt
// ------------
//
fds_uint32_t
OM_NodeContainer::om_bcast_dlt(const DLT* curDlt,
                               fds_bool_t to_sm,
                               fds_bool_t to_dm,
                               fds_bool_t to_am)
{
    TRACEFUNC;
    fds_uint32_t count = 0;
    if (to_sm) {
        count = dc_sm_nodes->agent_ret_foreach<const DLT*>(curDlt, om_send_dlt);
        LOGDEBUG << "Sent dlt to SM nodes successfully";
    }

    if (to_dm) {
        count += dc_dm_nodes->agent_ret_foreach<const DLT*>(curDlt, om_send_dlt);
        LOGDEBUG << "Sent dlt to DM nodes successfully";
    }
    if (to_am) {
        count += dc_am_nodes->agent_ret_foreach<const DLT*>(curDlt, om_send_dlt);
        LOGDEBUG << "Sent dlt to AM nodes successfully";
    }

    LOGDEBUG << "Sent dlt to " << count << " nodes successfully";
    return count;
}


// om_send_dlt_close
// -----------------------
//
static Error
om_send_dlt_close(fds_uint64_t cur_dlt_version, NodeAgent::pointer agent)
{
    return OM_SmAgent::agt_cast_ptr(agent)->om_send_dlt_close(cur_dlt_version);
}

// om_bcast_dlt_close
// ------------------
// @return number of nodes we sent the message to (and
// we are waiting for that many responses)
//
fds_uint32_t
OM_NodeContainer::om_bcast_dlt_close(fds_uint64_t cur_dlt_version)
{
    fds_uint32_t count = 0;
    count = dc_sm_nodes->agent_ret_foreach<fds_uint64_t>(cur_dlt_version,
                                                         om_send_dlt_close);
    LOGDEBUG << "Send dlt close to " << count << " nodes successfully";
    return count;
}

// om_send_sm_migration_abort
// --------------------------
//
static Error
om_send_sm_migration_abort(fds_uint64_t cur_dlt_version, NodeAgent::pointer agent)
{
    return OM_SmAgent::agt_cast_ptr(agent)->om_send_sm_abort_migration(cur_dlt_version);
}

// om_bcast_sm_migration_abort
// ----------------------------
// @return number of nodes we sent the message to (and
// we are waiting for that many responses)
//
fds_uint32_t
OM_NodeContainer::om_bcast_sm_migration_abort(fds_uint64_t cur_dlt_version)
{
    fds_uint32_t count = 0;
    count = dc_sm_nodes->agent_ret_foreach<fds_uint64_t>(cur_dlt_version,
                                                         om_send_sm_migration_abort);
    LOGDEBUG << "Sent SM Migration Abort to " << count << " nodes successfully";
    return count;
}

// om_send_dm_migration_abort
// --------------------------
//
static Error
om_send_dm_migration_abort(fds_uint64_t cur_dmt_version, NodeAgent::pointer agent)
{
    return OM_DmAgent::agt_cast_ptr(agent)->om_send_dm_abort_migration(cur_dmt_version);
}

// om_bcast_dm_migration_abort
// ---------------------------
// @return number of nodes we sent message to
//
fds_uint32_t
OM_NodeContainer::om_bcast_dm_migration_abort(fds_uint64_t cur_dmt_version) {
    fds_uint32_t count = 0;
    count = dc_dm_nodes->agent_ret_foreach<fds_uint64_t>(cur_dmt_version,
            om_send_dm_migration_abort);
    LOGDEBUG << "Sent DM migration abort to " << count << " DMs.";
    return count;
}



// om_send_dlt_close
// -----------------------
//
static void
om_send_scavenger_cmd(FDS_ProtocolInterface::FDSP_ScavengerCmd cmd,
                      NodeAgent::pointer agent) {
    OM_SmAgent::agt_cast_ptr(agent)->om_send_scavenger_cmd(cmd);
}

//
// For now sends scavenger start message
// TODO(xxx) extend to other scavenger commands
//
void
OM_NodeContainer::om_bcast_scavenger_cmd(FDS_ProtocolInterface::FDSP_ScavengerCmd cmd)
{
    dc_sm_nodes->agent_foreach<FDS_ProtocolInterface::FDSP_ScavengerCmd>(
        cmd, om_send_scavenger_cmd);
}


static void
om_send_stream_reg_cmd(fds_int32_t regId, fds_bool_t bAll,
                       NodeAgent::pointer agent) {
    OM_DmAgent::agt_cast_ptr(agent)->om_send_stream_reg_cmd(regId, bAll);
}

void
OM_NodeContainer::om_bcast_stream_register_cmd(fds_int32_t regId,
                                               fds_bool_t bAll)
{
    dc_dm_nodes->agent_foreach<fds_int32_t, fds_bool_t>(regId, bAll, om_send_stream_reg_cmd);
}

static Error
om_send_shutdown(fds_uint32_t ignore, NodeAgent::pointer agent) {
    return OM_SmAgent::agt_cast_ptr(agent)->om_send_shutdown();
}

// om_bcast_shutdown_msg
// ---------------------
//
fds_uint32_t
OM_NodeContainer::om_bcast_shutdown_msg(fpi::FDSP_MgrIdType svc_type)
{
    fds_uint32_t count = 0;

    if (svc_type == fpi::FDSP_DATA_MGR) {
        // send shutdown to DM nodes
        count = dc_dm_nodes->agent_ret_foreach<fds_uint32_t>(0, om_send_shutdown);
        LOGDEBUG << "Sent SHUTDOWN to " << count << " DM services successfully";
    } else if (svc_type == fpi::FDSP_STOR_MGR) {
        // send shutdown to SM nodes
        count = dc_sm_nodes->agent_ret_foreach<fds_uint32_t>(0, om_send_shutdown);
        LOGDEBUG << "Sent SHUTDOWN to " << count << " SM services successfully"; 
    } else if (svc_type == fpi::FDSP_ACCESS_MGR) {
        // send shutdown to AM nodes
        count = dc_am_nodes->agent_ret_foreach<fds_uint32_t>(0, om_send_shutdown);
        LOGDEBUG << "Sent SHUTDOWN to " << count << " AM services successfully";
    } else {
        LOGERROR << "Received Prepare For Shutdown request for invalid svc type.";
    }
    return count;
}

void OM_NodeContainer::om_bcast_svcmap()
{
    LOGDEBUG << "Broadcasting service map";

    auto svcMgr = MODULEPROVIDER()->getSvcMgr();
    boost::shared_ptr<std::string>buf;
    fpi::UpdateSvcMapMsgPtr updateMsg = boost::make_shared<fpi::UpdateSvcMapMsg>(); 

    /* Construct svcmap message */
    svcMgr->getSvcMap(updateMsg->updates);
    fds::serializeFdspMsg(*updateMsg, buf);

    /* Update the domain by broadcasting */
    auto header = svcMgr->getSvcRequestMgr()->newSvcRequestHeaderPtr(
        SvcRequestPool::SVC_UNTRACKED_REQ_ID,
        FDSP_MSG_TYPEID(fpi::UpdateSvcMapMsg),
        svcMgr->getSelfSvcUuid(),
        fpi::SvcUuid());

    // TODO(Rao): add the filter so that we don't send the broad cast to om
    svcMgr->broadcastAsyncSvcReqMessage(header, buf,
                                        [](const fpi::SvcInfo& info) {return true;});
}

}  // namespace fds
