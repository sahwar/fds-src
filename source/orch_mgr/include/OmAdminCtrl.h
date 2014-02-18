/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

/** Addmission Control class */
#ifndef SOURCE_ORCH_MGR_INCLUDE_OMADMINCTRL_H_
#define SOURCE_ORCH_MGR_INCLUDE_OMADMINCTRL_H_

#include <unordered_map>
#include <cstdio>
#include <string>
#include <vector>

#include <fds_types.h>
#include <fds_err.h>
#include <fds_placement_table.h>
#include <fds_volume.h>
#include <fds_typedefs.h>
#include <util/Log.h>
#include <platform/node-inventory.h>

namespace fds {

typedef fpi::FDSP_VolumeInfoTypePtr FdspVolInfoPtr;

class FdsAdminCtrl {
  public:
    FdsAdminCtrl(const std::string& om_prefix, fds_log* om_log);
    ~FdsAdminCtrl();

    /* Per local domain  dynamic disk resource  counters */
    fds_uint64_t  total_disk_iops_max;
    fds_uint64_t  total_disk_iops_min;
    fds_uint64_t  total_disk_capacity;
    fds_uint64_t  disk_latency_max;
    fds_uint64_t  disk_latency_min;
    fds_uint64_t  total_ssd_iops_max;
    fds_uint64_t  total_ssd_iops_min;
    fds_uint64_t  total_ssd_capacity;
    fds_uint64_t  ssd_latency_max;
    fds_uint64_t  ssd_latency_min;

    /* Available  capacity */
    fds_uint64_t  avail_disk_iops_max;
    fds_uint64_t  avail_disk_iops_min;
    fds_uint64_t  avail_disk_capacity;
    fds_uint64_t  avail_ssd_iops_max;
    fds_uint64_t  avail_ssd_iops_min;
    fds_uint64_t  avail_ssd_capacity;

    /*  per volume resouce  counter */
    fds_uint64_t   total_vol_iops_min;
    fds_uint64_t   total_vol_iops_max;
    fds_uint64_t   total_vol_disk_cap;

    void addDiskCapacity(const node_capability_t &n_info);
    void removeDiskCapacity(const node_capability_t &n_info);
    void getAvailableDiskCapacity(const FdspVolInfoPtr  pVolInfo);
    void updateAvailableDiskCapacity(const FdspVolInfoPtr pVolInfo);
    Error volAdminControl(VolumeDesc *pVolDesc);
    Error checkVolModify(VolumeDesc *cur_desc, VolumeDesc *new_desc);
    void updateAdminControlParams(VolumeDesc  *pVolInfo);

  private:
    void initDiskCapabilities();

    /* parent log */
    fds_log* parent_log;
};

}  // namespace fds
#endif  // SOURCE_ORCH_MGR_INCLUDE_OMADMINCTRL_H_
