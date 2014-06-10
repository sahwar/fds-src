/*
 * Copyright 2014 by Formation Data Systems, Inc.
 */
#ifndef SOURCE_NET_SERVICE_INCLUDE_NET_PLAT_SHARED_H_
#define SOURCE_NET_SERVICE_INCLUDE_NET_PLAT_SHARED_H_

#include <string>
#include <vector>
#include <map>
#include <fds-shmobj.h>
#include <net/net-service.h>
#include <fdsp/PlatNetSvc.h>
#include <platform/platform-lib.h>
#include <platform/node-inventory.h>
#include <net/PlatNetSvcHandler.h>

namespace fds {
class Platform;
class NetPlatSvc;
class NetPlatHandler;
class DomainAgent;
template <class SendIf, class RecvIf>class EndPoint;
struct ep_map_rec;

typedef EndPoint<fpi::PlatNetSvcClient, fpi::PlatNetSvcProcessor>  PlatNetEp;
typedef bo::intrusive_ptr<PlatNetEp>  PlatNetEpPtr;

class PlatNetPlugin : public EpEvtPlugin
{
  public:
    typedef boost::intrusive_ptr<PlatNetPlugin> pointer;
    typedef boost::intrusive_ptr<const PlatNetPlugin> const_ptr;

    explicit PlatNetPlugin(NetPlatSvc *svc);
    virtual ~PlatNetPlugin() {}

    virtual void ep_connected();
    virtual void ep_down();

    virtual void svc_up(EpSvcHandle::pointer handle);
    virtual void svc_down(EpSvc::pointer svc, EpSvcHandle::pointer handle);

  protected:
    NetPlatSvc              *plat_svc;
};

/**
 * Plugin for domain agent to deal with network error.
 */
class DomainAgentPlugin : public EpEvtPlugin
{
  public:
    typedef boost::intrusive_ptr<DomainAgentPlugin> pointer;
    typedef boost::intrusive_ptr<const DomainAgentPlugin> const_ptr;

    virtual ~DomainAgentPlugin() {}
    explicit DomainAgentPlugin(boost::intrusive_ptr<DomainAgent> agt) : pda_agent(agt) {}

    virtual void ep_connected();
    virtual void ep_down();
    virtual void svc_up(EpSvcHandle::pointer handle);
    virtual void svc_down(EpSvc::pointer svc, EpSvcHandle::pointer handle);

  protected:
    boost::intrusive_ptr<DomainAgent>  pda_agent;
};

/**
 * Node domain master agent.  This is the main interface to the master domain
 * service.
 */
class DomainAgent : public PmAgent
{
  public:
    typedef boost::intrusive_ptr<DomainAgent> pointer;
    typedef boost::intrusive_ptr<const DomainAgent> const_ptr;

    virtual ~DomainAgent() {}
    explicit DomainAgent(const NodeUuid &uuid, bool alloc_plugin = true);

    inline EpSvcHandle::pointer pda_rpc_handle() {
        return agt_domain_ep;
    }
    inline boost::shared_ptr<fpi::PlatNetSvcClient> pda_rpc() {
        return agt_domain_ep->svc_rpc<fpi::PlatNetSvcClient>();
    }
    virtual void pda_connect_domain(const fpi::DomainID &id);

  protected:
    friend class NetPlatSvc;
    friend class PlatformdNetSvc;

    DomainAgentPlugin::pointer            agt_domain_evt;
    EpSvcHandle::pointer                  agt_domain_ep;

    virtual void pda_register();
};

/**
 * Iterate through node info records in shared memory.
 */
class NodeInfoShmIter : public ShmObjIter
{
  public:
    virtual ~NodeInfoShmIter() {}
    explicit NodeInfoShmIter(bool rw = false);

    virtual bool
    shm_obj_iter_fn(int idx, const void *k, const void *r, size_t ksz, size_t rsz);

  protected:
    int                     it_no_rec;
    int                     it_no_rec_quit;
    bool                    it_shm_rw;
    const ShmObjRO         *it_shm;
    DomainNodeInv::pointer  it_local;
};

class NetPlatSvc : public NetPlatform
{
  public:
    explicit NetPlatSvc(const char *name);
    virtual ~NetPlatSvc();

    // Module methods.
    //
    virtual int  mod_init(SysParams const *const p);
    virtual void mod_startup();
    virtual void mod_enable_service();
    virtual void mod_shutdown();

    // Common net platform services.
    //
    virtual void nplat_refresh_shm();
    virtual EpSvcHandle::pointer nplat_domain_rpc(const fpi::DomainID &id) override;
    virtual void nplat_register_node(const fpi::NodeInfoMsg *msg) override;
    virtual bo::intrusive_ptr<EpSvcImpl> nplat_my_ep() override;

    inline std::string const *const nplat_domain_master(int *port) {
        *port = plat_lib->plf_get_om_svc_port();
        return plat_lib->plf_get_om_ip();
    }

  protected:
    PlatNetEpPtr                         plat_ep;
    PlatNetPlugin::pointer               plat_ep_plugin;
    bo::shared_ptr<NetPlatHandler>       plat_ep_hdler;
    DomainAgent::pointer                 plat_agent;
};

/**
 * Internal module vector providing network platform services.
 */
extern NetPlatSvc            gl_NetPlatform;

class NetPlatHandler : public PlatNetSvcHandler
{
  public:
    virtual ~NetPlatHandler() {}
    explicit NetPlatHandler(NetPlatSvc *svc)
        : fpi::PlatNetSvcIf(), net_plat(svc) {}

    // PlatNetSvcIf methods.
    //
    void allUuidBinding(const fpi::UuidBindMsg &mine) {}
    void notifyNodeInfo(std::vector<fpi::NodeInfoMsg> &ret,
                        const fpi::NodeInfoMsg &info, const bool bcast) {}
    void notifyNodeUp(fpi::RespHdr &ret, const fpi::NodeInfoMsg &info) {}
    virtual ServiceStatus getStatus(const int32_t nullarg) { return SVC_STATUS_INVALID; }
    virtual void getCounters(std::map<std::string, int64_t> & _return,
            const std::string& id) {}
    virtual void setConfigVal(const std::string& id, const int64_t val) {}

    void allUuidBinding(bo::shared_ptr<fpi::UuidBindMsg> &msg);
    void notifyNodeInfo(std::vector<fpi::NodeInfoMsg>    &ret,
                        bo::shared_ptr<fpi::NodeInfoMsg> &info,
                        bo::shared_ptr<bool>             &bcast);
    void notifyNodeUp(fpi::RespHdr &ret, bo::shared_ptr<fpi::NodeInfoMsg> &info);
    virtual fpi::ServiceStatus getStatus(boost::shared_ptr<int32_t>& nullarg);  // NOLINT
    virtual void getCounters(std::map<std::string, int64_t> & _return,
            boost::shared_ptr<std::string>& id);
    virtual void setConfigVal(boost::shared_ptr<std::string>& id,  // NOLINT
            boost::shared_ptr<int64_t>& val);

  protected:
    NetPlatSvc              *net_plat;
};

}  // namespace fds
#endif  // SOURCE_NET_SERVICE_INCLUDE_NET_PLAT_SHARED_H_
