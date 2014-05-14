/*
 * Copyright 2014 by Formation Data Systems, Inc.
 */
#include <endpoint-test.h>

namespace fds {

// RPC Plugin, the base class is generated by Thrift's IDL.
//
class ProbeTestAM_RPC : virtual public fpi::ProbeServiceAMIf
{
  public:
    ProbeTestAM_RPC() {}
    virtual ~ProbeTestAM_RPC() {}

    void msg_async_resp(const fpi::AsyncHdr &org, const fpi::AsyncHdr &resp) {}
    void msg_async_resp(boost::shared_ptr<fpi::AsyncHdr>    &org,
                        boost::shared_ptr<fpi::AsyncHdr> &resp) {}

    void am_creat_vol(fpi::ProbeAmCreatVolResp &ret,
                      const fpi::ProbeAmCreatVol &cmd) {}
    void am_creat_vol(fpi::ProbeAmCreatVolResp &ret,
                      boost::shared_ptr<fpi::ProbeAmCreatVol> &cmd) {}

    void am_probe_put_resp(const fpi::ProbeGetMsgResp &resp) {}
    void am_probe_put_resp(boost::shared_ptr<fpi::ProbeGetMsgResp> &resp) {}
};

ProbeEpTestAM                 gl_ProbeTestAM("Probe AM EP");
ProbeEpSvcTestAM              gl_ProbeSvcTestAM("Probe Svc AM");

/*
 * -----------------------------------------------------------------------------------
 *  ProbeEpTestAM
 * -----------------------------------------------------------------------------------
 */
int
ProbeEpTestAM::mod_init(SysParams const *const p)
{
    NetMgr *mgr;

    Module::mod_init(p);

    // Allocate the endpoint, bound to a physical port.
    //
    boost::shared_ptr<ProbeTestAM_RPC>hdler(new ProbeTestAM_RPC());
    probe_ep = new EndPoint<fpi::ProbeServiceSMClient, fpi::ProbeServiceAMProcessor>(
            7000,                           /* port number         */
            NodeUuid(0xfedcba),             /* my uuid             */
            NodeUuid(0xabcdef),             /* peer uuid           */
            boost::shared_ptr<fpi::ProbeServiceAMProcessor>(
                new fpi::ProbeServiceAMProcessor(hdler)),
            new ProbeEpPlugin());

    // Register the endpoint in the local domain.
    mgr = NetMgr::ep_mgr_singleton();
    mgr->ep_register(probe_ep);
    return 0;
}

void
ProbeEpTestAM::mod_startup()
{
    probe_ep->ep_activate();
}

void
ProbeEpTestAM::mod_shutdown()
{
}

/*
 * -----------------------------------------------------------------------------------
 *  ProbeEpSvcTestAM
 * -----------------------------------------------------------------------------------
 */
int
ProbeEpSvcTestAM::mod_init(SysParams const *const p)
{
    NetMgr       *mgr;
    fpi::SvcUuid  svc, mine;

    Module::mod_init(p);
    mine.svc_uuid = 0;

    // Locate service handle based on uuid; don't care where they are located.
    //
    mgr          = NetMgr::ep_mgr_singleton();
    svc.svc_uuid = 0x1234;
    am_hello     = mgr->svc_new_handle<fpi::ProbeServiceSMClient>(mine, svc);

    svc.svc_uuid = 0xcafe;
    am_bye       = mgr->svc_new_handle<fpi::ProbeServiceSMClient>(mine, svc);

    svc.svc_uuid = 0xbeef;
    am_poke      = mgr->svc_new_handle<fpi::ProbeServiceSMClient>(mine, svc);
    return 0;
}

void
ProbeEpSvcTestAM::mod_startup()
{
}

void
ProbeEpSvcTestAM::mod_shutdown()
{
}

}  // namespace fds
