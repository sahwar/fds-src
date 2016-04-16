/*
 * Copyright 2013 Formation Data Systems, Inc.
 */

// Standard includes.
#include <chrono>
#include <list>
#include <string>
#include <vector>

// System includes.
#include <boost/filesystem.hpp>
#include <fiu-control.h>
#include <fiu-local.h>

// Internal includes.
#include "dm-vol-cat/DmPersistVolDB.h"
#include "fdsp/event_api_types.h"
#include "fdsp/sm_api_types.h"
#include "fdsp/svc_types_types.h"
#include "lib/StatsCollector.h"
#include "net/net_utils.h"
#include "net/volumegroup_extensions.h"
#include "refcount/refcountmanager.h"
#include "timeline/timelinemanager.h"
#include "util/disk_utils.h"
#include "util/path.h"
#include "util/stringutils.h"
#include "counters.h"
#include "dmhandler.h"
#include "DmMigrationMgr.h"
#include "fds_timestamp.h"
#include "requestmanager.h"

// Class include.
#include "DataMgr.h"

#define VOLUME_REQUEST_CHECK() \
    auto volMeta = parentDm->getVolumeMeta(io->volId); \
    if (volMeta == nullptr) { \
        LOGWARN << "Volume not found. " << io->log_string(); \
        io->cb(ERR_VOL_NOT_FOUND, io); \
        break; \
    } \

namespace fds {

struct RemoveErroredVolume {
    explicit RemoveErroredVolume(DataMgr* dm, fds_volid_t vol_uuid)
            : dm(dm) , vol_uuid(vol_uuid) {}
    ~RemoveErroredVolume() {
        FDSGUARD(dm->vol_map_mtx);
        auto iter = dm->vol_meta_map.find(vol_uuid);
        if (iter != dm->vol_meta_map.end() && iter->second == NULL) {
            dm->vol_meta_map.erase(iter);
            LOGWARN << "vol:" << vol_uuid << " NOT added";
        } else {
            LOGNORMAL << "vol:" << vol_uuid << " added successfully";
        }
    }
    DataMgr* dm;
    fds_volid_t vol_uuid;
};

const std::hash<fds_volid_t> DataMgr::dmQosCtrl::volIdHash;
const std::hash<std::string> DataMgr::dmQosCtrl::blobNameHash;
const DataMgr::dmQosCtrl::SerialKeyHash DataMgr::dmQosCtrl::keyHash;

/**
 * Receiver DM processing of volume sync state.
 * @param[in] fdw_complete false if rsync is completed = start processing
 * forwarded updates (activate shadow queue); true if forwarding updates
 * is completed (activate volume queue)
 */
Error
DataMgr::processVolSyncState(fds_volid_t volume_id, fds_bool_t fwd_complete) {
    Error err(ERR_OK);
    LOGMIGRATE << "DM received volume sync state for volume " << std::hex
               << volume_id << std::dec << " forward complete? " << fwd_complete;

    // open catalogs so we can start processing updates
    // TODO(Andrew): Actually do something if this fails. It doesn't really matter
    // now because our caller never replies to other other DM anyways...
    fds_verify(ERR_OK == timeVolCat_->activateVolume(volume_id));

    if (!fwd_complete) {
        err = timeVolCat_->activateVolume(volume_id);
        if (err.ok()) {
            // start processing forwarded updates
            if (features.isCatSyncEnabled()) {
                catSyncRecv->startProcessFwdUpdates(volume_id);
            } else {
                LOGWARN << "catalog sync feature - NOT enabled";
            }
        }
    } else {
        LOGDEBUG << "Will queue DmIoMetaRecvd message "
                 << std::hex << volume_id << std::dec;
        if (features.isCatSyncEnabled()) {
            DmIoMetaRecvd* metaRecvdIo = new(std::nothrow) DmIoMetaRecvd(volume_id);
            err = enqueueMsg(catSyncRecv->shadowVolUuid(volume_id), metaRecvdIo);
            fds_verify(err.ok());
        } else {
            LOGWARN << "catalog sync feature - NOT enabled";
        }
    }

    if (!err.ok()) {
        LOGERROR << "DM failed to process vol sync state for volume " << std::hex
                 << volume_id << " fwd complete? " << fwd_complete << " " << err;
    }

    return err;
}

//
// receiving DM notified that forward is complete and ok to open
// qos queue. Some updates may still be forwarded, but they are ok to
// go in parallel with updates from AM
//
void DataMgr::handleForwardComplete(DmRequest *io) {
    LOGNORMAL << "Will open up QoS queue for volume " << std::hex
              << io->volId << std::dec;
    if (features.isCatSyncEnabled()) {
        catSyncRecv->handleFwdDone(io->volId);
    } else {
        LOGWARN << "catalog sync feature - NOT enabled";
    }

    vol_map_mtx->lock();
    auto volIt = vol_meta_map.find(io->volId);
    fds_verify(vol_meta_map.end() != volIt);
    auto vol_meta = volIt->second;
    vol_meta->dmVolQueue->activate();
    vol_map_mtx->unlock();

    if (features.isQosEnabled()) qosCtrl->markIODone(*io);
    delete io;
}

void DataMgr::handleDmFunctor(DmRequest *io)
{
    {
        dm::QueueHelper helper(*this, io);
        if (!io->cb) {
            helper.skipImplicitCb = true;
        }

        DmFunctor *request = static_cast<DmFunctor*>(io);
        request->func();
    }
    if (!io->cb) {
        delete io;
    }
}

//
// Called by stats collector to sample DM specific stats
//
void DataMgr::sampleDMStats(fds_uint64_t timestamp) {
    LOGTRACE << "Sampling DM stats";
    std::unordered_set<fds_volid_t> prim_vols;
    std::unordered_set<fds_volid_t>::const_iterator cit;

    // find all volumes for which this DM is primary (we only need
    // to collect capacity stats from DMs that are primary).
    vol_map_mtx->lock();
    for (const auto& iter : vol_meta_map) {
        if (!iter.second || iter.second->vol_desc->fSnapshot) {
            continue;
        }
        fds_volid_t volId (iter.first);
        if (amIPrimary(volId)) {
            prim_vols.insert(volId);
        }
    }
    vol_map_mtx->unlock();

    // collect usage stats for volumes for which this DM is primary
    for (cit = prim_vols.cbegin(); cit != prim_vols.cend(); ++cit) {
        sampleDMStatsForVol(*cit, timestamp);
    }

    /*
     * Piggyback on this method to determine if we're nearing disk capacity
     */
    if (sampleCounter % 5 == 0) {
        LOGDEBUG << "Checking disk utilization!";
        float_t pct_used = timeVolCat_->getUsedCapacityAsPct();

        // We want to log which disk is too full here
        if (pct_used >= DISK_CAPACITY_ERROR_THRESHOLD &&
            lastCapacityMessageSentAt < DISK_CAPACITY_ERROR_THRESHOLD) {
            LOGERROR << "ERROR: DM is utilizing " << pct_used << "% of available storage space!";
            lastCapacityMessageSentAt = pct_used;

            // set time volume catalog to unavailable -- no available storage space
            timeVolCat_->setUnavailable();

            // Send message to OM
            requestMgr->sendHealthCheckMsgToOM(fpi::HEALTH_STATE_ERROR, ERR_SERVICE_CAPACITY_FULL, "DM capacity is FULL! ");

        } else if (pct_used >= DISK_CAPACITY_ALERT_THRESHOLD &&
                   lastCapacityMessageSentAt < DISK_CAPACITY_ALERT_THRESHOLD) {
            LOGWARN << "ATTENTION: DM is utilizing " << pct_used << " of available storage space!";
            lastCapacityMessageSentAt = pct_used;

            // Send message to OM
            requestMgr->sendHealthCheckMsgToOM(fpi::HEALTH_STATE_LIMITED,
                                               ERR_SERVICE_CAPACITY_DANGEROUS, "DM is reaching dangerous capacity levels!");

        } else if (pct_used >= DISK_CAPACITY_WARNING_THRESHOLD &&
                   lastCapacityMessageSentAt < DISK_CAPACITY_WARNING_THRESHOLD) {
            LOGNORMAL << "DM is utilizing " << pct_used << " of available storage space!";

            requestMgr->sendEventMessageToOM(fpi::SYSTEM_EVENT, fpi::EVENT_CATEGORY_STORAGE,
                                             fpi::EVENT_SEVERITY_WARNING,
                                             fpi::EVENT_STATE_SOFT,
                                             "dm.capacity.warn",
                                             std::vector<fpi::MessageArgs>(), "");

            lastCapacityMessageSentAt = pct_used;
        } else {
            // If the used pct drops below alert levels reset so we resend the message when
            // we re-hit this condition
            if (pct_used < DISK_CAPACITY_ALERT_THRESHOLD) {
                lastCapacityMessageSentAt = 0;

                // Send message to OM resetting us to OK state
                requestMgr->sendHealthCheckMsgToOM(fpi::HEALTH_STATE_RUNNING, ERR_OK,
                                                   "DM utilization no longer at dangerous levels.");
            }
        }
        sampleCounter = 0;
    }
    sampleCounter++;

}

void DataMgr::sampleDMStatsForVol(fds_volid_t volume_id,
                                  fds_uint64_t timestamp) {
    Error err(ERR_OK);

    fds_uint64_t total_lbytes = 0;
    fds_uint64_t total_pbytes = 0;
    fds_uint64_t total_blobs = 0;
    fds_uint64_t total_lobjects = 0;
    fds_uint64_t total_pobjects = 0;

    if (timestamp == 0) {
        timestamp = util::getTimeStampNanos();
    }

    err = timeVolCat_->queryIface()->statVolumeLogical(volume_id,
                                                       &total_lbytes,
                                                       &total_blobs,
                                                       &total_lobjects);
    if (!err.ok()) {
        if (err.GetErrno() != ERR_VOL_NOT_FOUND) {
            LOGERROR << "Failed to get logical usage for vol " << volume_id << " " << err;
        }
        return;
    }

    err = timeVolCat_->queryIface()->statVolumePhysical(volume_id,
                                                        &total_pbytes,
                                                        &total_pobjects);
    if (!err.ok()) {
        if (err.GetErrno() != ERR_VOL_NOT_FOUND) {
            LOGERROR << "Failed to get physical usage for vol " << volume_id << " " << err;
        }
        return;
    }

    LOGDEBUG << "volume " << volume_id
             << " lbytes " << total_lbytes << " pbytes " << total_pbytes << " blobs "
             << total_blobs << " lobjects " << total_lobjects << " pobjects " << total_pobjects;
    StatsCollector::singleton()->recordEvent(volume_id,
                                             timestamp,
                                             STAT_DM_CUR_LBYTES,
                                             total_lbytes);
    StatsCollector::singleton()->recordEvent(volume_id,
                                             timestamp,
                                             STAT_DM_CUR_PBYTES,
                                             total_pbytes);
    StatsCollector::singleton()->recordEvent(volume_id,
                                             timestamp,
                                             STAT_DM_CUR_BLOBS,
                                             total_blobs);
    StatsCollector::singleton()->recordEvent(volume_id,
                                             timestamp,
                                             STAT_DM_CUR_LOBJECTS,
                                             total_lobjects);
    StatsCollector::singleton()->recordEvent(volume_id,
                                             timestamp,
                                             STAT_DM_CUR_POBJECTS,
                                             total_pobjects);
}

void DataMgr::handleLocalStatStream(fds_uint64_t start_timestamp,
                                    fds_volid_t volume_id,
                                    const std::vector<StatSlot>& slots) {
    LOGTRACE << "Streaming local stats to stats aggregator";
    statStreamAggr_->handleModuleStatStream(start_timestamp,
                                            volume_id,
                                            slots);
}

void DataMgr::handleInitVolCheck(DmRequest *dmRequest) {
    dm::QueueHelper helper(*this, dmRequest);

    DmIoVolumeCheck *request = static_cast<DmIoVolumeCheck*>(dmRequest);
    auto msgPtr = request->reqMessage;
    fds_volid_t incomingVolId;
    incomingVolId.v = msgPtr->volume_id;
    fpi::SvcUuid checkerUuid(msgPtr->volCheckerNodeUuid);
    int batchSize = msgPtr->batch_size;

    LOGDEBUG << "Received initial checker message for volume " << incomingVolId.v;
    // We do things now on the volumeMeta context
    auto volMeta = getVolumeMeta(incomingVolId, false);
    if (volMeta == nullptr) {
        LOGERROR << "Volume Metadata not found for specified volume: "
                << incomingVolId;
        helper.err = ERR_VOL_NOT_FOUND;
        return;
    } else {
        helper.skipImplicitCb = true;
        helper.err = volMeta->createHashCalcContext(dmRequest,
                                                    checkerUuid,
                                                    batchSize);
        helper.markIoDone();
    }
}
/**
 * Is called on timer to finish forwarding for all volumes that
 * are still forwarding, send DMT close ack, and remove vcat/tcat
 * of volumes that do not longer this DM responsibility
 */
void
DataMgr::finishCloseDMT() {
    std::unordered_set<fds_volid_t> done_vols;
    fds_bool_t all_finished = false;

    LOGNOTIFY << "Finish handling DMT close and send ack if not sent yet";

    // move all volumes that are in 'finish forwarding' state
    // to not forwarding state, and make a set of these volumes
    // the set is used so that we call catSyncMgr to cleanup/and
    // and send push meta done msg outside of vol_map_mtx lock
    vol_map_mtx->lock();
    for (auto iter = vol_meta_map.begin();
         iter != vol_meta_map.end();
         ++iter) {
        if (!iter->second) continue;
        fds_volid_t volid (iter->first);
        auto vol_meta = iter->second;
        if (vol_meta->isForwarding()) {
            vol_meta->setForwardFinish();
            done_vols.insert(volid);
        }
        // we expect that volume is not forwarding
        fds_verify(!vol_meta->isForwarding());
    }
    vol_map_mtx->unlock();

    for (std::unordered_set<fds_volid_t>::const_iterator it = done_vols.cbegin();
         it != done_vols.cend();
         ++it) {
        // remove the volume from sync_volume list
        LOGNORMAL << "CleanUP: remove Volume " << std::hex << *it << std::dec;
        if (features.isCatSyncEnabled()) {
            if (catSyncMgr->finishedForwardVolmeta(*it)) {
                all_finished = true;
            }
        } else {
            LOGWARN << "catalog sync feature - NOT enabled";
            all_finished = true;
        }
    }

    // at this point we expect that all volumes are done
    fds_verify(all_finished || !catSyncMgr->isSyncInProgress());

    // Note that finishedForwardVolmeta sends ack to close DMT when it returns true
    // either in this method or when we called finishedForwardVolume for the last
    // volume to finish forwarding

    LOGDEBUG << "Will cleanup catalogs for volumes DM is not responsible anymore";
    // TODO(Anna) remove volume catalog for volumes we finished forwarding
}

Error
DataMgr::deleteSnapshot(const fds_uint64_t snapshotId) {
    // TODO(umesh): implement this
    return ERR_OK;
}

std::string DataMgr::getSnapDirBase() const
{
    const FdsRootDir *root = MODULEPROVIDER()->proc_fdsroot();
    return root->dir_user_repo_dm();
}

std::string DataMgr::getSysVolumeName(const fds_volid_t &volId) const
{
    std::stringstream stream;
    stream << "SYSTEM_VOLUME_" << getVolumeDesc(volId)->tennantId;
    return stream.str();
}

const VolumeDesc* DataMgr::getVolumeDesc(fds_volid_t volId) const {
    FDSGUARD(vol_map_mtx);
    auto iter = vol_meta_map.find(volId);
    return (vol_meta_map.end() != iter && iter->second ?
            iter->second->vol_desc : nullptr);
}

void DataMgr::getActiveVolumes(std::vector<fds_volid_t>& vecVolIds) {
    vecVolIds.reserve(vol_meta_map.size());
    for (const auto& iter : vol_meta_map) {
        if (!iter.second) continue;
        const VolumeDesc* vdesc = iter.second->vol_desc;
        if (vdesc->isStateActive() && !vdesc->isSnapshot()) {
            vecVolIds.push_back(vdesc->volUUID);
        }
    }
}

std::string DataMgr::getSnapDirName(const fds_volid_t &volId,
                                    const fds_volid_t snapId) const
{
    std::stringstream stream;
    stream << getSnapDirBase() << "/" << volId << "/" << snapId;
    return stream.str();
}

void DataMgr::deleteUnownedVolumes() {
    LOGNORMAL << "DELETING unowned volumes";

    std::vector<fds_volid_t> volIds;
    std::vector<fds_volid_t> deleteList;
    auto mySvcUuid = MODULEPROVIDER()->getSvcMgr()->getSelfSvcUuid();

    /* Filter out unowned volumes */
    dmutil::getVolumeIds(MODULEPROVIDER()->proc_fdsroot(), volIds);
    for (const auto &volId : volIds) {
        DmtColumnPtr nodes = MODULEPROVIDER()->getSvcMgr()->getDMTNodesForVolume(volId);
        if (nodes->find(NodeUuid(mySvcUuid)) == -1) {
            /* Volume is not owned..add for delete*/
            deleteList.push_back(volId);
        }
    }

    /* Delete unowned volumes one at a time */
    for (const auto &volId : deleteList) {
        LOGNORMAL << "DELETING volume: " << volId;
        /* Flow for deleting the volume is to mark it for delete first
         * followed by acutally deleting it
         */
        Error err = removeVolume(volId, true);        // Mark for delete
        if (err == ERR_OK) {
            err = removeVolume(volId, false);         // Do the actual delete
        }
        // TODO(xxx): Remove snapshots once the api is available
        if (err != ERR_OK) {
            LOGWARN << "Ecountered error: " << err << " while deleting volume: " << volId;
        }
    }

}

//
// handle finish forward for volume 'volid'
//
void DataMgr::finishForwarding(fds_volid_t volid) {
    // FIXME(DAC): This local is only ever assigned.
    fds_bool_t all_finished = false;

    // set the state
    vol_map_mtx->lock();
    auto volIt = vol_meta_map.find(volid);
    fds_assert(vol_meta_map.end() != volIt);
    auto vol_meta = volIt->second;
    // Skip this verify because it's actually set later?
    // TODO(Andrew): If the above comment is correct, we
    // remove most of what this function actually do
    // fds_verify(vol_meta->isForwarding());
    vol_meta->setForwardFinish();
    vol_map_mtx->unlock();

    // notify cat sync mgr
    if (catSyncMgr->finishedForwardVolmeta(volid)) {
        all_finished = true;
    }
}

/**
 * Note that volId may not necessarily match volume id in ioReq
 * Example when it will not match: if we enqueue request for volumeA
 * to shadow queue, we will pass shadow queue's volId (queue id)
 * but ioReq will contain actual volume id; so maybe we should change
 * this method to pass queue id as first param not volid
 */
Error DataMgr::enqueueMsg(fds_volid_t volId,
                          DmRequest* ioReq) {
    Error err(ERR_OK);

    switch (ioReq->io_type) {
        case FDS_DM_PURGE_COMMIT_LOG:
        case FDS_DM_SNAP_VOLCAT:
        case FDS_DM_SNAPDELTA_VOLCAT:
        case FDS_DM_FWD_CAT_UPD:
        case FDS_DM_PUSH_META_DONE:
        case FDS_DM_META_RECVD:
            err = qosCtrl->enqueueIO(volId, static_cast<FDS_IOType*>(ioReq));
            break;
        default:
            fds_assert(!"Unknown message");
    };

    if (!err.ok()) {
        LOGERROR << "Failed to enqueue message " << ioReq->log_string()
                 << " to queue "<< std::hex << volId << std::dec << " " << err;
    }
    return err;
}

VolumeMetaPtr  DataMgr::getVolumeMeta(fds_volid_t volId, bool fMapAlreadyLocked) {
    if (!fMapAlreadyLocked) {
        FDSGUARD(*vol_map_mtx);
        auto iter = vol_meta_map.find(volId);
        if (iter != vol_meta_map.end()) {
            return iter->second;
        }
    } else {
        auto iter = vol_meta_map.find(volId);
        if (iter != vol_meta_map.end()) {
            return iter->second;
        }
    }
    return nullptr;
}

/*
 * Meant to be called holding the vol_map_mtx.
 * @param vol_will_sync true if this volume's meta will be synced from
 * other DM first
 */
Error DataMgr::addVolume(const std::string& vol_name,
                         fds_volid_t vol_uuid,
                         VolumeDesc *vdesc) {
    // check if the volume already exists ..
    {
        FDSGUARD(vol_map_mtx);
        if (volExistsLocked(vol_uuid) == true) {
            LOGDEBUG << "Received add request for existing vol uuid [" << vol_uuid << "]";
            return ERR_DUPLICATE;
        }
        // add a dummy vol meta [FS-3207]
        // This will make sure no other Volume with same id is being created at the same time
        vol_meta_map[vol_uuid] = nullptr;
    }

    // this will remove
    RemoveErroredVolume __removeErrorVolume__(this, vol_uuid);

    Error err(ERR_OK);
    bool fActivated = false;
    // create vol catalogs, etc first

    bool fPrimary = false;
    bool fOldVolume = (vdesc->isStateCreated() ||
                       vdesc->isStateActive() ||
                       vdesc->isStateMarkedForDeletion());
    bool fDbExists = false;
    // double check for file on disk
    std::string dbfile;
    if (vdesc->isSnapshot()) {
        dbfile = dmutil::getLevelDBFile(MODULEPROVIDER()->proc_fdsroot(),vdesc->srcVolumeId , vdesc->volUUID);
    } else {
        dbfile = dmutil::getLevelDBFile(MODULEPROVIDER()->proc_fdsroot(), vdesc->volUUID);
    }
    LOGDEBUG << dbfile;
    fDbExists = util::dirExists(dbfile);

    if (fDbExists && !fOldVolume) {
        LOGWARN << "db file [" << dbfile << "] exists - assuming old volume:" << vdesc->volUUID;
        fOldVolume = true;
    }

    if (fOldVolume && !fDbExists) {
        LOGWARN << "previously active vol:"<< vdesc->volUUID <<" but dbfile missing.. this should be either be a new Node or DM was down during previous addVolume";
    }

    if (vdesc->isClone()) {
        // clone happens only on primary
        fPrimary = amIPrimary(vdesc->srcVolumeId);
    } else if (vdesc->isSnapshot()) {
        // snapshot happens on all nodes
        fPrimary = amIinVolumeGroup(vdesc->srcVolumeId);
    } else {
        fPrimary = amIPrimary(vdesc->volUUID);
    }

    /* We only add the volume if the volume is owned by this DM */
    bool fShouldBeHere = true;

    if (features.isVolumegroupingEnabled()) {
        if (vdesc->isSnapshot()) {
            fShouldBeHere = amIinVolumeGroup(vdesc->srcVolumeId);
        } else if (vdesc->isClone()) {
            // TODO(prem,rao) : dig deeper
            fShouldBeHere = (amIinVolumeGroup(vdesc->volUUID) ||
                             amIinVolumeGroup(vdesc->srcVolumeId));
        } else {
            fShouldBeHere = amIinVolumeGroup(vdesc->volUUID);
        }
    }

    LOGNORMAL << "vol:" << vdesc->volUUID
              << " clone:" << vdesc->isClone()
              << " snap:" << vdesc->isSnapshot()
              << " state:" << vdesc->getState()
              << " old:" << fOldVolume
              << " primary:" << fPrimary
              << " shouldbehere:" << fShouldBeHere
              << " name:" << vdesc->name;

    if (features.isVolumegroupingEnabled() && !fShouldBeHere) {
        FDSGUARD(vol_map_mtx);
        vol_meta_map.erase(vol_uuid);
        LOGNORMAL << "Ignoring add volume: " << vol_uuid
                  << " as volume doesn't belong in the group";
        return ERR_OK;
    }

    if (vdesc->isSnapshot()) {
        if (!fPrimary) {
            LOGWARN << "not primary - nothing to do for snapshot "
                    << "for vol:" << vdesc->srcVolumeId;
            return err;
        }
    }

    // do this processing only in the case..
    if ((vdesc->isSnapshot() || (vdesc->isClone() && fPrimary)) && !fOldVolume) {
        auto volmeta = getVolumeMeta(vdesc->srcVolumeId);
        if (!volmeta) {
            GLOGWARN << "Volume [" << vdesc->srcVolumeId << "] not found!";
            return ERR_NOT_FOUND;
        }

        LOGDEBUG << "Creating a " << (vdesc->isClone()?"clone":"snapshot")
                 << " name:" << vdesc->name << " vol:" << vdesc->volUUID
                 << " srcVolume:"<< vdesc->srcVolumeId;

        // create commit log entry and enable buffering in case of snapshot
        if (vdesc->isSnapshot()) {
            err = timelineMgr->createSnapshot(vdesc);
        } else if (features.isTimelineEnabled()) {
            err = timelineMgr->createClone(vdesc);
        }
        if (err.ok()) fActivated = true;

    } else {
        LOGDEBUG << "Adding volume" << " name:" << vdesc->name << " vol:" << vdesc->volUUID;
        err = timeVolCat_->addVolume(*vdesc);
    }

    if (!err.ok()) {
        LOGERROR << "vol:" << vol_uuid << " failed to " << (vdesc->isSnapshot() ? "create snapshot"
                                                            : (vdesc->isClone() ? "create clone" : "add volume"));
        return err;
    }

    if (err.ok() && !fActivated) {
        // not going to sync this volume, activate volume
        // so that we can do get/put/del cat ops to this volume
        err = timeVolCat_->activateVolume(vdesc->volUUID);
        if (err.ok()) {
            fActivated = true;
        }
    }

    if (err.ok() && vdesc->isClone() && fPrimary && !fOldVolume) {
        err = copyVolumeToOtherDMs(vdesc->volUUID);
        if (!err.ok()) {
            LOGERROR << "failed to sync clone to other DMs. failing clone: " << vdesc->volUUID
                     << " of src:" << vdesc->srcVolumeId;
            return err;
        }
    }

    auto volmeta = MAKE_SHARED<VolumeMeta>(MODULEPROVIDER(),
                                           vol_name,
                                           vol_uuid,
                                           GetLog(),
                                           vdesc,
                                           this);

    if (vdesc->isSnapshot()) {
        volmeta->dmVolQueue.reset(qosCtrl->getQueue(vdesc->qosQueueId));
    }

    bool needReg = false;
    if (!volmeta->dmVolQueue.get()) {
        // FIXME: Why are we doubling the assured rate here?
        volmeta->dmVolQueue.reset(new FDS_VolumeQueue(4096,
                                                      vdesc->iops_throttle,
                                                      2*vdesc->iops_assured,
                                                      vdesc->relativePrio));
        volmeta->dmVolQueue->activate();
        needReg = true;
    }

    LOGDEBUG << "Added vol meta for vol uuid and per Volume queue " << std::hex
             << vol_uuid << std::dec << ", created catalogs";

    vol_map_mtx->lock();
    if (needReg) {
        err = qosCtrl->registerVolume(vdesc->isSnapshot() ? vdesc->qosQueueId : vol_uuid,
                                      static_cast<FDS_VolumeQueue*>(volmeta->dmVolQueue.get()));
    }

    if (!err.ok()) {
        vol_map_mtx->unlock();
        return err;
    }

    if (err.ok()) {
        // For now, volumes only land in the map if it is already active.
        if (fActivated) {
            if (features.isVolumegroupingEnabled() &&
                !(vdesc->isSnapshot())) {
                // volmeta->setPersistVolDB(getPersistDB(vol_uuid));
                /* We wait until coordinator tries to do an open */
                volmeta->setState(fpi::Offline, " - addVolume:coordinator not set");
            } else {
                volmeta->setState(fpi::Active, " - addVolume");
            }
        } else {
            LOGWARN << "vol:" << vol_uuid << " not activated";
            volmeta->setState(fpi::InError, " - addVolume");
        }

        // we registered queue and shadow queue if needed
        vol_meta_map[vol_uuid] = volmeta;
    }
    vol_map_mtx->unlock();

    if (!err.ok()) {
        // cleanup volmeta and deregister queue
        LOGERROR << "vol:" << vol_uuid << " cleaning up volume queue and vol meta because of error";
        qosCtrl->deregisterVolume(vdesc->isSnapshot() ? vdesc->qosQueueId : vol_uuid);
        volmeta->dmVolQueue.reset();
        return err;
    }

    if (vdesc->isSnapshot()) {
        return err;
    }

    // latest seq_number is provided to AM on volume open.
    if (fActivated) {
        Error err1;
        err1 = volmeta->initState();
        if (!err1.ok()) {
            LOGERROR << " volume state init failed .. ignoring err:" << err1;
        }
    }

    if (err.ok() && amIPrimary(vol_uuid)) {
        // will aggregate stats for this volume and log them
        statStreamAggr_->attachVolume(vol_uuid);
        // create volume stat  directory.
        const FdsRootDir *root = MODULEPROVIDER()->proc_fdsroot();
        const std::string stat_dir = root->dir_sys_repo_stats() + std::to_string(vol_uuid.get());
        auto sret = std::system((const char *)("mkdir -p "+stat_dir+" ").c_str());
    }

    // now load all the snapshots for the volume
    if (fOldVolume) {
        timelineMgr->loadSnapshot(vol_uuid);
    }

    return err;
}

Error DataMgr::_process_mod_vol(fds_volid_t vol_uuid, const VolumeDesc& voldesc)
{
    Error err(ERR_OK);

    vol_map_mtx->lock();
    /* make sure volume exists */
    if (volExistsLocked(vol_uuid) == false || nullptr == getVolumeMeta(vol_uuid, true)) {
        LOGERROR << "Received modify policy request for "
                 << "non-existant volume [" << vol_uuid
                 << ", " << voldesc.name << "]";
        err = Error(ERR_NOT_FOUND);
        vol_map_mtx->unlock();
        return err;
    }
    auto vm = vol_meta_map[vol_uuid];
    // FIXME: Why are we doubling assured?
    vm->vol_desc->modifyPolicyInfo(2 * voldesc.iops_assured,
                                   voldesc.iops_throttle,
                                   voldesc.relativePrio);
    err = qosCtrl->modifyVolumeQosParams(vol_uuid,
                                         2 * voldesc.iops_assured,
                                         voldesc.iops_throttle,
                                         voldesc.relativePrio);
    vol_map_mtx->unlock();
    vm->vol_desc->contCommitlogRetention = voldesc.contCommitlogRetention;
    auto volDb = getPersistDB(vol_uuid);
    if (volDb) {
        auto fArchiveLogs = features.isTimelineEnabled() &&
                !voldesc.isSnapshot() && voldesc.contCommitlogRetention > 0;
        volDb->setArchiveLogs(fArchiveLogs);
    } else {
        LOGERROR << "unable to get persist db for vol:" << vol_uuid;
    }
    LOGNORMAL << "Modify policy for volume:" << voldesc.name
              << " RESULT: " << err;

    return err;
}

Error DataMgr::removeVolume(fds_volid_t volId, fds_bool_t fMarkAsDeleted) {
    Error err(ERR_OK);
    
    // mark volume as deleted if it's not empty
    if (fMarkAsDeleted) {
        LOGNORMAL << "ctx:voldelete vol:" << volId << " will be marked for delete";
        err = timeVolCat_->markVolumeDeleted(volId);
        if (!err.ok()) {
            LOGERROR << "failed to mark volume as deleted " << err;
            return err;
        }
        return err;
    }

    // we we are here, fMarkAsDeleted == false
    LOGNORMAL << "ctx:voldelete vol:" << volId << " will be deleted from the system";
    err = timeVolCat_->deleteVolume(volId);
    if (err.ok()) {
        detachVolume(volId);
        // NOTE!! DONT worry about the error , as it may return FEATURE_DISABLED & OM cant handle it
        timelineMgr->removeVolume(volId);

        // remove any user-repo info.
        const auto root =  MODULEPROVIDER()->proc_fdsroot();
        std::string dir=util::strformat("%s/%ld", root->dir_user_repo_dm().c_str(), volId.get());
        LOGNOTIFY << "ctx:voldelete removing vol:" << volId << " from user-repo:" << dir;
        boost::filesystem::remove_all(dir);

    } else {
        LOGERROR << "ctx:voldelete vol:" << volId << " failed to remove from volume catalog err:" << err;
    }

    return err;
}

void DataMgr::detachVolume(fds_volid_t vol_uuid) {
    VolumeMetaPtr vol_meta;
    qosCtrl->deregisterVolume(vol_uuid);
    vol_map_mtx->lock();
    if (vol_meta_map.count(vol_uuid) > 0) {
        vol_meta = vol_meta_map[vol_uuid];
        vol_meta_map.erase(vol_uuid);
    }
    vol_map_mtx->unlock();
    if (vol_meta) {
        vol_meta->dmVolQueue.reset();
    }
    statStreamAggr_->detachVolume(vol_uuid);
    LOGNORMAL << "Detached vol meta for vol uuid "
              << std::hex << vol_uuid << std::dec;
}

Error DataMgr::deleteVolumeContents(fds_volid_t volId) {
    Error err(ERR_OK);
    // get list of blobs for volume
    fpi::BlobDescriptorListType blobList;
    VolumeCatalogQueryIface::ptr volCatIf = timeVolCat_->queryIface();
    blob_version_t version = 0;
    // FIXME(DAC): This needs to happen within the context of a transaction.
    err = volCatIf->listBlobs(volId, &blobList);
    LOGWARN << "deleting all [" << blobList.size() << "]"
            << " blobs from vol:" << volId;
    fpi::FDSP_MetaDataList metaList;
    fds_uint64_t blobSize;
    for (const auto& blob : blobList) {
        metaList.clear();
        version = 0;
        // FIXME(DAC): Error is ignored, and only the last error from deleteBlob will be returned
        //             to the caller.
        err = volCatIf->getBlobMeta(volId,
                                    blob.name,
                                    &version, &blobSize, &metaList);
        err = volCatIf->deleteBlob(volId, blob.name, version);
    }

    return err;
}

/**
 * For all the volumes under going Active Migration forwarding,
 * turn them off.
 */
Error DataMgr::notifyDMTClose(int64_t dmtVersion) {
    Error err(ERR_OK);

    // TODO(Andrew): Um, no where to we have a useful error statue
    // to even return.
    sendDmtCloseCb(err);
    LOGMIGRATE << "Sent DMT close message to OM";
    dmMigrationMgr->stopAllClientForwarding();
    err = dmMigrationMgr->finishActiveMigration(
        DmMigrationMgr::MigrationRole::MIGR_CLIENT, dmtVersion);
    return err;
}

//
// Handle DMT close for the volume in io req -- sends
// push meta done to destination DM
//
void DataMgr::handleDMTClose(DmRequest *io) {
    DmIoPushMetaDone *pushMetaDoneReq = static_cast<DmIoPushMetaDone*>(io);
    LOGMIGRATE << "Processed all commits that arrived before DMT close "
               << "will now notify dst DM to open up volume queues: vol "
               << std::hex << pushMetaDoneReq->volId << std::dec;

    // Notify any other DMs we're migrating to that it's OK to dispatch
    // from their blocked QoS queues.
    // If we commit any currently open or in process transactions they
    // will still get forwarded when they complete.
    fds_verify(ERR_OK == catSyncMgr->issueServiceVolumeMsg(pushMetaDoneReq->volId));
    LOGMIGRATE << "Sent message to migration destination to open blocked queues.";
    delete pushMetaDoneReq;
}

/*
 * Returns the maxObjSize in the volume.
 * TODO(Andrew): This should be refactored into a
 * common library since everyone needs it, not just DM
 * TODO(Andrew): Also this shouldn't be hard coded obviously...
 */
Error DataMgr::getVolObjSize(fds_volid_t volId,
                             fds_uint32_t *maxObjSize) {
    Error err(ERR_OK);

    /*
     * Get a local reference to the vol meta.
     */
    vol_map_mtx->lock();
    auto vol_meta = vol_meta_map[volId];
    vol_map_mtx->unlock();

    fds_verify(vol_meta != NULL);
    *maxObjSize = vol_meta->vol_desc->maxObjSizeInBytes;
    return err;
}

DataMgr::DataMgr(CommonModuleProviderIf *modProvider)
        : HasModuleProvider(modProvider),
          Module("dm")
{
    // NOTE: Don't put much stuff in the constructor.  Move any construction
    // into mod_init()
}

int DataMgr::mod_init(SysParams const *const param)
{
    Error err(ERR_OK);

    initHandlers();

    // make sure all vital directories exist
    const FdsRootDir *root = MODULEPROVIDER()->proc_fdsroot();
    FdsRootDir::fds_mkdir(root->dir_sys_repo_dm().c_str());
    FdsRootDir::fds_mkdir(root->dir_user_repo_dm().c_str());
    FdsRootDir::fds_mkdir(root->dir_timeline_dm().c_str());

    standalone = MODULEPROVIDER()->get_fds_config()->get<bool>("fds.dm.testing.standalone", false);
    numTestVols = 10;
    scheduleRate = 10000;
    shuttingDown = false;

    lastCapacityMessageSentAt = 0;
    sampleCounter = 0;
    counters = new fds::dm::Counters("dmcounters", MODULEPROVIDER()->get_cntrs_mgr().get());
    catSyncRecv = boost::make_shared<CatSyncReceiver>(this);
    auto timer = MODULEPROVIDER()->getTimer();
    closedmt_timer_task = boost::make_shared<CloseDMTTimerTask>(*timer,
                                                                std::bind(&DataMgr::finishCloseDMT,
                                                                          this));
    requestMgr = new dm::RequestManager(this);
    // qos threadpool config
    qosThreadCount = MODULEPROVIDER()->get_fds_config()->\
            get<fds_uint32_t>("fds.dm.qos.default_qos_threads", 10);
    qosOutstandingTasks = MODULEPROVIDER()->get_fds_config()->\
            get<fds_uint32_t>("fds.dm.qos.default_outstanding_io", 20);

    // If we're in test mode, don't daemonize.
    // TODO(Andrew): We probably want another config field and
    // not to override test_mode

    // Set testing related members
    testUturnAll = MODULEPROVIDER()->get_fds_config()->\
            get<bool>("fds.dm.testing.uturn_all", false);
    testUturnStartTx = MODULEPROVIDER()->get_fds_config()->\
            get<bool>("fds.dm.testing.uturn_starttx", false);
    testUturnUpdateCat = MODULEPROVIDER()->get_fds_config()->\
            get<bool>("fds.dm.testing.uturn_updatecat", false);
    testUturnSetMeta   = MODULEPROVIDER()->get_fds_config()->\
            get<bool>("fds.dm.testing.uturn_setmeta", false);

    // timeline feature toggle
    features.setTimelineEnabled(CONFIG_BOOL("fds.feature_toggle.common.enable_timeline", true));
    timelineMgr.reset(new timeline::TimelineManager(this));

    dmFullnessThreshold = MODULEPROVIDER()->get_fds_config()->\
            get<fds_uint32_t>("fds.dm.disk_fullness_threshold", 75);


    features.setExpungeEnabled(MODULEPROVIDER()->get_fds_config()->get<bool>(
        "fds.feature_toggle.common.periodic_expunge", false));

    // FEATURE TOGGLE: Serialization for consistency. Meant to ensure that
    // requests for a given serialization key are applied in the order they
    // are received.
    features.setSerializeReqsEnabled(MODULEPROVIDER()->get_fds_config()->get<bool>(
        "fds.dm.req_serialization", true));

    features.setVolumegroupingEnabled(MODULEPROVIDER()->get_fds_config()->get<bool>(
        "fds.feature_toggle.common.enable_volumegrouping", false));
    /**
     * FEATURE TOGGLE: Volume Open Support
     * Thu 02 Apr 2015 12:39:27 PM PDT
     */
    features.setVolumeTokensEnabled(!features.isVolumegroupingEnabled() &&
                                    MODULEPROVIDER()->get_fds_config()->get<bool>(
                                        "fds.feature_toggle.common.volume_open_support", false));

    /**
     * FEATURE TOGGLE: Sample DM stats for a volume with each Volume update. If not enabled,
     * DM stats are sampled at regular intervals according to schedule and the values presented
     * to a stats consumer may appear as if they do not match recent WRITEs against the volume.
     * The scheduled sampling will always be a minute or two behind. If enabled, the sampling
     * will be realtime (in addition to the scheduled sampling), taking place as soon as the
     * WRITE against the volume is committed. While the these numbers will look better, there
     * may be an intollerable impact on performance.
     */
    features.setRealTimeStatSamplingEnabled(MODULEPROVIDER()->get_fds_config()->get<bool>(
            "fds.dm.realtime_stats_sampling", false));

    vol_map_mtx = new fds_mutex("Volume map mutex");

    /*
     * Retrieves the number of primary DMs from the config file.
     */
    int primary_check = MODULEPROVIDER()->get_fds_config()->
            get<int>("fds.dm.number_of_primary");
    fds_verify(primary_check > 0);
    setNumOfPrimary((unsigned)primary_check);

    // FEATURE TOGGLE: Report stats to the new stats service.
    features.setSendToNewStatsServiceEnabled(MODULEPROVIDER()->get_fds_config()->get<bool>(
            "fds.feature_toggle.common.send_to_new_stats_service", false));

    /**
     * Instantiate migration manager.
     */
    dmMigrationMgr = DmMigrationMgr::unique_ptr(new DmMigrationMgr(*this));
    counters->clearMigrationCounters();

    fileTransfer.reset(new net::FileTransferService(MODULEPROVIDER()->proc_fdsroot()->dir_filetransfer(), MODULEPROVIDER()->getSvcMgr()));
    refCountMgr.reset(new refcount::RefCountManager(this));
    requestHandler.reset(new dm::Handler(*this));
    return 0;
}

void DataMgr::initHandlers() {
    // NOTE: For the new way of creating handler, see registerDmVolumeReqHandler()
    handlers[FDS_LIST_BLOB] = new dm::GetBucketHandler(*this);
    handlers[FDS_DELETE_BLOB] = new dm::DeleteBlobHandler(*this);
    handlers[FDS_DM_SYS_STATS] = new dm::DmSysStatsHandler(*this);
    handlers[FDS_CAT_UPD] = new dm::UpdateCatalogHandler(*this);
    handlers[FDS_GET_BLOB_METADATA] = new dm::GetBlobMetaDataHandler(*this);
    handlers[FDS_CAT_QRY] = new dm::QueryCatalogHandler(*this);
    handlers[FDS_START_BLOB_TX] = new dm::StartBlobTxHandler(*this);
    handlers[FDS_DM_STAT_STREAM] = new dm::StatStreamHandler(*this);
    handlers[FDS_COMMIT_BLOB_TX] = new dm::CommitBlobTxHandler(*this);
    handlers[FDS_CAT_UPD_ONCE] = new dm::UpdateCatalogOnceHandler(*this);
    handlers[FDS_SET_BLOB_METADATA] = new dm::SetBlobMetaDataHandler(*this);
    handlers[FDS_RENAME_BLOB] = new dm::RenameBlobHandler(*this);
    handlers[FDS_ABORT_BLOB_TX] = new dm::AbortBlobTxHandler(*this);
    handlers[FDS_DM_FWD_CAT_UPD] = new dm::ForwardCatalogUpdateHandler(*this);
    handlers[FDS_STAT_VOLUME] = new dm::StatVolumeHandler(*this);
    handlers[FDS_SET_VOLUME_METADATA] = new dm::SetVolumeMetadataHandler(*this);
    handlers[FDS_GET_VOLUME_METADATA] = new dm::GetVolumeMetadataHandler(*this);
    handlers[FDS_OPEN_VOLUME] = new dm::VolumeOpenHandler(*this);
    // handlers[FDS_DM_VOLUMEGROUP_UPDATE] = new dm::VolumegroupUpdateHandler(*this);
    handlers[FDS_CLOSE_VOLUME] = new dm::VolumeCloseHandler(*this);
    handlers[FDS_DM_RELOAD_VOLUME] = new dm::ReloadVolumeHandler(*this);
    handlers[FDS_DM_LOAD_FROM_ARCHIVE] = new dm::LoadFromArchiveHandler(*this);
    handlers[FDS_DM_MIGRATION] = new dm::DmMigrationHandler(*this);
    handlers[FDS_DM_RESYNC_INIT_BLOB] = new dm::DmMigrationBlobFilterHandler(*this);
    handlers[FDS_DM_MIG_DELTA_BLOBDESC] = new dm::DmMigrationDeltaBlobDescHandler(*this);
    handlers[FDS_DM_MIG_DELTA_BLOB] = new dm::DmMigrationDeltaBlobHandler(*this);
    handlers[FDS_DM_MIG_TX_STATE] = new dm::DmMigrationTxStateHandler(*this);
    handlers[FDS_DM_MIG_REQ_TX_STATE] = new dm::DmMigrationRequestTxStateHandler(*this);
    new dm::SimpleHandler(*this);
}

DataMgr::~DataMgr()
{
    // shutdown all data manager modules
    LOGDEBUG << "Received shutdown message DM ... shutdown modules..";
    mod_shutdown();

    vol_meta_map.clear();
    delete requestMgr;
    delete sysTaskQueue;
    delete vol_map_mtx;
    delete qosCtrl;
}

int DataMgr::run()
{

    _shutdownGate.waitUntilOpened();

    return 0;
}

void DataMgr::mod_startup()
{
    Error err;
    fds::DmDiskInfo     *info;
    fds::DmDiskQuery     in;
    fds::DmDiskQueryOut  out;
    fds_bool_t      useTestMode = false;

    runMode = NORMAL_MODE;

    useTestMode = MODULEPROVIDER()->get_fds_config()->get<bool>("fds.dm.testing.test_mode", false);
    if (useTestMode == true) {
        runMode = TEST_MODE;
        features.setTestModeEnabled(true);
    }
    LOGNORMAL << "Data Manager using control port "
              << MODULEPROVIDER()->getSvcMgr()->getSvcPort();

    setup_metasync_service();
    refCountMgr->mod_startup();

    LOGNORMAL;
}

void DataMgr::mod_enable_service() {
    Error err(ERR_OK);

    auto svcmgr = MODULEPROVIDER()->getSvcMgr();
    auto diskIOPsMin = standalone ? 60*1000 :
            atoi(svcmgr->getSvcProperty(svcmgr->getMappedSelfPlatformUuid(),
                                        "node_iops_min", "60000").c_str());

    // note that qos dispatcher in SM/DM uses total rate just to assign
    // guaranteed slots, it still will dispatch more IOs if there is more
    // perf capacity available (based on how fast IOs return). So setting
    // totalRate to node_iops_min does not actually restrict the SM from
    // servicing more IO if there is more capacity (eg.. because we have
    // cache and SSDs)
    scheduleRate = 2 * static_cast<uint32_t>(diskIOPsMin);
    fds_assert(scheduleRate > 0);
    LOGNOTIFY << "Will set totalRate to " << scheduleRate;

    /*
     *  init Data Manager  QOS class.
     */
    qosCtrl = new dmQosCtrl(this, qosThreadCount, FDS_QoSControl::FDS_DISPATCH_WFQ, GetLog());
    qosCtrl->runScheduler();

    // Create a queue for system (background) tasks
    sysTaskQueue = new FDS_VolumeQueue(1024, 10000, 20, FdsDmSysTaskPrio);
    sysTaskQueue->activate();
    err = qosCtrl->registerVolume(FdsDmSysTaskId, sysTaskQueue);
    fds_verify(err.ok());
    LOGNORMAL << "Registered System Task Queue";


    // create time volume catalog
    timeVolCat_ = DmTimeVolCatalog::ptr(new DmTimeVolCatalog(MODULEPROVIDER(),
                                                             "DM Time Volume Catalog",
                                                             *qosCtrl->threadPool,
                                                             *this));

    LOGNORMAL << "Finished timevolCat creation";

    // create stats aggregator that aggregates stats for vols for which
    // this DM is primary
    statStreamAggr_ =
            StatStreamAggregator::ptr(new StatStreamAggregator(MODULEPROVIDER(),
                                                               "DM Stat Stream Aggregator",
                                                               MODULEPROVIDER()->get_fds_config(),
                                                               *this));

    // enable collection of local stats in DM
    StatsCollector::singleton()->setSvcMgr(MODULEPROVIDER()->getSvcMgr());
    if (!standalone) {
        // since aggregator is in the same module, for stats that need to go to
        // local aggregator, we just directly stream to aggregator (not over network)
        StatsCollector::singleton()->startStreaming(
            std::bind(&DataMgr::sampleDMStats, this, std::placeholders::_1),
            std::bind(&DataMgr::handleLocalStatStream, this,
                      std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
        statStreamAggr_->mod_startup();

        // get DMT from OM if DMT already exist
        MODULEPROVIDER()->getSvcMgr()->getDMT();
        MODULEPROVIDER()->getSvcMgr()->getDLT();

        // Get the volume descriptors from OM
        getAllVolumeDescriptors();
    }
    LOGNORMAL << "Finished stat collection and pulling volume descriptors";

    // finish setting up time volume catalog
    timeVolCat_->mod_startup();

    LOGNORMAL << "Finished starting TVC";

    // Register the DLT manager with service layer so that
    // outbound requests have the correct dlt_version.
    if (!features.isTestModeEnabled()) {
        auto reqMgr = MODULEPROVIDER()->getSvcMgr()->getSvcRequestMgr();
        reqMgr->setDltManager(MODULEPROVIDER()->getSvcMgr()->getDltManager());
    }

    if (timeVolCat_->isUnavailable()) {
        // send health check msg to OM
        requestMgr->sendHealthCheckMsgToOM(fpi::HEALTH_STATE_ERROR, ERR_NODE_NOT_ACTIVE, "DM failed to start! ");
    }
}

void DataMgr::shutdown()
{
    flushIO();
    mod_shutdown();
}

//   Block new IO's  and flush queued IO's
void DataMgr::flushIO()
{
    shuttingDown = true;
    for (auto it = vol_meta_map.begin();
         it != vol_meta_map.end();
         ++it) {
        qosCtrl->quieseceIOs(it->first);
    }

}

void DataMgr::mod_shutdown()
{
    /* NOTE: DON'T DELETE ANY OBJECTES HERE.  DO ALL THE DELETIONS INSIDE DESTRUCTORS */

    // Don't double-free.
    {
        // The expected goofiness is due to c_e_s() setting "expected" to the current value if it
        // is not equal to expected. Which makes "expected" a misnomer as it's an in/out variable.
        // We don't care (or rather, we already know) what the current value is if it's not equal
        // to expected though, so we ignore it.
        bool expected = false;
        if (!shuttingDown.compare_exchange_strong(expected, true))
        {
            return;
        }
    }

    LOGNORMAL;
    if (refCountMgr.get()) {
        refCountMgr->mod_shutdown();
    }

    if (!standalone) {
        StatsCollector::singleton()->stopStreaming();
    }
    if ( statStreamAggr_ ) {
        statStreamAggr_->mod_shutdown();
    }

    if ( timeVolCat_ ) {
        timeVolCat_->mod_shutdown();
    }

    if (features.isCatSyncEnabled()) {
        catSyncMgr->mod_shutdown();
    } else {
        LOGWARN << "catalog sync feature - NOT enabled";
    }

    if (dmMigrationMgr) {
        dmMigrationMgr->mod_shutdown();
    }

    LOGNORMAL << "Destructing the Data Manager";

    for (auto it = vol_meta_map.begin();
         it != vol_meta_map.end();
         it++) {
        //  qosCtrl->quieseceIOs(it->first);
        qosCtrl->deregisterVolume(it->first);
    }

    qosCtrl->deregisterVolume(FdsDmSysTaskId);
    qosCtrl->threadPool->stop();

    _shutdownGate.open();
}

void DataMgr::setup_metasync_service()
{
    catSyncMgr.reset(new CatalogSyncMgr(1, this, *this));
    // TODO(xxx) should we start catalog sync manager when no OM?
    catSyncMgr->mod_startup();
}

void DataMgr::swapMgrId(const FDS_ProtocolInterface::
                        FDSP_MsgHdrTypePtr& fdsp_msg) {
    FDS_ProtocolInterface::FDSP_MgrIdType temp_id;
    temp_id = fdsp_msg->dst_id;
    fdsp_msg->dst_id = fdsp_msg->src_id;
    fdsp_msg->src_id = temp_id;

    fds_uint64_t tmp_addr;
    tmp_addr = fdsp_msg->dst_ip_hi_addr;
    fdsp_msg->dst_ip_hi_addr = fdsp_msg->src_ip_hi_addr;
    fdsp_msg->src_ip_hi_addr = tmp_addr;

    tmp_addr = fdsp_msg->dst_ip_lo_addr;
    fdsp_msg->dst_ip_lo_addr = fdsp_msg->src_ip_lo_addr;
    fdsp_msg->src_ip_lo_addr = tmp_addr;

    fds_uint32_t tmp_port;
    tmp_port = fdsp_msg->dst_port;
    fdsp_msg->dst_port = fdsp_msg->src_port;
    fdsp_msg->src_port = tmp_port;
}

std::string DataMgr::getPrefix() const {
    return stor_prefix;
}

/*
 * Intended to be called while holding the vol_map_mtx.
 */
fds_bool_t DataMgr::volExistsLocked(fds_volid_t vol_uuid) const {
    return vol_meta_map.count(vol_uuid) != 0;
}

fds_bool_t DataMgr::volExists(fds_volid_t vol_uuid) const {
    FDSGUARD(vol_map_mtx);
    return volExistsLocked(vol_uuid);
}

/**
 * Checks the current DMT to determine if this DM primary
 * or not for a given volume.
 * topPrimary - If specified to be true, will check to see if
 * this current DM is the top element of the DMT column.
 */
fds_bool_t
DataMgr::_amIPrimaryImpl(fds_volid_t &volUuid, bool topPrimary) {
    if (MODULEPROVIDER()->getSvcMgr()->hasCommittedDMT()) {
        const DmtColumnPtr nodes = MODULEPROVIDER()->getSvcMgr()->getDMTNodesForVolume(volUuid);
        fds_verify(nodes->getLength() > 0);
        const fpi::SvcUuid myUuid (MODULEPROVIDER()->getSvcMgr()->getSelfSvcUuid());

        if (topPrimary) {
            // Only the 0th element is considered top Primary
            return (myUuid == nodes->get(0).toSvcUuid());
        } else {
            // Anything else within number_of_primary is within primary group
            const uint numberOfPrimaryDMs = getNumOfPrimary();
            fds_verify(numberOfPrimaryDMs > 0);
            for (uint i = 0; i < numberOfPrimaryDMs && i < nodes->getLength() ; i++) {
                if (nodes->get(i).toSvcUuid() == myUuid) {
                    return true;
                }
            }
        }
    }
    return false;
}

fds_bool_t
DataMgr::amIPrimary(fds_volid_t volUuid) {
    return (_amIPrimaryImpl(volUuid, true));
}

fds_bool_t
DataMgr::amIinVolumeGroup(fds_volid_t volUuid) {
    if (features.isVolumegroupingEnabled()) {
        if (MODULEPROVIDER()->getSvcMgr()->hasCommittedDMT()) {
            const DmtColumnPtr nodes = MODULEPROVIDER()->getSvcMgr()->\
                                       getDMTNodesForVolume(volUuid);
            fds_verify(nodes->getLength() > 0);
            const fpi::SvcUuid myUuid (MODULEPROVIDER()->getSvcMgr()->getSelfSvcUuid());
            for (uint i = 0; i < nodes->getLength() ; i++) {
                if (nodes->get(i).toSvcUuid() == myUuid) {
                    return true;
                }
            }
        }
        return false;
    }
    return (_amIPrimaryImpl(volUuid, false));
}

/**
 * Make snapshot of volume catalog for sync and notify
 * CatalogSync.
 */
void
DataMgr::snapVolCat(DmRequest *io) {
    Error err(ERR_OK);
    fds_verify(io != NULL);

    DmIoSnapVolCat *snapReq = static_cast<DmIoSnapVolCat*>(io);
    fds_verify(snapReq != NULL);

    if (io->io_type == FDS_DM_SNAPDELTA_VOLCAT) {
        // we are doing second rsync, set volume state to forward
        // We are doing this before we do catalog rsync, so some
        // updates that will make it to the persistent volume catalog
        // will also be sent to the destination node; this is ok
        // because we are serializing the updates to the same blob.
        // so they will also make it in order (assuming that sending
        // update A before update B will also cause receiving DM to receive
        // update A before update B -- otherwise we will have a race
        // which will also happen with other forwarded updates).
        vol_map_mtx->lock();
        fds_verify(vol_meta_map.count(snapReq->volId) > 0);
        auto vol_meta = vol_meta_map[snapReq->volId];
        // TODO(Andrew): We're setting the forwarding state separately from
        // taking the snapshot, which means that we'll start forwarding
        // data that will also be in the delta snapshot that we rsync, which
        // is incorrect. These two steps should be made atomic.
        vol_meta->setForwardInProgress();
        vol_map_mtx->unlock();
        LOGMIGRATE << "Starting 2nd (delta) rsync for volume " << std::hex
                   << snapReq->volId << " to node "
                   << (snapReq->node_uuid).uuid_get_val() << std::dec;
    } else {
        fds_verify(FDS_DM_SNAP_VOLCAT == io->io_type);
        LOGMIGRATE << "Starting 1st rsync for volume " << std::hex
                   << snapReq->volId << " to node "
                   << (snapReq->node_uuid).uuid_get_val() << std::dec;
    }

    // sync the catalog
    err = timeVolCat_->queryIface()->syncCatalog(snapReq->volId,
                                                 snapReq->node_uuid);

    // notify sync mgr that we did rsync
    snapReq->dmio_snap_vcat_cb(snapReq->volId, err);

    // mark this request as complete
    if (features.isQosEnabled()) qosCtrl->markIODone(*snapReq);
    delete snapReq;
}

/**
 * Populates an fdsp message header with stock fields.
 *
 * @param[in] Ptr to msg header to modify
 */
void
DataMgr::initSmMsgHdr(fpi::FDSP_MsgHdrTypePtr msgHdr) {
    msgHdr->minor_ver = 0;
    msgHdr->msg_id    = 1;

    msgHdr->major_ver = 0xa5;
    msgHdr->minor_ver = 0x5a;

    msgHdr->num_objects = 1;
    msgHdr->frag_len    = 0;
    msgHdr->frag_num    = 0;

    msgHdr->tennant_id      = 0;
    msgHdr->local_domain_id = 0;

    msgHdr->src_id = fpi::FDSP_DATA_MGR;
    msgHdr->dst_id = fpi::FDSP_STOR_MGR;

    auto svcmgr = MODULEPROVIDER()->getSvcMgr();
    msgHdr->src_node_name = svcmgr->getSelfSvcName();

    msgHdr->origin_timestamp = fds::get_fds_timestamp_ms();

    msgHdr->err_code = ERR_OK;
    msgHdr->result   = fpi::FDSP_ERR_OK;
}

void DataMgr::InitMsgHdr(const fpi::FDSP_MsgHdrTypePtr& msg_hdr)
{
    msg_hdr->minor_ver = 0;
    msg_hdr->msg_code = fpi::FDSP_MSG_PUT_OBJ_REQ;
    msg_hdr->msg_id =  1;

    msg_hdr->major_ver = 0xa5;
    msg_hdr->minor_ver = 0x5a;

    msg_hdr->num_objects = 1;
    msg_hdr->frag_len = 0;
    msg_hdr->frag_num = 0;

    msg_hdr->tennant_id = 0;
    msg_hdr->local_domain_id = 0;

    msg_hdr->src_id = fpi::FDSP_DATA_MGR;
    msg_hdr->dst_id = fpi::FDSP_STOR_MGR;

    msg_hdr->src_node_name = "";

    msg_hdr->err_code = ERR_OK;
    msg_hdr->result = fpi::FDSP_ERR_OK;
}

void CloseDMTTimerTask::runTimerTask() {
    timeout_cb();
}

void DataMgr::setResponseError(fpi::FDSP_MsgHdrTypePtr& msg_hdr, const Error& err) {
    if (err.ok()) {
        msg_hdr->result  = fpi::FDSP_ERR_OK;
        msg_hdr->err_msg = "OK";
    } else {
        msg_hdr->result   = fpi::FDSP_ERR_FAILED;
        msg_hdr->err_msg  = "FDSP_ERR_FAILED";
        msg_hdr->err_code = err.GetErrno();
    }
}

Error
DataMgr::getAllVolumeDescriptors()
{
    Error err(ERR_OK);
    fpi::GetAllVolumeDescriptors list;
    err = MODULEPROVIDER()->getSvcMgr()->getAllVolumeDescriptors(list);

    if (!err.ok()) {
        return err;
    }

    for (auto const& volAdd : list.volumeList) {
        VolumeDesc desc(volAdd.vol_desc);
        fds_volid_t vol_uuid (desc.volUUID);
        GLOGNOTIFY << "Pulled create for vol "
                   << "[" << vol_uuid << ", "
                   << desc.getName() << "]";
        err = addVolume(getPrefix() + std::to_string(vol_uuid.get()),
                        vol_uuid,
                        &desc);
    }

    return err;
}

DmPersistVolDB::ptr DataMgr::getPersistDB(fds_volid_t volId) {
    // get the correct catalog
    DmVolumeCatalog::ptr volDirPtr =  boost::dynamic_pointer_cast
            <DmVolumeCatalog>(timeVolCat_->queryIface());

    if (volDirPtr.get() == NULL) {
        LOGERROR << "unable to get the vol dir ptr";
        return NULL;
    }

    DmPersistVolCat::ptr persistVolDirPtr = volDirPtr->getVolume(volId);
    if (persistVolDirPtr.get() == NULL) {
        LOGERROR << "unable to get the persist vol dir ptr for vol:" << volId;
        return NULL;
    }

    DmPersistVolDB::ptr voldDBPtr = boost::dynamic_pointer_cast <DmPersistVolDB>(persistVolDirPtr);
    return voldDBPtr;
}

void DataMgr::addToQueue(DmRequest* req) {
    requestHandler->addToQueue(req);
}

void DataMgr::addToQueue(std::function<void()>&& func, fds_volid_t volId) {
    requestHandler->addToQueue(new DmFunctor(volId, func));
}

Error DataMgr::copyVolumeToOtherDMs(fds_volid_t volId) {
    Error err;
    LOGNORMAL << "copying vol:" << volId << " to other dms";

    auto volDB = getPersistDB(volId);
    if (volDB.get() == NULL) {
        LOGWARN << "unable to get vol-db for vol:" << volId;
        return ERR_VOL_NOT_FOUND;
    }

    const fpi::SvcUuid me (MODULEPROVIDER()->getSvcMgr()->getSelfSvcUuid());
    DmtColumnPtr nodes = MODULEPROVIDER()->getSvcMgr()->getDMTNodesForVolume(volId);

    if (nodes->getLength() == 1) {
        LOGDEBUG << "no other node to copy to .. on a one node system!!";
        return err;
    }

    std::string archiveDir = dmutil::getTempDir();
    std::string archiveFileName = util::strformat("%ld.tgz",volId.get());
    std::string archiveFile =  archiveFileName;
    err = getPersistDB(volId)->archive(archiveDir, archiveFile);
    if (!err.ok()) {
        LOGWARN << "archiving failed for vol:" << volId << " error:" << err;
        return err;
    }

    archiveFile = archiveDir + std::string("/") + archiveFile;
    if (!util::fileExists(archiveFile)) {
        LOGWARN << "archive file missing : " << archiveFile;
        return ERR_NOT_FOUND;
    }

    LOGDEBUG << "archive file:" << archiveFile;

    auto transferCb = [&](fds::net::FileTransferService::Handle::ptr handle,const Error& err,
                          SHPTR<concurrency::TaskStatus> taskStatus) {
        taskStatus->error = err;
        taskStatus->done();
    };

    uint total = 0, failed = 0;
    for (uint i = 0; i < nodes->getLength(); i++) {
        if (nodes->get(i).toSvcUuid() == me) continue;
        total ++;
        SHPTR<concurrency::TaskStatus> taskStatus(new concurrency::TaskStatus());
        LOGNORMAL << "copying vol:" << volId << " to node:" << nodes->get(i);
        fileTransfer->send(nodes->get(i).toSvcUuid(), archiveFile, archiveFileName, std::bind(transferCb, PH_ARG1, PH_ARG2, taskStatus));
        // 10 minutes
        if (!taskStatus->await(10*60*1000)) {
            LOGWARN << "filetransfer did not complete in expected time: [" << archiveFile
                    << "] to:" << nodes->get(i);
            failed ++;
        } else {
            if (!taskStatus->error.ok()) {
                LOGWARN << "filetransfer did not complete : [" << archiveFile
                        << "] to:" << nodes->get(i) << " error:" << taskStatus->error;
                failed ++;
            } else {
                // send LOAD_FROM_ARCHIVE command to dm
                err = requestMgr->sendLoadFromArchiveRequest(nodes->get(i), volId, archiveFileName);
                if (!err.ok()) {
                    failed++;
                    LOGERROR << "loadfromarchive cmd failed to:" << nodes->get(i)
                             << " vol:" << volId
                             << " file:" << archiveFileName
                             << " error:" << err;
                }
            }
        }
    }

    LOGNORMAL << "vol:" << volId << " copied to [" << total << "] nodes with [" << failed << "] failures";
    // remove archive file
    boost::filesystem::remove(archiveFile);

    if (total > 0 && failed >= (1.0*total/2.0)) {
        LOGERROR << "volume copy failed : " << volId;
        return ERR_INVALID;
    }
    return ERR_OK;
}

Error DataMgr::copyVolumeToTargetDM(fpi::SvcUuid dmUuid, fds_volid_t volId, bool archivePolicy) {
    Error err;
    LOGNORMAL << "copying vol:" << volId << " to other Target DM:" << dmUuid;

    auto volDB = getPersistDB(volId);
    if (volDB.get() == NULL) {
        LOGWARN << "unable to get vol-db for vol:" << volId;
        return ERR_VOL_NOT_FOUND;
    }

    const fpi::SvcUuid me (MODULEPROVIDER()->getSvcMgr()->getSelfSvcUuid());
    const DmtColumnPtr nodes = MODULEPROVIDER()->getSvcMgr()->getDMTNodesForVolume(volId);
    if (dmUuid == me) {
        LOGDEBUG << "Nothing to do... both origin and target DM Uuids are the same";
        return err;
    }

    /* Find the nodeId that matches the svcUuid */
    uint nodeId;
    for (nodeId = 0; nodeId < nodes->getLength(); nodeId++){
        LOGDEBUG << "dmUuid:" << dmUuid << " // node[" << nodeId << "]:" << nodes->get(nodeId).toSvcUuid();
        if (nodes->get(nodeId).toSvcUuid() == dmUuid){
            break;
        } else if (nodeId == (nodes->getLength()-1)) {
            LOGDEBUG << "Cannot complete - destination dmUuid given does not exist.";
            return ERR_VOL_NOT_FOUND;
        }
    }

    LOGDEBUG << "Before archive policy";
    /*
     * If archive policy is on - then ask the target DM to tar up its volume before overwriting
     * the old policy
     * */
    if (archivePolicy) {
        err = requestMgr->sendArchiveVolumeRequest(nodes->get(nodeId), volId);
        if (!err.ok()) {
            LOGWARN << "Archiving on node " << nodes->get(nodeId) << " failed";
            return ERR_ARCHIVE_SNAP_TAR_FAILED;
        }
    }

    std::string archiveDir = dmutil::getTempDir();
    std::string archiveFileName = util::strformat("%ld.tgz",volId.get());
    std::string archiveFile =  archiveFileName;

    err = getPersistDB(volId)->archive(archiveDir, archiveFile);
    if (!err.ok()) {
        LOGWARN << "archiving failed for vol:" << volId << " error:" << err;
        return err;
    }

    archiveFile = archiveDir + std::string("/") + archiveFile;
    if (!util::fileExists(archiveFile)) {
        LOGWARN << "archive file missing : " << archiveFile;
        return ERR_NOT_FOUND;
    }

    LOGDEBUG << "archive file:" << archiveFile << " volid:" <<volId;

    auto transferCb = [&](fds::net::FileTransferService::Handle::ptr handle,const Error& err,
                          SHPTR<concurrency::TaskStatus> taskStatus) {
      taskStatus->error = err;
      taskStatus->done();
    };

    SHPTR<concurrency::TaskStatus> taskStatus(new concurrency::TaskStatus());
    LOGNORMAL << "copying vol:" << volId << " to node:" << nodes->get(nodeId);
    fileTransfer->send(nodes->get(nodeId).toSvcUuid(), archiveFile, archiveFileName, std::bind(transferCb, PH_ARG1, PH_ARG2, taskStatus));
    // 10 minutes
    if (!taskStatus->await(10*60*1000)) {
        LOGWARN << "filetransfer did not complete in expected time: [" << archiveFile
                    << "] to:" << nodes->get(nodeId);
    } else {
        if (!taskStatus->error.ok()) {
            LOGWARN << "filetransfer did not complete : [" << archiveFile
                        << "] to:" << nodes->get(nodeId) << " error:" << taskStatus->error;
        } else {
            // send LOAD_FROM_ARCHIVE command to dm
            err = requestMgr->sendLoadFromArchiveRequest(nodes->get(nodeId), volId, archiveFileName);
            if (!err.ok()) {
                LOGERROR << "could to send load from archive cmd to:" << nodes->get(nodeId)
                            << " vol:" << volId
                            << " file:" << archiveFileName
                            << " error:" << err;
            }
        }
    }
    // remove archive file
    boost::filesystem::remove(archiveFile);

    return ERR_OK;
}

Error DataMgr::archiveTargetVolume(fds_volid_t volId) {
    Error err;
    auto persistDB = getPersistDB(volId);

    /* Archive old volume */
    std::string archiveDir = dmutil::getTempDir();
    std::string archiveFileName = util::strformat("%ld.tgz",volId);
    std::string archiveFile =  archiveFileName;

    err = persistDB->archive(archiveDir, archiveFile);
    if (!err.ok()) {
        LOGWARN << "archiving failed for vol:" << volId << " error:" << err;
        return err;
    }

    archiveFile = archiveDir + std::string("/") + archiveFile;
    if (!util::fileExists(archiveFile)) {
        LOGWARN << "archive file missing : " << archiveFile;
        err = ERR_ARCHIVE_PUT_FAILED;
    }

    LOGDEBUG << "Archive of volume " << volId << " successful";
    return err;
}

Error DataMgr::waitForVolumeReadiness(fds_volid_t const volId, util::TimeStamp seconds) const {
    int count = seconds*2;
    bool fVolumeReady = false;
    auto vol = getVolumeDesc(volId);
    while (count > 0 && !fVolumeReady) {
        if (vol != nullptr) {
            fVolumeReady = (vol->isStateActive() || vol->isStateOffline());
        }
        if (!fVolumeReady) {
            if (count % 60 == 0) {
                LOGWARN << "vol:" << volId << " not ready yet";
            }
            usleep(500000);  // 0.5s
        }
        count --;
        vol = getVolumeDesc(volId);
    }

    if (!fVolumeReady) {
        LOGERROR << "vol:" << volId << " not ready after [" << seconds << " s]";
        return ERR_VOL_NOT_FOUND;
    }
    return ERR_OK;
}

//************************************************************************//
//   QOS Controller Functions                                             //
//************************************************************************//

Error DataMgr::dmQosCtrl::processIO(FDS_IOType* _io) {
    Error err(ERR_OK);

    DmRequest *io = static_cast<DmRequest*>(_io);

    // Stop the queue latency timer.
    PerfTracer::tracePointEnd(io->opQoSWaitCtx);

    // specifically pick what id to serialize/synchronize this request
    auto scheduleOnId = io->volId;

    // explicit background tasks
    switch (io->io_type) {
        case FDS_DM_RELOAD_VOLUME:
        case FDS_DM_LOAD_FROM_ARCHIVE:
            scheduleOnId = FdsDmSysTaskId;
            break;
        default:
            break;
    }

    SerialKey key(scheduleOnId, io->blob_name);
    fpi::FDSP_VolType volType(fpi::FDSP_VOL_S3_TYPE);
    {
        fds_mutex::scoped_lock l(*(parentDm->vol_map_mtx));
        auto mapEntry = parentDm->vol_meta_map.find(io->volId);
        if (mapEntry != parentDm->vol_meta_map.end()) {
            volType = mapEntry->second->vol_desc->volType;
        }
    }
    GLOGDEBUG << "processing : " << io->log_string() << " volType: " << volType
              << " key: " << key.first << ":" << key.second;
    switch (io->io_type){
        /* TODO(Rao):
         * 1. clean up unnecessary messages
         * 2. Use DataMgr::dmQosCtrl::schedule() instead of fds_threadpool directly here.
         *    Once we switch to volume grouping this clean up can be done.
         */
        case FDS_DM_SNAP_VOLCAT:
        case FDS_DM_SNAPDELTA_VOLCAT:
            threadPool->schedule(&DataMgr::snapVolCat, parentDm, io);
            break;
        case FDS_DM_PUSH_META_DONE:
            threadPool->schedule(&DataMgr::handleDMTClose, parentDm, io);
            break;
        case FDS_DM_PURGE_COMMIT_LOG:
            threadPool->schedule(io->proc, io);
            break;
        case FDS_DM_META_RECVD:
            threadPool->schedule(&DataMgr::handleForwardComplete, parentDm, io);
            break;

            /* End of new refactored DM message types */
            // catalog read handlers
        case FDS_LIST_BLOB:
        case FDS_GET_BLOB_METADATA:
        case FDS_CAT_QRY:
        case FDS_STAT_VOLUME:
        case FDS_GET_VOLUME_METADATA:
        case FDS_DM_LIST_BLOBS_BY_PATTERN:
        case FDS_DM_MIGRATION:
            // Other (stats, etc...) handlers
        case FDS_DM_SYS_STATS:
        case FDS_DM_STAT_STREAM:
            threadPool->schedule(&dm::Handler::handleQueueItem,
                                 parentDm->handlers.at(io->io_type),
                                 io);
            break;
        case FDS_RENAME_BLOB:
            if (parentDm->features.isVolumegroupingEnabled()) {
                schedule(scheduleOnId, true,
                         std::bind(&dm::Handler::handleQueueItem,
                                   parentDm->handlers.at(io->io_type),
                                   io));
                break;
            }
            // If serialization is enabled, serialize on both keys,
            // otherwise just schedule directly.
            if ((parentDm->features.isSerializeReqsEnabled())) {
                auto renameReq = static_cast<DmIoRenameBlob*>(io);
                SerialKey key2(scheduleOnId, renameReq->message->destination_blob);
                serialExecutor->scheduleOnHashKeys(keyHash(key),
                                                   keyHash(key2),
                                                   std::bind(&dm::Handler::handleQueueItem,
                                                             parentDm->handlers.at(io->io_type),
                                                             io));
            } else {
                threadPool->schedule(&dm::Handler::handleQueueItem,
                                     parentDm->handlers.at(io->io_type),
                                     io);
            }

            break;
            // catalog write handlers
        case FDS_DM_VOLUMEGROUP_UPDATE:
        {
            VOLUME_REQUEST_CHECK();
            schedule(scheduleOnId, true,
                     std::bind(&VolumeMeta::handleVolumegroupUpdate,
                               volMeta,
                               io));
            break;
        }
        case FDS_DM_MIG_FINISH_STATIC_MIGRATION:
        {
            VOLUME_REQUEST_CHECK();
            schedule(scheduleOnId, true,
                     std::bind(&VolumeMeta::handleFinishStaticMigration,
                               volMeta,
                               io));
            break;
        }
        case FDS_DELETE_BLOB:
        case FDS_CAT_UPD:
        case FDS_START_BLOB_TX:
        case FDS_COMMIT_BLOB_TX:
        case FDS_CAT_UPD_ONCE:
        case FDS_SET_BLOB_METADATA:
        case FDS_ABORT_BLOB_TX:
        case FDS_SET_VOLUME_METADATA:
        case FDS_OPEN_VOLUME:
        case FDS_CLOSE_VOLUME:
        case FDS_DM_RELOAD_VOLUME:
        case FDS_DM_LOAD_FROM_ARCHIVE:
        case FDS_DM_RESYNC_INIT_BLOB:
        case FDS_DM_MIG_DELTA_BLOB:
        case FDS_DM_MIG_DELTA_BLOBDESC:
        case FDS_DM_MIG_FINISH_VOL_RESYNC:
        case FDS_DM_MIG_REQ_TX_STATE:
        case FDS_DM_MIG_TX_STATE:
            if (parentDm->features.isVolumegroupingEnabled()) {
                schedule(scheduleOnId, true,
                         std::bind(&dm::Handler::handleQueueItem,
                                   parentDm->handlers.at(io->io_type),
                                   io));
                break;
            }
            // If serialization in enabled, serialize on the key
            // otherwise just schedule directly.
            // Note: We avoid this serialization in the block connector
            // case since the connector is ensuring we won't have overlapping
            // concurrent requests.
            // TODO(Andrew): Remove this block connector special casing. Either
            // AM should enforce the policy for all connectors or we always leave
            // it up to the connector.
            if ((parentDm->features.isSerializeReqsEnabled()) &&
                (volType != fpi::FDSP_VOL_BLKDEV_TYPE) &&
                (volType != fpi::FDSP_VOL_ISCSI_TYPE)) {
                LOGDEBUG << io->log_string()
                         << " synchronize on hashkey: " << key.first << ":" << key.second;
                serialExecutor->scheduleOnHashKey(keyHash(key),
                                                  std::bind(&dm::Handler::handleQueueItem,
                                                            parentDm->handlers.at(io->io_type),
                                                            io));
            } else {
                LOGDEBUG << io->log_string() << " not synchronized";
                threadPool->schedule(&dm::Handler::handleQueueItem,
                                     parentDm->handlers.at(io->io_type),
                                     io);
            }
            break;
        case FDS_DM_FWD_CAT_UPD:
            if (parentDm->features.isVolumegroupingEnabled()) {
                fds_panic("Not supported");
            }
            /* Forwarded IO during migration needs to synchronized on blob id */
            serialExecutor->scheduleOnHashKey(keyHash(key),
                                              std::bind(&dm::Handler::handleQueueItem,
                                                        parentDm->handlers.at(io->io_type),
                                                        io));
            break;
        case FDS_DM_FUNCTOR:
            schedule(scheduleOnId, true,
                     std::bind(&DataMgr::handleDmFunctor,
                               parentDm, io));
            break;
        case FDS_DM_VOLUME_CHK_MSG:
            serialExecutor->scheduleOnHashKey(io->volId.get(),
                                              std::bind(&DataMgr::handleInitVolCheck,
                                                        parentDm,
                                                        io));
            break;
        default:
            LOGWARN << "Unknown IO Type received";
            assert(0);
            break;
    }

    return err;
}

DataMgr::dmQosCtrl::dmQosCtrl(DataMgr *_parent,
                              uint32_t _max_thrds,
                              dispatchAlgoType algo,
                              fds_log *log) :
        FDS_QoSControl(_max_thrds, 2, algo, log, "DM") {
    parentDm = _parent;
    dispatcher = new QoSWFQDispatcher(this, parentDm->scheduleRate,
                                      parentDm->qosOutstandingTasks,
                                      false,
                                      log);

    serialExecutor = std::unique_ptr<SynchronizedTaskExecutor<size_t>>(
        new SynchronizedTaskExecutor<size_t>(*threadPool));
    if (parentDm->getModuleProvider()->get_cntrs_mgr()) {
        parentDm->getModuleProvider()->get_cntrs_mgr()->add_for_export(this);
    }
}


Error DataMgr::dmQosCtrl::markIODone(const FDS_IOType& _io) {
    Error err(ERR_OK);
    dispatcher->markIODone(const_cast<FDS_IOType *>(&_io));
    return err;
}

DataMgr::dmQosCtrl::~dmQosCtrl() {
    if (parentDm->getModuleProvider()->get_cntrs_mgr()) {
        parentDm->getModuleProvider()->get_cntrs_mgr()->remove_from_export(this);
    }

    delete dispatcher;
    if (dispatcherThread) {
        dispatcherThread->join();
    }
}

namespace dmutil {

std::string getVolumeDir(const FdsRootDir* _root, fds_volid_t volId, fds_volid_t snapId) {
    auto root = (_root == NULL)? MODULEPROVIDER()->proc_fdsroot() : _root;
    if (invalid_vol_id < snapId) {
        return util::strformat("%s/%ld/snapshot/%ld_vcat.ldb",
                               root->dir_user_repo_dm().c_str(),
                               volId.get(), snapId.get());
    } else {
        return util::strformat("%s/%ld",
                               root->dir_sys_repo_dm().c_str(), volId.get());
    }
}

std::string getTempDir(const FdsRootDir* _root, fds_volid_t volId) {
    auto root = (_root == NULL)? MODULEPROVIDER()->proc_fdsroot() : _root;
    std::string dir=util::strformat("%s/tmp", root->dir_sys_repo_dm().c_str());
    root->fds_mkdir(dir.c_str());
    return dir;
}

// location of all snapshots for a volume
std::string getSnapshotDir(const FdsRootDir* root, fds_volid_t volId) {
    return util::strformat("%s/%ld/snapshot",
                           root->dir_user_repo_dm().c_str(), volId.get());
}
// location of journal archives
std::string getTimelineDir(const FdsRootDir* root, fds_volid_t volId) {
    return util::strformat("%s/%ld",root->dir_timeline_dm().c_str(), volId.get());
}

std::string getVolumeMetaDir(const FdsRootDir* root, fds_volid_t volId) {
    return util::strformat("%s/%ld/volumemeta",
                           root->dir_user_repo_dm().c_str(), volId.get());
}

std::string getLevelDBFile(const FdsRootDir* root, fds_volid_t volId, fds_volid_t snapId) {
    if (invalid_vol_id < snapId) {
        return util::strformat("%s/%ld/snapshot/%ld_vcat.ldb",
                               root->dir_user_repo_dm().c_str(), volId.get(), snapId.get());
    } else {
        return util::strformat("%s/%ld/%ld_vcat.ldb",
                               root->dir_sys_repo_dm().c_str(), volId.get(), volId.get());
    }
}

void getVolumeIds(const FdsRootDir* root, std::vector<fds_volid_t>& vecVolumes) {
    std::vector<std::string> vecNames;

    util::getSubDirectories(root->dir_sys_repo_dm(), vecNames);

    for (const auto& name : vecNames) {
        vecVolumes.push_back(fds_volid_t(std::atoll(name.c_str())));
    }
    std::sort(vecVolumes.begin(), vecVolumes.end());
}

std::string getTimelineDBPath(const FdsRootDir* root) {
    const std::string dmDir = root->dir_sys_repo_dm();
    return util::strformat("%s/timeline.db", dmDir.c_str());
}

float_t getUsedCapacityOfSysRepo(const FdsRootDir* _root) {
    // Get fds root dir
    auto root = (_root == NULL)? MODULEPROVIDER()->proc_fdsroot() : _root;
    // Get sys-repo dir
    DiskUtils::CapacityPair cap = DiskUtils::getDiskConsumedSize(root->dir_sys_repo_dm());

    // Calculate pct
    float_t result = ((1. * cap.usedCapacity) / cap.totalCapacity) * 100;
    GLOGDEBUG << "Found DM user-repo disk capacity of (" << cap.usedCapacity << "/" << cap.totalCapacity << ") = " << result;

    return result;
}


float_t getUsedCapacityOfUserRepo(const FdsRootDir* _root) {
    // Get fds root dir
    auto root = (_root == NULL)? MODULEPROVIDER()->proc_fdsroot() : _root;
    // Get user-repo dir
    DiskUtils::CapacityPair cap = DiskUtils::getDiskConsumedSize(root->dir_user_repo_dm());

    // Calculate pct
    float_t result = ((1. * cap.usedCapacity) / cap.totalCapacity) * 100;
    GLOGDEBUG << "Found DM user-repo disk capacity of (" << cap.usedCapacity << "/" << cap.totalCapacity << ") = " << result;

    return result;
}

}  // namespace dmutil



}  // namespace fds
