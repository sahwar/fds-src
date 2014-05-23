/*
 * Copyright 2014 by Formation Data Systems, Inc.
 */
#include <string>
#include <stdlib.h>
#include <dlt.h>
#include <fds-shmobj.h>
#include <net/net-service.h>
#include <platform/platform-lib.h>
#include <platform/node-inv-shmem.h>

namespace fds {

// --------------------------------------------------------------------------------------
// Node Inventory
// --------------------------------------------------------------------------------------

const ShmObjRO *
NodeInventory::node_shm_ctrl() const
{
    if (node_svc_type == fpi::FDSP_STOR_HVISOR) {
        return NodeShmCtrl::shm_am_inventory();
    }
    return NodeShmCtrl::shm_node_inventory();
}

// node_fill_shm_inv
// -----------------
//
void
NodeInventory::node_fill_shm_inv(const ShmObjRO *shm, int ro, int rw)
{
    const node_data_t *info;

    fds_assert(ro != -1);
    info = shm->shm_get_rec<node_data_t>(ro);
    if (node_ro_idx == -1) {
        node_svc_type = info->nd_svc_type;
        strncpy(rs_name, info->nd_auto_name, RS_NAME_MAX - 1);
    } else {
        fds_verify(info->nd_svc_type == node_svc_type);
        fds_assert(info->nd_service_uuid == rs_uuid.uuid_get_val());
    }
    node_ro_idx = ro;
    node_rw_idx = rw;
}

// node_info_msg_to_shm
// --------------------
// Convert data from the node info message to record to save to the shared memory seg.
//
/* static */ void
NodeInventory::node_info_msg_to_shm(const NodeInfoMsg *msg, node_data_t *rec)
{
    const UuidBindMsg *msg_bind;

    msg_bind             = &msg->node_loc;
    rec->nd_node_uuid    = msg_bind->svc_node.svc_uuid.svc_uuid;
    rec->nd_service_uuid = msg_bind->svc_id.svc_uuid.svc_uuid;
    rec->nd_base_port    = msg->nd_base_port;
    rec->nd_svc_type     = msg->node_loc.svc_type;
    rec->nd_node_state   = fpi::FDS_Node_Discovered;
    rec->nd_dlt_version  = DLT_VER_INVALID;
    rec->nd_disk_type    = msg->node_stor.disk_type;

    node_stor_cap_to_shm(&msg->node_stor, &rec->nd_capability);
    strncpy(rec->nd_ip_addr, msg_bind->svc_addr.c_str(), FDS_MAX_IP_STR);
    strncpy(rec->nd_auto_name, msg_bind->svc_auto_name.c_str(), FDS_MAX_NODE_NAME);
    strncpy(rec->nd_assign_name, msg_bind->svc_id.svc_name.c_str(), FDS_MAX_NODE_NAME);
}

// node_info_msg_frm_shm
// ---------------------
// Convert data from shm at the given index to the node info message.
//
/* static */ void
NodeInventory::node_info_msg_frm_shm(bool am, int ro_idx, NodeInfoMsg *msg)
{
    UuidBindMsg           *msg_bind;
    const ShmObjRO        *shm;
    const node_data_t     *rec;

    if (am == true) {
        shm = NodeShmCtrl::shm_am_inventory();
    } else {
        shm = NodeShmCtrl::shm_node_inventory();
    }
    fds_verify(ro_idx != -1);
    rec = shm->shm_get_rec<node_data_t>(ro_idx);

    msg_bind = &msg->node_loc;
    msg_bind->svc_id.svc_uuid.svc_uuid   = rec->nd_service_uuid;
    msg_bind->svc_node.svc_uuid.svc_uuid = rec->nd_node_uuid;
    msg_bind->svc_type                   = rec->nd_svc_type;
    msg->nd_base_port                    = rec->nd_base_port;

    msg_bind->svc_addr.assign(rec->nd_ip_addr);
    msg_bind->svc_auto_name.assign(rec->nd_auto_name);
    msg_bind->svc_id.svc_name.assign(rec->nd_assign_name);
    node_stor_cap_frm_shm(&msg->node_stor, &rec->nd_capability);
}

// init_plat_info_msg
// ------------------
// Format the NodeInfoMsg that will be processed by functions above.  The data is taken
// from the platform lib to represent the HW node running the SW.
//
void
NodeInventory::init_plat_info_msg(fpi::NodeInfoMsg *msg) const
{
    UuidBindMsg    *msg_bind;
    Platform::ptr   plat = Platform::platf_const_singleton();
    EpSvc::pointer  psvc = NetPlatform::nplat_singleton()->nplat_my_ep();

    msg_bind = &msg->node_loc;
    psvc->ep_fmt_uuid_binding(msg_bind, &msg->node_domain);
    init_stor_cap_msg(&msg->node_stor);

    msg->nd_base_port           = plat->plf_get_my_ctrl_port();
    msg_bind->svc_type          = plat->plf_get_node_type();
    msg_bind->svc_auto_name     = *plat->plf_get_my_ip();
    msg_bind->svc_id.svc_name   = "";
    msg_bind->svc_node.svc_name = *plat->plf_get_my_name();
    msg_bind->svc_node.svc_uuid.svc_uuid = plat->plf_get_my_uuid()->uuid_get_val();
}

// init_node_info_msg
// ------------------
// Format the NodeInfoMsg with data from this node agent obj.
void
NodeInventory::init_node_info_msg(fpi::NodeInfoMsg *msg) const
{
    node_info_msg_frm_shm(node_svc_type == fpi::FDSP_STOR_HVISOR ? true : false,
                          node_ro_idx, msg);
}

// node_stor_cap_to_shm
// --------------------
// Format storage capability info in shared memory from a network message.
//
/* static */ void
NodeInventory::node_stor_cap_to_shm(const fpi::StorCapMsg *msg, node_stor_cap_t *stor)
{
    stor->disk_capacity    = msg->disk_capacity;
    stor->disk_iops_max    = msg->disk_iops_max;
    stor->disk_iops_min    = msg->disk_iops_min;
    stor->disk_latency_max = msg->disk_latency_max;
    stor->disk_latency_min = msg->disk_latency_min;
    stor->ssd_iops_max     = msg->ssd_iops_max;
    stor->ssd_iops_min     = msg->ssd_iops_min;
    stor->ssd_capacity     = msg->ssd_capacity;
    stor->ssd_latency_max  = msg->ssd_latency_max;
    stor->ssd_latency_min  = msg->ssd_latency_min;
}

// node_stor_cap_frm_shm
// ---------------------
// Format storage capability info to a network message from data in shared memory.
//
/* static */ void
NodeInventory::node_stor_cap_frm_shm(fpi::StorCapMsg *msg, const node_stor_cap_t *stor)
{
    msg->disk_capacity    = stor->disk_capacity;
    msg->disk_iops_max    = stor->disk_iops_max;
    msg->disk_iops_min    = stor->disk_iops_min;
    msg->disk_latency_max = stor->disk_latency_max;
    msg->disk_latency_min = stor->disk_latency_min;
    msg->ssd_iops_max     = stor->ssd_iops_max;
    msg->ssd_iops_min     = stor->ssd_iops_min;
    msg->ssd_capacity     = stor->ssd_capacity;
    msg->ssd_latency_max  = stor->ssd_latency_max;
    msg->ssd_latency_min  = stor->ssd_latency_min;
}

// init_stor_cap_msg
// -----------------
//
void
NodeInventory::init_stor_cap_msg(fpi::StorCapMsg *msg) const
{
    const ShmObjRO    *shm;
    const node_data_t *rec;

    if (node_ro_idx == -1) {
        /* Don't have real numbers, made up some values. */
        msg->disk_iops_max     = 20000;
        msg->disk_iops_min     = 2000;
        msg->disk_capacity     = 0x7ffff;
        msg->disk_latency_max  = 1000000 / msg->disk_iops_min;
        msg->disk_latency_min  = 1000000 / msg->disk_iops_max;
        msg->ssd_iops_max      = 200000;
        msg->ssd_iops_min      = 20000;
        msg->ssd_capacity      = 0x1000;
        msg->ssd_latency_max   = 1000000 / msg->ssd_iops_min;
        msg->ssd_latency_min   = 1000000 / msg->ssd_iops_max;
        msg->ssd_count         = 2;
        msg->disk_count        = 24;
        msg->disk_type         = FDS_DISK_SATA;
    } else {
        shm = node_shm_ctrl();
        rec = shm->shm_get_rec<node_data_t>(node_ro_idx);
        node_stor_cap_frm_shm(msg, &rec->nd_capability);
    }
}

// TODO(Vy): deprecate this function.
//
void
NodeInventory::node_set_inventory(NodeInvData const *const inv)
{
    node_inv = inv;
    strncpy(rs_name, inv->nd_node_name.c_str(), RS_NAME_MAX - 1);
}

// node_fill_inventory
// -------------------
// TODO(Vy): depricate this function.
//
void
NodeInventory::node_fill_inventory(const FdspNodeRegPtr msg)
{
    NodeInvData       *data;
    node_capability_t *ncap;

    data = new NodeInvData();
    ncap = &data->nd_capability;

    data->nd_uuid           = NodeUuid(msg->node_uuid.uuid);
    data->nd_service_uuid   = NodeUuid(msg->service_uuid.uuid);
    data->nd_ip_addr        = msg->ip_lo_addr;
    data->nd_ip_str         = netSession::ipAddr2String(data->nd_ip_addr);
    data->nd_data_port      = msg->data_port;
    data->nd_ctrl_port      = msg->control_port;
    data->nd_migration_port = msg->migration_port;
    data->nd_node_name      = msg->node_name;
    data->nd_node_type      = msg->node_type;
    data->nd_node_state     = FDS_ProtocolInterface::FDS_Node_Discovered;
    data->nd_disk_type      = msg->disk_info.disk_type;
    data->nd_dlt_version    = DLT_VER_INVALID;

    ncap->disk_capacity     = msg->disk_info.disk_capacity;
    ncap->disk_iops_max     = msg->disk_info.disk_iops_max;
    ncap->disk_iops_min     = msg->disk_info.disk_iops_min;
    ncap->disk_latency_max  = msg->disk_info.disk_latency_max;
    ncap->disk_latency_min  = msg->disk_info.disk_latency_min;
    ncap->ssd_iops_max      = msg->disk_info.ssd_iops_max;
    ncap->ssd_iops_min      = msg->disk_info.ssd_iops_min;
    ncap->ssd_capacity      = msg->disk_info.ssd_capacity;
    ncap->ssd_latency_max   = msg->disk_info.ssd_latency_max;
    ncap->ssd_latency_min   = msg->disk_info.ssd_latency_min;
    strncpy(rs_name, data->nd_ip_str.c_str(), RS_NAME_MAX - 1);

    // TODO(vy): fix the weight.
    data->nd_gbyte_cap = ncap->ssd_capacity;
    node_inv = data;
}

// node_update_inventory
// ---------------------
//
void
NodeInventory::node_update_inventory(const FdspNodeRegPtr msg)
{
}

// init_msg_hdr
// ------------
//
void
NodeInventory::init_msg_hdr(FDSP_MsgHdrTypePtr msgHdr) const
{
    Platform::ptr plat = Platform::platf_const_singleton();

    msgHdr->minor_ver = 0;
    msgHdr->msg_id    = 1;
    msgHdr->major_ver = 0xa5;
    msgHdr->minor_ver = 0x5a;

    msgHdr->num_objects = 1;
    msgHdr->frag_len    = 0;
    msgHdr->frag_num    = 0;

    msgHdr->tennant_id      = 0;
    msgHdr->local_domain_id = 0;
    msgHdr->err_code        = ERR_OK;
    msgHdr->result          = FDS_ProtocolInterface::FDSP_ERR_OK;

    if (plat == NULL) {
        /* We'll get here if this is OM */
        msgHdr->src_id        = FDSP_ORCH_MGR;
        msgHdr->src_node_name = "";
    } else {
        msgHdr->src_id        = plat->plf_get_node_type();
        msgHdr->src_node_name = *plat->plf_get_my_name();
    }
}

// init_node_info_pkt
// ------------------
//
void
NodeInventory::init_node_info_pkt(fpi::FDSP_Node_Info_TypePtr pkt) const
{
    pkt->node_id        = 0;
    pkt->node_uuid      = node_inv->nd_uuid.uuid_get_val();
    pkt->service_uuid   = node_inv->nd_service_uuid.uuid_get_val();
    pkt->ip_hi_addr     = 0;
    pkt->ip_lo_addr     = node_inv->nd_ip_addr;
    pkt->node_type      = node_inv->nd_node_type;
    pkt->node_name      = node_inv->nd_node_name;
    pkt->node_state     = node_inv->nd_node_state;
    pkt->control_port   = node_inv->nd_ctrl_port;
    pkt->data_port      = node_inv->nd_data_port;
    pkt->migration_port = node_inv->nd_migration_port;
}

// init_node_reg_pkt
// -----------------
//
void
NodeInventory::init_node_reg_pkt(fpi::FDSP_RegisterNodeTypePtr pkt) const
{
    Platform::ptr plat = Platform::platf_const_singleton();
    pkt->node_type     = plat->plf_get_node_type();
    pkt->node_name     = *plat->plf_get_my_name();
    pkt->ip_hi_addr    = 0;
    pkt->ip_lo_addr    = str_to_ipv4_addr(*plat->plf_get_my_ip());
    pkt->control_port  = plat->plf_get_my_ctrl_port();
}

// set_node_state
// --------------
//
void
NodeInventory::set_node_state(FdspNodeState state)
{
    // TODO(Vy): do this in platform side.
    const_cast<NodeInvData *>(node_inv)->nd_node_state = state;
}

void
NodeInventory::set_node_dlt_version(fds_uint64_t dlt_version)
{
    // TODO(Vy): do this in platform side.
    const_cast<NodeInvData *>(node_inv)->nd_dlt_version = dlt_version;
}

std::ostream& operator<< (std::ostream &os, const NodeInvData& node) {
    os << "["
       << " uuid:" << node.nd_uuid
       << " capacity:" << node.nd_gbyte_cap
       << " ipnum:" << node.nd_ip_addr
       << " ip:" << node.nd_ip_str.c_str()
       << " data.port:" << node.nd_data_port
       << " ctrl.port:" << node.nd_ctrl_port
       << " migration.port:" << node.nd_migration_port
       << " disk.type:" << node.nd_disk_type
       << " name:" << node.nd_node_name
       << " type:" << node.nd_node_type
       << " state:" << node.nd_node_state
       << " dlt.version:" << node.nd_dlt_version
       << " disk.capacity:" << node.nd_capability.disk_capacity
       << " disk.iops.max:" << node.nd_capability.disk_iops_max
       << " disk.iops.min:" << node.nd_capability.disk_iops_min
       << " disk.latency.max:" << node.nd_capability.disk_latency_max
       << " disk.latency.min:" << node.nd_capability.disk_latency_min
       << " ssd.iops.max:" << node.nd_capability.ssd_iops_max
       << " ssd.iops.min:" << node.nd_capability.ssd_iops_min
       << " ssd.capacity:" << node.nd_capability.ssd_capacity
       << " ssd.latency.max:" << node.nd_capability.ssd_latency_max
       << " ssd.latency.min:" << node.nd_capability.ssd_latency_min
       << "]";

    return os;
}

uint32_t NodeServices::write(serialize::Serializer*  s) const {
    uint32_t b = 0;
    b += s->writeI64(sm.uuid_get_val());
    b += s->writeI64(dm.uuid_get_val());
    b += s->writeI64(am.uuid_get_val());
    return b;
}

uint32_t NodeServices::read(serialize::Deserializer* s) {
    uint32_t b = 0;
    fds_uint64_t uuid;
    b += s->readI64(uuid); sm = uuid;
    b += s->readI64(uuid); dm = uuid;
    b += s->readI64(uuid); am = uuid;
    return b;
}

std::ostream& operator<< (std::ostream &os, const NodeServices& node) {
    os << "["
       << " sm:" << node.sm
       << " dm:" << node.dm
       << " am:" << node.am
       << "]";
    return os;
}

// --------------------------------------------------------------------------------------
// Node Agents
// --------------------------------------------------------------------------------------

// node_stor_weight
// ----------------
//
fds_uint64_t
NodeAgent::node_stor_weight() const
{
    // lets normalize = nodes have same weight if their
    // capacity is within 10GB diff
    fds_uint64_t weight = node_inv->nd_gbyte_cap / 10;
    if (weight < 1) {
        weight = 1;
    }
    return weight;
}

// node_set_weight
// ---------------
//
void
NodeAgent::node_set_weight(fds_uint64_t weight)
{
    const_cast<NodeInvData *>(node_inv)->nd_gbyte_cap = weight;
}

AgentContainer::AgentContainer(FdspNodeType id) : RsContainer()
{
    ac_cpSessTbl = boost::shared_ptr<netSessionTbl>(new netSessionTbl(id));
}

// agent_handshake
// ---------------
//
void
AgentContainer::agent_handshake(boost::shared_ptr<netSessionTbl> net,
                                NodeAgentDpRespPtr               resp,
                                NodeAgent::pointer               agent)
{
}

// --------------------------------------------------------------------------------------
// PM Agent
// --------------------------------------------------------------------------------------

// --------------------------------------------------------------------------------------
// SM Agent
// --------------------------------------------------------------------------------------
SmAgent::SmAgent(const NodeUuid &uuid)
    : NodeAgent(uuid), sm_sess(NULL), sm_reqt(NULL) {}

SmAgent::~SmAgent()
{
    /* TODO(Vy): shutdown netsession and cleanup stuffs here */
}

// sm_handshake
// ------------
//
/* virtual */ void
SmAgent::sm_handshake(boost::shared_ptr<netSessionTbl> net, NodeAgentDpRespPtr sm_resp)
{
    sm_sess = net->startSession<netDataPathClientSession>(
            node_inv->nd_ip_str, node_inv->nd_data_port,
            FDSP_STOR_MGR, 1 /* just 1 channel */, sm_resp);

    sm_reqt    = sm_sess->getClient();
    sm_sess_id = sm_sess->getSessionId();

    FDS_PLOG(g_fdslog) << "Handshake with SM: " << node_inv->nd_ip_str
        << ", port " << node_inv->nd_data_port << ", sess id " << sm_sess_id;
}

NodeAgentDpClientPtr SmAgent::get_sm_client() {
    return sm_reqt;
}

std::string SmAgent::get_sm_sess_id() {
    return sm_sess_id;
}

SmContainer::SmContainer(FdspNodeType id) : AgentContainer(id) {}

// agent_handshake
// ---------------
//
void
SmContainer::agent_handshake(boost::shared_ptr<netSessionTbl> net,
                             NodeAgentDpRespPtr               sm_resp,
                             NodeAgent::pointer               agent)
{
    SmAgent::pointer sm = agt_cast_ptr<SmAgent>(agent);
    sm->sm_handshake(net, sm_resp);
}

// --------------------------------------------------------------------------------------
// OM Agent
// --------------------------------------------------------------------------------------
OmAgent::OmAgent(const NodeUuid &uuid)
    : NodeAgent(uuid), om_sess(NULL), om_reqt(NULL) {}

OmAgent::~OmAgent()
{
    /* TODO(Vy): shutdown netsession and cleanup stuffs here */
}

// init_msg_hdr
// ------------
//
void
OmAgent::init_msg_hdr(fpi::FDSP_MsgHdrTypePtr hdr) const
{
    NodeInventory::init_msg_hdr(hdr);
    hdr->msg_code     = FDSP_MSG_PUT_OBJ_REQ;  // TODO(Vy): cleanup these codes.
    hdr->dst_id       = FDSP_ORCH_MGR;
    hdr->session_uuid = om_sess_id;
}

// init_node_reg_pkt
// -----------------
//
void
OmAgent::init_node_reg_pkt(fpi::FDSP_RegisterNodeTypePtr pkt) const
{
    Platform::ptr plat = Platform::platf_const_singleton();

    NodeInventory::init_node_reg_pkt(pkt);
    pkt->node_type         = plat->plf_get_node_type();
    pkt->node_uuid.uuid    = plat->plf_get_my_uuid()->uuid_get_val();
    pkt->service_uuid.uuid = pkt->node_uuid.uuid + 1 + pkt->node_type;
    pkt->node_name         = *plat->plf_get_my_name();
    pkt->ip_hi_addr        = 0;
    pkt->ip_lo_addr        = fds::str_to_ipv4_addr(*plat->plf_get_my_ip());
    pkt->control_port      = plat->plf_get_my_ctrl_port();
    pkt->data_port         = plat->plf_get_my_data_port();
    pkt->migration_port    = plat->plf_get_my_migration_port();
}

// om_register_node
// ----------------
//
void
OmAgent::om_register_node(fpi::FDSP_RegisterNodeTypePtr reg)
{
    fpi::FDSP_MsgHdrTypePtr hdr(new FDSP_MsgHdrType);

    fds_verify(om_reqt != NULL);
    init_msg_hdr(hdr);
    om_reqt->RegisterNode(hdr, reg);
}

// om_handshake
// ------------
//
/* virtual */ void
OmAgent::om_handshake(boost::shared_ptr<netSessionTbl> net,
                      OmRespDispatchPtr                om_dispatch,
                      std::string                      om_ip,
                      fds_uint32_t                     om_port)
{
    om_sess = net->startSession<netOMControlPathClientSession>(
            om_ip, om_port, FDSP_ORCH_MGR, 1 /* just 1 channel for now */, om_dispatch);

    om_reqt    = om_sess->getClient();
    om_sess_id = om_sess->getSessionId();
}

// --------------------------------------------------------------------------------------
// AgentContainer
// --------------------------------------------------------------------------------------
AgentContainer::~AgentContainer()
{
    ac_cpSessTbl->endAllSessions();
}

// agent_register
// --------------
//
Error
AgentContainer::agent_register(const NodeUuid       &uuid,
                               const FdspNodeRegPtr  msg,
                               NodeAgent::pointer   *out,
                               bool                  activate)
{
    fds_bool_t         add;
    std::string        name;
    NodeAgent::pointer agent;

    add   = false;
    *out  = NULL;
    name  = msg->node_name;
    agent = agt_cast_ptr<NodeAgent>(agent_info(uuid));
    if (agent == NULL) {
        add   = activate;
        agent = agt_cast_ptr<NodeAgent>(rs_alloc_new(uuid));
        agent->node_fill_inventory(msg);
    } else {
        if (name.compare(agent->get_node_name()) != 0) {
            return Error(ERR_DUPLICATE_UUID);
        }
        agent->node_update_inventory(msg);
    }
    *out = agent;
    if (add == true) {
        agent_activate(agent);
    }
    return Error(ERR_OK);
}

// agent_register
// --------------
//
void
AgentContainer::agent_register(const ShmObjRO     *shm,
                               NodeAgent::pointer *out,
                               int                 ro,
                               int                 rw,
                               bool                activate)
{
    bool                add;
    const node_data_t  *info;
    NodeAgent::pointer  agent;

    add   = false;
    *out  = NULL;
    info  = shm->shm_get_rec<node_data_t>(ro);
    agent = agt_cast_ptr<NodeAgent>(agent_info(info->nd_service_uuid));
    if (agent == NULL) {
        add   = activate;
        agent = agt_cast_ptr<NodeAgent>(rs_alloc_new(info->nd_service_uuid));
    }
    agent->node_fill_shm_inv(shm, ro, rw);

    *out = agent;
    if (add == true) {
        agent_activate(agent);
    }
}

// agent_unregister
// ----------------
//
Error
AgentContainer::agent_unregister(const NodeUuid &uuid, const std::string &name)
{
    NodeAgent::pointer  agent;

    agent = agent_info(uuid);
    if ((agent == NULL) ||
        (name.compare(agent->get_node_name()) != 0)) {
        return Error(ERR_NOT_FOUND);
    }
    agent_deactivate(agent);
    return Error(ERR_OK);
}

// --------------------------------------------------------------------------------------
// Node Container
// --------------------------------------------------------------------------------------

DomainContainer::~DomainContainer() {}
DomainContainer::DomainContainer(char const *const name)
    : dc_om_master(NULL), dc_om_nodes(NULL),
      dc_sm_nodes(NULL), dc_dm_nodes(NULL),
      dc_am_nodes(NULL), dc_pm_nodes(NULL) {}

DomainContainer::DomainContainer(char const *const       name,
                                 OmAgent::pointer        master,
                                 AgentContainer::pointer sm,
                                 AgentContainer::pointer dm,
                                 AgentContainer::pointer am,
                                 AgentContainer::pointer pm,
                                 AgentContainer::pointer om)
    : dc_om_master(master), dc_om_nodes(om),
      dc_sm_nodes(sm), dc_dm_nodes(dm), dc_am_nodes(am), dc_pm_nodes(pm) {}

// dc_container_frm_msg
// --------------------
// Get the right container based on message type.
//
AgentContainer::pointer
DomainContainer::dc_container_frm_msg(FdspNodeType node_type)
{
    AgentContainer::pointer nodes;

    switch (node_type) {
        case FDS_ProtocolInterface::FDSP_STOR_MGR:
            nodes = dc_sm_nodes;
            break;

        case FDS_ProtocolInterface::FDSP_DATA_MGR:
            nodes = dc_dm_nodes;
            break;

        case FDS_ProtocolInterface::FDSP_STOR_HVISOR:
            nodes = dc_am_nodes;
            break;

        case FDS_ProtocolInterface::FDSP_PLATFORM:
            nodes = dc_pm_nodes;
            break;

        default:
            nodes = dc_om_nodes;
            break;
    }
    fds_verify(nodes != NULL);
    return nodes;
}

// dc_register_node
// ----------------
//
Error
DomainContainer::dc_register_node(const NodeUuid       &uuid,
                                  const FdspNodeRegPtr  msg,
                                  NodeAgent::pointer   *agent)
{
    AgentContainer::pointer nodes;

    FDS_PLOG(g_fdslog) << "Domain register uuid " << std::hex
        << msg->node_uuid.uuid << ", svc uuid " << msg->service_uuid.uuid
        << ", node type " << std::dec << msg->node_type;

    nodes = dc_container_frm_msg(msg->node_type);
    // TODO(Andrew): TOTAL HACK! This sleep prevents a race
    // where the node's control interface isn't initialized
    // yet and we try and connect too early. The real fix
    // should not register until its control interface is
    // fully initialized.
    sleep(2);
    return nodes->agent_register(uuid, msg, agent, true);
}

void
DomainContainer::dc_register_node(const ShmObjRO *shm,
                                  NodeAgent::pointer *agent, int ro, int rw)
{
    const node_data_t       *node;
    AgentContainer::pointer  container;

    fds_verify(ro != -1);
    node = shm->shm_get_rec<node_data_t>(ro);

    LOGDEBUG << "Platform domain register node uuid " << std::hex
        << node->nd_node_uuid << ", svc uuid " << node->nd_service_uuid;

    container = dc_container_frm_msg(node->nd_svc_type);
    return container->agent_register(shm, agent, ro, rw);
}

// dc_unregister_node
// ------------------
//
Error
DomainContainer::dc_unregister_node(const NodeUuid &uuid, const std::string &name)
{
    AgentContainer::pointer nodes;
    NodeAgent::pointer      agent;
    FdspNodeType            svc[] = {
        fpi::FDSP_STOR_MGR,
        fpi::FDSP_DATA_MGR,
        fpi::FDSP_STOR_HVISOR,
        fpi::FDSP_ORCH_MGR
    };

    for (int i = 0; svc[i] != FDS_ProtocolInterface::FDSP_ORCH_MGR; i++) {
        nodes = dc_container_frm_msg(svc[i]);
        if (nodes == NULL) {
            continue;
        }
        nodes->agent_unregister(uuid, name);
    }
    return Error(ERR_OK);
}

// dc_unregister_agent
// -------------------
//
Error
DomainContainer::dc_unregister_agent(const NodeUuid &uuid, FdspNodeType type)
{
    AgentContainer::pointer nodes;
    NodeAgent::pointer      agent;

    nodes = dc_container_frm_msg(type);
    if (nodes != NULL) {
        agent = nodes->agent_info(uuid);
        if (agent != NULL) {
            nodes->agent_deactivate(agent);
            return Error(ERR_OK);
        }
    }
    return Error(ERR_NOT_FOUND);
}

}  // namespace fds
