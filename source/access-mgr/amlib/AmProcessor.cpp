/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

#include <AmProcessor.h>

#include <algorithm>
#include <deque>
#include <string>

#include "fds_process.h"
#include "fds_timer.h"
#include <ObjectId.h>
#include "FdsRandom.h"
#include <fiu-control.h>
#include <util/fiu_util.h>
#include "AmDispatcher.h"
#include "AmTxManager.h"
#include "AmVolume.h"
#include "AmVolumeTable.h"
#include "AmVolumeAccessToken.h"

#include "requests/requests.h"
#include "AsyncResponseHandlers.h"

namespace fds {

#define AMPROCESSOR_CB_HANDLER(func, ...) \
    std::bind(&func, this, ##__VA_ARGS__ , std::placeholders::_1)

/*
 * Atomic for request ids.
 */
std::atomic_uint nextIoReqId;

/**
 * AM request processing layer. The processor handles state and
 * execution for AM requests.
 */
class AmProcessor_impl
{
    using shutdown_cb_type = std::function<void(void)>;

  public:
    explicit AmProcessor_impl(CommonModuleProviderIf *modProvider) : amDispatcher(new AmDispatcher(modProvider)),
                         txMgr(new AmTxManager()),
                         volTable(nullptr),
                         prepareForShutdownCb(nullptr)
    { }

    AmProcessor_impl(AmProcessor_impl const&) = delete;
    AmProcessor_impl& operator=(AmProcessor_impl const&) = delete;
    ~AmProcessor_impl() = default;

    void start(shutdown_cb_type&& cb);

    void prepareForShutdownMsgRespBindCb(shutdown_cb_type&& cb)
        { prepareForShutdownCb = cb; }

    void prepareForShutdownMsgRespCallCb();

    bool stop();

    Error enqueueRequest(AmRequest* amReq);

    Error modifyVolumePolicy(fds_volid_t vol_uuid, const VolumeDesc& vdesc) {
        return volTable->modifyVolumePolicy(vol_uuid, vdesc);
    }

    void registerVolume(const VolumeDesc& volDesc, fds_int64_t const token = invalid_vol_token);
    void registerVolumeCb(const VolumeDesc& volDesc,
                          fds_int64_t const token,
                          Error const error);

    void renewToken(const fds_volid_t vol_id);
    void renewTokenCb(fds_volid_t const vol_id,
                      fds_int64_t const new_token,
                      Error const error);

    Error removeVolume(const VolumeDesc& volDesc);

    Error updateQoS(long int const* rate, float const* throttle)
        { return volTable->updateQoS(rate, throttle); }

    Error updateDlt(bool dlt_type, std::string& dlt_data, std::function<void (const Error&)> cb)
        { return amDispatcher->updateDlt(dlt_type, dlt_data, cb); }

    Error updateDmt(bool dmt_type, std::string& dmt_data)
        { return amDispatcher->updateDmt(dmt_type, dmt_data); }

    Error getDMT()
    { return amDispatcher->getDMT(); }

    Error getDLT()
    { return amDispatcher->getDLT(); }

    bool isShuttingDown() const
    { return shut_down; }

  private:
    /// Unique ptr to the dispatcher layer
    std::unique_ptr<AmDispatcher> amDispatcher;

    /// Unique ptr to the transaction manager
    std::unique_ptr<AmTxManager> txMgr;

    /// Unique ptr to the volume table
    std::unique_ptr<AmVolumeTable> volTable;

    /// Unique ptr to a random num generator for tx IDs
    std::unique_ptr<RandNumGenerator> randNumGen;

    FdsTimer token_timer;

    shutdown_cb_type shutdown_cb;
    bool shut_down { false };

    shutdown_cb_type prepareForShutdownCb;

    void processBlobReq(AmRequest *amReq);

    std::shared_ptr<AmVolume> getVolume(AmRequest* amReq, bool const allow_snapshot=true);
    inline bool haveCacheToken(std::shared_ptr<AmVolume> const& volume) const;

    /**
     * FEATURE TOGGLE: Single AM Enforcement
     * Wed 01 Apr 2015 01:52:55 PM PDT
     */
    bool volume_open_support { false };
    std::chrono::duration<fds_uint32_t> vol_tok_renewal_freq {30};

    /**
     * Processes a get volume metadata request
     */
    void getVolumeMetadata(AmRequest *amReq);

    /**
     * Attachment request, retrieve volume descriptor
     */
    void attachVolume(AmRequest *amReq);
    void detachVolume(AmRequest *amReq);

    /**
     * Processes a abort blob transaction
     */
    void abortBlobTx(AmRequest *amReq);
    void abortBlobTxCb(AmRequest *amReq, const Error &error);

    /**
     * Processes a commit blob transaction
     */
    void commitBlobTx(AmRequest *amReq);
    void commitBlobTxCb(AmRequest *amReq, const Error& error);

    /**
     * Processes a delete blob request
     */
    void deleteBlob(AmRequest *amReq);

    /**
     * Processes a get blob request
     */
    void getBlob(AmRequest *amReq);
    void getBlobCb(AmRequest *amReq, const Error& error);

    /**
     * Look for object data in cache
     * or fallback to SM
     */
    void getObject(AmRequest *amReq);

    /**
     * Processes a put blob request
     */
    void putBlob(AmRequest *amReq);
    void putBlobCb(AmRequest *amReq, const Error& error);

    /**
     * Processes a set volume metadata request
     */
    void setVolumeMetadata(AmRequest *amReq);

    /**
     * Processes a start blob transaction
     */
    void startBlobTx(AmRequest *amReq);
    void startBlobTxCb(AmRequest *amReq, const Error &error);

    /**
     * Processes a stat volume request
     */
    void statVolume(AmRequest *amReq);
    void statVolumeCb(AmRequest *amReq, const Error &error);

    /**
     * Callback for catalog query request
     */
    void queryCatalogCb(AmRequest *amReq, const Error& error);

    /**
     * Processes a set metadata on blob request
     */
    void setBlobMetadata(AmRequest *amReq);

    /**
     * Processes a stat blob request
     */
    void statBlob(AmRequest *amReq);
    void statBlobCb(AmRequest *amReq, const Error& error);

    /**
     * Processes a volumeContents (aka ListBucket) request
     */
    void volumeContents(AmRequest *amReq);

    /**
     * Generic callback for a few responses
     */
    void respond_and_delete(AmRequest *amReq, const Error& error)
        { respond(amReq, error); delete amReq; }

    void respond(AmRequest *amReq, const Error& error);

};

Error
AmProcessor_impl::enqueueRequest(AmRequest* amReq) {
    Error err;
    if (shut_down) {
        err = ERR_SHUTTING_DOWN;
    } else {
        fds_verify(amReq->magicInUse() == true);

        amReq->io_req_id = atomic_fetch_add(&nextIoReqId, (fds_uint32_t)1);
        err = volTable->enqueueRequest(amReq);

        /** Queue and dispatch an attachment if we didn't find a volume */
        if (ERR_VOL_NOT_FOUND == err) {
            err = amDispatcher->attachVolume(amReq->volume_name);
        }
    }
    if (!err.ok())
        { amReq->cb->call(err); }
    return err;
}

void
AmProcessor_impl::processBlobReq(AmRequest *amReq) {
    fds::PerfTracer::tracePointEnd(amReq->qos_perf_ctx);

    fds_verify(amReq->io_module == FDS_IOType::ACCESS_MGR_IO);
    fds_verify(amReq->magicInUse() == true);

    /*
     * Drain the queue if we are shutting down.
     */
    if (shut_down) {
        respond_and_delete(amReq, ERR_SHUTTING_DOWN);
        return;
    }

    switch (amReq->io_type) {
        case fds::FDS_START_BLOB_TX:
            startBlobTx(amReq);
            break;
        case fds::FDS_COMMIT_BLOB_TX:
            commitBlobTx(amReq);
            break;
        case fds::FDS_ABORT_BLOB_TX:
            abortBlobTx(amReq);
            break;

        case fds::FDS_DETACH_VOL:
            detachVolume(amReq);
            break;

        case fds::FDS_ATTACH_VOL:
            attachVolume(amReq);
            break;

        case fds::FDS_IO_READ:
        case fds::FDS_GET_BLOB:
            getBlob(amReq);
            break;

        case fds::FDS_IO_WRITE:
        case fds::FDS_PUT_BLOB_ONCE:
        case fds::FDS_PUT_BLOB:
            putBlob(amReq);
            break;

        case fds::FDS_SET_BLOB_METADATA:
            setBlobMetadata(amReq);
            break;

        case fds::FDS_STAT_VOLUME:
            statVolume(amReq);
            break;

        case fds::FDS_SET_VOLUME_METADATA:
            setVolumeMetadata(amReq);
            break;

        case fds::FDS_GET_VOLUME_METADATA:
            getVolumeMetadata(amReq);
            break;

        case fds::FDS_DELETE_BLOB:
            deleteBlob(amReq);
            break;

        case fds::FDS_STAT_BLOB:
            statBlob(amReq);
            break;

        case fds::FDS_VOLUME_CONTENTS:
            volumeContents(amReq);
            break;

        default :
            LOGCRITICAL << "unimplemented request: " << amReq->io_type;
            respond_and_delete(amReq, ERR_NOT_IMPLEMENTED);
            break;
    }
}

void
AmProcessor_impl::start(shutdown_cb_type&& cb)
{
    FdsConfigAccessor conf(g_fdsprocess->get_fds_config(), "fds.am.");
    if (conf.get<fds_bool_t>("testing.uturn_processor_all")) {
        fiu_enable("am.uturn.processor.*", 1, NULL, 0);
    }
    auto qos_threads = conf.get<int>("qos_threads");

    /**
     * FEATURE TOGGLE: Single AM Enforcement
     * Wed 01 Apr 2015 01:52:55 PM PDT
     */
    FdsConfigAccessor features(g_fdsprocess->get_fds_config(), "fds.feature_toggle.");
    volume_open_support = features.get<bool>("common.volume_open_support", false);
    if (volume_open_support) {
        vol_tok_renewal_freq =
            std::chrono::duration<fds_uint32_t>(conf.get<fds_uint32_t>("token_renewal_freq"));
    }

    shutdown_cb = std::move(cb);
    randNumGen = RandNumGenerator::unique_ptr(
        new RandNumGenerator(RandNumGenerator::getRandSeed()));
    amDispatcher->start();
    txMgr->init();

    auto closure = [this](AmRequest* amReq) mutable -> void { this->processBlobReq(amReq); };
    volTable.reset(new AmVolumeTable(qos_threads, GetLog()));
    volTable->registerCallback(std::move(closure));
}

void
AmProcessor_impl::respond(AmRequest *amReq, const Error& error) {
    volTable->markIODone(amReq);
    amReq->cb->call(error);

    // If we're shutting down check if the
    // queue is empty and make the callback
    if (isShuttingDown()) {
        stop();
    }
}

void AmProcessor_impl::prepareForShutdownMsgRespCallCb() {
    if (prepareForShutdownCb) {
        prepareForShutdownCb();
        prepareForShutdownCb = nullptr;
    }
}

bool AmProcessor_impl::stop() {
    shut_down = true;
    if (volTable->drained()) {
        // Close all attached volumes before finishing shutdown
        std::deque<std::pair<fds_volid_t, fds_int64_t>> tokens;
        volTable->getVolumeTokens(tokens);
        for (auto const& token_pair: tokens) {
            if (invalid_vol_token != token_pair.second) {
                amDispatcher->dispatchCloseVolume(token_pair.first, token_pair.second);
            }
        }
        shutdown_cb();
        return true;
    }
    return false;
}

std::shared_ptr<AmVolume>
AmProcessor_impl::getVolume(AmRequest* amReq, bool const allow_snapshot) {
    // check if this is a snapshot
    auto shVol = volTable->getVolume(amReq->io_vol_id);
    if (!shVol) {
        LOGCRITICAL << "unable to get volume info for vol: " << amReq->io_vol_id;
        respond_and_delete(amReq, FDSN_StatusErrorUnknown);
    } else if (!allow_snapshot && shVol->voldesc->isSnapshot()) {
        LOGWARN << "txn on a snapshot is not allowed.";
        respond_and_delete(amReq, FDSN_StatusErrorAccessDenied);
        shVol = nullptr;
    }
    return shVol;
}

bool
AmProcessor_impl::haveCacheToken(std::shared_ptr<AmVolume> const& volume) const {
    if (volume) {
        /**
         * FEATURE TOGGLE: Single AM Enforcement
         * Wed 01 Apr 2015 01:52:55 PM PDT
         */
        if (!volume_open_support || (invalid_vol_token != volume->getToken())) {
            return true;
        }
    }
    return false;
}


void
AmProcessor_impl::registerVolume(const VolumeDesc& volDesc, fds_int64_t const token) {
    /** First we need to open the volume for access */
    auto cb = [this, volDesc](fds_int64_t const token, Error const e) mutable -> void {
        this->registerVolumeCb(volDesc, token, e);
    };

    /**
     * FEATURE TOGGLE: Single AM Enforcement
     * Wed 01 Apr 2015 01:52:55 PM PDT
     */
    if (volume_open_support) {
        amDispatcher->dispatchOpenVolume(volDesc.volUUID, token, cb);
    } else {
        // Create a fake token that doesn't expire.
        auto access_token = boost::make_shared<AmVolumeAccessToken>(
                token_timer,
                invalid_vol_token,
                nullptr);
        volTable->registerVolume(volDesc, access_token);
        txMgr->registerVolume(volDesc);
    }
}

void
AmProcessor_impl::registerVolumeCb(const VolumeDesc& volDesc,
                                   fds_int64_t const token,
                                   Error const error) {
    Error err {error};
    if (ERR_OK == err) {
        GLOGDEBUG << "For volume: " << volDesc.volUUID
                  << ", received access token: 0x" << std::hex << token;

    }

    // Build an access token that will renew itself at regular
    // intervals
    auto access_token = boost::make_shared<AmVolumeAccessToken>(
        token_timer,
        token,
        [this, vol_id = volDesc.volUUID] () mutable -> void {
        this->renewToken(vol_id);
        });

    err = volTable->registerVolume(volDesc, access_token);
    if ((ERR_OK == err) && (invalid_vol_token != token)) {
        // Yay, success! Create caches if we have a token
        txMgr->registerVolume(volDesc);
    } else {
        LOGNOTIFY << "Could not open volume, R/O without cache support mode.";
    }

    if (ERR_OK != err) {
        LOGNOTIFY << "Failed to register volume, error: " << error;
        // Flush the volume's wait queue and return errors for pending requests
        volTable->removeVolume(volDesc);
        return;
    }

    // Renew this token at a regular interval
    auto timer_task = boost::dynamic_pointer_cast<FdsTimerTask>(access_token);
    if (!token_timer.scheduleRepeated(timer_task, vol_tok_renewal_freq))
        { LOGWARN << "Failed to schedule token renewal timer!"; }

}

void
AmProcessor_impl::renewToken(const fds_volid_t vol_id) {
    // Get the current volume and token
    auto shVol = volTable->getVolume(vol_id);
    if (!shVol) {
        LOGDEBUG << "Ignoring token renewal for unknown (detached?) volume: " << vol_id;
        return;
    }

    // Dispatch for a renewal to DM, update the token on success. Remove the
    // volume otherwise.

    auto cb = [this, vol_id]
                (fds_int64_t const token, Error const e) mutable -> void {
                    this->renewTokenCb(vol_id, token, e);
                };
    amDispatcher->dispatchOpenVolume(vol_id, shVol->getToken(), cb);
}

void
AmProcessor_impl::renewTokenCb(fds_volid_t const vol_id,
                               fds_int64_t const new_token,
                               Error const error) {
    // Get the current volume
    auto shVol = volTable->getVolume(vol_id);

    // Volume could have detached since we dispatched renewal.
    if (!shVol) {
        LOGDEBUG << "Received renewal for detached volume: " << vol_id;
        return;
    }

    if ((ERR_OK != error) || (new_token != shVol->getToken())) {
        // Remove the volume from the caches (if there is one)
        txMgr->invalidateMetaCache(*shVol->voldesc);
        if (invalid_vol_token == new_token) {
            LOGERROR << "Failed to renew token, going R/O: " << error;
        }
        shVol->setToken(new_token);
        LOGDEBUG << "Received new token: " << new_token;
    } else {
        LOGDEBUG << "Received renwal of token: " << new_token;
    }
}

Error
AmProcessor_impl::removeVolume(const VolumeDesc& volDesc) {
    LOGNORMAL << "Removing volume: " << volDesc.name;
    Error err{ERR_OK};

    // If we had a token for a volume, give it back to DM
    auto shVol = volTable->getVolume(volDesc.volUUID);
    if (shVol) {
        // If we had a cache token for this volume, close it
        fds_int64_t token = shVol->getToken();
        if (invalid_vol_token != token) {
            if (token_timer.cancel(boost::dynamic_pointer_cast<FdsTimerTask>(shVol->access_token))) {
                LOGDEBUG << "Canceled timer for token: 0x" << std::hex << token;
            } else {
                LOGWARN << "Failed to cancel timer, volume with re-attach!";
            }
            amDispatcher->dispatchCloseVolume(volDesc.volUUID, token);
        }
    }

    // Remove the volume from the caches (if there is one)
    txMgr->removeVolume(volDesc);

    // Remove the volume from QoS/VolumeTable, this is
    // called to clear any waiting requests with an error and
    // remove the QoS allocations
    err = volTable->removeVolume(volDesc);

    if (shut_down && volTable->drained())
    {
       shutdown_cb();
    }
    return err;
}

void
AmProcessor_impl::statVolume(AmRequest *amReq) {
    fiu_do_on("am.uturn.processor.statVol",
              respond_and_delete(amReq, ERR_OK); \
              return;);

    amReq->proc_cb = AMPROCESSOR_CB_HANDLER(AmProcessor_impl::statVolumeCb, amReq);
    amDispatcher->dispatchStatVolume(amReq);
}

void
AmProcessor_impl::statVolumeCb(AmRequest *amReq, const Error &error) {
    StatVolumeCallback::ptr cb =
            SHARED_DYN_CAST(StatVolumeCallback, amReq->cb);
    cb->volStat = static_cast<StatVolumeReq *>(amReq)->volumeStatus;
    respond_and_delete(amReq, error);
}

void
AmProcessor_impl::setVolumeMetadata(AmRequest *amReq) {
    fiu_do_on("am.uturn.processor.setVolMetadata",
              respond_and_delete(amReq, ERR_OK); \
              return;);

    auto shVol = getVolume(amReq, false);
    if (!haveCacheToken(shVol)) {
        respond_and_delete(amReq, ERR_VOLUME_ACCESS_DENIED);
        return;
    }
    amReq->proc_cb = AMPROCESSOR_CB_HANDLER(AmProcessor_impl::respond_and_delete, amReq);
    amDispatcher->dispatchSetVolumeMetadata(amReq);
}

void
AmProcessor_impl::getVolumeMetadata(AmRequest *amReq) {
    fiu_do_on("am.uturn.processor.getVolMetadata",
              respond_and_delete(amReq, ERR_OK); \
              return;);

    amReq->proc_cb = AMPROCESSOR_CB_HANDLER(AmProcessor_impl::respond_and_delete, amReq);
    amDispatcher->dispatchGetVolumeMetadata(amReq);
}

void
AmProcessor_impl::attachVolume(AmRequest *amReq) {
    // This really can not fail, we have to be attached to be here
    auto shVol = getVolume(amReq);
    if (!shVol) return;

    boost::shared_ptr<AttachCallback> cb = SHARED_DYN_CAST(AttachCallback, amReq->cb);
    cb->volDesc = boost::make_shared<VolumeDesc>(*shVol->voldesc);
    respond_and_delete(amReq, ERR_OK);
}

void
AmProcessor_impl::detachVolume(AmRequest *amReq) {
    // This really can not fail, we have to be attached to be here
    auto shVol = getVolume(amReq);
    if (!shVol) return;

    removeVolume(*shVol->voldesc);
    respond_and_delete(amReq, ERR_OK);
}

void
AmProcessor_impl::abortBlobTx(AmRequest *amReq) {
    amReq->proc_cb = AMPROCESSOR_CB_HANDLER(AmProcessor_impl::abortBlobTxCb, amReq);
    amDispatcher->dispatchAbortBlobTx(amReq);
}

void
AmProcessor_impl::abortBlobTxCb(AmRequest *amReq, const Error &error) {
    AbortBlobTxReq *blobReq = static_cast<AbortBlobTxReq *>(amReq);
    if (ERR_OK != txMgr->abortTx(*(blobReq->tx_desc)))
        LOGWARN << "Transaction unknown";

    respond_and_delete(amReq, error);
}

void
AmProcessor_impl::startBlobTx(AmRequest *amReq) {
    fiu_do_on("am.uturn.processor.startBlobTx",
              respond_and_delete(amReq, ERR_OK); \
              return;);

    auto shVol = getVolume(amReq, false);
    if (!haveCacheToken(shVol)) {
        respond_and_delete(amReq, ERR_VOLUME_ACCESS_DENIED);
        return;
    }

    // Generate a random transaction ID to use
    static_cast<StartBlobTxReq*>(amReq)->tx_desc =
        boost::make_shared<BlobTxId>(randNumGen->genNumSafe());
    amReq->proc_cb = AMPROCESSOR_CB_HANDLER(AmProcessor_impl::startBlobTxCb, amReq);

    amDispatcher->dispatchStartBlobTx(amReq);
}

void
AmProcessor_impl::startBlobTxCb(AmRequest *amReq, const Error &error) {
    StartBlobTxCallback::ptr cb = SHARED_DYN_CAST(StartBlobTxCallback,
                                                  amReq->cb);

    StartBlobTxReq *blobReq = static_cast<StartBlobTxReq *>(amReq);
    if (error.ok()) {
        // Update callback and record new open transaction
        cb->blobTxId  = *blobReq->tx_desc;
        fds_verify(txMgr->addTx(amReq->io_vol_id,
                                *blobReq->tx_desc,
                                blobReq->dmt_version,
                                amReq->getBlobName()) == ERR_OK);
    }

    respond_and_delete(amReq, error);
}

void
AmProcessor_impl::deleteBlob(AmRequest *amReq) {
    auto shVol = getVolume(amReq, false);
    if (!haveCacheToken(shVol)) {
        respond_and_delete(amReq, ERR_VOLUME_ACCESS_DENIED);
        return;
    }

    DeleteBlobReq* blobReq = static_cast<DeleteBlobReq *>(amReq);
    LOGDEBUG    << " volume:" << amReq->io_vol_id
                << " blob:" << amReq->getBlobName()
                << " txn:" << blobReq->tx_desc;

    // Update the tx manager with the delete op
    txMgr->updateTxOpType(*(blobReq->tx_desc), amReq->io_type);

    amReq->proc_cb = AMPROCESSOR_CB_HANDLER(AmProcessor_impl::respond_and_delete, amReq);
    amDispatcher->dispatchDeleteBlob(amReq);
}

void
AmProcessor_impl::putBlob(AmRequest *amReq) {
    fiu_do_on("am.uturn.processor.putBlob",
              respond_and_delete(amReq, ERR_OK); \
              return;);

    auto shVol = getVolume(amReq, false);

    if (!haveCacheToken(shVol)) {
        respond_and_delete(amReq, ERR_VOLUME_ACCESS_DENIED);
        return;
    }

    // Convert the offset to use a Byte term instead of Object
    fds_uint32_t maxObjSize = shVol->voldesc->maxObjSizeInBytes;
    amReq->blob_offset = (amReq->blob_offset * maxObjSize);

    // Use a stock object ID if the length is 0.
    PutBlobReq *blobReq = static_cast<PutBlobReq *>(amReq);
    if (amReq->data_len == 0) {
        LOGWARN << "zero size object - "
                << " [objkey:" << amReq->getBlobName() <<"]";
        amReq->obj_id = ObjectID();
    } else {
        SCOPED_PERF_TRACEPOINT_CTX(amReq->hash_perf_ctx);
        amReq->obj_id = ObjIdGen::genObjectId(blobReq->dataPtr->c_str(), amReq->data_len);
    }

    amReq->proc_cb = AMPROCESSOR_CB_HANDLER(AmProcessor_impl::putBlobCb, amReq);

    if (amReq->io_type == FDS_PUT_BLOB_ONCE) {
        // Sending the update in a single request. Create transaction ID to
        // use for the single request
        blobReq->setTxId(BlobTxId(randNumGen->genNumSafe()));
        amDispatcher->dispatchUpdateCatalogOnce(amReq);
    } else {
        amDispatcher->dispatchUpdateCatalog(amReq);
    }

    // Either dispatch the put blob request or, if there's no data, just call
    // our callback handler now (NO-OP).
    amReq->data_len > 0 ? amDispatcher->dispatchPutObject(amReq) :
                          blobReq->notifyResponse(ERR_OK);
}

void
AmProcessor_impl::putBlobCb(AmRequest *amReq, const Error& error) {
    PutBlobReq *blobReq = static_cast<PutBlobReq *>(amReq);

    if (error.ok()) {
        auto tx_desc = blobReq->tx_desc;
        // Add the Tx to the manager if this an updateOnce
        if (amReq->io_type == FDS_PUT_BLOB_ONCE) {
            fds_verify(txMgr->addTx(amReq->io_vol_id,
                                    *tx_desc,
                                    blobReq->dmt_version,
                                    amReq->getBlobName()) == ERR_OK);
            // Stage the transaction metadata changes
            fds_verify(txMgr->updateStagedBlobDesc(*tx_desc, blobReq->final_meta_data));
        }

        // Update the transaction manager with the stage offset update
        if (ERR_OK != txMgr->updateStagedBlobOffset(*tx_desc,
                                                    amReq->getBlobName(),
                                                    amReq->blob_offset,
                                                    amReq->obj_id)) {
            // An abort or commit already caused the tx
            // to be cleaned up. Short-circuit
            LOGNOTIFY << "Response no longer has active transaction: " << tx_desc->getValue();
            delete amReq;
            return;
        }

        // Update the transaction manager with the staged object data
        if (amReq->data_len > 0) {
            fds_verify(txMgr->updateStagedBlobObject(*tx_desc,
                                                     amReq->obj_id,
                                                     blobReq->dataPtr)
                   == ERR_OK);
        }

        if (amReq->io_type == FDS_PUT_BLOB_ONCE) {
            txMgr->commitTx(*tx_desc, blobReq->final_blob_size);
        }
    }

    respond_and_delete(amReq, error);
}

void
AmProcessor_impl::getBlob(AmRequest *amReq) {
    fiu_do_on("am.uturn.processor.getBlob",
              respond_and_delete(amReq, ERR_OK); \
              return;);

    auto shVol = getVolume(amReq, true);
    auto volId = amReq->io_vol_id;
    if (!shVol) {
        LOGCRITICAL << "getBlob failed to get volume for vol " << volId;
        getBlobCb(amReq, ERR_INVALID);
        return;
    }

    // We can only read from the cache if we have an access token managing it
    auto can_cache = haveCacheToken(shVol);

    // TODO(Anna) We are doing update catalog using absolute
    // offsets, so we need to be consistent in query catalog
    // Review this in the next sprint!
    fds_uint32_t maxObjSize = shVol->voldesc->maxObjSizeInBytes;
    amReq->blob_offset = (amReq->blob_offset * maxObjSize);
    GetBlobReq *blobReq = static_cast<GetBlobReq *>(amReq);
    Error err = ERR_OK;

    if (can_cache) {
        // If we need to return metadata, check the cache
        if (blobReq->get_metadata) {
            BlobDescriptor::ptr cachedBlobDesc = txMgr->getBlobDescriptor(volId,
                                                                          amReq->getBlobName(),
                                                                          err);
            if (ERR_OK == err) {
                LOGTRACE << "Found cached blob descriptor for " << std::hex
                         << volId << std::dec << " blob " << amReq->getBlobName();
                blobReq->metadata_cached = true;
                auto cb = SHARED_DYN_CAST(GetObjectWithMetadataCallback, amReq->cb);
                // Fill in the data here
                cb->blobDesc = cachedBlobDesc;
            }
        }

        // Check cache for object ID
        ObjectID::ptr objectId = txMgr->getBlobOffsetObject(volId,
                                                              amReq->getBlobName(),
                                                              amReq->blob_offset,
                                                              err);
        // ObjectID was found in the cache
        if ((ERR_OK == err) && (blobReq->metadata_cached == blobReq->get_metadata)) {
            blobReq->oid_cached = true;
            // TODO(Andrew): Consider adding this back when we revisit
            // zero length objects
            amReq->obj_id = *objectId;
            return getObject(amReq);
        }
    } else {
        LOGDEBUG << "Can't read from cache, dispatching to DM.";
    }

    // need to read from datamgr
    amReq->proc_cb = AMPROCESSOR_CB_HANDLER(AmProcessor_impl::queryCatalogCb, amReq);
    amDispatcher->dispatchQueryCatalog(amReq);
}

void
AmProcessor_impl::getObject(AmRequest *amReq) {
    Error err = ERR_OK;
    fds_volid_t volId = amReq->io_vol_id;

    // We can read data from the cache
    // Check cache for object data
    boost::shared_ptr<std::string> objectData = txMgr->getBlobObject(volId,
                                                                     amReq->obj_id,
                                                                     err);
    if (err == ERR_OK) {
        // Data was found in cache, so fill data and callback
        LOGTRACE << "Found cached object " << amReq->obj_id;

        // Pull out the GET callback object so we can populate it
        // with cache contents and send it to the requester.
        GetObjectCallback::ptr cb = SHARED_DYN_CAST(GetObjectCallback, amReq->cb);

        cb->returnSize = std::min(amReq->data_len, objectData->size());
        cb->return_buffers = boost::make_shared<std::vector<boost::shared_ptr<std::string>>>(1, objectData);

        // Report results of GET request to requestor.
        respond_and_delete(amReq, err);
        return;
    }

    // We couldn't find the data in the cache Fallback to retrieving
    // the data from the SM.
    amReq->proc_cb = AMPROCESSOR_CB_HANDLER(AmProcessor_impl::getBlobCb, amReq);
    amDispatcher->dispatchGetObject(amReq);
}

void
AmProcessor_impl::getBlobCb(AmRequest *amReq, const Error& error) {
    auto blobReq = static_cast<GetBlobReq *>(amReq);
    if (error == ERR_NOT_FOUND &&
        (!blobReq->retry || (blobReq->obj_id != blobReq->last_obj_id))) {
        // TODO(bszmyd): Mon 23 Mar 2015 02:40:25 AM PDT
        // This is somewhat of a trick since we don't really support
        // transactions. If we can't find the object, do an index lookup
        // again. If we get back the same ObjectID, then fine...try SM again,
        // but that's the end of it.
        blobReq->oid_cached = false;
        std::swap(blobReq->obj_id, blobReq->last_obj_id);
        blobReq->retry = true;
        blobReq->proc_cb = AMPROCESSOR_CB_HANDLER(AmProcessor_impl::queryCatalogCb, amReq);
        GLOGDEBUG << "Dispatching retry on [ " << blobReq->volume_name
                  << ", " << blobReq->getBlobName()
                  << ", 0x" << std::hex << blobReq->blob_offset << std::dec
                  << "B ]";
        amDispatcher->dispatchQueryCatalog(amReq);
        return;
    }

    respond(amReq, error);

    GetObjectCallback::ptr cb = SHARED_DYN_CAST(GetObjectCallback, amReq->cb);
    // Insert original Object into cache. Do not insert the truncated version
    // since we really do have all the data. This is done after response to
    // reduce latency.
    if (ERR_OK == error && cb->return_buffers) {
        txMgr->putObject(amReq->io_vol_id,
                           amReq->obj_id,
                           cb->return_buffers->front());
        // If we have a cache token, we can stash this metadata
        if (haveCacheToken(getVolume(amReq, true))) {
            if (!blobReq->oid_cached) {
                txMgr->putOffset(amReq->io_vol_id,
                                   BlobOffsetPair(amReq->getBlobName(), amReq->blob_offset),
                                   boost::make_shared<ObjectID>(amReq->obj_id));
            }

            if (!blobReq->metadata_cached && blobReq->get_metadata) {
                auto cb = SHARED_DYN_CAST(GetObjectWithMetadataCallback, amReq->cb);
                if (cb->blobDesc)
                    txMgr->putBlobDescriptor(amReq->io_vol_id,
                                               amReq->getBlobName(),
                                               cb->blobDesc);
            }
        }
    }

    delete amReq;
}


void
AmProcessor_impl::setBlobMetadata(AmRequest *amReq) {
    auto shVol = getVolume(amReq, false);
    if (!haveCacheToken(shVol)) {
        respond_and_delete(amReq, ERR_VOLUME_ACCESS_DENIED);
        return;
    }

    SetBlobMetaDataReq *blobReq = static_cast<SetBlobMetaDataReq *>(amReq);

    fds_verify(txMgr->getTxDmtVersion(*(blobReq->tx_desc), &(blobReq->dmt_version)));
    amReq->proc_cb = AMPROCESSOR_CB_HANDLER(AmProcessor_impl::respond_and_delete, amReq);

    amDispatcher->dispatchSetBlobMetadata(amReq);
}

void
AmProcessor_impl::statBlob(AmRequest *amReq) {
    fds_volid_t volId = amReq->io_vol_id;
    LOGDEBUG << "volume:" << volId <<" blob:" << amReq->getBlobName();

    auto shVol = getVolume(amReq, true);
    auto can_cache = haveCacheToken(shVol);

    if (can_cache) {
        // Check cache for blob descriptor
        Error err(ERR_OK);
        BlobDescriptor::ptr cachedBlobDesc = txMgr->getBlobDescriptor(volId,
                                                                        amReq->getBlobName(),
                                                                        err);
        if (ERR_OK == err) {
            LOGTRACE << "Found cached blob descriptor for " << std::hex
                << volId << std::dec << " blob " << amReq->getBlobName();

            StatBlobCallback::ptr cb = SHARED_DYN_CAST(StatBlobCallback, amReq->cb);
            // Fill in the data here
            cb->blobDesc = cachedBlobDesc;
            statBlobCb(amReq, ERR_OK);
            return;
        }
        LOGTRACE << "Did not find cached blob descriptor for " << std::hex
            << volId << std::dec << " blob " << amReq->getBlobName();
    }

    amReq->proc_cb = AMPROCESSOR_CB_HANDLER(AmProcessor_impl::statBlobCb, amReq);
    amDispatcher->dispatchStatBlob(amReq);
}

void
AmProcessor_impl::queryCatalogCb(AmRequest *amReq, const Error& error) {
    if (error == ERR_OK) {
        getObject(amReq);
    } else {
        respond_and_delete(amReq, error);
    }
}

void
AmProcessor_impl::statBlobCb(AmRequest *amReq, const Error& error) {
    respond(amReq, error);

    // Insert metadata into cache.
    if (ERR_OK == error && haveCacheToken(getVolume(amReq, true))) {
        txMgr->putBlobDescriptor(amReq->io_vol_id,
                                   amReq->getBlobName(),
                                   SHARED_DYN_CAST(StatBlobCallback, amReq->cb)->blobDesc);
    }

    delete amReq;
}

void
AmProcessor_impl::volumeContents(AmRequest *amReq) {
    amReq->proc_cb = AMPROCESSOR_CB_HANDLER(AmProcessor_impl::respond_and_delete, amReq);
    amDispatcher->dispatchVolumeContents(amReq);
}

void
AmProcessor_impl::commitBlobTx(AmRequest *amReq) {
    amReq->proc_cb = AMPROCESSOR_CB_HANDLER(AmProcessor_impl::commitBlobTxCb, amReq);
    amDispatcher->dispatchCommitBlobTx(amReq);
}

void
AmProcessor_impl::commitBlobTxCb(AmRequest *amReq, const Error &error) {
    // Push the committed update to the cache and remove from manager
    // TODO(Andrew): Inserting the entire tx transaction currently
    // assumes that the tx descriptor has all of the contents needed
    // for a blob descriptor (e.g., size, version, etc..). Today this
    // is true for S3/Swift and doesn't get used anyways for block (so
    // the actual cached descriptor for block will not be correct).
    if (ERR_OK == error) {
        CommitBlobTxReq *blobReq = static_cast<CommitBlobTxReq *>(amReq);
        txMgr->updateStagedBlobDesc(*(blobReq->tx_desc), blobReq->final_meta_data);
        txMgr->commitTx(*(blobReq->tx_desc), blobReq->final_blob_size);
    }

    respond_and_delete(amReq, error);
}

/**
 * Pimpl forwarding methods. Should just call the underlying implementaion
 */
AmProcessor::AmProcessor(CommonModuleProviderIf *modProvider)
        : enable_shared_from_this<AmProcessor>(),
          _impl(new AmProcessor_impl(modProvider))
{ }

AmProcessor::~AmProcessor() = default;

void AmProcessor::start(shutdown_cb_type&& cb)
{ return _impl->start(std::move(cb)); }

void AmProcessor::prepareForShutdownMsgRespBindCb(shutdown_cb_type&& cb)
{
    return _impl->prepareForShutdownMsgRespBindCb(std::move(cb));
}

void AmProcessor::prepareForShutdownMsgRespCallCb()
{
    return _impl->prepareForShutdownMsgRespCallCb();
}

bool AmProcessor::stop()
{ return _impl->stop(); }

Error AmProcessor::enqueueRequest(AmRequest* amReq)
{ return _impl->enqueueRequest(amReq); }

bool AmProcessor::isShuttingDown() const
{ return _impl->isShuttingDown(); }

Error AmProcessor::modifyVolumePolicy(fds_volid_t vol_uuid, const VolumeDesc& vdesc)
{ return _impl->modifyVolumePolicy(vol_uuid, vdesc); }

void AmProcessor::registerVolume(const VolumeDesc& volDesc)
{ return _impl->registerVolume(volDesc); }

Error AmProcessor::removeVolume(const VolumeDesc& volDesc)
{ return _impl->removeVolume(volDesc); }

Error AmProcessor::updateDlt(bool dlt_type, std::string& dlt_data, std::function<void (const Error&)> cb)
{ return _impl->updateDlt(dlt_type, dlt_data, cb); }

Error AmProcessor::updateDmt(bool dmt_type, std::string& dmt_data)
{ return _impl->updateDmt(dmt_type, dmt_data); }

Error AmProcessor::updateQoS(long int const* rate, float const* throttle)
{ return _impl->updateQoS(rate, throttle); }

Error AmProcessor::getDMT()
{ return _impl->getDMT(); }

Error AmProcessor::getDLT()
{ return _impl->getDLT(); }

}  // namespace fds
