/*
 * Copyright 2014 by Formation Data Systems, Inc.
 */
#include <string>
#include <platform.h>
#include <fds_process.h>
#include <disk.h>
#include <net-platform.h>

namespace fds {

NodePlatform gl_NodePlatform;

// -------------------------------------------------------------------------------------
// Node Specific Platform
// -------------------------------------------------------------------------------------
NodePlatform::NodePlatform()
    : Platform("Node-Platform",
               FDSP_PLATFORM,
               new DomainNodeInv("Node-Platform-NodeInv",
                                 NULL,
                                 new SmContainer(FDSP_PLATFORM),
                                 new DmContainer(FDSP_PLATFORM),
                                 new AmContainer(FDSP_PLATFORM),
                                 new PlatAgentContainer(),
                                 new OmContainer(FDSP_PLATFORM)),
               new DomainClusterMap("Node-Platform-ClusMap",
                                 NULL,
                                 new SmContainer(FDSP_PLATFORM),
                                 new DmContainer(FDSP_PLATFORM),
                                 new AmContainer(FDSP_PLATFORM),
                                 new PlatAgentContainer(),
                                 new OmContainer(FDSP_PLATFORM)),
               new DomainResources("Node-Resources"),
               NULL)
{
    plf_node_evt  = new NodePlatEvent(plf_resources, plf_clus_map, this);
    plf_vol_evt   = new VolPlatEvent(plf_resources, plf_clus_map, this);
    plf_node_type = FDSP_PLATFORM;

    Platform::platf_assign_singleton(&gl_NodePlatform);
}

void
NodePlatform::mod_load_from_config()
{
}

int
NodePlatform::mod_init(SysParams const *const param)
{
    Platform::mod_init(param);
    FdsConfigAccessor conf(g_fdsprocess->get_conf_helper());

    disk_ctrl        = DiskPlatModule::dsk_plat_singleton();
    plf_my_ctrl_port = conf.get_abs<int>("fds.plat.control_port");
    plf_my_conf_port = plf_conf_port(plf_my_ctrl_port);
    plf_my_data_port = plf_data_port(plf_my_ctrl_port);
    plf_my_migr_port = plf_migration_port(plf_my_ctrl_port);
    plf_my_ip        = util::get_local_ip();
    plf_my_node_name = conf.get_abs<std::string>("fds.plat.id", "auto");
    return 0;
}

void
NodePlatform::mod_startup()
{
    Platform::mod_startup();
}

void
NodePlatform::mod_shutdown()
{
    Platform::mod_shutdown();
}

void
NodePlatform::plf_start_node_services(const fpi::FDSP_ActivateNodeTypePtr &msg)
{
    plf_process->plf_start_node_services(msg);
}

/**
 * Required factory methods.
 */
PlatRpcReqt *
NodePlatform::plat_creat_reqt_disp()
{
    return new PlatformRpcReqt(this);
}

PlatRpcResp *
NodePlatform::plat_creat_resp_disp()
{
    return new PlatformRpcResp(this);
}

// RPC handlers
// --------------------------------------------------------------------------------------
PlatformRpcReqt::PlatformRpcReqt(const Platform *plf) : PlatRpcReqt(plf) {}
PlatformRpcReqt::~PlatformRpcReqt() {}

void
PlatformRpcReqt::NotifyNodeAdd(fpi::FDSP_MsgHdrTypePtr     &msg_hdr,
                               fpi::FDSP_Node_Info_TypePtr &node_info)
{
    std::cout << "Got it!" << std::endl;
}

void
PlatformRpcReqt::NotifyNodeActive(fpi::FDSP_MsgHdrTypePtr       &hdr,
                                  fpi::FDSP_ActivateNodeTypePtr &info)
{
    std::cout << "Received message to activate node" << std::endl;
    Platform     *plat = const_cast<Platform *>(plf_mgr);
    NodePlatform *node = static_cast<NodePlatform *>(plat);
    node->plf_start_node_services(info);
}

void
PlatformRpcReqt::NotifyNodeRmv(fpi::FDSP_MsgHdrTypePtr     &msg_hdr,
                               fpi::FDSP_Node_Info_TypePtr &node_info)
{
}

PlatformRpcResp::PlatformRpcResp(const Platform *plf) : PlatRpcResp(plf) {}
PlatformRpcResp::~PlatformRpcResp() {}

void
PlatformRpcResp::RegisterNodeResp(fpi::FDSP_MsgHdrTypePtr       &hdr,
                                  fpi::FDSP_RegisterNodeTypePtr &node)
{
    std::cout << "Got it resp!" << std::endl;
}

}  // namespace fds
