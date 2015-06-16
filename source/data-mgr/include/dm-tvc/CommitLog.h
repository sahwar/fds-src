/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#ifndef SOURCE_DATA_MGR_INCLUDE_DM_TVC_COMMITLOG_H_
#define SOURCE_DATA_MGR_INCLUDE_DM_TVC_COMMITLOG_H_

#include <string>
#include <vector>

#include <fds_module.h>
#include <fds_error.h>
#include <util/timeutils.h>
#include <concurrency/Mutex.h>
#include <ObjectLogger.h>
#include <lib/Catalog.h>
#include <blob/BlobTypes.h>
#include <DmBlobTypes.h>

namespace fds {

// commit log transaction details
struct CommitLogTx : serialize::Serializable {
    typedef boost::shared_ptr<CommitLogTx> ptr;
    typedef boost::shared_ptr<const CommitLogTx> const_ptr;

    BlobTxId::const_ptr txDesc;
    std::string name;
    fds_int32_t blobMode;

    fds_uint64_t started;
    fds_uint64_t committed;     // commit issued by user, but not written yet
    bool blobDelete;
    bool snapshot;

    BlobObjList::ptr blobObjList;
    MetaDataList::ptr metaDataList;

    CatWriteBatch wb;

    blob_version_t blobVersion;
    fds_uint64_t nameId;
    fds_uint64_t blobSize;

    CommitLogTx() : txDesc(0), blobMode(0), started(0), committed(0), blobDelete(false),
            snapshot(false), blobObjList(new BlobObjList()), metaDataList(new MetaDataList()),
            blobVersion(blob_version_invalid), nameId(0), blobSize(0) {}

    virtual uint32_t write(serialize::Serializer * s) const override;
    virtual uint32_t read(serialize::Deserializer * d) override;
};

typedef std::unordered_map<BlobTxId, CommitLogTx::ptr> TxMap;

/**
 * Manages pending DM catalog updates and commits. Used
 * for 2-phase commit operations. This management
 * includes the on-disk log and the in-memory state.
 */
class DmCommitLog : public Module {
  public:
    typedef boost::shared_ptr<DmCommitLog> ptr;
    typedef boost::shared_ptr<const DmCommitLog> const_ptr;

    // ctor & dtor
    DmCommitLog(const std::string &modName, const fds_volid_t volId, const fds_uint32_t objSize);
    ~DmCommitLog();

    // module overrides
    int  mod_init(SysParams const *const param) override;
    void mod_startup() override;
    void mod_shutdown() override;

    /*
     * operations
     */
    // start transaction
    Error startTx(BlobTxId::const_ptr & txDesc, const std::string & blobName,
            fds_int32_t blobMode);

    // update blob data (T can be BlobObjList or MetaDataList)
    template<typename T>
    Error updateTx(BlobTxId::const_ptr & txDesc, boost::shared_ptr<const T> & blobData);

    // update blob data (T can be fpi::FDSP_BlobObjectList)
    template<typename T>
    Error updateTx(BlobTxId::const_ptr & txDesc, const T & blobData);

    // delete blob
    Error deleteBlob(BlobTxId::const_ptr & txDesc, const blob_version_t blobVersion);

    // commit transaction (time at which commit is ACKed)
    CommitLogTx::ptr commitTx(BlobTxId::const_ptr & txDesc, Error & status);

    // rollback transaction
    Error rollbackTx(BlobTxId::const_ptr & txDesc);

    // snapshot
    Error snapshot(BlobTxId::const_ptr & txDesc, const std::string & name);

    // get transaction
    CommitLogTx::const_ptr getTx(BlobTxId::const_ptr & txDesc);

    // check if any transaction is pending from before the given time
    fds_bool_t isPendingTx(const fds_uint64_t tsNano = util::getTimeStampNanos());

    // get active transactions
    fds_uint32_t getActiveTx() {
        FDSGUARD(lockTxMap_);
        return txMap_.size();
    }

  private:
    TxMap txMap_;    // in-memory state
    fds_mutex lockTxMap_;

    fds_volid_t volId_;
    fds_uint32_t objSize_;
    bool started_;


    // Methods
    Error validateSubsequentTx(const BlobTxId & txId);

    void upsertBlobData(CommitLogTx & tx, boost::shared_ptr<const BlobObjList> & data) {
        if (tx.blobObjList) {
            tx.blobObjList->merge(*data);
        } else {
            tx.blobObjList.reset(new BlobObjList(*data));
        }
    }

    void upsertBlobData(CommitLogTx & tx, boost::shared_ptr<const MetaDataList> & data) {
        if (tx.metaDataList) {
            tx.metaDataList->merge(*data);
        } else {
            tx.metaDataList.reset(new MetaDataList(*data));
        }
    }

    void upsertBlobData(CommitLogTx & tx, const fpi::FDSP_BlobObjectList & data);

    Error snapshotInsert(BlobTxId::const_ptr & txDesc);
};
}  /* namespace fds */

#endif  // SOURCE_DATA_MGR_INCLUDE_DM_TVC_COMMITLOG_H_
