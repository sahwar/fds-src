/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

#include <algorithm>
#include <string>
#include <ObjectId.h>
#include "FdsRandom.h"
#include <AmProcessor.h>
#include <fiu-control.h>
#include <util/fiu_util.h>
#include <am-tx-mgr.h>

#include "requests/requests.h"
#include "AsyncResponseHandlers.h"

namespace fds {

#define AMPROCESSOR_CB_HANDLER(func, ...) \
    std::bind(&func, this, ##__VA_ARGS__ , std::placeholders::_1)

AmProcessor::AmProcessor(const std::string &modName,
                         AmDispatcher::shared_ptr _amDispatcher,
                         std::shared_ptr<StorHvQosCtrl> _qosCtrl,
                         std::shared_ptr<StorHvVolumeTable> _volTable)
        : Module(modName.c_str()),
          amDispatcher(_amDispatcher),
          qosCtrl(_qosCtrl),
          volTable(_volTable),
          txMgr(new AmTxManager()) {
    FdsConfigAccessor conf(g_fdsprocess->get_fds_config(), "fds.am.");
    if (conf.get<fds_bool_t>("testing.uturn_processor_all")) {
        fiu_enable("am.uturn.processor.*", 1, NULL, 0);
    }
    randNumGen = RandNumGenerator::unique_ptr(
        new RandNumGenerator(RandNumGenerator::getRandSeed()));
}

AmProcessor::~AmProcessor()
{}

void
AmProcessor::respond(AmRequest *amReq, const Error& error) {
    qosCtrl->markIODone(amReq);
    amReq->cb->call(error);

    /*
     * If we're shutting down and there are no
     * more outstanding requests, tell the QoS
     * Dispatcher that we're shutting down.
     */
    if (am->isShuttingDown() &&
            (qosCtrl->htb_dispatcher->num_outstanding_ios == 0))
    {
        am->stop();
    }
}

StorHvVolumeTable::volume_ptr_type
AmProcessor::getNoSnapshotVolume(AmRequest* amReq) {
    // check if this is a snapshot
    auto shVol = volTable->getVolume(amReq->io_vol_id);
    if (!shVol) {
        LOGCRITICAL << "unable to get volume info for vol: " << amReq->io_vol_id;
        respond_and_delete(amReq, FDSN_StatusErrorUnknown);
    } else if (shVol->voldesc->isSnapshot()) {
        LOGWARN << "txn on a snapshot is not allowed.";
        respond_and_delete(amReq, FDSN_StatusErrorAccessDenied);
        shVol = nullptr;
    }
    return shVol;
}

Error
AmProcessor::addVolume(const VolumeDesc& volDesc) {
    return txMgr->addVolume(volDesc);
}

void
AmProcessor::getVolumeMetadata(AmRequest *amReq) {
    fiu_do_on("am.uturn.processor.getVolMeta",
              respond_and_delete(amReq, ERR_OK); \
              return;);

    amReq->proc_cb = AMPROCESSOR_CB_HANDLER(AmProcessor::getVolumeMetadataCb, amReq);
    amDispatcher->dispatchGetVolumeMetadata(amReq);
}

void
AmProcessor::getVolumeMetadataCb(AmRequest *amReq, const Error &error) {
    GetVolumeMetaDataCallback::ptr cb =
            SHARED_DYN_CAST(GetVolumeMetaDataCallback, amReq->cb);
    cb->volumeMetaData = static_cast<GetVolumeMetaDataReq *>(amReq)->volumeMetadata;
    respond_and_delete(amReq, error);
}

void
AmProcessor::abortBlobTx(AmRequest *amReq) {
    amReq->proc_cb = AMPROCESSOR_CB_HANDLER(AmProcessor::abortBlobTxCb, amReq);
    amDispatcher->dispatchAbortBlobTx(amReq);
}

void
AmProcessor::abortBlobTxCb(AmRequest *amReq, const Error &error) {
    AbortBlobTxReq *blobReq = static_cast<AbortBlobTxReq *>(amReq);
    if (ERR_OK != txMgr->abortTx(*(blobReq->tx_desc)))
        LOGWARN << "Transaction unknown";

    respond_and_delete(amReq, error);
}

void
AmProcessor::startBlobTx(AmRequest *amReq) {
    auto shVol = getNoSnapshotVolume(amReq);
    if (!shVol) return;

    fiu_do_on("am.uturn.processor.startBlobTx",
              respond_and_delete(amReq, ERR_OK); \
              return;);

    // Generate a random transaction ID to use
    static_cast<StartBlobTxReq*>(amReq)->tx_desc =
        boost::make_shared<BlobTxId>(randNumGen->genNumSafe());
    amReq->proc_cb = AMPROCESSOR_CB_HANDLER(AmProcessor::startBlobTxCb, amReq);

    amDispatcher->dispatchStartBlobTx(amReq);
}

void
AmProcessor::startBlobTxCb(AmRequest *amReq, const Error &error) {
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
AmProcessor::deleteBlob(AmRequest *amReq) {
    auto shVol = getNoSnapshotVolume(amReq);
    if (!shVol) return;

    DeleteBlobReq* blobReq = static_cast<DeleteBlobReq *>(amReq);
    LOGDEBUG    << " volume:" << amReq->io_vol_id
                << " blob:" << amReq->getBlobName()
                << " txn:" << blobReq->tx_desc;

    // Update the tx manager with the delete op
    txMgr->updateTxOpType(*(blobReq->tx_desc), amReq->io_type);

    amReq->proc_cb = AMPROCESSOR_CB_HANDLER(AmProcessor::respond_and_delete, amReq);
    amDispatcher->dispatchDeleteBlob(amReq);
}

void
AmProcessor::putBlob(AmRequest *amReq) {
    auto shVol = getNoSnapshotVolume(amReq);
    if (!shVol) return;

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

    fiu_do_on("am.uturn.processor.putBlob",
              respond_and_delete(amReq, ERR_OK); \
              return;);

    amReq->proc_cb = AMPROCESSOR_CB_HANDLER(AmProcessor::putBlobCb, amReq);

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
AmProcessor::putBlobCb(AmRequest *amReq, const Error& error) {
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
            fds_verify(txMgr->commitTx(*tx_desc, blobReq->final_blob_size) == ERR_OK);
        }
    }

    respond_and_delete(amReq, error);
}

void
AmProcessor::getBlob(AmRequest *amReq) {
    fiu_do_on("am.uturn.processor.getBlob",
              respond_and_delete(amReq, ERR_OK); \
              return;);

    fds_volid_t volId = amReq->io_vol_id;
    auto shVol = volTable->getVolume(volId);
    if (!shVol) {
        LOGCRITICAL << "getBlob failed to get volume for vol " << volId;
        getBlobCb(amReq, ERR_INVALID);
        return;
    }

    // TODO(Anna) We are doing update catalog using absolute
    // offsets, so we need to be consistent in query catalog
    // Review this in the next sprint!
    fds_uint32_t maxObjSize = shVol->voldesc->maxObjSizeInBytes;
    amReq->blob_offset = (amReq->blob_offset * maxObjSize);

    // If we need to return metadata, check the cache
    Error err = ERR_OK;
    GetBlobReq *blobReq = static_cast<GetBlobReq *>(amReq);
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

        // Check cache for object data
        boost::shared_ptr<std::string> objectData = txMgr->getBlobObject(volId,
                                                                           *objectId,
                                                                           err);
        if (err == ERR_OK) {
            // Data was found in cache, so fill data and callback
            LOGTRACE << "Found cached object " << *objectId;

            // Pull out the GET callback object so we can populate it
            // with cache contents and send it to the requester.
            GetObjectCallback::ptr cb = SHARED_DYN_CAST(GetObjectCallback, amReq->cb);

            cb->returnSize = std::min(amReq->data_len, objectData->size());
            cb->returnBuffer = objectData;

            // Report results of GET request to requestor.
            respond_and_delete(amReq, err);
        } else {
            // We couldn't find the data in the cache even though the id was
            // obtained there. Fallback to retrieving the data from the SM.
            amReq->proc_cb = AMPROCESSOR_CB_HANDLER(AmProcessor::getBlobCb, amReq);
            amDispatcher->dispatchGetObject(amReq);
        }
    } else {
        amReq->proc_cb = AMPROCESSOR_CB_HANDLER(AmProcessor::queryCatalogCb, amReq);
        amDispatcher->dispatchQueryCatalog(amReq);
    }
}

void
AmProcessor::getBlobCb(AmRequest *amReq, const Error& error) {
    respond(amReq, error);

    GetObjectCallback::ptr cb = SHARED_DYN_CAST(GetObjectCallback, amReq->cb);
    // Insert original Object into cache. Do not insert the truncated version
    // since we really do have all the data. This is done after response to
    // reduce latency.
    if (ERR_OK == error && cb->returnBuffer) {
        txMgr->putObject(amReq->io_vol_id,
                           amReq->obj_id,
                           cb->returnBuffer);
        auto blobReq = static_cast<GetBlobReq*>(amReq);
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

    delete amReq;
}


void
AmProcessor::setBlobMetadata(AmRequest *amReq) {
    SetBlobMetaDataReq *blobReq = static_cast<SetBlobMetaDataReq *>(amReq);

    fds_verify(txMgr->getTxDmtVersion(*(blobReq->tx_desc), &(blobReq->dmt_version)));
    amReq->proc_cb = AMPROCESSOR_CB_HANDLER(AmProcessor::respond_and_delete, amReq);

    amDispatcher->dispatchSetBlobMetadata(amReq);
}

void
AmProcessor::statBlob(AmRequest *amReq) {
    fds_volid_t volId = amReq->io_vol_id;
    LOGDEBUG << "volume:" << volId <<" blob:" << amReq->getBlobName();

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

    amReq->proc_cb = AMPROCESSOR_CB_HANDLER(AmProcessor::statBlobCb, amReq);
    amDispatcher->dispatchStatBlob(amReq);
}

void
AmProcessor::queryCatalogCb(AmRequest *amReq, const Error& error) {
    if (error == ERR_OK) {
        amReq->proc_cb = AMPROCESSOR_CB_HANDLER(AmProcessor::getBlobCb, amReq);
        amDispatcher->dispatchGetObject(amReq);
    } else {
        respond_and_delete(amReq, error);
    }
}

void
AmProcessor::statBlobCb(AmRequest *amReq, const Error& error) {
    respond(amReq, error);

    // Insert metadata into cache.
    if (ERR_OK == error) {
        txMgr->putBlobDescriptor(amReq->io_vol_id,
                                   amReq->getBlobName(),
                                   SHARED_DYN_CAST(StatBlobCallback, amReq->cb)->blobDesc);
    }

    delete amReq;
}

void
AmProcessor::volumeContents(AmRequest *amReq) {
    amReq->proc_cb = AMPROCESSOR_CB_HANDLER(AmProcessor::respond_and_delete, amReq);
    amDispatcher->dispatchVolumeContents(amReq);
}

void
AmProcessor::commitBlobTx(AmRequest *amReq) {
    amReq->proc_cb = AMPROCESSOR_CB_HANDLER(AmProcessor::commitBlobTxCb, amReq);
    amDispatcher->dispatchCommitBlobTx(amReq);
}

void
AmProcessor::commitBlobTxCb(AmRequest *amReq, const Error &error) {
    // Push the committed update to the cache and remove from manager
    // TODO(Andrew): Inserting the entire tx transaction currently
    // assumes that the tx descriptor has all of the contents needed
    // for a blob descriptor (e.g., size, version, etc..). Today this
    // is true for S3/Swift and doesn't get used anyways for block (so
    // the actual cached descriptor for block will not be correct).
    if (ERR_OK == error) {
        CommitBlobTxReq *blobReq = static_cast<CommitBlobTxReq *>(amReq);
        fds_verify(txMgr->updateStagedBlobDesc(*(blobReq->tx_desc), blobReq->final_meta_data));
        fds_verify(txMgr->commitTx(*(blobReq->tx_desc), blobReq->final_blob_size) == ERR_OK);
    }

    respond_and_delete(amReq, error);
}
}  // namespace fds
