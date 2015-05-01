/*
 * Copyright 2013 Formation Data Systems, Inc.
 */
#include <string>
#include <OmAdminCtrl.h>

#define REPLICATION_FACTOR     (4)

// TRUST Anna & Vinay on these values :)
#define LOAD_FACTOR            (0.9)
#define BURST_FACTOR           (0.3)

namespace fds {

FdsAdminCtrl::FdsAdminCtrl(const std::string& om_prefix, fds_log* om_log)
        : parent_log(om_log), num_nodes(0) {
    /* init the disk  resource  variable */
    initDiskCapabilities();
}

FdsAdminCtrl::~FdsAdminCtrl() {
}

void FdsAdminCtrl::addDiskCapacity(const fpi::FDSP_AnnounceDiskCapability *diskCaps)
{
    ++num_nodes;

    total_node_iops_max += diskCaps->node_iops_max;
    total_node_iops_min += diskCaps->node_iops_min;
    total_disk_capacity += diskCaps->disk_capacity;
    total_ssd_capacity  += diskCaps->ssd_capacity;

    avail_node_iops_max += diskCaps->node_iops_max;
    avail_node_iops_min += diskCaps->node_iops_min;
    avail_disk_capacity += diskCaps->disk_capacity;
    avail_ssd_capacity  += diskCaps->ssd_capacity;

    LOGNOTIFY << "Total Disk Resources "
              << "\n  Total Node iops Max : " << total_node_iops_max
              << "\n  Total Node iops Min : " << total_node_iops_min
              << "\n  Total Disk capacity : " << total_disk_capacity
              << "\n  Total Ssd capacity : " << total_ssd_capacity
              << "\n  Avail Disk capacity : " << avail_disk_capacity
              << "\n  Avail Disk  iops max : " << avail_node_iops_max
              << "\n  Avail Disk  iops min : " << avail_node_iops_min;
}

void FdsAdminCtrl::removeDiskCapacity(const fpi::FDSP_AnnounceDiskCapability *diskCaps)
{
    fds_verify(num_nodes > 0);
    --num_nodes;

    fds_verify(total_node_iops_max >= static_cast<fds_uint64_t>(diskCaps->node_iops_max));
    total_node_iops_max -= diskCaps->node_iops_max;
    total_node_iops_min -= diskCaps->node_iops_min;

    total_disk_capacity -= diskCaps->disk_capacity;
    total_ssd_capacity  -= diskCaps->ssd_capacity;

    avail_node_iops_max -= diskCaps->node_iops_max;
    avail_node_iops_min -= diskCaps->node_iops_min;
    avail_disk_capacity -= diskCaps->disk_capacity;
    avail_ssd_capacity  -= diskCaps->ssd_capacity;

    LOGNOTIFY << "Total Disk Resources "
              << "\n  Total Node iops Max : " << total_node_iops_max
              << "\n  Total Node iops Min : " << total_node_iops_min
              << "\n  Total Disk capacity : " << total_disk_capacity
              << "\n  Total Ssd capacity : " << total_ssd_capacity
              << "\n  Avail Disk capacity : " << avail_disk_capacity
              << "\n  Avail Disk  iops max : " << avail_node_iops_max
              << "\n  Avail Disk  iops min : " << avail_node_iops_min;
}

void FdsAdminCtrl::updateAdminControlParams(VolumeDesc  *pVolDesc)
{
    /* release  the resources since volume is deleted */

    // remember that volume descriptor has capacity in MB
    // but disk and ssd capacity is in GB
    double vol_capacity_GB = pVolDesc->capacity / 1024;

    LOGNOTIFY<< "desc iops_min: " << pVolDesc->iops_min
             << "desc iops_max: " << pVolDesc->iops_max
             << "desc capacity (MB): " << pVolDesc->capacity
             << "total iops min : " << total_vol_iops_min
             << "total iops max: " << total_vol_iops_max
             << "total capacity (GB): " << total_vol_disk_cap_GB;
    fds_verify(pVolDesc->iops_min <= total_vol_iops_min);
    fds_verify(pVolDesc->iops_max <= total_vol_iops_max);
    fds_verify(vol_capacity_GB <= total_vol_disk_cap_GB);

    total_vol_iops_min -= pVolDesc->iops_min;
    total_vol_iops_max -= pVolDesc->iops_max;
    total_vol_disk_cap_GB -= vol_capacity_GB;
}

fds_uint64_t FdsAdminCtrl::getMaxIOPC() const {
    double replication_factor = REPLICATION_FACTOR;
    if (num_nodes == 0) return 0;

    if (replication_factor > num_nodes) {
        // we will access at most num_nodes nodes
        replication_factor = num_nodes;
    }
    fds_verify(replication_factor != 0);  // make sure REPLICATION_FACTOR > 0
    double max_iopc_subcluster = (avail_node_iops_max/replication_factor);
    return max_iopc_subcluster;
}

Error FdsAdminCtrl::volAdminControl(VolumeDesc  *pVolDesc)
{
    Error err(ERR_OK);
    double iopc_subcluster = 0;
    double max_iopc_subcluster = 0;
    double iopc_subcluster_result = 0;
    // remember that volume descriptor has capacity in MB
    // but disk and ssd capacity is in GB
    double vol_capacity_GB = pVolDesc->capacity / 1024;
    fds_uint32_t replication_factor = REPLICATION_FACTOR;

    fds_verify(replication_factor != 0);  // make sure REPLICATION_FACTOR > 0
    if (replication_factor > num_nodes) {
        // we will access at most num_nodes nodes
        replication_factor = num_nodes;
    }
    if (replication_factor == 0) {
        // if we are here, then num_nodes == 0 (whis is # of SMs)
        LOGERROR << "Cannot admit any volume if there are no SMs! "
                 << " Start at least on SM and try again!";
        return Error(ERR_VOL_ADMISSION_FAILED);
    }

    // using iops min for iopc of subcluster, which we will use
    // for min_iops admission; max iops is what AM will allow to SMs
    // for better utilization of system perf capacity. We are starting
    // with more pessimistic admission control on min_iops, once QoS is
    // more stable, we may admit on average case or whatever we will find
    // works best
    iopc_subcluster = (avail_node_iops_min/replication_factor);
    // TODO(Anna) I think max_iopc_subcluster should be calculated as
    // num_nodes * min(node_iops_min from all nodes) / replication_factor
    max_iopc_subcluster = (avail_node_iops_max/replication_factor);

    if (pVolDesc->iops_guarantee <= 0) {
        LOGWARN << "iops gurantee is zero";
        pVolDesc->iops_guarantee = 0;
    }

    // https://formationds.atlassian.net/browse/FS-597
    pVolDesc->iops_min = int(pVolDesc->iops_guarantee * 0.01 *
            ((pVolDesc->iops_max == 0)?iopc_subcluster*LOAD_FACTOR:pVolDesc->iops_max));
    
    LOGNORMAL << "new data "
              << "[iops.min:" << pVolDesc->iops_min << "] "
              << "[iops.max:" << pVolDesc->iops_max << "] "
              << "[iops.guarantee:" << pVolDesc->iops_guarantee << "] ";

    // Check max object size
    if ((pVolDesc->maxObjSizeInBytes < minVolObjSize) ||
        ((pVolDesc->maxObjSizeInBytes % minVolObjSize) != 0)) {
        // We expect the max object size to be at least some min size
        // and a multiple of that size
        LOGERROR << "Invalid maximum object size of " << pVolDesc->maxObjSizeInBytes
                 << ", the minimum size is " << minVolObjSize;
        return Error(ERR_VOL_ADMISSION_FAILED);
    }

    if ((pVolDesc->iops_max > 0) && (pVolDesc->iops_min > pVolDesc->iops_max)) {
        LOGERROR << " Cannot admit volume " << pVolDesc->name
                 << " -- iops_min must be below iops_max";
        return Error(ERR_VOL_ADMISSION_FAILED);
    }

    if ((total_vol_disk_cap_GB + vol_capacity_GB) > avail_disk_capacity) {
        LOGERROR << " Cluster is running out of disk capacity \n"
                 << " Volume's capacity (GB) " << vol_capacity_GB
                 << "total volume disk  capacity (GB):" << total_vol_disk_cap_GB;
        return Error(ERR_VOL_ADMISSION_FAILED);
    }

    LOGNORMAL << *pVolDesc;
    LOGNORMAL << " iopc_subcluster: " << iopc_subcluster
              << " replication_factor: " << replication_factor
              << " iops_max: " << total_vol_iops_max
              << " iops_min: " << total_vol_iops_min
              << " loadfactor: " << LOAD_FACTOR
              << " BURST_FACTOR: " << BURST_FACTOR;

    /* check the resource availability, if not return Error  */
    if (((total_vol_iops_min + pVolDesc->iops_min) <= (iopc_subcluster * LOAD_FACTOR)) &&
        ((((total_vol_iops_min) +
           ((total_vol_iops_max - total_vol_iops_min) * BURST_FACTOR))) + \
         (pVolDesc->iops_min +
          ((pVolDesc->iops_max - pVolDesc->iops_min) * BURST_FACTOR)) <= \
         max_iopc_subcluster)) {
        total_vol_iops_min += pVolDesc->iops_min;
        total_vol_iops_max += pVolDesc->iops_max;
        total_vol_disk_cap_GB += vol_capacity_GB;

        LOGNOTIFY << "updated disk params disk-cap:"
                  << avail_disk_capacity << ":: min:"
                  << total_vol_iops_min << ":: max:"
                  << total_vol_iops_max << "\n";
        LOGNORMAL << " admin control successful \n";
        return Error(ERR_OK);
    } else  {
        LOGERROR << " Unable to create Volume,Running out of IOPS";
        LOGERROR << "Available disk Capacity:"
                 << avail_disk_capacity << "::Total min IOPS:"
                 <<  total_vol_iops_min << ":: Total max IOPS:"
                 << total_vol_iops_max;
        return Error(ERR_VOL_ADMISSION_FAILED);
    }
    return err;
}

Error FdsAdminCtrl::checkVolModify(VolumeDesc *cur_desc, VolumeDesc *new_desc)
{
    Error err(ERR_OK);
    double cur_total_vol_disk_cap_GB = total_vol_disk_cap_GB;
    fds_uint64_t cur_total_vol_iops_min = total_vol_iops_min;
    fds_uint64_t cur_total_vol_iops_max = total_vol_iops_max;

    /* check modify by first subtracting current vol values and doing admin 
     * control based on new values */
    updateAdminControlParams(cur_desc);

    err = volAdminControl(new_desc);
    if (!err.ok()) {
        /* cannot admit volume -- revert to original values */
        total_vol_disk_cap_GB = cur_total_vol_disk_cap_GB;
        total_vol_iops_min = cur_total_vol_iops_min;
        total_vol_iops_max = cur_total_vol_iops_max;
        return err;
    }
    /* success */
    return err;
}

void FdsAdminCtrl::initDiskCapabilities()
{
    total_node_iops_max = 0;
    total_node_iops_min = 0;
    total_disk_capacity = 0;
    total_ssd_capacity = 0;

    /* Available  capacity */
    avail_node_iops_max = 0;
    avail_node_iops_min = 0;
    avail_disk_capacity = 0;
    avail_ssd_capacity = 0;
    /*  per volume capacity */
    total_vol_iops_min = 0;
    total_vol_iops_max = 0;
    total_vol_disk_cap_GB = 0;
}

void FdsAdminCtrl::userQosToServiceQos(fpi::FDSP_VolumeDescType *voldesc,
                                       fpi::FDSP_MgrIdType svc_type) {
    fds_verify(voldesc != NULL);
    // currently only SM has actual translation
    if (svc_type == fpi::FDSP_STOR_MGR) {
        double replication_factor = REPLICATION_FACTOR;
        if (num_nodes == 0) return;  // for now just keep user qos policy

        if (replication_factor > num_nodes) {
            // we will access at most num_nodes nodes
            replication_factor = num_nodes;
        }
        fds_verify(replication_factor != 0);  // make sure REPLICATION_FACTOR > 0

        // translate min_iops
        double vol_min_iops = voldesc->iops_min;
        voldesc->iops_min = vol_min_iops * replication_factor / num_nodes;
        LOGNORMAL << "User min_iops " << vol_min_iops
                  << " SM volume's min_iops " << voldesc->iops_min;
    }
}

}  // namespace fds
