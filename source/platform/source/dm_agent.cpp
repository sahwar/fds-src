/*
 * Copyright 2014 by Formation Data Systems, Inc.
 */

#include <net/net-service-tmpl.hpp>
#include "platform/platform.h"
#include "platform/dm_svc_ep.h"

namespace fds
{
    // --------------------------------------------------------------------------------------
    // DmAgent
    // --------------------------------------------------------------------------------------
    DmAgent::~DmAgent()
    {
    }

    DmAgent::DmAgent(const NodeUuid &uuid) : NodeAgent(uuid)
    {
        node_svc_type = fpi::FDSP_DATA_MGR;
#if 0
        dm_ep_svc     = Platform::platf_singleton()->plat_new_dm_svc(this, 0, 0);
        NetMgr::ep_mgr_singleton()->ep_register(dm_ep_svc, false);
#endif
    }

    // agent_ep_plugin
    // ---------------
    //
    EpEvtPlugin::pointer DmAgent::agent_ep_plugin()
    {
        return nullptr;
    }

    // agent_ep_svc
    // ------------
    //
    DmSvcEp::pointer DmAgent::agent_ep_svc()
    {
        return nullptr;
    }

    // agent_bind_ep
    // -------------
    //
    void DmAgent::agent_bind_ep()
    {
#if 0
        EpSvcImpl::pointer    ep = NetPlatform::nplat_singleton()->nplat_my_ep();
        NodeAgent::agent_bind_ep(ep, dm_ep_svc);
#endif
    }
}  // namespace fds
