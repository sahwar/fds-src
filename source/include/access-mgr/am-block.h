/*
 * Copyright 2014 by Formation Data Systems, Inc.
 */
#ifndef SOURCE_INCLUDE_ACCESS_MGR_AM_BLOCK_H_
#define SOURCE_INCLUDE_ACCESS_MGR_AM_BLOCK_H_

#include <unordered_map>
#include <fds_ptr.h>
#include <fds_module.h>
#include <shared/fds-constants.h>
#include <concurrency/spinlock.h>

class StorHvCtrl;

namespace fds {

class BlockMod;
class NbdBlockMod;

extern BlockMod             *gl_BlockMod;
extern NbdBlockMod           gl_NbdBlockMod;

typedef struct blk_vol_creat blk_vol_creat_t;
struct blk_vol_creat
{
    /* Input params. */
    const char        *v_name;
    const char        *v_dev;
    fds_uint64_t       v_uuid;
    fds_uint64_t       v_vol_blksz;
    fds_uint32_t       v_blksz;
    bool               v_test_vol_flag;
    /* Output params. */
    char               v_blkdev[FDS_MAX_VOL_NAME];
};

/**
 * Block volume descriptor.
 */
class BlkVol
{
  public:
    typedef bo::intrusive_ptr<BlkVol> ptr;

    virtual ~BlkVol();
    BlkVol(const char *name, const char *dev,
           fds_uint64_t uuid, fds_uint64_t vol_sz, fds_uint32_t blk_sz);
    BlkVol(const char *name, const char *dev,
           fds_uint64_t uuid, fds_uint64_t vol_sz, fds_uint32_t blk_sz, bool test_vol_flag);


    char               vol_name[FDS_MAX_VOL_NAME];
    char               vol_dev[FDS_MAX_VOL_NAME];
    fds_uint64_t       vol_uuid;              /* volume uuid; used as the key. */
    fds_uint64_t       vol_sz_blks;           /* volume size in blocks. */
    fds_uint32_t       vol_blksz_byte;        /* volume block size in bytes. */
    fds_uint32_t       vol_blksz_mask;
    bool               vol_test_flag;         /* flag to sprcify unit test volume */

  protected:
    fds_mutex          vol_mtx;
    boost::condition   vol_waitq;

  private:
    INTRUSIVE_PTR_DEFS(BlkVol, vol_refcnt);
};
typedef std::unordered_map<fds_uint64_t, BlkVol::ptr> BlkVolMap;

/**
 * Upcast refcnt pointer to a block vol.
 */
template <class T>
bo::intrusive_ptr<T> blk_vol_cast(BlkVol::ptr p)
{
    return static_cast<T *>(p.get());
}

/**
 * The block volume module interface.
 */
class BlockMod : public Module
{
  public:
    virtual ~BlockMod();
    BlockMod();

    static BlockMod *blk_singleton() { return gl_BlockMod; }
    static void blk_bind_to_am(StorHvCtrl *amc)
    {
        if (gl_BlockMod != NULL) {
            gl_BlockMod->blk_amc = amc;
        }
    }

    /* Module methods. */
    virtual int  mod_init(SysParams const *const p) = 0;
    virtual void mod_startup() = 0;
    virtual void mod_enable_service() = 0;
    virtual void mod_shutdown() = 0;

    /* Block driver methods. */
    virtual int blk_attach_vol(blk_vol_creat_t *req) = 0;
    virtual int blk_detach_vol(fds_uint64_t uuid);
    virtual int blk_suspend_vol(fds_uint64_t uuid) = 0;

    StorHvCtrl              *blk_amc;

  protected:
    BlkVolMap                blk_vols;
    Spinlock                 blk_splck;

    virtual BlkVol::ptr blk_alloc_vol(const blk_vol_creat_t *req) = 0;
    virtual BlkVol::ptr blk_creat_vol(const blk_vol_creat_t *req);
};

}  // namespace fds
#endif  // SOURCE_INCLUDE_ACCESS_MGR_AM_BLOCK_H_
