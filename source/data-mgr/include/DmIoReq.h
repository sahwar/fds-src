/*
 * Copyright 2013 Formation Data Systems, Inc.
 */

/*
 * Classes for DM requests that go through QoS queues
 */

#ifndef SOURCE_DATA_MGR_INCLUDE_DMIOREQ_H_
#define SOURCE_DATA_MGR_INCLUDE_DMIOREQ_H_

#include <string>
#include <fds_error.h>
#include <fds_types.h>
#include <fds_volume.h>
#include <fds_resource.h>
#include <fdsp/FDSP_types.h>
#include <fds_typedefs.h>
#include <blob/BlobTypes.h>
#include <fdsp/dm_api_types.h>
#include <fdsp/fds_stream_types.h>

#define FdsDmSysTaskId      fds_volid_t(0x8fffffff)
#define FdsDmSysTaskPrio    5

namespace fds {

// Some logging routines have external linkage
// ======
extern std::string logString(const FDS_ProtocolInterface::AbortBlobTxMsg& abortBlobTx);
extern std::string logString(const FDS_ProtocolInterface::CommitBlobTxMsg& commitBlobTx);
extern std::string logString(const FDS_ProtocolInterface::DeleteBlobMsg& msg);
extern std::string logString(const FDS_ProtocolInterface::GetBlobMetaDataMsg& msg);
extern std::string logString(const FDS_ProtocolInterface::GetBucketMsg& msg);
extern std::string logString(const FDS_ProtocolInterface::GetBucketRspMsg& msg);
extern std::string logString(const FDS_ProtocolInterface::StatVolumeMsg& msg);
extern std::string logString(const FDS_ProtocolInterface::SetVolumeMetadataMsg& msg);
extern std::string logString(const FDS_ProtocolInterface::GetVolumeMetadataMsgRsp& msg);
extern std::string logString(const FDS_ProtocolInterface::QueryCatalogMsg& qryCat);
extern std::string logString(const FDS_ProtocolInterface::SetBlobMetaDataMsg& msg);
extern std::string logString(const FDS_ProtocolInterface::StartBlobTxMsg& stBlobTx);
extern std::string logString(const FDS_ProtocolInterface::UpdateCatalogMsg& updCat);
extern std::string logString(const FDS_ProtocolInterface::UpdateCatalogRspMsg& updCat);
extern std::string logString(const FDS_ProtocolInterface::UpdateCatalogOnceMsg& updCat);
extern std::string logString(const FDS_ProtocolInterface::UpdateCatalogOnceRspMsg& updCat);
extern std::string logString(const FDS_ProtocolInterface::OpenVolumeMsg& msg);
extern std::string logString(const FDS_ProtocolInterface::CloseVolumeMsg& msg);
extern std::string logString(const FDS_ProtocolInterface::ReloadVolumeMsg& msg);
extern std::string logString(const FDS_ProtocolInterface::CtrlNotifyDMStartMigrationMsg& msg);
extern std::string logString(const FDS_ProtocolInterface::CtrlNotifyInitialBlobFilterSetMsg& msg);
// ======

    /*
     * TODO: Make more generic name than catalog request
     */
    class dmCatReq : public FDS_IOType {
      public:
        fds_volid_t  volId;
        std::string blob_name;
        blob_version_t blob_version;
        std::string session_uuid;
        BlobTxId::const_ptr blobTxId;
        std::function<void(const Error &e, dmCatReq *dmRequest)> cb = NULL;
        std::function<void(dmCatReq * req)> proc = NULL;

        dmCatReq(fds_volid_t  _volId,
                 const std::string &_blobName,
                 std::string  _session_uuid,
                 blob_version_t _blob_version,
                 fds_io_op_t  _ioType)
                : volId(_volId), blob_name(_blobName), session_uuid(_session_uuid) {
            io_type = _ioType;
            io_vol_id = _volId;
            blob_version = _blob_version;
            // perf-trace related data
        }

        dmCatReq() {
        }

        fds_volid_t getVolId() const {
            return volId;
        }

        virtual ~dmCatReq() {
        }

        void setBlobVersion(blob_version_t version) {
            blob_version = version;
        }
        void setBlobTxId(BlobTxId::const_ptr id) {
            blobTxId = id;
        }

        BlobTxId::const_ptr getBlobTxId() const {
            return blobTxId;
        }

        // Why is this not a ostream operator?
        virtual std::string log_string() const {
            std::stringstream ret;
            ret << "dmCatReq for vol " << std::hex << volId
                << std::dec << " io_type " << io_type;
            return ret.str();
        }
    };

    /**
     * Handler that process dmCatReq
     */
    class DmIoReqHandler {
  public:
        virtual Error enqueueMsg(fds_volid_t volId, dmCatReq* ioReq) = 0;
    };

    /**
     * Request to make a snapshot of a volume db
     */
    class DmIoSnapVolCat: public dmCatReq {
  public:
        // TODO(xxx) what other params do we need?
      typedef std::function<void (fds_volid_t volid,
                                  const Error& error)> CbType;

  public:
        DmIoSnapVolCat() {
        }

        // Why is this not a ostream operator?
        virtual std::string log_string() const override {
            std::stringstream ret;
            ret << "dmIoSnapVolCat for vol "
                << std::hex << volId << std::dec << " first rsync? "
                << (io_type == FDS_DM_SNAP_VOLCAT);
            return ret.str();
        }

        // volume is part of base class: use getVolId()
        /* response callback */
        CbType dmio_snap_vcat_cb;
        NodeUuid node_uuid;
    };

/**
 * Request that marks close DMT for a volume
 * Will send PushMeta done msg to destination DM
 */
class DmIoPushMetaDone: public dmCatReq {
  public:
    explicit DmIoPushMetaDone(fds_volid_t _volId)
            : dmCatReq(_volId, "", "", blob_version_invalid,
                       FDS_DM_PUSH_META_DONE) {
    }

    // Why is this not a ostream operator?
    virtual std::string log_string() const override {
        std::stringstream ret;
        ret << "dmIoPushMetaDone for vol "
            << std::hex << volId << std::dec;
        return ret.str();
    }

    // volume is part of base class: use getVolId()
};

/**
 * Request that marks push meta done on receiving side
 */
class DmIoMetaRecvd: public dmCatReq {
  public:
    explicit DmIoMetaRecvd(fds_volid_t _volId)
            : dmCatReq(_volId, "", "", blob_version_invalid,
                       FDS_DM_META_RECVD) {
    }
    friend std::ostream& operator<<(std::ostream& out, const DmIoMetaRecvd& io) {
        return out << "DmIoMetaRecvd vol " << std::hex << io.volId << std::dec;
    }
    // volume is part of base class: use getVolId()
};



/**
 * Request to Commit Blob Tx
 */
class DmIoCommitBlobTx: public dmCatReq {
  public:
    typedef std::function<void (const Error &e, DmIoCommitBlobTx *blobTx)> CbType;
    fpi::CommitBlobTxRspMsg rspMsg;

  public:
    DmIoCommitBlobTx(const fds_volid_t  &_volId,
                     const std::string &_blobName,
                     const blob_version_t &_blob_version,
                     fds_uint64_t _dmt_version,
                     const sequence_id_t _seq_id)
            : dmCatReq(_volId, _blobName, "", _blob_version, FDS_COMMIT_BLOB_TX) {
        dmt_version = _dmt_version;
        sequence_id = _seq_id;

        // perf-trace related data
        opReqFailedPerfEventType = PerfEventType::DM_TX_COMMIT_REQ_ERR;

        opReqLatencyCtx.type = PerfEventType::DM_TX_COMMIT_REQ;
        opReqLatencyCtx.reset_volid(_volId);

        opLatencyCtx.type = PerfEventType::DM_VOL_CAT_WRITE;
        opLatencyCtx.reset_volid(_volId);

        opQoSWaitCtx.type = PerfEventType::DM_TX_QOS_WAIT;
        opQoSWaitCtx.reset_volid(_volId);
    }
    virtual ~DmIoCommitBlobTx() {
    }

    virtual std::string log_string() const override {
        std::stringstream ret;
        ret << "DmIoCommitBlobTx vol "
            << std::hex << volId << std::dec;
        return ret.str();
    }

    friend std::ostream& operator<<(std::ostream& out, const DmIoCommitBlobTx& io) {
        return out << "DmIoCommitBlobTx vol " << std::hex << io.volId << std::dec
                   << " blob " << io.blob_name
                   << ", dmt_version " << io.dmt_version << " TX " << *(io.ioBlobTxDesc);
    }

    BlobTxId::const_ptr ioBlobTxDesc;
    fds_uint64_t dmt_version;
    sequence_id_t sequence_id;
    /* response callback */
    CbType dmio_commit_blob_tx_resp_cb;
};

// Forward declaration. Defined further down.
class DmIoUpdateCatOnce;
class DmIoCommitBlobOnce : public  DmIoCommitBlobTx {
  public:
   using DmIoCommitBlobTx::DmIoCommitBlobTx;
   DmIoUpdateCatOnce *parent;
};

/**
 * Request to Abort Blob Tx
 */
class DmIoAbortBlobTx: public dmCatReq {
  public:
    typedef std::function<void (const Error &e, DmIoAbortBlobTx *blobTx)> CbType;

  public:
    DmIoAbortBlobTx(const fds_volid_t  &_volId,
                    const std::string &_blobName,
                    const blob_version_t &_blob_version)
            : dmCatReq(_volId, _blobName, "", _blob_version, FDS_ABORT_BLOB_TX) {
        // perf-trace related data
        opReqFailedPerfEventType = PerfEventType::DM_TX_OP_REQ_ERR;

        opReqLatencyCtx.type = PerfEventType::DM_TX_OP_REQ;
        opReqLatencyCtx.reset_volid(_volId);

        opLatencyCtx.type = PerfEventType::DM_TX_OP;
        opLatencyCtx.reset_volid(_volId);

        opQoSWaitCtx.type = PerfEventType::DM_TX_QOS_WAIT;
        opQoSWaitCtx.reset_volid(_volId);
    }

    virtual std::string log_string() const override {
        std::stringstream ret;
        ret << "DmIoAbortBlobTx vol "
            << std::hex << volId << std::dec;
        return ret.str();
    }

    BlobTxId::const_ptr ioBlobTxDesc;
    /* response callback */
    CbType dmio_abort_blob_tx_resp_cb;
};

/**
 * Request to Start Blob Tx
 */
class DmIoStartBlobTx: public dmCatReq {
  public:
    typedef std::function<void (const Error &e, DmIoStartBlobTx *blobTx)> CbType;

    DmIoStartBlobTx(const fds_volid_t  &_volId,
                    const std::string &_blobName,
                    const blob_version_t &_blob_version,
                    const fds_int32_t _blob_mode,
                    const fds_uint64_t _dmt_ver)
            : dmCatReq(_volId, _blobName,
                        "", _blob_version, FDS_START_BLOB_TX), blob_mode(_blob_mode),
            dmt_version(_dmt_ver) {
        // perf-trace related data
        opReqFailedPerfEventType = PerfEventType::DM_TX_OP_REQ_ERR;

        opReqLatencyCtx.type = PerfEventType::DM_TX_OP_REQ;
        opReqLatencyCtx.reset_volid(_volId);

        opLatencyCtx.type = PerfEventType::DM_TX_OP;
        opLatencyCtx.reset_volid(_volId);

        opQoSWaitCtx.type = PerfEventType::DM_TX_QOS_WAIT;
        opQoSWaitCtx.reset_volid(_volId);
    }

    virtual std::string log_string() const override {
        std::stringstream ret;
        ret << "DmIoStartBlobTx vol " << std::hex << volId << std::dec
            << ", dmt_version " << dmt_version << " TX " << *ioBlobTxDesc;
        return ret.str();
    }

    friend std::ostream& operator<<(std::ostream& out, const DmIoStartBlobTx& io) {
        return out << "DmIoStartBlobTx vol " << std::hex << io.volId << std::dec
                   << " blob " << io.blob_name << " blob mode " << io.blob_mode
                   << ", dmt_version " << io.dmt_version << " TX " << *(io.ioBlobTxDesc);
    }

    BlobTxId::const_ptr ioBlobTxDesc;
    fds_uint64_t dmt_version;
    fds_int32_t blob_mode;
    /* response callback */
    CbType dmio_start_blob_tx_resp_cb;
};

/**
 * Request to query catalog
 */
class DmIoQueryCat: public dmCatReq {
 public:
    typedef std::function<void (const Error &e, DmIoQueryCat *req)> CbType;
    boost::shared_ptr<fpi::QueryCatalogMsg> queryMsg;

  public:
    explicit DmIoQueryCat(boost::shared_ptr<fpi::QueryCatalogMsg>& qMsg)
            : dmCatReq(fds_volid_t(qMsg->volume_id),
                       qMsg->blob_name,
                       "",
                       qMsg->blob_version,
                       FDS_CAT_QRY),
              queryMsg(qMsg) {
        // perf-trace related data
        opReqFailedPerfEventType = PerfEventType::DM_QUERY_REQ_ERR;

        opReqLatencyCtx.type = PerfEventType::DM_QUERY_REQ;
        opReqLatencyCtx.reset_volid(io_vol_id);

        opLatencyCtx.type = PerfEventType::DM_VOL_CAT_READ;
        opLatencyCtx.reset_volid(io_vol_id);

        opQoSWaitCtx.type = PerfEventType::DM_QUERY_QOS_WAIT;
        opQoSWaitCtx.reset_volid(io_vol_id);
    }

    virtual std::string log_string() const override {
        std::stringstream ret;
        ret << "DmIoQueryCat "
            << std::hex << volId << std::dec;
        return ret.str();
    }

    /* response callback */
    CbType dmio_querycat_resp_cb;
};


class DmIoMigDltBlob : public dmCatReq {
  public:
    typedef std::function<void (const Error &e, DmIoMigDltBlob *req)> CbType;
    explicit DmIoMigDltBlob(boost::shared_ptr<fpi::CtrlNotifyDeltaBlobsMsg>& fwdMsg)
            : dmCatReq(fds_volid_t(fwdMsg->volume_id), "", "", 0,
                       FDS_DM_MIG_DELT_BLB), fwdCatMsg(fwdMsg) {}

    virtual std::string log_string() const override {
        std::stringstream ret;
        ret << "DmIoMigDltBlob vol " << std::hex << volId << std::dec;
        return ret.str();
    }

    friend std::ostream& operator<<(std::ostream& out, const DmIoMigDltBlob& io) {
        return out << "DmIoMigDltBlob vol " << std::hex << io.volId << std::dec;
    }

    boost::shared_ptr<fpi::CtrlNotifyDeltaBlobsMsg> fwdCatMsg;
    CbType dmio_fwdcat_resp_cb;
};

/**
 * New request to update catalog
 */
class DmIoFwdCat : public dmCatReq {
  public:
    typedef std::function<void (const Error &e, DmIoFwdCat *req)> CbType;
    explicit DmIoFwdCat(boost::shared_ptr<fpi::ForwardCatalogMsg>& fwdMsg)
            : dmCatReq(fds_volid_t(fwdMsg->volume_id), fwdMsg->blob_name, "", fwdMsg->blob_version,
                       FDS_DM_FWD_CAT_UPD), fwdCatMsg(fwdMsg) {}

    virtual std::string log_string() const override {
        std::stringstream ret;
        ret << "DmIoFwdCat vol " << std::hex << volId << std::dec;
        return ret.str();
    }

    friend std::ostream& operator<<(std::ostream& out, const DmIoFwdCat& io) {
        return out << "DmIoFwdCat vol " << std::hex << io.volId << std::dec
                   << " blob " << io.blob_name << " blob version " << io.blob_version;
    }

    boost::shared_ptr<fpi::ForwardCatalogMsg> fwdCatMsg;
    CbType dmio_fwdcat_resp_cb;
};


/**
 * Request to update catalog
 */
class DmIoUpdateCat: public dmCatReq {
  public:
    typedef std::function<void (const Error &e, DmIoUpdateCat *req)> CbType;

  public:
    explicit DmIoUpdateCat(boost::shared_ptr<fpi::UpdateCatalogMsg>& _updcatMsg)
            : dmCatReq(fds_volid_t(_updcatMsg->volume_id), _updcatMsg->blob_name, "", _updcatMsg->blob_version,
            FDS_CAT_UPD), ioBlobTxDesc(new BlobTxId(_updcatMsg->txId)),
            obj_list(_updcatMsg->obj_list), updcatMsg(_updcatMsg) {
        // perf-trace related data
        opReqFailedPerfEventType = PerfEventType::DM_TX_OP_REQ_ERR;

        opReqLatencyCtx.type = PerfEventType::DM_TX_OP_REQ;
        opReqLatencyCtx.reset_volid(io_vol_id);

        opLatencyCtx.type = PerfEventType::DM_TX_OP;
        opLatencyCtx.reset_volid(io_vol_id);

        opQoSWaitCtx.type = PerfEventType::DM_TX_QOS_WAIT;
        opQoSWaitCtx.reset_volid(io_vol_id);
    }

    virtual std::string log_string() const override {
        std::stringstream ret;
        ret << "DmIoUpdateCat vol "
            << std::hex << volId << std::dec;
        return ret.str();
    }

    BlobTxId::const_ptr ioBlobTxDesc;
    const fpi::FDSP_BlobObjectList & obj_list;
    boost::shared_ptr<fpi::UpdateCatalogMsg> updcatMsg;

    /* response callback */
    CbType dmio_updatecat_resp_cb;
};

/**
 * Request to update catalog in a single request.
 */
class DmIoUpdateCatOnce : public dmCatReq {
  public:
    explicit DmIoUpdateCatOnce(
        boost::shared_ptr<fpi::UpdateCatalogOnceMsg>& _updcatMsg,
        DmIoCommitBlobTx *_commitReq)
            : dmCatReq(fds_volid_t(_updcatMsg->volume_id),
                       _updcatMsg->blob_name,
                       "",
                       _updcatMsg->blob_version,
                       FDS_CAT_UPD_ONCE),
              ioBlobTxDesc(new BlobTxId(_updcatMsg->txId)),
              commitBlobReq(_commitReq),
              updcatMsg(_updcatMsg) {
        // perf-trace related data
        opReqFailedPerfEventType = PerfEventType::DM_TX_COMMIT_REQ_ERR;

        opReqLatencyCtx.type = PerfEventType::DM_TX_COMMIT_REQ;
        opReqLatencyCtx.reset_volid(volId);

        opLatencyCtx.type = PerfEventType::DM_VOL_CAT_WRITE;
        opLatencyCtx.reset_volid(volId);

        opQoSWaitCtx.type = PerfEventType::DM_TX_QOS_WAIT;
        opQoSWaitCtx.reset_volid(volId);
    }

    std::string log_string() const override {
        std::stringstream ret;
        ret << "DmIoUpdateCat vol "
            << std::hex << volId << std::dec;
        return ret.str();
    }

    BlobTxId::const_ptr ioBlobTxDesc;
    DmIoCommitBlobTx *commitBlobReq;
    boost::shared_ptr<fpi::UpdateCatalogOnceMsg> updcatMsg;

    // response callback
    typedef std::function<void (const Error &e, DmIoUpdateCatOnce *req)> CbType;
    CbType dmio_updatecat_resp_cb;
};

/**
 * Request to set blob metadata
 */
class DmIoSetBlobMetaData: public dmCatReq {
  public:
    typedef std::function<void (const Error &e, DmIoSetBlobMetaData *req)> CbType;

  public:
    explicit DmIoSetBlobMetaData(boost::shared_ptr<fpi::SetBlobMetaDataMsg> & _setMDMsg)
            : dmCatReq(fds_volid_t(_setMDMsg->volume_id), _setMDMsg->blob_name, "", _setMDMsg->blob_version,
            FDS_SET_BLOB_METADATA), ioBlobTxDesc(new BlobTxId(_setMDMsg->txId)),
            md_list(_setMDMsg->metaDataList), setMDMsg(_setMDMsg) {
        // perf-trace related data
        opReqFailedPerfEventType = PerfEventType::DM_TX_OP_REQ_ERR;

        opReqLatencyCtx.type = PerfEventType::DM_TX_OP_REQ;
        opReqLatencyCtx.reset_volid(io_vol_id);

        opLatencyCtx.type = PerfEventType::DM_TX_OP;
        opLatencyCtx.reset_volid(io_vol_id);

        opQoSWaitCtx.type = PerfEventType::DM_TX_QOS_WAIT;
        opQoSWaitCtx.reset_volid(io_vol_id);
    }

    virtual std::string log_string() const override {
        std::stringstream ret;
        ret << "DmIoSetBlobMetaData vol "
            << std::hex << volId << std::dec;
        return ret.str();
    }
    BlobTxId::const_ptr ioBlobTxDesc;
    const fpi::FDSP_MetaDataList & md_list;
    boost::shared_ptr<const fpi::SetBlobMetaDataMsg> setMDMsg;  // keep the reference
    /* response callback */
    CbType dmio_setmd_resp_cb;
};

/**
 * Request to delete catalog
 */
class DmIoDeleteCat: public dmCatReq {
  public:
    typedef std::function<void (const Error &e, DmIoDeleteCat *req)> CbType;
  public:
    DmIoDeleteCat(const fds_volid_t  &_volId,
                  const std::string &_blobName,
                  const blob_version_t &_blob_version)
            : dmCatReq(_volId, _blobName, "", _blob_version, FDS_DELETE_BLOB_SVC) {
    }

    virtual std::string log_string() const override {
        std::stringstream ret;
        ret << "DmIoDeleteCat vol "
            << std::hex << volId << std::dec;
        return ret.str();
    }

    /* response callback */
    CbType dmio_deletecat_resp_cb;
};


class DmIoGetBlobMetaData: public dmCatReq {
  public:
    typedef std::function<void (const Error &e, DmIoGetBlobMetaData *req)> CbType;

  public:
    DmIoGetBlobMetaData(const fds_volid_t  &_volId,
                        const std::string &_blobName,
                        const blob_version_t &_blob_version,
                        boost::shared_ptr<fpi::GetBlobMetaDataMsg> message)
            : message(message), dmCatReq(_volId, _blobName,
                     "", _blob_version, FDS_GET_BLOB_METADATA) {
        // perf-trace related data
        opReqFailedPerfEventType = PerfEventType::DM_QUERY_REQ_ERR;

        opReqLatencyCtx.type = PerfEventType::DM_QUERY_REQ;
        opReqLatencyCtx.reset_volid(_volId);

        opLatencyCtx.type = PerfEventType::DM_VOL_CAT_READ;
        opLatencyCtx.reset_volid(_volId);

        opQoSWaitCtx.type = PerfEventType::DM_QUERY_QOS_WAIT;
        opQoSWaitCtx.reset_volid(_volId);
    }

    ~DmIoGetBlobMetaData() {
     }

    virtual std::string log_string() const override {
        std::stringstream ret;
        ret << "DmIoGetBlobMetaData vol "
            << std::hex << volId << std::dec;
        return ret.str();
    }
    boost::shared_ptr<fpi::GetBlobMetaDataMsg> message;
    /* response callback */
    CbType dmio_getmd_resp_cb;
};

struct DmIoStatVolume : dmCatReq {
    typedef std::function<void (const Error &e, DmIoStatVolume *req)> CbType;

    explicit DmIoStatVolume(boost::shared_ptr<fpi::StatVolumeMsg> message)
            : dmCatReq(fds_volid_t(message->volume_id), "", "", 0, FDS_STAT_VOLUME), msg(message) {
        // perf-trace related data
        opReqFailedPerfEventType = PerfEventType::DM_QUERY_REQ_ERR;

        opReqLatencyCtx.type = PerfEventType::DM_QUERY_REQ;
        opReqLatencyCtx.reset_volid(io_vol_id);

        opLatencyCtx.type = PerfEventType::DM_VOL_CAT_READ;
        opLatencyCtx.reset_volid(io_vol_id);

        opQoSWaitCtx.type = PerfEventType::DM_QUERY_QOS_WAIT;
        opQoSWaitCtx.reset_volid(io_vol_id);
    }

    boost::shared_ptr<fpi::StatVolumeMsg> msg;
    // response callback
    CbType dmio_get_volmd_resp_cb;
};

struct DmIoSetVolumeMetaData : dmCatReq {
    typedef std::function<void (const Error &e, DmIoSetVolumeMetaData *req)> CbType;

    explicit DmIoSetVolumeMetaData(boost::shared_ptr<fpi::SetVolumeMetadataMsg> message)
            : dmCatReq(fds_volid_t(message->volumeId), "", "", 0, FDS_SET_VOLUME_METADATA), msg(message) {
    }

    boost::shared_ptr<fpi::SetVolumeMetadataMsg> msg;
    // response callback
    CbType dmio_set_volmd_resp_cb;
};

struct DmIoGetVolumeMetadata : dmCatReq {
    typedef std::function<void (const Error &e, DmIoGetVolumeMetadata *req)> CbType;

    explicit DmIoGetVolumeMetadata(fds_volid_t volId, boost::shared_ptr<fpi::GetVolumeMetadataMsgRsp> message)
            : dmCatReq(volId, "", "", 0, FDS_GET_VOLUME_METADATA), msg(message) {
    }

    boost::shared_ptr<fpi::GetVolumeMetadataMsgRsp> msg;
    // response callback
    CbType dmio_get_volmd_resp_cb;
};

struct DmIoVolumeOpen : dmCatReq {
    typedef std::function<void (const Error &e, DmIoVolumeOpen *req)> CbType;

    explicit DmIoVolumeOpen(boost::shared_ptr<fpi::OpenVolumeMsg> message)
            : dmCatReq(fds_volid_t(message->volume_id), "", "", 0, FDS_OPEN_VOLUME),
              token(message->token),
              access_mode(message->mode) {
    }

    fds_int64_t token;
    fpi::VolumeAccessMode access_mode;
    sequence_id_t sequence_id;

    // response callback
    CbType dmio_get_volmd_resp_cb;
};

struct DmIoVolumeClose : dmCatReq {
    typedef std::function<void (const Error &e, DmIoVolumeClose *req)> CbType;

    explicit DmIoVolumeClose(fds_volid_t volId, fds_int64_t _token)
            : dmCatReq(volId, "", "", 0, FDS_CLOSE_VOLUME), token(_token) {
    }

    fds_int64_t token;
    // response callback
    CbType dmio_get_volmd_resp_cb;
};

/**
 * stat from a single module for volume(s)
 */
class DmIoStatStream : public dmCatReq {
  public:
    typedef std::function<void (const Error &e, DmIoStatStream *req)> CbType;
    DmIoStatStream(fds_volid_t volid, boost::shared_ptr<fpi::StatStreamMsg>& statMsg)
            : dmCatReq(volid, "", "", 1,
                       FDS_DM_STAT_STREAM), statStreamMsg(statMsg) {}

    friend std::ostream& operator<<(std::ostream& out, const DmIoStatStream& io) {
        return out << "DmIoStatStream timestamp " << (io.statStreamMsg)->start_timestamp;
    }

    boost::shared_ptr<fpi::StatStreamMsg> statStreamMsg;
    CbType dmio_statstream_resp_cb;
};


struct DmIoGetSysStats : dmCatReq {
    boost::shared_ptr<fpi::GetDmStatsMsg> message;
    explicit DmIoGetSysStats(boost::shared_ptr<fpi::GetDmStatsMsg> message)
            : message(message) , dmCatReq(fds_volid_t(message->volume_id), "", "", 0, FDS_DM_SYS_STATS) {}
};

struct DmIoGetBucket : dmCatReq {
    boost::shared_ptr<fpi::GetBucketMsg> message;
    boost::shared_ptr<fpi::GetBucketRspMsg> response;
    explicit DmIoGetBucket(boost::shared_ptr<fpi::GetBucketMsg> message)
            : message(message),
              response(new fpi::GetBucketRspMsg()),
              dmCatReq(fds_volid_t(message->volume_id), "", "", 0, FDS_LIST_BLOB) {}
};

struct DmIoDeleteBlob: dmCatReq {
    boost::shared_ptr<fpi::DeleteBlobMsg> message;
    explicit DmIoDeleteBlob(boost::shared_ptr<fpi::DeleteBlobMsg> message)
            : message(message) , dmCatReq(fds_volid_t(message->volume_id),
                                          message->blob_name,
                                          "",
                                          message->blob_version,
                                          FDS_DELETE_BLOB) {
    }
};

struct DmIoReloadVolume : dmCatReq {
    boost::shared_ptr<fpi::ReloadVolumeMsg> message;
    boost::shared_ptr<fpi::ReloadVolumeRspMsg> response;
    explicit DmIoReloadVolume(boost::shared_ptr<fpi::ReloadVolumeMsg> msg)
            : message(msg),
              response(new fpi::ReloadVolumeRspMsg()),
              dmCatReq(fds_volid_t(msg->volume_id), "", "", 0, FDS_DM_RELOAD_VOLUME) {
    }

    friend std::ostream& operator<<(std::ostream& out, const DmIoReloadVolume& io) {
        return out << "DmIoReloadVolume vol " << std::hex << io.volId << std::dec;
    }

};

struct DmIoMigration : dmCatReq {
    boost::shared_ptr<fpi::CtrlNotifyDMStartMigrationMsg> message;
    boost::shared_ptr<fpi::CtrlNotifyDMStartMigrationRspMsg> response;
    std::function<void(fpi::AsyncHdrPtr&, fpi::CtrlNotifyDMStartMigrationMsgPtr&,
    		const Error &e, dmCatReq *dmRequest)> localCb = NULL;
    boost::shared_ptr<fpi::AsyncHdr> asyncHdrPtr = NULL;
    explicit DmIoMigration(fds_volid_t volid, boost::shared_ptr<fpi::CtrlNotifyDMStartMigrationMsg> msg)
            : message(msg),
              response(new fpi::CtrlNotifyDMStartMigrationRspMsg()),
              dmCatReq(fds_volid_t(volid), "", "", 0, FDS_DM_MIGRATION) {
    }

    friend std::ostream& operator<<(std::ostream& out, const DmIoMigration& io) {
        return out << "DmIoMigration vol " << io.volId.get();
    }

};

struct DmIoResyncInitialBlob : dmCatReq {
	boost::shared_ptr<fpi::CtrlNotifyInitialBlobFilterSetMsg> message;
	boost::shared_ptr<fpi::ResyncInitialBlobFilterSetRspMsg> response;
	NodeUuid destNodeUuid;
    std::function<void(fpi::AsyncHdrPtr&, fpi::CtrlNotifyInitialBlobFilterSetMsgPtr&,
    		const Error &e, dmCatReq *dmRequest)> localCb = NULL;
    explicit DmIoResyncInitialBlob(fds_volid_t volid, boost::shared_ptr<fpi::CtrlNotifyInitialBlobFilterSetMsg> msg,
    		NodeUuid &_destNodeUuid)
            : message(msg),
              response(new fpi::ResyncInitialBlobFilterSetRspMsg()),
              dmCatReq(fds_volid_t(volid), "", "", 0, FDS_DM_RESYNC_INIT_BLOB),
			  destNodeUuid(_destNodeUuid) {
    }

    friend std::ostream& operator<<(std::ostream& out, const DmIoResyncInitialBlob& io) {
    	return out << "DmIoResyncInitialBlob vol " << io.volId.get();
    }
};

}  // namespace fds

#endif  // SOURCE_DATA_MGR_INCLUDE_DMIOREQ_H_
