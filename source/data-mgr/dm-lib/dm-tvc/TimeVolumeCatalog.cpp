/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
extern "C" {
#include <sys/types.h>
#include <sys/inotify.h>
#include <dirent.h>
}

#include <functional>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <limits>
#include <DataMgr.h>
#include <cstdlib>
#include <util/disk_utils.h>
#include <boost/make_shared.hpp>
#include <fiu-control.h>
#include <fiu-local.h>
#include <dm-tvc/TimeVolumeCatalog.h>
#include <dm-tvc/VolumeAccessTable.h>
#include <dm-vol-cat/DmPersistVolDB.h>
#include <fds_process.h>
#include <ObjectLogger.h>
#include "fdsp/sm_api_types.h"
#include <leveldb/copy_env.h>
#include <leveldb/cat_journal.h>

#define COMMITLOG_GET(_volId_, _commitLog_)                             \
    do {                                                                \
        fds_scoped_lock l(commitLogLock_);                               \
        try {                                                           \
            _commitLog_ = commitLogs_.at(_volId_);                      \
        } catch(const std::out_of_range &oor) {                         \
            LOGWARN << "unable to get commit log for vol:" << _volId_;  \
            return ERR_VOL_NOT_FOUND;                                   \
        }                                                               \
    } while (false)

#define TVC_CHECK_AVAILABILITY()                                       \
    TimeVolumeCatalogState curState = currentState.load();             \
    if (curState == TVC_UNAVAILABLE) {                                 \
        LOGERROR << "TVC in UNAVAILABLE state. ";                      \
        return ERR_NODE_NOT_ACTIVE;                                    \
    } else if (curState == TVC_INIT) {                                 \
        LOGERROR << "TVC is coming up, but not ready to accept IO";    \
        return ERR_NOT_READY;                                          \
    }                                                                  \

namespace fds {

void
DmTimeVolCatalog::notifyVolCatalogSync(BlobTxList::const_ptr sycndTxList) {
}

DmTimeVolCatalog::DmTimeVolCatalog(CommonModuleProviderIf *modProvider,
                                   const std::string &name,
                                   fds_threadpool &tp,
                                   DataMgr& dataManager)
        : HasModuleProvider(modProvider),
          Module(name.c_str()),
          dataManager_(dataManager),
          currentState(TVC_INIT),
          opSynchronizer_(tp),
          config_helper_(MODULEPROVIDER()->get_conf_helper()),
          tp_(tp) {
    volcat = DmVolumeCatalog::ptr(new DmVolumeCatalog(MODULEPROVIDER(), "DM Volume Catalog"));

    /**
     * FEATURE TOGGLE: Volume Open Support
     * Thu 02 Apr 2015 12:39:27 PM PDT
     */
    if (dataManager.features.isVolumeTokensEnabled()) {
        vol_tok_lease_time =
            std::chrono::duration<fds_uint32_t>(
                config_helper_.get_abs<fds_uint32_t>("fds.dm.token_lease_time"));
    }

    // TODO(Andrew): The module vector should be able to take smart pointers.
    // To get around this for now, we're extracting the raw pointer and
    // expecting that any reference to are done once this returns...
    static Module* tvcMods[] = {
        static_cast<Module *>(volcat.get()),
        NULL
    };
}

DmTimeVolCatalog::~DmTimeVolCatalog() {
}

/**
 * Module initialization
 */
int
DmTimeVolCatalog::mod_init(SysParams const *const p) {
    Module::mod_init(p);
    return 0;
}

/**
 * Module startup
 */
void
DmTimeVolCatalog::mod_startup() {
    // check if mount point for volume catalog exists
    std::string path = MODULEPROVIDER()->proc_fdsroot()->dir_sys_repo_dm() + "/.tempFlush";
    fds_bool_t diskTestFailure = DiskUtils::diskFileTest(path);
    if (!diskTestFailure) {
        // check if we have enough disk space
        float_t pct_used = getUsedCapacityAsPct();
        if (pct_used >= DISK_CAPACITY_ERROR_THRESHOLD) {
            LOGERROR << "ERROR: DM already used " << pct_used << "% of available storage space!"
                     <<" Setting DM to UNAVAILABLE state";
            diskTestFailure = true;
        }
    } else {
        LOGERROR << "Looks like DM sys repo mount point "
                 << MODULEPROVIDER()->proc_fdsroot()->dir_sys_repo_dm()
                 << " is unreachable; setting DM to UNAVAILABLE state";
    }
    if (diskTestFailure) {
        setUnavailable();
        return;
    }

    // looks like simple checks pass, setting DM state to READY
    LOGDEBUG << "TVC state -> READY";
    currentState = TVC_READY;
}

/**
 * Module shutdown
 */
void
DmTimeVolCatalog::mod_shutdown() {
}

void
DmTimeVolCatalog::setUnavailable() {
    LOGDEBUG << "Setting TVC state to UNAVAILABLE. This will reject future write IO";
    currentState = TVC_UNAVAILABLE;
}

fds_bool_t
DmTimeVolCatalog::isUnavailable() {
    TimeVolumeCatalogState curState = currentState.load();
    return (curState == TVC_UNAVAILABLE);
}

void DmTimeVolCatalog::createCommitLog(const VolumeDesc& voldesc) {
    LOGDEBUG << "Will prepare commit log for new volume "
             << std::hex << voldesc.volUUID << std::dec;
    /* NOTE: Here the lock can be expensive.  We may want to provide an init() api
     * on DmCommitLog so that initialization can happen outside the lock
     */

    if (voldesc.isSnapshot()) {
        return;
    }

    fds_scoped_lock l(commitLogLock_);
    commitLogs_[voldesc.volUUID] =
        boost::make_shared<DmCommitLog>(MODULEPROVIDER()->proc_fdsroot(),
                                        "DM",
                                        voldesc.volUUID,
                                        voldesc.maxObjSizeInBytes);
    commitLogs_[voldesc.volUUID]->mod_init(mod_params);
    commitLogs_[voldesc.volUUID]->mod_startup();
}

Error
DmTimeVolCatalog::addVolume(const VolumeDesc& voldesc) {
    createCommitLog(voldesc);
    return volcat->addCatalog(voldesc);
}

Error
DmTimeVolCatalog::openVolume(fds_volid_t const volId,
                             fpi::SvcUuid const& client_uuid,
                             fds_int64_t& token,
                             fpi::VolumeAccessMode const& mode,
                             sequence_id_t& sequence_id,
                             int32_t &version) {
    Error err = ERR_OK;
    /**
     * FEATURE TOGGLE: Volume Open Support
     * Thu 02 Apr 2015 12:39:27 PM PDT
     */
    if (dataManager_.features.isVolumeTokensEnabled()) {
        std::unique_lock<std::mutex> lk(accessTableLock_);
        auto it = accessTable_.find(volId);
        if (accessTable_.end() == it) {
            auto table = new DmVolumeAccessTable(volId, MODULEPROVIDER()->getTimer());
            table->getToken(client_uuid, token, mode, vol_tok_lease_time);
            accessTable_[volId].reset(table);
        } else {
            // Table already exists, check if we can attach again or
            // renew the existing token.
            err = it->second->getToken(client_uuid, token, mode, vol_tok_lease_time);
        }
    }

    auto volumeMeta = dataManager_.getVolumeMeta(volId, false);
    sequence_id = volumeMeta->getSequenceId();
    version = volumeMeta->getVersion();

    return err;
}

Error
DmTimeVolCatalog::closeVolume(fds_volid_t const volId, fds_int64_t const token) {
    Error err = ERR_OK;
    /**
     * FEATURE TOGGLE: Volume Open Support
     * Thu 02 Apr 2015 12:39:27 PM PDT
     */
    if (dataManager_.features.isVolumeTokensEnabled()) {
        std::unique_lock<std::mutex> lk(accessTableLock_);
        auto it = accessTable_.find(volId);
        if (accessTable_.end() != it) {
            it->second->removeToken(token);
        }
    }

    // TODO(bszmyd): Tue 07 Apr 2015 01:02:51 AM PDT
    // Determine if there is any usefulness returning an
    // error to this operation. Seems like it should be
    // idempotent and never fail upon initial design.
    return err;
}

Error
DmTimeVolCatalog::copyVolume(VolumeDesc & voldesc, fds_volid_t origSrcVolume) {
    Error rc(ERR_OK);
    DmCommitLog::ptr commitLog;
    // COMMITLOG_GET(voldesc.srcVolumeId, commitLog);
    // fds_assert(commitLog);

    if (invalid_vol_id == origSrcVolume) {
        origSrcVolume = voldesc.srcVolumeId;
    }
    LOGDEBUG << "copying into volume [" << voldesc.volUUID
             << "] from srcvol:" << voldesc.srcVolumeId;

    if (voldesc.isClone()) {
        createCommitLog(voldesc);
    }

    // Create snapshot of volume catalog
    rc = volcat->copyVolume(voldesc);
    if (!rc.ok()) {
        LOGERROR << "Failed to copy catalog for snapshot '"
                  << std::hex << voldesc.volUUID << std::dec
                  << "', volume '" << std::hex << voldesc.srcVolumeId;
        return rc;
    }

    return rc;
}

Error
DmTimeVolCatalog::activateVolume(fds_volid_t volId) {
    LOGDEBUG << "Will activate commit log for volume "
             << std::hex << volId << std::dec;
    return volcat->activateCatalog(volId);
}

Error
DmTimeVolCatalog::markVolumeDeleted(fds_volid_t volId) {
    auto vol = volcat->getVolume(volId);

    if (vol == NULL)
       return ERR_VOL_NOT_FOUND;                                   \

    // if (!vol->isSnapshot() && isPendingTx(volId, 0)) return ERR_VOL_NOT_EMPTY;
    Error err = volcat->markVolumeDeleted(volId);
    if (err.ok()) {
        // TODO(Anna) @Umesh we should mark commit log as
        // deleted and reject all tx to this commit log
        // only mark log as deleted if no pending tx, otherwise
        // return error
        LOGDEBUG << "path:voldelete Marked volume as deleted, vol:" << volId;
    }
    return err;
}

Error
DmTimeVolCatalog::deleteVolume(fds_volid_t volId) {
    Error err = volcat->deleteCatalog(volId);
    if (err.ok()) {
        fds_scoped_lock l(commitLogLock_);
        if (commitLogs_.count(volId) > 0) {
            // found commit log
            commitLogs_.erase(volId);
            LOGNOTIFY << "path:voldelete vol:" << volId << " removed commit logs";
        }
    } else {
        LOGERROR << "path:voldelete vol:" << volId << " will not remove commit logs err:" << err;
    }
    return err;
}

Error
DmTimeVolCatalog::setVolumeMetadata(fds_volid_t volId,
                                    const fpi::FDSP_MetaDataList &metadataList,
                                    const sequence_id_t seq_id) {
    return volcat->setVolumeMetadata(volId, metadataList, seq_id);
}

Error
DmTimeVolCatalog::startBlobTx(fds_volid_t volId,
                              const std::string &blobName,
                              const fds_int32_t blobMode,
                              BlobTxId::const_ptr txDesc,
                              fds_uint64_t dmtVersion) {
    TVC_CHECK_AVAILABILITY();
    LOGDEBUG << "Starting transaction " << *txDesc << " for blob " << blobName <<
            " volume " << std::hex << volId << std::dec << " blob mode " << blobMode;
    DmCommitLog::ptr commitLog;
    COMMITLOG_GET(volId, commitLog);
    return commitLog->startTx(txDesc, blobName, blobMode, dmtVersion);
}

Error
DmTimeVolCatalog::updateBlobTx(fds_volid_t volId,
                               BlobTxId::const_ptr txDesc,
                               const fpi::FDSP_BlobObjectList &objList) {
    TVC_CHECK_AVAILABILITY();
    LOGDEBUG << "Updating offsets for transaction " << *txDesc
             << " volume " << std::hex << volId << std::dec;
    DmCommitLog::ptr commitLog;
    COMMITLOG_GET(volId, commitLog);
#ifdef ACTIVE_TX_IN_WRITE_BATCH
    return commitLog->updateTx(txDesc, objList);
#else
    auto boList = boost::make_shared<const BlobObjList>(objList);
    return commitLog->updateTx(txDesc, boList);
#endif
}

Error
DmTimeVolCatalog::updateBlobTx(fds_volid_t volId,
                               BlobTxId::const_ptr txDesc,
                               const fpi::FDSP_MetaDataList &metaList) {
    TVC_CHECK_AVAILABILITY();
    LOGDEBUG << "Updating metadata for transaction " << *txDesc
             << " volume " << std::hex << volId << std::dec;

    auto mdList = boost::make_shared<const MetaDataList>(metaList);
    DmCommitLog::ptr commitLog;
    COMMITLOG_GET(volId, commitLog);
    return commitLog->updateTx(txDesc, mdList);
}

Error
DmTimeVolCatalog::deleteBlob(fds_volid_t volId,
                             BlobTxId::const_ptr txDesc,
                             blob_version_t const blob_version,
                             bool const expunge_data) {
    TVC_CHECK_AVAILABILITY();
    LOGDEBUG << "Deleting Blob for transaction " << *txDesc << ", volume " <<
            std::hex << volId << std::dec << " version " << blob_version
            << " (expunge: [" << expunge_data << "])";

    DmCommitLog::ptr commitLog;
    COMMITLOG_GET(volId, commitLog);
    return commitLog->deleteBlob(txDesc, blob_version, expunge_data);
}

Error
DmTimeVolCatalog::commitBlobTx(fds_volid_t volId,
                               const std::string &blobName,
                               BlobTxId::const_ptr txDesc,
                               const sequence_id_t seq_id,
                               const DmTimeVolCatalog::CommitCb &cb) {
    TVC_CHECK_AVAILABILITY();
    LOGDEBUG << "Will commit transaction " << *txDesc << " for volume "
             << std::hex << volId << std::dec << " blob " << blobName;
    DmCommitLog::ptr commitLog;
    COMMITLOG_GET(volId, commitLog);

    if (dataManager_.features.isVolumegroupingEnabled()) {
        commitBlobTxWork(volId, blobName, commitLog, txDesc, seq_id, cb);
        return ERR_OK;
    }

    opSynchronizer_.scheduleOnHashKey(std::hash<std::string>()(blobName),
                                      std::bind(&DmTimeVolCatalog::commitBlobTxWork,
                                                this,
                                                volId,
                                                blobName,
                                                commitLog,
                                                txDesc,
                                                seq_id,
                                                cb));
    return ERR_OK;
}

void
DmTimeVolCatalog::commitBlobTxWork(fds_volid_t volid,
				   const std::string &blobName,
                                   DmCommitLog::ptr &commitLog,
                                   BlobTxId::const_ptr txDesc,
                                   const sequence_id_t seq_id,
                                   const DmTimeVolCatalog::CommitCb &cb) {
    Error e;
    blob_version_t blob_version = blob_version_invalid;
    LOGDEBUG << "Committing transaction " << *txDesc << " for volume "
             << std::hex << volid << std::dec;

    // Lock the commit log with a READ lock to block a snapshot/forward transition
    // during migration
    auto auto_lock = commitLog->getCommitLock();
    CommitLogTx::ptr commit_data = commitLog->commitTx(txDesc, e);
    if (e.ok()) {
        BlobMetaDesc desc;

        e = volcat->getBlobMetaDesc(volid, blobName, desc);

        // If error code is not OK, no blob to collide with. If given blob's name doesn't
        // doesn't match the existing blob metadata, there is a UUID collision. The
        // resolution is to try again with a different blob name (e.g. append a character)

        // Collision check is done here to piggyback on the synchronization. The
        // goal is to avoid 2 new, colliding blobs from both passing this check
        // due to a data race.
        if (e.ok() && desc.desc.blob_name != blobName){
            LOGERROR << "Blob Id collision for new blob " << blobName << " on volume "
                 << std::hex << volid << std::dec << " with existing blob "
                 << desc.desc.blob_name;

            e = Error(ERR_HASH_COLLISION);
        } else {
            e = doCommitBlob(volid, blob_version, seq_id, commit_data);
        }
    }

    fds_assert(txDesc != nullptr);
    if (commit_data != nullptr) {
        cb(e,
           blob_version,
           commit_data->blobObjList,
           commit_data->metaDataList,
           commit_data->blobSize);
    } else {
        fds_verify(!e.OK());
        cb(e,
           blob_version,
           nullptr,
           nullptr,
           0);
    }
}

Error
DmTimeVolCatalog::doCommitBlob(fds_volid_t volid, blob_version_t & blob_version,
                               const sequence_id_t seq_id, CommitLogTx::ptr commit_data) {
    Error e;
    if (commit_data->blobDelete) {
        e = volcat->deleteBlob(volid, commit_data->name, commit_data->blobVersion, commit_data->blobExpunge);
        // AM is sending in blob_version_invalid. We overload it here internally.
        LOGDEBUG << "Blob version set to delete for volID: " << volid << " blob name: "
        		<< commit_data->name;
        blob_version = blob_version_deleted;
    } else {
#ifdef ACTIVE_TX_IN_WRITE_BATCH
        e = volcat->putBlob(volid, commit_data->name, commit_data->blobSize,
                            commit_data->metaDataList, commit_data->wb, seq_id,
                            0 != (commit_data->blobMode & blob::TRUNCATE));
#else
        if (commit_data->blobObjList && !commit_data->blobObjList->empty()) {
            if (commit_data->blobMode & blob::TRUNCATE) {
                commit_data->blobObjList->setEndOfBlob();
            }
            e = volcat->putBlob(volid, commit_data->name, commit_data->metaDataList,
                                commit_data->blobObjList, commit_data->txDesc, seq_id);
        } else {
            e = volcat->putBlobMeta(volid, commit_data->name,
                                    commit_data->metaDataList,
                                    commit_data->txDesc, seq_id);
        }

        if (ERR_OK == e) {
            // Update the blob size and metadata in the commit data
            fpi::FDSP_MetaDataList metaList;
            e = volcat->getBlobMeta(volid,
                                    commit_data->name,
                                    nullptr,
                                    &commit_data->blobSize,
                                    &metaList);
            commit_data->metaDataList = boost::make_shared<MetaDataList>(metaList);

            // if the operation suceeded, update cached copy of sequence_id
            dataManager_.getVolumeMeta(volid, false)->setSequenceId(seq_id);
        }
#endif
    }

    return e;
}

Error DmTimeVolCatalog::updateFwdCommittedBlob(fds_volid_t volid,
                                         const std::string &blobName,
                                         blob_version_t blobVersion,
                                         const fpi::FDSP_BlobObjectList &objList,
                                         const fpi::FDSP_MetaDataList &metaList,
                                         const sequence_id_t seq_id) {
    TVC_CHECK_AVAILABILITY();

    Error err(ERR_OK);
    // TODO(xxx): use blob mode to tell if that's a deletion
    if (blobVersion == blob_version_deleted) {
        LOGDEBUG << "Applying committed forwarded delete for blob " << blobName
                 << " volume " << std::hex << volid << std::dec;
        err = volcat->deleteBlob(volid, blobName, blobVersion);
    } else {
        LOGDEBUG << "Applying committed forwarded update for blob " << blobName
                 << " volume " << std::hex << volid << std::dec;

        if (objList.size() > 0) {
            BlobObjList::ptr olist(new(std::nothrow) BlobObjList(objList));
            // never trunctating is less bad than always truncating. hopefully forwarding is going away soon.
            //olist->setEndOfBlob();

            if (metaList.size() > 0) {
                MetaDataList::ptr mlist(new(std::nothrow) MetaDataList(metaList));
                err = volcat->putBlob(volid, blobName, mlist, olist, BlobTxId::ptr(), seq_id);
            } else {
                err = volcat->putBlob(volid, blobName, MetaDataList::ptr(),
                                      olist, BlobTxId::ptr(), seq_id);
            }
        } else {
            fds_verify(metaList.size() > 0);
            MetaDataList::ptr mlist(new(std::nothrow) MetaDataList(metaList));
            err = volcat->putBlobMeta(volid, blobName, mlist, BlobTxId::ptr(), seq_id);
        }

        if (ERR_OK == err) {
            // if the operation suceeded, update cached copy of sequence_id
            dataManager_.getVolumeMeta(volid, false)->setSequenceId(seq_id);
        }
    }
    return err;
}

Error
DmTimeVolCatalog::abortBlobTx(fds_volid_t volId,
                              BlobTxId::const_ptr txDesc) {
    DmCommitLog::ptr commitLog;
    COMMITLOG_GET(volId, commitLog);
    return commitLog->rollbackTx(txDesc);
}

fds_bool_t
DmTimeVolCatalog::isPendingTx(fds_volid_t volId, fds_uint64_t timeNano) {
    DmCommitLog::ptr commitLog;
    COMMITLOG_GET(volId, commitLog);
    return commitLog->isPendingTx(timeNano);
}

Error
DmTimeVolCatalog::getCommitlog(fds_volid_t volId,  DmCommitLog::ptr &commitLog) {
    Error rc(ERR_OK);
    COMMITLOG_GET(volId, commitLog);
    return rc;
}

Error
DmTimeVolCatalog::migrateDescriptor(fds_volid_t volId,
                                    const std::string& blobName,
                                    const std::string& blobData) {
    return volcat->migrateDescriptor(volId, blobName, blobData, *dataManager_.getVolumeMeta(volId, false));
}

float_t
DmTimeVolCatalog::getUsedCapacityAsPct() {
    // Error injection points
    // Causes the method to return DISK_CAPACITY_ERROR_THRESHOLD capacity
    fiu_do_on("dm.get_used_capacity_error", \
              fiu_disable("dm.get_used_capacity_warn"); \
              fiu_disable("dm.get_used_capacity_alert"); \
              LOGDEBUG << "Err inection: returning max used disk capacity as " \
              << DISK_CAPACITY_ERROR_THRESHOLD; \
              return DISK_CAPACITY_ERROR_THRESHOLD; );

    // Causes the method to return DISK_CAPACITY_ALERT_THRESHOLD + 1 % capacity
    fiu_do_on("dm.get_used_capacity_alert", \
              fiu_disable("dm.get_used_capacity_warn"); \
              fiu_disable("dm.get_used_capacity_error"); \
              LOGDEBUG << "Err injection: returning max used disk capacity as " \
              << DISK_CAPACITY_ALERT_THRESHOLD + 1; \
              return DISK_CAPACITY_ALERT_THRESHOLD + 1; );

    // Causes the method to return DISK_CAPACITY_WARNING_THRESHOLD + 1 % capacity
    fiu_do_on("dm.get_used_capacity_warn", \
              fiu_disable("dm.get_used_capacity_error"); \
              fiu_disable("dm.get_used_capacity_alert"); \
              LOGDEBUG << "Err injection: returning max used disk capacity as " \
              << DISK_CAPACITY_WARNING_THRESHOLD + 1; \
              return DISK_CAPACITY_WARNING_THRESHOLD + 1; );

    // Calculate disk usage capacity pct
    float_t result = dmutil::getUsedCapacityOfSysRepo(MODULEPROVIDER()->proc_fdsroot());
    GLOGDEBUG << "Found DM sys-repo disk capacity of " << result;

    return result;
}

}  // namespace fds
