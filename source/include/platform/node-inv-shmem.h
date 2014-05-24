/*
 * Copyright 2013 by Formation Data Systems, Inc.
 */
#ifndef SOURCE_INCLUDE_PLATFORM_NODE_INV_SHMEM_H_
#define SOURCE_INCLUDE_PLATFORM_NODE_INV_SHMEM_H_

#include <string>
#include <ostream>
#include <fds-shmobj.h>
#include <fds_module.h>
#include <fds_typedefs.h>
#include <concurrency/Mutex.h>
#include <shared/fds-constants.h>

/**
 * Platform POD structures in shared memory.
 */
namespace fds {
class FdsShmem;
class NodeShmCtrl;
struct node_shm_queue;

namespace fpi = FDS_ProtocolInterface;
typedef fpi::FDSP_NodeState            FdspNodeState;
typedef fpi::FDSP_MgrIdType            FdspNodeType;
typedef fpi::FDSP_RegisterNodeTypePtr  FdspNodeRegPtr;

typedef struct node_stor_cap
{
    fds_uint64_t             disk_capacity;
    fds_uint32_t             disk_iops_max;
    fds_uint32_t             disk_iops_min;
    fds_uint32_t             disk_latency_max;
    fds_uint32_t             disk_latency_min;
    fds_uint32_t             ssd_iops_max;
    fds_uint32_t             ssd_iops_min;
    fds_uint64_t             ssd_capacity;
    fds_uint32_t             ssd_latency_max;
    fds_uint32_t             ssd_latency_min;
} node_stor_cap_t;

typedef struct node_data
{
    fds_uint64_t             nd_node_uuid;
    fds_uint64_t             nd_service_uuid;

    fds_uint32_t             nd_base_port;
    char                     nd_ip_addr[FDS_MAX_IP_STR];
    char                     nd_auto_name[FDS_MAX_NODE_NAME];
    char                     nd_assign_name[FDS_MAX_NODE_NAME];

    FdspNodeType             nd_svc_type;
    FdspNodeState            nd_node_state;
    fds_uint64_t             nd_dlt_version;
    fds_uint32_t             nd_disk_type;
    node_stor_cap_t          nd_capability;
} node_data_t;

typedef enum
{
    NODE_SHM_EMPTY          = 0,
    NODE_SHM_ACTIVE         = 0xfeedcafe
} node_shm_state_e;

/**
 * Node inventory shared memory segment layout
 */
typedef struct node_shm_inventory
{
    fds_uint32_t             shm_magic;
    fds_uint32_t             shm_version;
    node_shm_state_e         shm_state;

    fds_uint32_t             shm_node_inv_off;
    fds_uint32_t             shm_node_inv_key_off;
    fds_uint32_t             shm_node_inv_key_siz;
    fds_uint32_t             shm_node_inv_obj_siz;

    fds_uint32_t             shm_uuid_bind_off;
    fds_uint32_t             shm_uuid_bind_key_off;
    fds_uint32_t             shm_uuid_bind_key_siz;
    fds_uint32_t             shm_uuid_bind_obj_siz;

    fds_uint32_t             shm_am_inv_off;
    fds_uint32_t             shm_am_inv_key_off;
    fds_uint32_t             shm_am_inv_key_siz;
    fds_uint32_t             shm_am_inv_obj_siz;
} node_shm_inventory_t;

#if 0
/**
 * Obj represents a node from RO inventory data.
 */
class NodeInvData
{
  public:
    explicit NodeInvData(const node_data_t *shm);
    virtual ~NodeInvData();

    NodeUuid     nd_uuid() const { return NodeUuid(nd_shm->nd_uuid); }
    NodeUuid     nd_service_uuid() const { return NodeUuid(nd_shm->nd_service_uuid); }
    const char  *nd_node_name() const { return nd_shm->nd_node_name; }
    std::string  nd_ip_str() const { return net::ipaddr2string(nd_shm->nd_ip_addr); }

    fds_uint64_t nd_dlt_version() const;
    fds_uint32_t nd_ip_addr() const { return nd_shm->nd_ip_addr; }
    fds_uint32_t nd_data_port() const { return nd_shm->nd_data_port; }
    fds_uint32_t nd_ctrl_port() const { return nd_shm->nd_ctrl_port; }
    fds_uint32_t nd_migration_port() const { return nd_shm->nd_migration_port; }
    fds_uint32_t nd_disk_type() const { return nd_shm->nd_disk_type; }

    FdspNodeState nd_node_state() const;
    FdspNodeType  nd_node_type() const { return nd_shm->nd_node_type; }
    const node_capability_t &nd_capability() const { return nd_shm->nd_capability; }

    virtual fds_uint64_t nd_gbyte_cap() const;
    virtual void node_set_gbyte_cap(fds_uint64_t cap);
    virtual void node_update_inventory(const FdspNodeRegPtr msg);
    virtual void node_set_state(FdspNodeState state) { nd_my_node_state = state; }
    virtual void node_set_dlt_version(fds_uint64_t ver) { nd_my_dlt_version = ver; }

    friend std::ostream& operator << (std::ostream &os, const NodeInvData& node);

  private:
    const node_data_t       *nd_shm;
    fds_uint64_t             nd_my_gb_cap;
    fds_uint64_t             nd_my_dlt_version;
    FdspNodeState            nd_my_node_state;
};

#endif

extern NodeShmCtrl           gl_NodeShmROCtrl;
extern NodeShmCtrl          *gl_NodeShmCtrl;

class NodeShmCtrl : public Module
{
  public:
    virtual ~NodeShmCtrl() {}
    explicit NodeShmCtrl(const char *name);

    static const int shm_max_svc   = MAX_DOMAIN_EP_SVC;
    static const int shm_max_ams   = FDS_MAX_AM_NODES;
    static const int shm_max_nodes = MAX_DOMAIN_NODES;

    static const int shm_queue_hdr     = 128;
    static const int shm_q_item_size   = 64;
    static const int shm_q_item_count  = 510;             /** 32K segment */
    static const int shm_svc_consumers = 8;

    static ShmObjROKeyUint64 *shm_am_inventory() { return gl_NodeShmCtrl->shm_am_inv; }
    static ShmObjROKeyUint64 *shm_uuid_binding() { return gl_NodeShmCtrl->shm_uuid_bind; }
    static ShmObjROKeyUint64 *shm_node_inventory() {
        return gl_NodeShmCtrl->shm_node_inv;
    }
    static ShmConPrdQueue *shm_consumer() { return gl_NodeShmCtrl->shm_cons_q; }
    static ShmConPrdQueue *shm_producer() { return gl_NodeShmCtrl->shm_prod_q; }

    /**
     * Module methods
     */
    virtual int  mod_init(SysParams const *const p) override;
    virtual void mod_startup() override;
    virtual void mod_shutdown() override;

  protected:
    int                        shm_flags;
    size_t                     shm_node_off;
    size_t                     shm_node_siz;
    size_t                     shm_uuid_off;
    size_t                     shm_uuid_siz;
    size_t                     shm_am_off;
    size_t                     shm_am_siz;
    size_t                     shm_total_siz;

    /* Shared memory command queue, RW in lib. */
    char                       shm_rw_name[FDS_MAX_UUID_STR];
    FdsShmem                  *shm_rw;
    ShmObjRW                  *shm_rw_data;
    ShmConPrdQueue            *shm_cons_q;
    ShmConPrdQueue            *shm_prod_q;
    struct node_shm_queue     *shm_queue;

    /* Node inventory and uuid binding, RO in plat lib, RW in platform. */
    char                       shm_name[FDS_MAX_UUID_STR];
    FdsShmem                  *shm_ctrl;
    ShmObjROKeyUint64         *shm_am_inv;
    ShmObjROKeyUint64         *shm_node_inv;
    ShmObjROKeyUint64         *shm_uuid_bind;
    struct node_shm_inventory *shm_node_hdr;

    void      shm_init_header(struct node_shm_inventory *hdr);
    FdsShmem *shm_create_obj(const char *fmt, char *name, int size);

    virtual void shm_setup_queue();
};

/**
 * Producer/consumer queue in shared memory of a node.
 */
typedef struct node_shm_queue
{
    fds_uint32_t             smq_hdr_size;
    fds_uint32_t             smq_item_size;
    shm_con_prd_sync_t       smq_sync;

    /*   Must be together for proper memory layout */
    shm_nprd_1con_q_t        smq_svc2plat;         /* *< multiple svcs to 1 platformd. */
    int                      smq_svcq_pad[NodeShmCtrl::shm_svc_consumers];

    shm_1prd_ncon_q_t        smq_plat2svc;         /* *< 1 platformd to multiple svcs. */
    int                      smq_plat_pad[NodeShmCtrl::shm_svc_consumers];
} node_shm_queue_t;

/**
 * Opaque item in producer/consumer queue in shared memory.
 */
typedef struct node_shm_queue_item
{
    char                     smq_item[NodeShmCtrl::shm_q_item_size];
} node_shm_queue_item_t;

// cc_assert(node_inv_shm0, sizeof(node_shm_queue_t) <= NodeShmCtrl::shm_queue_hdr);
cc_assert(node_inv_shm1, sizeof(node_shm_queue_item_t) <= NodeShmCtrl::shm_q_item_size);

}  // namespace fds
#endif  // SOURCE_INCLUDE_PLATFORM_NODE_INV_SHMEM_H_
