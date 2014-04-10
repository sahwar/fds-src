/*
 * Copyright 2014 by Formation Data Systems, Inc.
 */
#include <persistentdata.h>
#include <string>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <fds_err.h>

#include <fstream>
#include <iostream>
#include <persistent_layer/dm_io.h>
#include <fds_process.h>
#include <tokFileMgr.h>

using namespace fds;  // NOLINT
namespace diskio {

// ----------------------------------------------------------------------------
DataIOModule            gl_dataIOMod("Disk IO Module");
DataDiscoveryModule     dataDiscoveryMod("Data Discovery Module");
static DataIO          *sgt_dataIO;

// ----------------------------------------------------------------------------
// \DataIOModule
// ----------------------------------------------------------------------------
DataIOModule::~DataIOModule() {}
DataIOModule::DataIOModule(char const *const name)
    : Module(name), io_mtx("io mtx")
{
    static fds::Module *dataIOIntern[] = {
        &dataDiscoveryMod,
        &dataIndexMod,
        nullptr
    };
    mod_intern    = dataIOIntern;
    io_ssd_curr   = 0;
    io_hdd_curr   = 0;
    io_ssd_total  = 0;
    io_hdd_total  = 0;
    io_ssd_diskid = NULL;
    io_hdd_diskid = NULL;
    io_token_db   = new tokenFileDb();
}

DataDiscIter::~DataDiscIter() {}
DataDiscIter::DataDiscIter(DataIOModule *io) : cb_io(io), cb_root_dir(NULL) {}

// obj_callback
// ------------
// Single threaded path called during initialization.
//
bool
DataDiscIter::obj_callback()
{
    if (cb_tier == flashTier) {
        cb_io->disk_mount_ssd_path(cb_root_dir->c_str(), cb_disk_id);
    } else {
        cb_io->disk_mount_hdd_path(cb_root_dir->c_str(), cb_disk_id);
    }
    cb_root_dir = NULL;
    return true;
}

// disk_mount_ssd_path
// -------------------
//
void
DataIOModule::disk_mount_ssd_path(const char *path, fds_uint16_t disk_id)
{
    io_ssd_diskid[io_ssd_curr++] = disk_id;
    fds_verify(io_ssd_curr <= io_ssd_total);
}

// disk_mount_hdd_path
// -------------------
//
void
DataIOModule::disk_mount_hdd_path(const char *path, fds_uint16_t disk_id)
{
    io_hdd_diskid[io_hdd_curr++] = disk_id;
    fds_verify(io_hdd_curr <= io_hdd_total);
}

// disk_hdd_io
// -----------
//
PersisDataIO *
DataIOModule::disk_hdd_io(DataTier            tier,
                          fds_uint32_t        file_id,
                          meta_obj_id_t const *const oid)
{
    fds_uint16_t        disk_id;
    PersisDataIO       *ioc;
    const fds_uint32_t *token;

    // io_token_db depends on DataDiscoveryModule::disk_hdd_path(disk_id) for the
    // path to the underlaying device.
    //
    token   = reinterpret_cast<const fds_uint32_t *>(oid->metaDigest);
    disk_id = io_hdd_diskid[oid->metaDigest[0] % io_hdd_curr];
    ioc     = io_token_db->openTokenFile(tier, disk_id, *token, file_id);

    fds_verify(ioc != NULL);
    return ioc;
}

// disk_hdd_disk
// -------------
//
PersisDataIO *
DataIOModule::disk_hdd_disk(DataTier            tier,
                            fds_uint16_t        disk_id,
                            fds_uint32_t        file_id,
                            meta_obj_id_t const *const oid)
{
    // Should also handle the case that disk_id doesn't match anything discovered to
    // handle disk failures.  Fix tokenFileDB interface too...
    //
    const fds_uint32_t *token = reinterpret_cast<const fds_uint32_t *>(oid->metaDigest);
    return io_token_db->openTokenFile(tier, disk_id, *token, file_id);
}

//
// Handle notification about start of garbage collection for given token
// and tier
//
void
DataIOModule::notify_start_gc(fds::fds_token_id tok_id,
                              DataTier tier)
{
    fds::Error err(fds::ERR_OK);
    fds_uint16_t disk_id;

    // No matter how we distribute tokens over disks, we will iterate
    // over all disks, and notify tokenFileDb for all of them.
    // tokenFileDB class will return ERR_NOT_FOUND for notifications that
    // do not apply, which we will ignore
    for (fds_uint32_t idx = 0; idx < io_hdd_curr; ++idx) {
        disk_id = io_hdd_diskid[idx];
        err = io_token_db->notifyStartGC(disk_id, tok_id, tier);
        fds_verify(err.ok() || (err == fds::Error(fds::ERR_NOT_FOUND)));
    }
}

//
// Notify about end of garbage collection for a given token and tier
//
fds::Error
DataIOModule::notify_end_gc(fds::fds_token_id tok_id,
                            DataTier tier)
{
    fds::Error err(fds::ERR_OK);
    fds_uint16_t disk_id;

    // No matter how we distribute tokens over disks, we will iterate
    // over all disks, and notify tokenFileDb for all of them.
    // tokenFileDB class will return ERR_NOT_FOUND for notifications that
    // do not apply, which we will ignore
    for (fds_uint32_t idx = 0; idx < io_hdd_curr; ++idx) {
        disk_id = io_hdd_diskid[idx];
        err = io_token_db->notifyEndGC(disk_id, tok_id, tier);
        fds_verify(err.ok() || (err == fds::Error(fds::ERR_NOT_FOUND)));
    }
    return err;
}

// \mod_init
// ---------
// Single thread init. path, no lock is needed.
// Must change this code to support hotplug.
//
int
DataIOModule::mod_init(fds::SysParams const *const param)
{
    Module::mod_init(param);

    std::cout << "DataIOModule init..." << std::endl;
    sgt_dataIO = new DataIO;

    // TODO(Vy): need to support hotplug here, allocate extra spaces.  Also fix this
    // module to use singleton or member data.
    //
    io_ssd_total = dataDiscoveryMod.disk_ssd_discovered() << 1;
    if (io_ssd_total == 0) {
        io_ssd_total = 128;
    }
    io_ssd_diskid = new fds_uint16_t [io_ssd_total];
    memset(io_ssd_diskid, 0xf, io_ssd_total * sizeof(*io_ssd_diskid));

    io_hdd_total = dataDiscoveryMod.disk_hdd_discovered() << 1;
    if (io_hdd_total == 0) {
        io_hdd_total = 128;
    }
    io_hdd_diskid = new fds_uint16_t [io_hdd_total];
    memset(io_hdd_diskid, 0xf, io_hdd_total * sizeof(*io_hdd_diskid));

    // Iterate through the discovery module to create all io handling objects.
    DataDiscIter iter(this);
    iter.cb_tier = flashTier;
    dataDiscoveryMod.disk_ssd_iter(&iter);

    iter.cb_tier = diskTier;
    dataDiscoveryMod.disk_hdd_iter(&iter);
    return 0;
}

// \mod_startup
// ------------
//
void
DataIOModule::mod_startup()
{
    Module::mod_startup();
    std::cout << "DataIOModule startup..." << std::endl;
}

// \mod_shutdown
// -------------
//
void
DataIOModule::mod_shutdown()
{
    Module::mod_shutdown();
}

// ----------------------------------------------------------------------------
// \DataDiscoveryModule
// ----------------------------------------------------------------------------
DataDiscoveryModule::~DataDiscoveryModule() {}
DataDiscoveryModule::DataDiscoveryModule(char const *const name)
    : Module(name), pd_hdd_found(0), pd_ssd_found(0) {}

// \mod_init
// ---------
//
int
DataDiscoveryModule::mod_init(fds::SysParams const *const param)
{
    Module::mod_init(param);
    disk_open_map();
    return 0;
}

// disk_open_map
// -------------
//
void
DataDiscoveryModule::disk_open_map()
{
    int           idx;
    fds_uint64_t  uuid;
    std::string   path, dev;

    const fds::FdsRootDir *dir = fds::g_fdsprocess->proc_fdsroot();
    std::ifstream map(dir->dir_dev() + std::string("/disk-map"), std::ifstream::in);

    fds_verify(map.fail() == false);
    while (!map.eof()) {
        map >> dev >> idx >> std::hex >> uuid >> std::dec >> path;
        if (map.fail()) {
            break;
        }
        LOGNORMAL << "dev " << dev << ", path " << path << ", uuid " << uuid;
        if (strstr(path.c_str(), "hdd") != NULL) {
            pd_hdd_found++;
            pd_hdd_map[idx] = path;
        } else if (strstr(path.c_str(), "ssd") != NULL) {
            pd_ssd_found++;
            pd_ssd_map[idx] = path;
        } else {
            fds_panic("Unknown path: %s\n", path.c_str());
        }
        pd_uuids[idx] = uuid;
    }
    if (pd_hdd_found == 0) {
        fds_panic("Can't find any HDD\n");
    }
}

// dsk_hdd_iter
// ------------
//
void
DataDiscoveryModule::disk_hdd_iter(DataDiscIter *iter)
{
    for (auto it = pd_hdd_map.cbegin(); it != pd_hdd_map.cend(); it++) {
        iter->cb_root_dir = &it->second;
        iter->cb_disk_id  = it->first;
        if (iter->obj_callback() == false) {
            break;
        }
    }
}

// dsk_ssd_iter
// ------------
//
void
DataDiscoveryModule::disk_ssd_iter(DataDiscIter *iter)
{
    for (auto it = pd_ssd_map.cbegin(); it != pd_ssd_map.cend(); it++) {
        iter->cb_root_dir = &it->second;
        iter->cb_disk_id  = it->first;
        if (iter->obj_callback() == false) {
            break;
        }
    }
}

// \disk_hdd_path
// --------------
// Return the root path to access the disk at given route index.
//
const char *
DataDiscoveryModule::disk_hdd_path(fds_uint16_t disk_id)
{
    std::string &hdd = pd_hdd_map[disk_id];
    fds_verify(!hdd.empty());
    return hdd.c_str();
}

// \disk_ssd_path
// --------------
// Return the root path to access the disk at given route index.
//
const char *
DataDiscoveryModule::disk_ssd_path(fds_uint16_t disk_id)
{
    std::string &ssd = pd_ssd_map[disk_id];
    fds_verify(!ssd.empty());
    return ssd.c_str();
}

// \mod_startup
// ------------
//
void
DataDiscoveryModule::mod_startup()
{
    Module::mod_startup();
}

// \mod_shutdown
// -------------
//
void
DataDiscoveryModule::mod_shutdown()
{
    Module::mod_shutdown();
}

// ----------------------------------------------------------------------------
// \PersisDataIO::PersisDataIO
// ----------------------------------------------------------------------------
PersisDataIO::PersisDataIO()
    : pd_queue(2, 1000), pd_counters_("PM", nullptr){}

// \sersisDataIO::~PersisDataIO
// ----------------------------
//
PersisDataIO::~PersisDataIO()
{
}

// \PersisDataIO::disk_read
// ------------------------
// Handle all the queueing for persistent read IO.
//
fds::Error
diskio::PersisDataIO::disk_read(DiskRequest *req)
{
    fds::Error  err(fds::ERR_OK);
    bool block = req->req_blocking_mode();

    pd_queue.rq_enqueue(req, pd_ioq_rd_pending);
    err = disk_do_read(req);

    // In non-blocking mode, the request may already be freed when we're here.
    if (block == true) {
        // If the request was created with non-blocking option, this is no-op.
        req->req_wait();
    }
    if (err != fds::ERR_OK) {
        pd_counters_.diskR_Err.incr();
    }
    return err;
}

// \PersisDataIO::disk_read_done
// -----------------------------
//
void
PersisDataIO::disk_read_done(DiskRequest *req)
{
    // If the request was created with blocking option, this will wake it up.
    // Also note that the req may be freed after this call returns.
    req->req_complete();
}

// \PersisDataIO::disk_write
// -------------------------
//
fds::Error
diskio::PersisDataIO::disk_write(DiskRequest *req)
{
    fds::Error  err(fds::ERR_OK);
    bool block = req->req_blocking_mode();

    pd_queue.rq_enqueue(req, pd_ioq_rd_pending);
    err = disk_do_write(req);

    // In non-blocking mode, the request may already be freed when we're here.
    if (block == true) {
        // If the request was created with non-blocking option, this is no-op.
        req->req_wait();
    }
    if (err != fds::ERR_OK) {
        pd_counters_.diskR_Err.incr();
    }
    return err;
}

// \PersisDataIO::disk_write_done
// ------------------------------
//
void
PersisDataIO::disk_write_done(DiskRequest *req)
{
    // If the request was created with blocking option, this will wake it up.
    // Also note that the req may be freed after this call returns.
    req->req_complete();
}

// ----------------------------------------------------------------------------
// \DataIO
//
// Switch board interface to route data IO to the right handler.
// ----------------------------------------------------------------------------
DataIO::DataIO() {}
DataIO::~DataIO() {}

// \disk_singleton
// ---------------
//
DataIO &
DataIO::disk_singleton()
{
    return *sgt_dataIO;
}

// \DataIO::disk_read
// ------------------
//
fds::Error
diskio::DataIO::disk_read(DiskRequest *req)
{
    PersisDataIO   *iop;
    obj_phy_loc_t  *loc;

    loc = req->req_get_phy_loc();
    iop = gl_dataIOMod.disk_hdd_disk(req->getTier(),
                                     loc->obj_stor_loc_id,
                                     loc->obj_file_id,
                                     req->req_get_oid());
    return iop->disk_read(req);
}

// \DataIO::disk_write
// -------------------
//
fds::Error
diskio::DataIO::disk_write(DiskRequest *req)
{
    PersisDataIO  *iop;

    iop = gl_dataIOMod.disk_hdd_io(req->getTier(), WRITE_FILE_ID, req->req_get_oid());
    return iop->disk_write(req);
}

// \DataIO::disk_remap_obj
// -----------------------
//
void
DataIO::disk_remap_obj(meta_obj_id_t const *const obj_id,
                       meta_vol_io_t const *const cur_vio,
                       meta_vol_io_t const *const new_vio)
{
}

// \DataIO::disk_destroy_vol
// -------------------------
//
void
DataIO::disk_destroy_vol(meta_vol_adr_t *vol)
{
}

// \DataIO::disk_loc_path_info
// ---------------------------
//
void
DataIO::disk_loc_path_info(fds_uint16_t loc_id, std::string *path)
{
}

//
// Handle notification about start of garbage collection for given token
// and tier
//
void
DataIO::notify_start_gc(fds::fds_token_id tok_id,
                        DataTier tier)
{
    gl_dataIOMod.notify_start_gc(tok_id, tier);
}

//
// Notify about end of garbage collection for a given token and tier
//
fds::Error
DataIO::notify_end_gc(fds::fds_token_id tok_id,
                      DataTier tier)
{
    return gl_dataIOMod.notify_end_gc(tok_id, tier);
}


}  // namespace diskio
