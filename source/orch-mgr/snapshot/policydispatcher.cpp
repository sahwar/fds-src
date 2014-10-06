/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#include <vector>

#include <snapshot/policydispatcher.h>
#include <OmResources.h>
#include <orchMgr.h>
#include <snapshot/svchandler.h>

#include <util/stringutils.h>
#include <util/timeutils.h>
#include <unistd.h>

namespace atc = apache::thrift::concurrency;
namespace fds { namespace snapshot {

PolicyDispatcher::PolicyDispatcher(OrchMgr* om) : om(om),
                                                  runner(&PolicyDispatcher::run, this) {
}

bool PolicyDispatcher::process(const Task& task) {
    LOGDEBUG << "received message to process task : " << task.policyId;
    atc::Synchronized s(monitor);
    policyq.push(task.policyId);
    monitor.notifyAll();
    return true;
}

void PolicyDispatcher::run() {
    uint64_t policyId = 0;

    while (true) {
        // this block is needed because we need to hold the lock only for a short time
        {
            atc::Synchronized s(monitor);
            while (policyq.empty()) {
                monitor.waitForever();
            }
            policyId = policyq.front();
            policyq.pop();
        }

        std::vector<int64_t> vecVolumes;
        om->getConfigDB()->listVolumesForSnapshotPolicy(vecVolumes, policyId);

        LOGDEBUG << "snapshot requests will be dispatched for ["
                 << vecVolumes.size() << "] volumes";

        fpi::SnapshotPolicy policy;
        om->getConfigDB()->getSnapshotPolicy(policyId, policy);

        VolumeDesc volumeDesc("", 1);
        for (auto volId : vecVolumes) {
            if (!om->getConfigDB()->getVolume(volId, volumeDesc)) {
                LOGWARN << "unable to get infor for vol: " << volId;
                continue;
            }

            // create the structure
            fpi::Snapshot snapshot;
            snapshot.snapshotName = util::strlower(
                util::strformat("snap.%s.%s.%ld",
                                volumeDesc.name.c_str(),
                                policy.policyName.c_str(),
                                util::getTimeStampMillis()));
            snapshot.volumeId = volId;
            snapshot.snapshotId = getUuidFromVolumeName(snapshot.snapshotName);
            snapshot.snapshotPolicyId = policyId;
            snapshot.creationTimestamp = util::getTimeStampMillis();
            snapshot.retentionTimeSeconds = policy.retentionTimeSeconds;
            // activate snap right away.
            snapshot.state = fpi::ResourceState::Loading;
            LOGDEBUG << "snapshot request for volumeid:" << volId
                     << " name:" << snapshot.snapshotName;

            // send the snapshot create request
            // NOTE : we are not sending the snapshot create request anymore
            // it will be handled by addVolume ...
            // om->snapshotMgr.svcHandler.omSnapshotCreate(snapshot);

            OM_NodeContainer *local = OM_NodeDomainMod::om_loc_domain_ctrl();
            VolumeContainer::pointer volContainer = local->om_vol_mgr();
            fds::Error err = volContainer->addSnapshot(snapshot);
            if ( !err.ok() ) {
                LOGWARN << "snapshot add failed : " << err;
                continue;
            }
            // add this snapshot to the retention manager ...
            om->snapshotMgr.deleteScheduler->addSnapshot(snapshot);
        }
    }
}

}  // namespace snapshot
}  // namespace fds
