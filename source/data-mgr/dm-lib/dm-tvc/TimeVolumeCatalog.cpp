/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#include <string>
#include <boost/make_shared.hpp>
#include <dm-tvc/TimeVolumeCatalog.h>

#define COMMITLOG_GET(_volId_, _commitLog_) \
    do { \
        fds_scoped_spinlock l(commitLogLock_); \
        try { \
            _commitLog_ = commitLogs_.at(_volId_); \
        } catch(const std::out_of_range &oor) { \
            return ERR_VOL_NOT_FOUND; \
        } \
    } while (false)

namespace fds {

void
DmTimeVolCatalog::notifyVolCatalogSync(BlobTxList::const_ptr sycndTxList) {
}

DmTimeVolCatalog::DmTimeVolCatalog(const std::string &name, fds_threadpool &tp)
        : Module(name.c_str()),
        opSynchronizer_(tp)
{
    volcat = DmVolumeCatalog::ptr(new DmVolumeCatalog("DM Volume Catalog"));
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
}

/**
 * Module shutdown
 */
void
DmTimeVolCatalog::mod_shutdown() {
}

Error
DmTimeVolCatalog::addVolume(const VolumeDesc& voldesc) {
    LOGDEBUG << "Will prepare commit log for new volume "
             << std::hex << voldesc.volUUID << std::dec;

    fds_scoped_spinlock l(commitLogLock_);
    if (commitLogs_.find(voldesc.volUUID) != commitLogs_.end()) {
        return ERR_VOL_DUPLICATE;
    }

    std::ostringstream oss;
    oss << "Volume_" << voldesc.volUUID;
    /* NOTE: Here the lock can be expensive.  We may want to provide an init() api
     * on DmCommitLog so that initialization can happen outside the lock
     */
    commitLogs_[voldesc.volUUID] = boost::make_shared<DmCommitLog>("DM", oss.str());
    commitLogs_[voldesc.volUUID]->mod_init(mod_params);
    commitLogs_[voldesc.volUUID]->mod_startup();

    return volcat->addCatalog(voldesc);
}

Error
DmTimeVolCatalog::activateVolume(fds_volid_t volId) {
    LOGDEBUG << "Will activate commit log for volume "
             << std::hex << volId << std::dec;
    // TODO(Anna): revisit if we need to do anything with commit log
    // when integrating meta sync (multi-dm)
    return volcat->activateCatalog(volId);
}

Error
DmTimeVolCatalog::startBlobTx(fds_volid_t volId,
                              const std::string &blobName,
                              const fds_int32_t blobMode,
                              BlobTxId::const_ptr txDesc) {
    LOGDEBUG << "Starting transaction " << *txDesc << " for blob " << blobName <<
            " volume " << std::hex << volId << std::dec << " blob mode " << blobMode;
    DmCommitLog::ptr commitLog;
    COMMITLOG_GET(volId, commitLog);
    return commitLog->startTx(txDesc, blobName, blobMode);
}

Error
DmTimeVolCatalog::updateBlobTx(fds_volid_t volId,
                               BlobTxId::const_ptr txDesc,
                               const fpi::FDSP_BlobObjectList &objList) {
    LOGDEBUG << "Updating offsets for transaction " << *txDesc
             << " volume " << std::hex << volId << std::dec;
    auto boList = boost::make_shared<const BlobObjList>(objList);

    DmCommitLog::ptr commitLog;
    COMMITLOG_GET(volId, commitLog);
    return commitLog->updateTx(txDesc, boList);
}

Error
DmTimeVolCatalog::updateBlobTx(fds_volid_t volId,
                               BlobTxId::const_ptr txDesc,
                               const fpi::FDSP_MetaDataList &metaList) {
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
                             blob_version_t blob_version) {
    LOGDEBUG << "Deleting Blob for transaction " << *txDesc << ", volume " <<
            std::hex << volId << std::dec << " version " << blob_version;

    DmCommitLog::ptr commitLog;
    COMMITLOG_GET(volId, commitLog);
    return commitLog->deleteBlob(txDesc, blob_version);
}

Error
DmTimeVolCatalog::commitBlobTx(fds_volid_t volId,
                               const std::string &blobName,
                               BlobTxId::const_ptr txDesc,
                               const DmTimeVolCatalog::CommitCb &cb) {
    LOGDEBUG << "Will commit transaction " << *txDesc << " for volume "
             << std::hex << volId << std::dec << " blob " << blobName;
    DmCommitLog::ptr commitLog;
    COMMITLOG_GET(volId, commitLog);
    opSynchronizer_.schedule(blobName,
                             std::bind(&DmTimeVolCatalog::commitBlobTxWork,
                                       this, volId, commitLog, txDesc, cb));
    return ERR_OK;
}

Error
DmTimeVolCatalog::updateFwdCommittedBlob(fds_volid_t volId,
                                         const std::string &blobName,
                                         blob_version_t blobVersion,
                                         const fpi::FDSP_BlobObjectList &objList,
                                         const fpi::FDSP_MetaDataList &metaList,
                                         const DmTimeVolCatalog::FwdCommitCb &fwdCommitCb) {
    LOGDEBUG << "Will apply committed blob update from another DM for volume "
             << std::hex << volId << std::dec << " blob " << blobName;

    // we don't go through commit log, but we need to serialized fwd updates
    opSynchronizer_.schedule(blobName,
                             std::bind(&DmTimeVolCatalog::updateFwdBlobWork,
                                       this, volId, blobName, blobVersion,
                                       objList, metaList, fwdCommitCb));

    return ERR_OK;
}

void
DmTimeVolCatalog::commitBlobTxWork(fds_volid_t volid,
                                   DmCommitLog::ptr &commitLog,
                                   BlobTxId::const_ptr txDesc,
                                   const DmTimeVolCatalog::CommitCb &cb) {
    Error e;
    blob_version_t blob_version = blob_version_invalid;
    LOGDEBUG << "Committing transaction " << *txDesc << " for volume "
             << std::hex << volid << std::dec;
    CommitLogTx::const_ptr commit_data = commitLog->commitTx(txDesc, e);
    if (e.ok()) {
        if (commit_data->blobDelete) {
            e = volcat->deleteBlob(volid, commit_data->blobName, commit_data->blobVersion);
            blob_version = commit_data->blobVersion;
        } else if (commit_data->blobObjList && (commit_data->blobObjList->size() > 0)) {
            if (commit_data->blobMode & blob::TRUNCATE) {
                commit_data->blobObjList->setEndOfBlob();
            }
            e = volcat->putBlob(volid, commit_data->blobName, commit_data->metaDataList,
                                commit_data->blobObjList, txDesc);
        } else {
            e = volcat->putBlobMeta(volid, commit_data->blobName,
                                    commit_data->metaDataList, txDesc);
        }
    }
    cb(e, blob_version, commit_data->blobObjList, commit_data->metaDataList);
    commitLog->purgeTx(txDesc);
}

void DmTimeVolCatalog::updateFwdBlobWork(fds_volid_t volid,
                                         const std::string &blobName,
                                         blob_version_t blobVersion,
                                         const fpi::FDSP_BlobObjectList &objList,
                                         const fpi::FDSP_MetaDataList &metaList,
                                         const DmTimeVolCatalog::FwdCommitCb &fwdCommitCb) {
    Error err(ERR_OK);
    // TODO(xxx): use blob mode to tell if that's a deletion
    if (blobVersion == blob_version_deleted) {
        LOGDEBUG << "Applying committed forwarded delete for blob " << blobName
                 << " volume " << std::hex << volid << std::dec;
        fds_panic("not implemented!");
        // err = volcat->deleteBlob(volid, blobName, blobVersion);
    } else {
        LOGDEBUG << "Applying committed forwarded update for blob " << blobName
                 << " volume " << std::hex << volid << std::dec;

        if (objList.size() > 0) {
            BlobObjList::ptr olist(new(std::nothrow) BlobObjList(objList));
            // TODO(anna) pass truncate op, for now always truncate
            olist->setEndOfBlob();

            if (metaList.size() > 0) {
                MetaDataList::ptr mlist(new(std::nothrow) MetaDataList(metaList));
                err = volcat->putBlob(volid, blobName, mlist, olist, BlobTxId::ptr());
            } else {
                err = volcat->putBlob(volid, blobName, MetaDataList::ptr(),
                                      olist, BlobTxId::ptr());
            }
        } else {
            fds_verify(metaList.size() > 0);
            MetaDataList::ptr mlist(new(std::nothrow) MetaDataList(metaList));
            err = volcat->putBlobMeta(volid, blobName, mlist, BlobTxId::ptr());
        }
    }

    fwdCommitCb(err);
}

Error
DmTimeVolCatalog::abortBlobTx(fds_volid_t volId,
                              BlobTxId::const_ptr txDesc) {
    return ERR_OK;
}

fds_bool_t
DmTimeVolCatalog::isPendingTx(fds_volid_t volId, fds_uint64_t timeNano) {
    DmCommitLog::ptr commitLog;
    COMMITLOG_GET(volId, commitLog);
    return commitLog->isPendingTx(timeNano);
}

}  // namespace fds
