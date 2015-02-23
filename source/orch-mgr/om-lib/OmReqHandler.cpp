/*
 * Copyright 2013 Formation Data Systems, Inc.
 */

#include <vector>
#include <string>
#include <orchMgr.h>
#include <net/net-service-tmpl.hpp>
#include <NetSession.h>
#include <OmResources.h>
#include <net/SvcRequest.h>
#include <fdsp_utils.h>
#include <fiu-local.h>
#include <fdsp/fds_service_types.h>
#include <net/BaseAsyncSvcHandler.h>
#include <net/PlatNetSvcHandler.h>
#include "platform/node_data.h"
#include "platform/platform.h"

#undef LOGGERPTR
#define LOGGERPTR orchMgr->GetLog()
namespace fds {

FDSP_ConfigPathReqHandler::FDSP_ConfigPathReqHandler(OrchMgr *oMgr) {
    orchMgr = oMgr;
}

int32_t FDSP_ConfigPathReqHandler::CreateVol(
    const ::FDS_ProtocolInterface::FDSP_MsgHdrType& fdsp_msg,
    const ::FDS_ProtocolInterface::FDSP_CreateVolType& crt_vol_req) {
    // Don't do anything here. This stub is just to keep cpp compiler happy
    return 0;
}

int32_t FDSP_ConfigPathReqHandler::CreateVol(
    ::FDS_ProtocolInterface::FDSP_MsgHdrTypePtr& fdsp_msg,
    ::FDS_ProtocolInterface::FDSP_CreateVolTypePtr& crt_vol_req) {
    Error err(ERR_OK);
    LOGNOTIFY << "Received create volume " << crt_vol_req->vol_name
              << " from " << fdsp_msg->src_node_name  << " node uuid: "
              << std::hex << fdsp_msg->src_service_uuid.uuid << std::dec;

    try {
        OM_NodeDomainMod *domain = OM_NodeDomainMod::om_local_domain();
        OM_NodeContainer *local = OM_NodeDomainMod::om_loc_domain_ctrl();
        if (domain->om_local_domain_up()) {
            err = local->om_create_vol(fdsp_msg, crt_vol_req, nullptr);
        } else {
            LOGWARN << "OM Local Domain is not up yet, rejecting volume "
                    << " create; try later";
            err = Error(ERR_NOT_READY);
        }
    }
    catch(...) {
        LOGERROR << "Orch Mgr encountered exception while "
                 << "processing create volume";
        err = Error(ERR_NETWORK_TRANSPORT);  // only transport throws exceptions
    }

    return err.GetErrno();
}

int32_t FDSP_ConfigPathReqHandler::SnapVol(
    const ::FDS_ProtocolInterface::FDSP_MsgHdrType& fdsp_msg,
    const ::FDS_ProtocolInterface::FDSP_CreateVolType& snap_vol_req) {
    // Don't do anything here. This stub is just to keep cpp compiler happy
    return 0;
}

int32_t FDSP_ConfigPathReqHandler::SnapVol(
    ::FDS_ProtocolInterface::FDSP_MsgHdrTypePtr& fdsp_msg,
    ::FDS_ProtocolInterface::FDSP_CreateVolTypePtr& snap_vol_req) {
    Error err(ERR_OK);
    LOGNOTIFY << "Received Snap volume " << snap_vol_req->vol_name
              << " from " << fdsp_msg->src_node_name  << " node uuid: "
              << std::hex << fdsp_msg->src_service_uuid.uuid << std::dec;

    try {
        OM_NodeDomainMod *domain = OM_NodeDomainMod::om_local_domain();
        OM_NodeContainer *local = OM_NodeDomainMod::om_loc_domain_ctrl();
        if (domain->om_local_domain_up()) {
            err = local->om_snap_vol(fdsp_msg, snap_vol_req);
        } else {
            LOGWARN << "OM Local Domain is not up yet, rejecting snap "
                    << " create; try later";
            err = Error(ERR_NOT_READY);
        }
    }
    catch(...) {
        LOGERROR << "Orch Mgr encountered exception while "
                 << "processing snap volume";
        err = Error(ERR_NETWORK_TRANSPORT);  // only transport throws exceptions
    }

    return err.GetErrno();
}


int32_t FDSP_ConfigPathReqHandler::DeleteVol(
    const ::FDS_ProtocolInterface::FDSP_MsgHdrType& fdsp_msg,
    const ::FDS_ProtocolInterface::FDSP_DeleteVolType& del_vol_req) {
    // Don't do anything here. This stub is just to keep cpp compiler happy
    return 0;
}

int32_t FDSP_ConfigPathReqHandler::DeleteVol(
    ::FDS_ProtocolInterface::FDSP_MsgHdrTypePtr& fdsp_msg,
    ::FDS_ProtocolInterface::FDSP_DeleteVolTypePtr& del_vol_req) {

    Error err(ERR_OK);
    try {
        // no need to check if local domain is up, because vol create
        // would be rejected so om_delete_vol will return error
        OM_NodeContainer *local = OM_NodeDomainMod::om_loc_domain_ctrl();
        err = local->om_delete_vol(fdsp_msg, del_vol_req);
    }
    catch(...) {
        LOGERROR << "Orch Mgr encountered exception while "
                 << "processing delete volume";
        err = Error(ERR_NETWORK_TRANSPORT);  // only transport throws exceptions
    }

    return err.GetErrno();
}

int32_t FDSP_ConfigPathReqHandler::ModifyVol(
    const ::FDS_ProtocolInterface::FDSP_MsgHdrType& fdsp_msg,
    const ::FDS_ProtocolInterface::FDSP_ModifyVolType& mod_vol_req) {
    // Don't do anything here. This stub is just to keep cpp compiler happy
    return 0;
}

int32_t FDSP_ConfigPathReqHandler::ModifyVol(
    ::FDS_ProtocolInterface::FDSP_MsgHdrTypePtr& fdsp_msg,
    ::FDS_ProtocolInterface::FDSP_ModifyVolTypePtr& mod_vol_req) {
    Error err(ERR_OK);
    LOGNOTIFY << "Received modify volume " << (mod_vol_req->vol_desc).vol_name
              << " from " << fdsp_msg->src_node_name  << " node uuid: "
              << std::hex << fdsp_msg->src_service_uuid.uuid << std::dec;

    try {
        // no need to check if local domain is up, because volume create
        // would be rejected so om_modify_vol will return error in that case
        OM_NodeContainer *local = OM_NodeDomainMod::om_loc_domain_ctrl();
        err = local->om_modify_vol(mod_vol_req);
    }
    catch(...) {
        LOGERROR << "Orch Mgr encountered exception while "
                 << "processing modify volume";
        err = Error(ERR_NETWORK_TRANSPORT);  // only transport throws
    }

    return err.GetErrno();
}

int32_t FDSP_ConfigPathReqHandler::CreatePolicy(
    const ::FDS_ProtocolInterface::FDSP_MsgHdrType& fdsp_msg,
    const ::FDS_ProtocolInterface::FDSP_CreatePolicyType& crt_pol_req) {
    // Don't do anything here. This stub is just to keep cpp compiler happy
    return 0;
}

int32_t FDSP_ConfigPathReqHandler::CreatePolicy(
    ::FDS_ProtocolInterface::FDSP_MsgHdrTypePtr& fdsp_msg,
    ::FDS_ProtocolInterface::FDSP_CreatePolicyTypePtr& crt_pol_req) {

    int err = 0;
    try {
        err = orchMgr->CreatePolicy(fdsp_msg, crt_pol_req);
    }
    catch(...) {
        LOGERROR << "Orch Mgr encountered exception while "
                 << "processing create policy";
        return -1;
    }

    return err;
}

int32_t FDSP_ConfigPathReqHandler::DeletePolicy(
    const ::FDS_ProtocolInterface::FDSP_MsgHdrType& fdsp_msg,
    const ::FDS_ProtocolInterface::FDSP_DeletePolicyType& del_pol_req) {
    // Don't do anything here. This stub is just to keep cpp compiler happy
    return 0;
}

int32_t FDSP_ConfigPathReqHandler::DeletePolicy(
    ::FDS_ProtocolInterface::FDSP_MsgHdrTypePtr& fdsp_msg,
    ::FDS_ProtocolInterface::FDSP_DeletePolicyTypePtr& del_pol_req) {

    int err = 0;
    try {
        err = orchMgr->DeletePolicy(fdsp_msg, del_pol_req);
    }
    catch(...) {
        LOGERROR << "Orch Mgr encountered exception while "
                 << "processing delete policy";
        return -1;
    }

    return err;
}

int32_t FDSP_ConfigPathReqHandler::ModifyPolicy(
    const ::FDS_ProtocolInterface::FDSP_MsgHdrType& fdsp_msg,
    const ::FDS_ProtocolInterface::FDSP_ModifyPolicyType& mod_pol_req) {
    // Don't do anything here. This stub is just to keep cpp compiler happy
    return 0;
}

int32_t FDSP_ConfigPathReqHandler::ModifyPolicy(
    ::FDS_ProtocolInterface::FDSP_MsgHdrTypePtr& fdsp_msg,
    ::FDS_ProtocolInterface::FDSP_ModifyPolicyTypePtr& mod_pol_req) {

    int err = 0;
    try {
        err = orchMgr->ModifyPolicy(fdsp_msg, mod_pol_req);
    }
    catch(...) {
        LOGERROR << "Orch Mgr encountered exception while "
                 << "processing modify policy";
        return -1;
    }

    return err;
}

int32_t FDSP_ConfigPathReqHandler::AttachVol(
    const ::FDS_ProtocolInterface::FDSP_MsgHdrType& fdsp_msg,
    const ::FDS_ProtocolInterface::FDSP_AttachVolCmdType& atc_vol_req) {
    // Don't do anything here. This stub is just to keep cpp compiler happy
    return 0;
}

int32_t FDSP_ConfigPathReqHandler::AttachVol(
    ::FDS_ProtocolInterface::FDSP_MsgHdrTypePtr& fdsp_msg,
    ::FDS_ProtocolInterface::FDSP_AttachVolCmdTypePtr& atc_vol_req) {
    Error err(ERR_OK);
    LOGNOTIFY << "Received attach volume " << atc_vol_req->vol_name
              << " from " << fdsp_msg->src_node_name  << " node uuid: "
              << std::hex << fdsp_msg->src_service_uuid.uuid << std::dec;

    try {
        // no need to check if local domain is up, because volume create
        // would fail in that case so om_attach_vol would return error too
        OM_NodeContainer *local = OM_NodeDomainMod::om_loc_domain_ctrl();
        err = local->om_attach_vol(fdsp_msg, atc_vol_req);
    }
    catch(...) {
        LOGERROR << "Orch Mgr encountered exception while "
                 << "processing attach volume";
        err = Error(ERR_NETWORK_TRANSPORT);
    }

    return err.GetErrno();
}

int32_t FDSP_ConfigPathReqHandler::DetachVol(
    const ::FDS_ProtocolInterface::FDSP_MsgHdrType& fdsp_msg,
    const ::FDS_ProtocolInterface::FDSP_AttachVolCmdType& dtc_vol_req) {
    // Don't do anything here. This stub is just to keep cpp compiler happy
    return 0;
}

int32_t FDSP_ConfigPathReqHandler::DetachVol(
    ::FDS_ProtocolInterface::FDSP_MsgHdrTypePtr& fdsp_msg,
    ::FDS_ProtocolInterface::FDSP_AttachVolCmdTypePtr& dtc_vol_req) {
    Error err(ERR_OK);
    LOGNOTIFY << "Received detach volume " << dtc_vol_req->vol_name
              << " from " << fdsp_msg->src_node_name << " node uuid: "
              << std::hex << fdsp_msg->src_service_uuid.uuid << std::dec;
    try {
        OM_NodeContainer *local = OM_NodeDomainMod::om_loc_domain_ctrl();
        err = local->om_detach_vol(fdsp_msg, dtc_vol_req);
    }
    catch(...) {
        LOGERROR << "Orch Mgr encountered exception while "
                 << "processing detach volume";
        err = Error(ERR_NETWORK_TRANSPORT);  // only transport throws
    }

    return err.GetErrno();
}

int32_t FDSP_ConfigPathReqHandler::AssociateRespCallback(
    const int64_t ident) {
    // Don't do anything here. This stub is just to keep cpp compiler happy
    return 0;
}

int32_t FDSP_ConfigPathReqHandler::AssociateRespCallback(
    boost::shared_ptr<int64_t>& ident) {
    return 0;
}

int32_t FDSP_ConfigPathReqHandler::CreateDomain(
    const ::FDS_ProtocolInterface::FDSP_MsgHdrType& fdsp_msg,
    const ::FDS_ProtocolInterface::FDSP_CreateDomainType& crt_dom_req) {
    // Don't do anything here. This stub is just to keep cpp compiler happy
    return 0;
}

int32_t FDSP_ConfigPathReqHandler::CreateDomain(
    ::FDS_ProtocolInterface::FDSP_MsgHdrTypePtr& fdsp_msg,
    ::FDS_ProtocolInterface::FDSP_CreateDomainTypePtr& crt_dom_req) {

    int err = 0;
    try {
        OM_NodeDomainMod *domain = OM_NodeDomainMod::om_local_domain();
        err = domain->om_create_domain(crt_dom_req);
    }
    catch(...) {
        LOGERROR << "Orch Mgr encountered exception while "
                 << "processing create domain";
        return -1;
    }

    return err;
}

int32_t FDSP_ConfigPathReqHandler::RemoveServices(
    const ::FDS_ProtocolInterface::FDSP_MsgHdrType& fdsp_msg,
    const ::FDS_ProtocolInterface::FDSP_RemoveServicesType& rm_svc_req) {
    // Don't do anything here. This stub is just to keep cpp compiler happy
    return 0;
}

int32_t FDSP_ConfigPathReqHandler::RemoveServices(
    ::FDS_ProtocolInterface::FDSP_MsgHdrTypePtr& fdsp_msg,
    ::FDS_ProtocolInterface::FDSP_RemoveServicesTypePtr& rm_svc_req) {

    Error err(ERR_OK);
    try {
        LOGNORMAL << "Received remove services for node" << rm_svc_req->node_name
		  << " UUID " << std::hex << rm_svc_req->node_uuid.uuid << std::dec
                  << " remove am ? " << rm_svc_req->remove_am
                  << " remove sm ? " << rm_svc_req->remove_sm
                  << " remove dm ? " << rm_svc_req->remove_dm;

        OM_NodeDomainMod *domain = OM_NodeDomainMod::om_local_domain();
        NodeUuid node_uuid;
        if (rm_svc_req->node_uuid.uuid > 0) {
            node_uuid = rm_svc_req->node_uuid.uuid;
        }
        // else ok if 0, will search by name

        err = domain->om_del_services(rm_svc_req->node_uuid.uuid,
                                      rm_svc_req->node_name,
                                      rm_svc_req->remove_sm,
                                      rm_svc_req->remove_dm,
                                      rm_svc_req->remove_am);

        if (!err.ok()) {
            LOGERROR << "RemoveServices: Failed to remove services for node "
                     << rm_svc_req->node_name << ", uuid "
                     << std::hex << rm_svc_req->node_uuid.uuid
                     << std::dec << ", result: " << err.GetErrstr();
        }
    }
    catch(...) {
        LOGERROR << "Orch Mgr encountered exception while "
                 << "processing rmv node";
        err = Error(ERR_NOT_FOUND);
    }

    return err.GetErrno();
}

int32_t FDSP_ConfigPathReqHandler::DeleteDomain(
    const ::FDS_ProtocolInterface::FDSP_MsgHdrType& fdsp_msg,
    const ::FDS_ProtocolInterface::FDSP_CreateDomainType& del_dom_req) {
    // Don't do anything here. This stub is just to keep cpp compiler happy
    return 0;
}

int32_t FDSP_ConfigPathReqHandler::DeleteDomain(
    ::FDS_ProtocolInterface::FDSP_MsgHdrTypePtr& fdsp_msg,
    ::FDS_ProtocolInterface::FDSP_CreateDomainTypePtr& del_dom_req) {

    int err = 0;
    try {
        OM_NodeDomainMod *domain = OM_NodeDomainMod::om_local_domain();
        err = domain->om_delete_domain(del_dom_req);
    }
    catch(...) {
        LOGERROR << "Orch Mgr encountered exception while "
                 << "processing create domain";
        return -1;
    }

    return err;
}

int32_t FDSP_ConfigPathReqHandler::ShutdownDomain(
    const ::FDS_ProtocolInterface::FDSP_MsgHdrType& fdsp_msg,
    const ::FDS_ProtocolInterface::FDSP_ShutdownDomainType& shut_dom_req) {
    // Don't do anything here. This stub is just to keep cpp compiler happy
    return 0;
}

int32_t FDSP_ConfigPathReqHandler::ShutdownDomain(
    ::FDS_ProtocolInterface::FDSP_MsgHdrTypePtr& fdsp_msg,
    ::FDS_ProtocolInterface::FDSP_ShutdownDomainTypePtr& shut_dom_req) {

    Error err(ERR_OK);
    try {
        OM_NodeDomainMod *domain = OM_NodeDomainMod::om_local_domain();
        err = domain->om_shutdown_domain();
    }
    catch(...) {
        LOGERROR << "Orch Mgr encountered exception while "
                 << "processing shutdown domain";
        return -1;
    }

    return err.GetErrno();
}

int32_t FDSP_ConfigPathReqHandler::SetThrottleLevel(
    const ::FDS_ProtocolInterface::FDSP_MsgHdrType& fdsp_msg,
    const ::FDS_ProtocolInterface::FDSP_ThrottleMsgType& throttle_msg) {
    // Don't do anything here. This stub is just to keep cpp compiler happy
    return 0;
}

int32_t FDSP_ConfigPathReqHandler::SetThrottleLevel(
    ::FDS_ProtocolInterface::FDSP_MsgHdrTypePtr& fdsp_msg,
    ::FDS_ProtocolInterface::FDSP_ThrottleMsgTypePtr& throttle_msg) {

    int err = 0;
    try {
        assert((throttle_msg->throttle_level >= -10) &&
               (throttle_msg->throttle_level <= 10));
        OM_NodeContainer *local = OM_NodeDomainMod::om_loc_domain_ctrl();
        local->om_set_throttle_lvl(throttle_msg->throttle_level);
    }
    catch(...) {
        LOGERROR << "Orch Mgr encountered exception while "
                 << "processing set throttle level";
        return -1;
    }

    return err;
}

void FDSP_ConfigPathReqHandler::GetVolInfo(
    ::FDS_ProtocolInterface::FDSP_VolumeDescType& _return,
    const ::FDS_ProtocolInterface::FDSP_MsgHdrType& fdsp_msg,
    const ::FDS_ProtocolInterface::FDSP_GetVolInfoReqType& vol_info_req) {
    // Don't do anything here. This stub is just to keep cpp compiler happy
}

void FDSP_ConfigPathReqHandler::GetVolInfo(
    ::FDS_ProtocolInterface::FDSP_VolumeDescType& _return,
    ::FDS_ProtocolInterface::FDSP_MsgHdrTypePtr& fdsp_msg,
    ::FDS_ProtocolInterface::FDSP_GetVolInfoReqTypePtr& vol_info_req) {
    LOGNOTIFY << "Received Get volume info request for volume: "
              << vol_info_req->vol_name;

    OM_NodeContainer *local = OM_NodeDomainMod::om_loc_domain_ctrl();
    VolumeContainer::pointer vol_list = local->om_vol_mgr();
    VolumeInfo::pointer vol = vol_list->get_volume(vol_info_req->vol_name);
    if (vol) {
        vol->vol_fmt_desc_pkt(&_return);
        LOGNOTIFY << "Volume " << vol_info_req->vol_name
                  << " -- min iops " << _return.iops_min << ",max iops "
                  << _return.iops_max << ", prio " << _return.rel_prio
                  << " media policy " << _return.mediaPolicy;
    } else {
        LOGWARN << "Volume " << vol_info_req->vol_name << " not found";
        FDS_ProtocolInterface::FDSP_VolumeNotFound except;
        except.message = std::string("Volume not found");
        throw except;
    }
}

int32_t FDSP_ConfigPathReqHandler::ActivateAllNodes(
    const ::FDS_ProtocolInterface::FDSP_MsgHdrType& fdsp_msg,
    const ::FDS_ProtocolInterface::FDSP_ActivateAllNodesType& act_node_msg) {
    // Don't do anything here. This stub is just to keep cpp compiler happy
    return 0;
}

int32_t FDSP_ConfigPathReqHandler::ActivateAllNodes(
    ::FDS_ProtocolInterface::FDSP_MsgHdrTypePtr& fdsp_msg,
    ::FDS_ProtocolInterface::FDSP_ActivateAllNodesTypePtr& act_node_msg) {
    int err = 0;
    try {
        int domain_id = act_node_msg->domain_id;
        // use default domain for now
        OM_NodeContainer *local = OM_NodeDomainMod::om_loc_domain_ctrl();

        LOGNORMAL << "Received Activate All Nodes Req for domain " << domain_id;

        local->om_cond_bcast_activate_services(act_node_msg->activate_sm,
                                               act_node_msg->activate_dm,
                                               act_node_msg->activate_am);
    }
    catch(...) {
        LOGERROR << "Orch Mgr encountered exception while "
                 << "processing activate all nodes";
        return -1;
    }

    return err;
}

int32_t FDSP_ConfigPathReqHandler::ActivateNode(
    const ::FDS_ProtocolInterface::FDSP_MsgHdrType& fdsp_msg,
    const ::FDS_ProtocolInterface::FDSP_ActivateOneNodeType& act_node_msg) {
    // Don't do anything here. This stub is just to keep cpp compiler happy
    return 0;
}

int32_t FDSP_ConfigPathReqHandler::ActivateNode(
    ::FDS_ProtocolInterface::FDSP_MsgHdrTypePtr& fdsp_msg,
    ::FDS_ProtocolInterface::FDSP_ActivateOneNodeTypePtr& act_node_msg) {
    Error err(ERR_OK);
    try {
        int domain_id = act_node_msg->domain_id;
        // use default domain for now
        OM_NodeContainer *local = OM_NodeDomainMod::om_loc_domain_ctrl();
        NodeUuid node_uuid((act_node_msg->node_uuid).uuid);

        LOGNORMAL << "Received Activate Node Req for domain " << domain_id
                  << " node uuid " << std::hex
                  << node_uuid.uuid_get_val() << std::dec;

        err = local->om_activate_node_services(node_uuid,
                                               act_node_msg->activate_sm,
                                               act_node_msg->activate_dm,
                                               act_node_msg->activate_am);
    }
    catch(...) {
        LOGERROR << "Orch Mgr encountered exception while "
                 << "processing activate all nodes";
        err = Error(ERR_NOT_FOUND);  // need some better error code
    }

    return err.GetErrno();
}

int32_t FDSP_ConfigPathReqHandler::ScavengerCommand(
    const ::FDS_ProtocolInterface::FDSP_MsgHdrType& fdsp_msg,
    const ::FDS_ProtocolInterface::FDSP_ScavengerType& gc_msg) {
    // Don't do anything here. This stub is just to keep cpp compiler happy
    return 0;
}

int32_t FDSP_ConfigPathReqHandler::ScavengerCommand(
    ::FDS_ProtocolInterface::FDSP_MsgHdrTypePtr& fdsp_msg,
    ::FDS_ProtocolInterface::FDSP_ScavengerTypePtr& gc_msg) {
    switch (gc_msg->cmd) {
        case FDS_ProtocolInterface::FDSP_SCAVENGER_ENABLE:
            LOGNOTIFY << "Received scavenger enable command";
            break;
        case FDS_ProtocolInterface::FDSP_SCAVENGER_DISABLE:
            LOGNOTIFY << "Received scavenger disable command";
            break;
        case FDS_ProtocolInterface::FDSP_SCAVENGER_START:
            LOGNOTIFY << "Received scavenger start command";
            break;
        case FDS_ProtocolInterface::FDSP_SCAVENGER_STOP:
            LOGNOTIFY << "Received scavenger stop command";
            break;
    };

    // send scavenger start message to all SMs
    OM_NodeContainer *local = OM_NodeDomainMod::om_loc_domain_ctrl();
    local->om_bcast_scavenger_cmd(gc_msg->cmd);
    return 0;
}

void FDSP_ConfigPathReqHandler::ListServices(
    std::vector<FDSP_Node_Info_Type> & ret,
    const FDSP_MsgHdrType& fdsp_msg) {
    // Do nothing
}

static void add_to_vector(std::vector<fpi::FDSP_Node_Info_Type> &vec,  // NOLINT
                          NodeAgent::pointer ptr) {
    Platform     *plat;
    node_data_t   ndata;
    fds_uint32_t  base;

    base = ptr->node_base_port();
    plat = Platform::platf_singleton();

    ptr->node_info_frm_shm(&ndata);
    NodeUuid uuid = ndata.nd_node_uuid;
    fpi::FDSP_Node_Info_Type nodeInfo = fpi::FDSP_Node_Info_Type();
    nodeInfo.node_uuid = ndata.nd_node_uuid;
    nodeInfo.service_uuid = ndata.nd_service_uuid;
    nodeInfo.node_name = ptr->get_node_name();
    nodeInfo.node_type = ndata.nd_svc_type;
    nodeInfo.node_state = ptr->node_state();
    nodeInfo.ip_lo_addr = netSession::ipString2Addr(ptr->get_ip_str());
    nodeInfo.control_port = plat->plf_get_my_ctrl_port(base);
    nodeInfo.data_port = plat->plf_get_my_data_port(base);
    nodeInfo.migration_port = plat->plf_get_my_migration_port(base);
    vec.push_back(nodeInfo);
}

void FDSP_ConfigPathReqHandler::ListServices(
    std::vector<FDSP_Node_Info_Type> &vec,
    boost::shared_ptr<FDSP_MsgHdrType>& fdsp_msg) {

    OM_NodeContainer *local = OM_NodeDomainMod::om_loc_domain_ctrl();

    local->om_sm_nodes()->
        agent_foreach<std::vector<FDSP_Node_Info_Type> &>(vec, add_to_vector);
    local->om_am_nodes()->
        agent_foreach<std::vector<FDSP_Node_Info_Type> &>(vec, add_to_vector);
    local->om_dm_nodes()->
        agent_foreach<std::vector<FDSP_Node_Info_Type> &>(vec, add_to_vector);
    local->om_pm_nodes()->
        agent_foreach<std::vector<FDSP_Node_Info_Type> &>(vec, add_to_vector);
}

void FDSP_ConfigPathReqHandler::ListVolumes(
    std::vector<FDS_ProtocolInterface::FDSP_VolumeDescType> & _return,
    const ::FDS_ProtocolInterface::FDSP_MsgHdrType& fdsp_msg) {
    // Don't do anything here. This stub is just to keep cpp compiler happy
}

static void
add_vol_to_vector(std::vector<FDS_ProtocolInterface::FDSP_VolumeDescType> &vec,  // NOLINT
                  VolumeInfo::pointer vol) {
    FDS_ProtocolInterface::FDSP_VolumeDescType voldesc;
    vol->vol_fmt_desc_pkt(&voldesc);
    FDS_PLOG_SEV(g_fdslog, fds_log::notification)
            << "Volume in list: " << voldesc.vol_name << ":"
            << std::hex << voldesc.volUUID << std::dec
            << "min iops " << voldesc.iops_min << ",max iops "
            << voldesc.iops_max << ", prio " << voldesc.rel_prio;
    vec.push_back(voldesc);
}

void FDSP_ConfigPathReqHandler::ListVolumes(
    std::vector<FDS_ProtocolInterface::FDSP_VolumeDescType> & _return,
    ::FDS_ProtocolInterface::FDSP_MsgHdrTypePtr& fdsp_msg) {
    LOGNOTIFY<< "OM received ListVolumes message";
    OM_NodeContainer *local = OM_NodeDomainMod::om_loc_domain_ctrl();
    VolumeContainer::pointer vols = local->om_vol_mgr();
    // list volumes that are not in 'delete pending' state
    vols->vol_up_foreach<std::vector<FDSP_VolumeDescType> &>(_return, add_vol_to_vector);
}

FDSP_OMControlPathReqHandler::FDSP_OMControlPathReqHandler(
    OrchMgr *oMgr) {
    orchMgr = oMgr;

    // REGISTER_FDSP_MSG_HANDLER(fpi::CtrlNotifyMigrationStatus, migrationDone);
}

void FDSP_OMControlPathReqHandler::RegisterNode(
    const ::FDS_ProtocolInterface::FDSP_MsgHdrType& fdsp_msg,
    const ::FDS_ProtocolInterface::FDSP_RegisterNodeType& reg_node_req) {
    // Don't do anything here. This stub is just to keep cpp compiler happy
}

void FDSP_OMControlPathReqHandler::RegisterNode(
    ::FDS_ProtocolInterface::FDSP_MsgHdrTypePtr& fdsp_msg,
    ::FDS_ProtocolInterface::FDSP_RegisterNodeTypePtr& reg_node_req) {
    OM_NodeDomainMod *domain = OM_NodeDomainMod::om_local_domain();
    NodeUuid new_node_uuid;

    if (reg_node_req->node_uuid.uuid == 0) {
        LOGERROR << "Refuse to register a node without valid uuid: node_type "
            << reg_node_req->node_type << ", name " << reg_node_req->node_name;
        return;
    }
    new_node_uuid.uuid_set_type(reg_node_req->node_uuid.uuid, reg_node_req->node_type);
    Error err = domain->om_reg_node_info(new_node_uuid, reg_node_req);
    if (!err.ok()) {
        LOGERROR << "Node Registration failed for "
                 << reg_node_req->node_name << ":" << std::hex
                 << new_node_uuid.uuid_get_val() << std::dec
                 << " node_type " << reg_node_req->node_type
                 << ", result: " << err.GetErrstr();
        return;
    }
    LOGNORMAL << "Done Registered new node " << reg_node_req->node_name << std::hex
              << ", node uuid " << reg_node_req->node_uuid.uuid
              << ", svc uuid " << new_node_uuid.uuid_get_val()
              << ", node type " << reg_node_req->node_type << std::dec;
}

void FDSP_OMControlPathReqHandler::NotifyQueueFull(
    const ::FDS_ProtocolInterface::FDSP_MsgHdrType& fdsp_msg,
    const ::FDS_ProtocolInterface::FDSP_NotifyQueueStateType& queue_state_info){
    // Don't do anything here. This stub is just to keep cpp compiler happy
}

void FDSP_OMControlPathReqHandler::NotifyQueueFull(
    ::FDS_ProtocolInterface::FDSP_MsgHdrTypePtr& fdsp_msg,
    ::FDS_ProtocolInterface::FDSP_NotifyQueueStateTypePtr& queue_state_info) {
    orchMgr->NotifyQueueFull(fdsp_msg, queue_state_info);
}

void FDSP_OMControlPathReqHandler::migrationDone(boost::shared_ptr<fpi::AsyncHdr>& hdr,
        boost::shared_ptr<fpi::CtrlNotifyMigrationStatus>& status) {
}

}  // namespace fds
