/*
 * Copyright 2014 by Formation Data Systems, Inc.
 */
#include <vector>
#include <platform.h>
#include <fds-shmobj.h>
#include <net/net-service-tmpl.hpp>
#include <net-platform.h>
#include <platform/node-inv-shmem.h>
#include <net/RpcFunc.h>

namespace fds {

class NodeInfoIter : public ResourceIter
{
  public:
    virtual ~NodeInfoIter() {}
    explicit NodeInfoIter(std::vector<fpi::NodeInfoMsg> &res) : nd_iter_ret(res) {}

    virtual bool rs_iter_fn(Resource::pointer curr)
    {
        fpi::NodeInfoMsg    msg;
        NodeAgent::pointer  node;

        node = agt_cast_ptr<NodeAgent>(curr);
        node->init_node_info_msg(&msg);
        nd_iter_ret.push_back(msg);
        return true;
    }

  protected:
    std::vector<fpi::NodeInfoMsg>        &nd_iter_ret;
};

class NodeUpdateIter : public NodeAgentIter
{
  public:
    typedef bo::intrusive_ptr<NodeUpdateIter> pointer;

    bool rs_iter_fn(Resource::pointer curr)
    {
        DomainAgent::pointer          agent;
        EpSvcHandle::pointer          eph;
        std::vector<fpi::NodeInfoMsg> ret;

        agent = agt_cast_ptr<DomainAgent>(curr);
        auto rpc = agent->node_svc_rpc(&eph);
        if (rpc != NULL) {
            NET_SVC_RPC_CALL(eph, rpc, notifyNodeInfo, ret, *(nd_reg_msg.get()), false);
        }
        return true;
    }
    /**
     * Update to platform nodes in a worker thread.
     */
    static void
    node_reg_update(NodeUpdateIter::pointer itr, bo::shared_ptr<fpi::NodeInfoMsg> &info)
    {
        itr->nd_reg_msg = info;
        itr->foreach_pm();
    }

  protected:
    bo::shared_ptr<fpi::NodeInfoMsg>  nd_reg_msg;
};

/*
 * -----------------------------------------------------------------------------------
 * RPC Handlers
 * -----------------------------------------------------------------------------------
 */
PlatformEpHandler::~PlatformEpHandler() {}
PlatformEpHandler::PlatformEpHandler(PlatformdNetSvc *svc)
    : fpi::PlatNetSvcIf(), net_plat(svc) {
    REGISTER_FDSP_MSG_HANDLER(fpi::CtrlNotifyDLTUpdate, NotifyDLTUpdate);
}

// NotifyDLTUpdate
// ---------------
//
void
PlatformEpHandler::NotifyDLTUpdate(boost::shared_ptr<fpi::AsyncHdr>       &hdr,
                              boost::shared_ptr<fpi::CtrlNotifyDLTUpdate> &dlt)
{
    LOGNORMAL << " we receive dlt updating msg ";
#if 0
    Error err(ERR_OK);
    LOGNOTIFY << "OMClient received new DLT commit version  "
            << dlt->dlt_data.dlt_type;
    err = gl_omClient.updateDlt(dlt->dlt_data.dlt_type, dlt->dlt_data.dlt_data);
    hdr->msg_code = err.GetErrno();
    sendAsyncResp(*hdr, FDSP_MSG_TYPEID(fpi::CtrlNotifyDLTUpdate), *dlt);
#endif
}

// allUuidBinding
// --------------
// Each platform deamon in the domain receives this request from a peer daemon to
// notify it about a new uuid to physical binding.
//
// The protocol flow is documented in net-service/endpoint/ep-map.cpp.
//
void
PlatformEpHandler::allUuidBinding(bo::shared_ptr<fpi::UuidBindMsg> &msg)
{
    int              idx;
    ep_shmq_req_t    rec;
    EpPlatLibMod    *shm;
    ShmConPrdQueue  *plat;

    LOGDEBUG << "Platform domain master, all uuidBind is called";
    EpPlatLibMod::ep_uuid_bind_frm_msg(&rec.smq_rec, msg.get());

    /* Save the binding to shared memory, in the uuid binding section. */
    shm = EpPlatLibMod::ep_shm_singleton();
    idx = shm->ep_map_record(&rec.smq_rec);
    fds_assert(idx != -1);

    /* Cache the binding info. */
    NetMgr::ep_mgr_singleton()->ep_register_binding(&rec.smq_rec, idx);

    LOGDEBUG << "Send SHMQ_REQ_UUID_BIND: idx " << rec.smq_idx
        << ", uuid " << rec.smq_rec.rmp_uuid << ", port "
        << EpAttr::netaddr_get_port(&rec.smq_rec.rmp_addr);

    /*
     * Notify all services running in the node about the new binding.  This can be
     * lazy update since the binding is already in shared memory.
     */
    rec.smq_idx  = idx;
    rec.smq_type = msg->svc_type;
    rec.smq_hdr.smq_code = SHMQ_REQ_UUID_BIND;

    plat = NodeShmCtrl::shm_producer();
    plat->shm_producer(static_cast<void *>(&rec), sizeof(rec), 0);
}

// notifyNodeAdd
// -------------
//
void
PlatformEpHandler::notifyNodeActive(bo::shared_ptr<fpi::FDSP_ActivateNodeType> &info)
{
    LOGDEBUG << "Received message to activate node";
    Platform     *plat = Platform::platf_singleton();
    NodePlatform *node = static_cast<NodePlatform *>(plat);
    node->plf_start_node_services(info);
}

// notifyNodeInfo
// --------------
//
void
PlatformEpHandler::notifyNodeInfo(std::vector<fpi::NodeInfoMsg>    &ret,
                                  bo::shared_ptr<fpi::NodeInfoMsg> &msg,
                                  bo::shared_ptr<bool>             &bcast)
{
    DomainNodeInv::pointer local;

    LOGDEBUG << "recv notifyNodeInfo " << msg->node_loc.svc_id.svc_uuid.svc_uuid
        << ", bcast " << *bcast;

    net_plat->nplat_register_node(msg.get());
    if (*bcast == true) {
        fds_threadpool          *thr;
        NodeInfoIter             info(ret);
        NodeUpdateIter::pointer  update = new NodeUpdateIter();

        thr = NetMgr::ep_mgr_thrpool();
        thr->schedule(&NodeUpdateIter::node_reg_update, update, msg);

        // Go through the entire domain to send back inventory info to the new node.

        local = Platform::platf_singleton()->plf_node_inventory();
        local->dc_foreach_dm(&info);
        std::cout << "Sent back " << info.rs_iter_count() << std::endl;
    }
}

// getDomainNodes
// --------------
//
void
PlatformEpHandler::getDomainNodes(fpi::DomainNodes                 &ret,
                                  bo::shared_ptr<fpi::DomainNodes> &dom)
{
    DomainNodeInv::pointer local;

    local = Platform::platf_singleton()->plf_node_inventory();
    local->dc_node_svc_info(ret);
}

}  // namespace fds
