/*
 * Copyright 2014 By Formation Data Systems, Inc.
 */
#ifndef SOURCE_PLATFORM_INCLUDE_PLATFORM_H_
#define SOURCE_PLATFORM_INCLUDE_PLATFORM_H_

#include <string>
#include <fds-shmobj.h>
#include <fds_process.h>
#include <kvstore/platformdb.h>
#include <shared/fds-constants.h>
#include <platform/platform-lib.h>
#include <platform/node-inv-shmem.h>

namespace fds {
class ShmObjRWKeyUint64;
class NodeShmRWCtrl;
class DiskPlatModule;
class NodePlatformProc;
struct node_shm_inventory;

class NodePlatform : public Platform
{
  public:
    NodePlatform();
    virtual ~NodePlatform() {}

    /**
     * Module methods
     */
    void mod_load_from_config();
    virtual int  mod_init(SysParams const *const param) override;
    virtual void mod_startup() override;
    virtual void mod_enable_service() override;
    virtual void mod_shutdown() override;

    void        plf_start_node_services(const fpi::FDSP_ActivateNodeTypePtr &msg);
    inline void plf_bind_process(NodePlatformProc *ptr) { plf_process = ptr; }

  protected:
    NodePlatformProc    *plf_process;
    DiskPlatModule      *disk_ctrl;

    virtual PlatRpcReqt *plat_creat_reqt_disp();
    virtual PlatRpcResp *plat_creat_resp_disp();
};

/**
 * Platform daemon module controls share memory segments.
 */
extern NodeShmRWCtrl         gl_NodeShmRWCtrl;

class NodeShmRWCtrl : public NodeShmCtrl
{
  public:
    virtual ~NodeShmRWCtrl() {}
    explicit NodeShmRWCtrl(const char *name);

    static ShmObjRWKeyUint64 *shm_am_rw_inv() {
        return gl_NodeShmRWCtrl.shm_am_rw;
    }
    static ShmObjRWKeyUint64 *shm_node_rw_inv() {
        return gl_NodeShmRWCtrl.shm_node_rw;
    }
    static ShmObjRW *shm_uuid_rw_binding() {
        return gl_NodeShmRWCtrl.shm_uuid_rw;
    }
    static ShmObjRWKeyUint64 *shm_node_rw_inv(FdspNodeType type)
    {
        if (type == fpi::FDSP_STOR_HVISOR) {
            return gl_NodeShmRWCtrl.shm_am_rw;
        }
        return gl_NodeShmRWCtrl.shm_node_rw;
    }

    /**
     * Module methods
     */
    virtual int  mod_init(SysParams const *const p) override;
    virtual void mod_startup() override;
    virtual void mod_shutdown() override;

  protected:
    ShmObjRWKeyUint64       *shm_am_rw;
    ShmObjRWKeyUint64       *shm_node_rw;
    ShmObjRW                *shm_uuid_rw;

    void shm_init_queue(node_shm_queue_t *queue);
    void shm_init_header(node_shm_inventory_t *hdr);

    virtual void shm_setup_queue() override;
};

/**
 * Platform daemon RPC handlers.  Only overwrite what's specific to Platform.
 */
class PlatformRpcReqt : public PlatRpcReqt
{
  public:
    explicit PlatformRpcReqt(const Platform *plf);
    virtual ~PlatformRpcReqt();

    void NotifyNodeAdd(fpi::FDSP_MsgHdrTypePtr     &msg_hdr,
                       fpi::FDSP_Node_Info_TypePtr &node_info);

    void NotifyNodeRmv(fpi::FDSP_MsgHdrTypePtr     &msg_hdr,
                       fpi::FDSP_Node_Info_TypePtr &node_info);

    void NotifyNodeActive(fpi::FDSP_MsgHdrTypePtr       &hdr,
                          fpi::FDSP_ActivateNodeTypePtr &info);
};

/**
 * Platform daemon RPC response handlers.
 */
class PlatformRpcResp : public PlatRpcResp
{
  public:
    explicit PlatformRpcResp(const Platform *plf);
    virtual ~PlatformRpcResp();

    void RegisterNodeResp(fpi::FDSP_MsgHdrTypePtr       &hdr,
                          fpi::FDSP_RegisterNodeTypePtr &resp);
};

extern NodePlatform gl_NodePlatform;

class NodePlatformProc : public PlatformProcess
{
  public:
    NodePlatformProc(int argc, char **argv, Module **vec);

    void proc_pre_startup() override;
    int  run() override;

    void plf_fill_disk_capacity_pkt(fpi::FDSP_RegisterNodeTypePtr pkt);

  protected:
    friend class NodePlatform;

    void plf_load_node_data();
    void plf_scan_hw_inventory();
    void plf_start_node_services(const fpi::FDSP_ActivateNodeTypePtr &msg);
};

}  // namespace fds

#endif  // SOURCE_PLATFORM_INCLUDE_PLATFORM_H_
