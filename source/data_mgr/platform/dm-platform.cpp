/*
 * Copyright 2014 by Formation Data Systems, Inc.
 */
#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <dm-platform.h>
#include <fds_process.h>

namespace fds {

DmPlatform gl_DmPlatform;

// -------------------------------------------------------------------------------------
// DM Platform Event Handler
// -------------------------------------------------------------------------------------
void
DmVolEvent::plat_evt_handler(const FDSP_MsgHdrTypePtr &hdr)
{
}

// -------------------------------------------------------------------------------------
// DM Specific Platform
// -------------------------------------------------------------------------------------
DmPlatform::DmPlatform()
    : Platform("DM-Platform",
               FDSP_DATA_MGR,
               new DomainNodeInv("DM-Platform-NodeInv",
                                 NULL,
                                 new SmContainer(FDSP_DATA_MGR),
                                 new DmContainer(FDSP_DATA_MGR),
                                 new AmContainer(FDSP_DATA_MGR),
                                 new PmContainer(FDSP_DATA_MGR),
                                 new OmContainer(FDSP_DATA_MGR)),
               new DomainClusterMap("DM-Platform-ClusMap",
                                 NULL,
                                 new SmContainer(FDSP_DATA_MGR),
                                 new DmContainer(FDSP_DATA_MGR),
                                 new AmContainer(FDSP_DATA_MGR),
                                 new PmContainer(FDSP_DATA_MGR),
                                 new OmContainer(FDSP_DATA_MGR)),
               new DomainResources("DM-Resources"),
               NULL)
{
    Platform::platf_assign_singleton(&gl_DmPlatform);
    plf_node_type = FDSP_DATA_MGR;
}

int
DmPlatform::mod_init(SysParams const *const param)
{
    FdsConfigAccessor conf(g_fdsprocess->get_conf_helper());

    Platform::mod_init(param);

    if (conf.get_abs<bool>("fds.dm.test_mode", false) == true) {
        plf_my_ctrl_port = conf.get_abs<int>("fds.dm.cp_port");
        plf_my_data_port = conf.get_abs<int>("fds.dm.port");
    }
    plf_om_ip_str    = conf.get_abs<std::string>("fds.dm.om_ip");
    plf_my_ip        = util::get_local_ip();
    plf_my_node_name = plf_my_ip;

    LOGNORMAL << "My ctrl port " << plf_my_ctrl_port
        << ", data port " << plf_my_data_port;

    plf_vol_evt  = new DmVolEvent(plf_resources, plf_clus_map, this);
    plf_node_evt = new NodePlatEvent(plf_resources, plf_clus_map, this);

    return 0;
}

void
DmPlatform::mod_startup()
{
}

void
DmPlatform::mod_shutdown()
{
}

// plf_reg_node_info
// -----------------
//
void
DmPlatform::plf_reg_node_info(const NodeUuid &uuid, const FdspNodeRegPtr msg)
{
    NodeAgent::pointer     new_node;
    DomainNodeInv::pointer local;

    local = plf_node_inventory();
    Error err = local->dc_register_node(uuid, msg, &new_node);
    if (err != ERR_OK) {
        fds_verify(0);
    }
    AgentContainer::pointer svc = local->dc_container_frm_msg(msg->node_type);
    svc->agent_handshake(plf_net_sess, plf_dpath_resp, new_node);
}

// Factory methods required for DM RPC.
//
PlatRpcReqt *
DmPlatform::plat_creat_reqt_disp()
{
    return new DmRpcReq(this);
}

PlatRpcResp *
DmPlatform::plat_creat_resp_disp()
{
    return new PlatRpcResp(this);
}

PlatDataPathResp *
DmPlatform::plat_creat_dpath_resp()
{
    return new PlatDataPathResp(this);
}

// --------------------------------------------------------------------------------------
// RPC handlers
// --------------------------------------------------------------------------------------
DmRpcReq::DmRpcReq(const Platform *plf) : PlatRpcReqt(plf) {}
DmRpcReq::~DmRpcReq() {}

void
DmRpcReq::NotifyAddVol(fpi::FDSP_MsgHdrTypePtr    &msg_hdr,
                       fpi::FDSP_NotifyVolTypePtr &msg)
{
}

void
DmRpcReq::NotifyRmVol(fpi::FDSP_MsgHdrTypePtr    &msg_hdr,
                      fpi::FDSP_NotifyVolTypePtr &msg)
{
}

void
DmRpcReq::NotifyModVol(fpi::FDSP_MsgHdrTypePtr    &msg_hdr,
                       fpi::FDSP_NotifyVolTypePtr &msg)
{
}

void
DmRpcReq::AttachVol(fpi::FDSP_MsgHdrTypePtr    &msg_hdr,
                    fpi::FDSP_AttachVolTypePtr &vol_msg)
{
}

void
DmRpcReq::DetachVol(fpi::FDSP_MsgHdrTypePtr    &msg_hdr,
                    fpi::FDSP_AttachVolTypePtr &vol_msg)
{
}

void
DmRpcReq::NotifyNodeAdd(fpi::FDSP_MsgHdrTypePtr     &msg_hdr,
                        fpi::FDSP_Node_Info_TypePtr &node_info)
{
}

void
DmRpcReq::NotifyNodeRmv(fpi::FDSP_MsgHdrTypePtr     &msg_hdr,
                        fpi::FDSP_Node_Info_TypePtr &node_info)
{
}

void
DmRpcReq::NotifyDLTUpdate(fpi::FDSP_MsgHdrTypePtr    &msg_hdr,
                          fpi::FDSP_DLT_Data_TypePtr &dlt_info)
{
}

void
DmRpcReq::NotifyDMTUpdate(fpi::FDSP_MsgHdrTypePtr &msg_hdr,  // NOLINT
                          fpi::FDSP_DMT_TypePtr   &dmt)
{
}

void
DmRpcReq::NotifyStartMigration(fpi::FDSP_MsgHdrTypePtr    &hdr,
                               fpi::FDSP_DLT_Data_TypePtr &dlt)
{
}

}  // namespace fds
