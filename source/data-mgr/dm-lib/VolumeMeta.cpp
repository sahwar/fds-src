/*
 * Copyright 2013 Formation Data Systems, Inc.
 */

#include <string>
#include <list>
#include <VolumeMeta.h>
#include <fds_process.h>
#include <util/timeutils.h>

namespace fds {

VolumeMeta::VolumeMeta(const std::string& _name,
                       fds_volid_t _uuid,
                       VolumeDesc* desc)
              : fwd_state(VFORWARD_STATE_NONE), dmVolQueue(0)
{
    const FdsRootDir *root = g_fdsprocess->proc_fdsroot();

    vol_mtx = new fds_mutex("Volume Meta Mutex");
    vol_desc = new VolumeDesc(_name, _uuid);
    dmCopyVolumeDesc(vol_desc, desc);

    root->fds_mkdir(root->dir_sys_repo_dm().c_str());
    root->fds_mkdir(root->dir_user_repo_dm().c_str());
}

VolumeMeta::VolumeMeta(const std::string& _name,
                       fds_volid_t _uuid,
                       fds_log* _dm_log,
                       VolumeDesc* _desc)
        : VolumeMeta(_name, _uuid, _desc) {
}

VolumeMeta::~VolumeMeta() {
    delete vol_desc;
    delete vol_mtx;
}

// Returns true if volume is in forwarding state, and qos queue is empty;
// otherwise returns false (volume not in forwarding state or qos queue
// is not empty
//
void VolumeMeta::finishForwarding() {
    vol_mtx->lock();
    setForwardFinish();
    vol_mtx->unlock();
    LOGMIGRATE << "finishForwarding for volume " << *vol_desc
               << ", state " << fwd_state;
}

void VolumeMeta::dmCopyVolumeDesc(VolumeDesc *v_desc, VolumeDesc *pVol) {
    v_desc->name = pVol->name;
    v_desc->volUUID = pVol->volUUID;
    v_desc->tennantId = pVol->tennantId;
    v_desc->localDomainId = pVol->localDomainId;
    v_desc->globDomainId = pVol->globDomainId;

    v_desc->maxObjSizeInBytes = pVol->maxObjSizeInBytes;
    v_desc->capacity = pVol->capacity;
    v_desc->volType = pVol->volType;
    v_desc->maxQuota = pVol->maxQuota;
    v_desc->replicaCnt = pVol->replicaCnt;

    v_desc->consisProtocol = fpi::FDSP_ConsisProtoType(pVol->consisProtocol);
    v_desc->appWorkload = pVol->appWorkload;
    v_desc->mediaPolicy = pVol->mediaPolicy;

    v_desc->volPolicyId = pVol->volPolicyId;
    v_desc->iops_throttle = pVol->iops_throttle;
    v_desc->iops_assured = pVol->iops_assured;
    v_desc->relativePrio = pVol->relativePrio;
    v_desc->fSnapshot = pVol->fSnapshot;
    v_desc->srcVolumeId = pVol->srcVolumeId;
    v_desc->qosQueueId = pVol->qosQueueId;
    v_desc->contCommitlogRetention = pVol->contCommitlogRetention;
    v_desc->timelineTime = pVol->timelineTime;
}

}  // namespace fds
