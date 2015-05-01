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
#include <fds_error.h>
#include <fds_volume.h>
#include <fds_typedefs.h>
#include <util/Log.h>


namespace FDS_ProtocolInterface {
class FDSP_AnnounceDiskCapability;
};
namespace fpi = FDS_ProtocolInterface;

namespace fds {

class FdsAdminCtrl {
  public:
    FdsAdminCtrl(const std::string& om_prefix, fds_log* om_log);
    ~FdsAdminCtrl();

    // Defines minimum object size in a volume in bytes
    static const fds_uint32_t minVolObjSize = 512;

    /* Per local domain  dynamic disk resource  counters */
    fds_uint64_t  total_node_iops_max;
    fds_uint64_t  total_node_iops_min;
    fds_uint64_t  total_disk_capacity;
    fds_uint64_t  total_ssd_capacity;

    /* Available  capacity */
    fds_uint64_t  avail_node_iops_max;
    fds_uint64_t  avail_node_iops_min;
    fds_uint64_t  avail_disk_capacity;
    fds_uint64_t  avail_ssd_capacity;

    /*  per volume resouce  counter */
    fds_uint64_t  total_vol_iops_min;
    fds_uint64_t  total_vol_iops_max;
    double        total_vol_disk_cap_GB;

    void addDiskCapacity(const fpi::FDSP_AnnounceDiskCapability *caps);
    void removeDiskCapacity(const fpi::FDSP_AnnounceDiskCapability *caps);
    Error volAdminControl(VolumeDesc *pVolDesc);
    Error checkVolModify(VolumeDesc *cur_desc, VolumeDesc *new_desc);
    void updateAdminControlParams(VolumeDesc  *pVolInfo);

    /* returns max iopc of the subcluster*/
    fds_uint64_t getMaxIOPC() const;

    /**
     * Translates volume qos policy provided by the user
     * to a volume qos policy for a specific service
     * @param[in,out] voldesc volume descriptor containing volume's qos
     * policy; when method returns, qos specific fields will be overwritten
     * to a setting for volume for a given service
     * @param[in] svc_type is type of service (AM, SM, or DM)
     */
    void userQosToServiceQos(FDS_ProtocolInterface::FDSP_VolumeDescType *voldesc,
                             FDS_ProtocolInterface::FDSP_MgrIdType svc_type);

  private:
    void initDiskCapabilities();

    /* number of nodes reported disk capacity */
    fds_uint32_t num_nodes;

    /* parent log */
    fds_log* parent_log;
};

}  // namespace fds
#endif  // SOURCE_ORCH_MGR_INCLUDE_OMADMINCTRL_H_
