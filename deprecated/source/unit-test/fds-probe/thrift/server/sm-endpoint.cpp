/*
 * Copyright 2014 by Formation Data Systems, Inc.
 */
#include <endpoint-test.h>
#include <fdsp/fds_service_types.h>
#include <net/SvcRequestPool.h>
#include <net/PlatNetSvcHandler.h>

namespace fds {

// RPC Plugin, the base class is generated by Thrift's IDL.
//
class ProbeTestServer : virtual public PlatNetSvcHandler
{
  public:
    ProbeTestServer();
    virtual ~ProbeTestServer() {}

    void server_get(boost::shared_ptr<fpi::AsyncHdr>     &hdr,
                    boost::shared_ptr<fpi::GetObjectMsg> &msg);

    void server_put(boost::shared_ptr<fpi::AsyncHdr>     &hdr,
                    boost::shared_ptr<fpi::PutObjectMsg> &msg);
};

ProbeTestServer::ProbeTestServer()
{
    REGISTER_FDSP_MSG_HANDLER(fpi::GetObjectMsg, server_get);
    REGISTER_FDSP_MSG_HANDLER(fpi::PutObjectMsg, server_put);
}

void
ProbeTestServer::server_get(boost::shared_ptr<fpi::AsyncHdr>     &hdr,
                            boost::shared_ptr<fpi::GetObjectMsg> &msg)
{
    // std::cout << "server get" << std::endl;
}

void
ProbeTestServer::server_put(boost::shared_ptr<fpi::AsyncHdr>     &hdr,
                            boost::shared_ptr<fpi::PutObjectMsg> &msg)
{
    // std::cout << "server put" << std::endl;
}

ProbeEpTestSM                 gl_ProbeTestSM("Probe SM EP");

// mode_init
// ---------
//
int
ProbeEpTestSM::mod_init(SysParams const *const p)
{
    return Module::mod_init(p);
}

// mod_startup
// -----------
//
void
ProbeEpTestSM::mod_startup()
{
    Module::mod_startup();
}

// mod_enable_service
// ------------------
//
void
ProbeEpTestSM::mod_enable_service()
{
    NetMgr *mgr;

    Module::mod_enable_service();

    // Allocate the endpoint, bound to a physical port.
    //
    svc_recv   = boost::shared_ptr<ProbeTestServer>(new ProbeTestServer());
    svc_plugin = new ProbeEpPlugin();
    svc_ep     = new PlatNetEp(
            6000,
            NodeUuid(0xabcdef),
            NodeUuid(0x0ULL),
            boost::shared_ptr<fpi::PlatNetSvcProcessor>(
                new fpi::PlatNetSvcProcessor(svc_recv)), svc_plugin);

    // Register the endpoint in the local domain.
    mgr = NetMgr::ep_mgr_singleton();
    mgr->ep_register(svc_ep);
}

// mod_shutdown
// ------------
//
void
ProbeEpTestSM::mod_shutdown()
{
    svc_ep     = NULL;
    svc_recv   = NULL;
    svc_plugin = NULL;
}

}  // namespace fds
