/*
 * Copyright 2014 by Formation Data Systems, Inc.
 */
#ifndef SOURCE_NET_SERVICE_INCLUDE_EP_MAP_H_
#define SOURCE_NET_SERVICE_INCLUDE_EP_MAP_H_

#include <netinet/in.h>
#include <string>
#include <fds-shmobj.h>
#include <net/net-service.h>
#include <shared/fds-constants.h>
#include <platform/node-inv-shmem.h>

namespace fds {

/*
 * POD data in shared memory to map uuid to the right physical entity.
 */
typedef struct ep_map_rec ep_map_rec_t;
struct ep_map_rec
{
    struct sockaddr          rmp_addr;
    fds_uint64_t             rmp_uuid;
    char                     rmp_name[MAX_SVC_NAME_LEN];
};

/**
 * Item to put in shared memory queue.
 */
typedef struct ep_shmq_req
{
    shmq_req_t               smq_hdr;      /**< standard shm queue header. */
    int                      smq_idx;      /**< index in shm queue header. */
    FdspNodeType             smq_type;     /**< AM nodes or platform domain nodes. */
    ep_map_rec_t             smq_rec;      /**< the actual mapping record. */
} ep_shmq_req_t;

typedef struct ep_shmq_node_reg
{
    shmq_req_t               smq_hdr;
    int                      smq_idx;
    FdspNodeType             smq_type;      /**< AM nodes or domain nodes section.  */
    fds_uint64_t             smq_node_uuid; /**< consistency check with actual rec.  */
    fds_uint64_t             smq_svc_uuid;  /**< consistency check with actual rec.  */
} ep_shmq_node_req_t;

cc_assert(ep_map0, sizeof(ep_shmq_req_t) <= sizeof(node_shm_queue_item_t));
cc_assert(ep_map1, sizeof(ep_shmq_node_req_t) <= sizeof(node_shm_queue_item_t));

extern EpPlatLibMod         *gl_EpShmPlatLib;

class EpPlatLibMod : public Module
{
  public:
    explicit EpPlatLibMod(const char *name);
    virtual ~EpPlatLibMod() {}

    static EpPlatLibMod *ep_shm_singleton() { return gl_EpShmPlatLib; }

    // Module methods.
    //
    virtual int  mod_init(SysParams const *const p) override;
    virtual void mod_startup() override;
    virtual void mod_enable_service() override;
    virtual void mod_shutdown() override;

    virtual int  ep_map_record(const ep_map_rec_t *rec);
    virtual int  ep_unmap_record(fds_uint64_t uuid, int idx);
    virtual int  ep_lookup_rec(fds_uint64_t uuid, ep_map_rec_t *out);
    virtual int  ep_lookup_rec(int idx, fds_uint64_t uuid, ep_map_rec_t *out);
    virtual int  ep_lookup_rec(const char *name, ep_map_rec_t *out);

    /**
     * Lookup node-info records in shared memory.
     */
    virtual int  node_info_lookup(int idx, fds_uint64_t node_uuid, ep_map_rec_t *out);

    inline const ep_map_rec_t *ep_get_rec(int idx) {
        return ep_uuid_bind->shm_get_rec<ep_map_rec_t>(idx);
    }
    /**
     * Convert node info to endpoint mapping record format.
     */
    static void ep_node_info_to_mapping(const node_data_t *src, ep_map_rec_t *dest);
    static void ep_uuid_bind_to_msg(const ep_map_rec_t *src, fpi::UuidBindMsg *msg);
    static void ep_uuid_bind_frm_msg(ep_map_rec_t *src, const fpi::UuidBindMsg *msg);

  protected:
    int                      ep_my_type;
    ShmObjROKeyUint64       *ep_uuid_bind;
    ShmObjROKeyUint64       *ep_node_bind;
    ShmObjROKeyUint64       *ep_am_bind;

    int ep_req_map_record(fds_uint32_t op, const ep_map_rec_t *rec);
};

class EpPlatformdMod : public EpPlatLibMod
{
  public:
    explicit EpPlatformdMod(const char *name);
    virtual ~EpPlatformdMod() {}

    static EpPlatformdMod *ep_shm_singleton();

    virtual void mod_startup() override;
    virtual void mod_enable_service() override;

    virtual int  ep_map_record(const ep_map_rec_t *rec) override;
    virtual int  ep_unmap_record(fds_uint64_t uuid, int idx) override;

    void node_reg_notify(const node_data_t *info);

  protected:
    ShmObjRWKeyUint64       *ep_uuid_rw;
};

}  // namespace fds
#endif  // SOURCE_NET_SERVICE_INCLUDE_EP_MAP_H_
