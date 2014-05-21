/*
 * Copyright 2014 by Formation Data Systems, Inc.
 */
#include <string>
#include <vector>
#include <ep-map.h>
#include <net/net-service-tmpl.hpp>
#include <platform/platform-lib.h>
#include <net-plat-shared.h>

namespace fds {

NetPlatSvc                   gl_NetPlatform("NetPlatform");
NetPlatform                 *gl_NetPlatSvc = &gl_NetPlatform;

/*
 * -----------------------------------------------------------------------------------
 * Exported module
 * -----------------------------------------------------------------------------------
 */
NetPlatform::NetPlatform(const char *name) : Module(name)
{
    static Module *net_plat_deps[] = {
        NULL
    };
    netmgr     = NULL;
    plat_lib   = NULL;
    mod_intern = net_plat_deps;
}

NetPlatSvc::~NetPlatSvc() {}
NetPlatSvc::NetPlatSvc(const char *name) : NetPlatform(name)
{
    plat_ep        = NULL;
    plat_ep_plugin = NULL;
    plat_ep_hdler  = NULL;
    plat_agent     = NULL;
}

int
NetPlatSvc::mod_init(SysParams const *const p)
{
    netmgr   = NetMgr::ep_mgr_singleton();
    plat_lib = Platform::platf_singleton();
    return Module::mod_init(p);
}

void
NetPlatSvc::mod_startup()
{
    Module::mod_startup();

    // Create the agent and register the ep
    plat_agent     = new DomainAgent(plat_lib->plf_my_node_uuid());
    plat_ep_hdler  = bo::shared_ptr<NetPlatHandler>(new NetPlatHandler(this));
    plat_ep_plugin = new PlatNetPlugin(this);
    plat_ep        = new PlatNetEp(
            plat_lib->plf_get_my_data_port(),     /* hack, need to consolidate ports */
            *plat_lib->plf_get_my_plf_svc_uuid(), /* bind to my platform lib svc  */
            NodeUuid(0ULL),                       /* pure server mode */
            bo::shared_ptr<fpi::PlatNetSvcProcessor>(
                new fpi::PlatNetSvcProcessor(plat_ep_hdler)),
            plat_ep_plugin);
    std::cout << "startup shared platform net svc" << std::endl;
}

void
NetPlatSvc::mod_enable_service()
{
    Module::mod_enable_service();

    // Regiser my node endpoint.
    netmgr->ep_register(plat_ep, false);
    if (!plat_lib->plf_is_om_node()) {
        plat_agent->pda_connect_domain(fpi::DomainID());
    }
}

void
NetPlatSvc::mod_shutdown()
{
    Module::mod_shutdown();
}

// nplat_domain_rpc
// ----------------
// Get the RPC handles needed to contact the master platform services.
//
EpSvcHandle::pointer
NetPlatSvc::nplat_domain_rpc(const fpi::DomainID &id)
{
    return plat_agent->pda_rpc_handle();
}

// nplat_my_ep
// -----------
//
EpSvc::pointer
NetPlatSvc::nplat_my_ep()
{
    return plat_ep;
}

/*
 * -----------------------------------------------------------------------------------
 * Domain Agent
 * -----------------------------------------------------------------------------------
 */
DomainAgent::DomainAgent(const NodeUuid &uuid)
    : PmAgent(uuid), agt_domain_evt(this), agt_domain_ep(NULL) {}

/**
 * pda_connect_domain
 * ------------------
 */
void
DomainAgent::pda_connect_domain(const fpi::DomainID &id)
{
    int                port;
    NetPlatform       *net;
    PlatNetEpPtr       eptr;

    if (agt_domain_ep != NULL) {
        return;
    }
    net  = NetPlatform::nplat_singleton();
    eptr = ep_cast_ptr<PlatNetEp>(net->nplat_my_ep());
    fds_verify(eptr != NULL);

    std::string const *const om_ip = net->nplat_domain_master(&port);
    eptr->ep_new_handle(eptr, port, *om_ip, &agt_domain_ep, &agt_domain_evt);
}

/**
 * pda_update_binding
 * ------------------
 */
void
DomainAgent::pda_update_binding(const struct ep_map_rec *rec, int cnt)
{
}

/**
 * ep_connected
 * ------------
 */
void
DomainAgentPlugin::ep_connected()
{
    std::vector<UuidBindMsg> ret;
    auto rpc = pda_agent->pda_rpc();

    std::cout << "Agent domain connected..." << std::endl;
    rpc->allUuidBinding(ret, UuidBindMsg());
}

/**
 * ep_down
 * -------
 */
void
DomainAgentPlugin::ep_down()
{
}

/**
 * svc_up
 * ------
 */
void
DomainAgentPlugin::svc_up(EpSvcHandle::pointer handle)
{
}

/**
 * svc_down
 * --------
 */
void
DomainAgentPlugin::svc_down(EpSvc::pointer svc, EpSvcHandle::pointer handle)
{
}

/*
 * -----------------------------------------------------------------------------------
 * Endpoint Plugin
 * -----------------------------------------------------------------------------------
 */
PlatNetPlugin::PlatNetPlugin(NetPlatSvc *svc) : plat_svc(svc) {}

void
PlatNetPlugin::ep_connected()
{
}

void
PlatNetPlugin::ep_down()
{
}

void
PlatNetPlugin::svc_up(EpSvcHandle::pointer handle)
{
}

void
PlatNetPlugin::svc_down(EpSvc::pointer svc, EpSvcHandle::pointer handle)
{
}

/*
 * -----------------------------------------------------------------------------------
 * RPC Handlers
 * -----------------------------------------------------------------------------------
 */
void
NetPlatHandler::allUuidBinding(std::vector<fpi::UuidBindMsg>   &ret,
                               bo::shared_ptr<fpi::UuidBindMsg> &msg)
{
    std::cout << "all uuidBind there" << std::endl;
}

void
NetPlatHandler::notifyNodeInfo(std::vector<fpi::NodeInfoMsg>    &ret,
                               bo::shared_ptr<fpi::NodeInfoMsg> &inf)
{
}

void
NetPlatHandler::notifyNodeUp(fpi::RespHdr &ret, bo::shared_ptr<fpi::NodeInfoMsg> &info)
{
}

}  // namespace fds
