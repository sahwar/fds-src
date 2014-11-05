/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#include <responsehandler.h>
#include <util/Log.h>
#include <util/timeutils.h>
#include <string>
#include <sstream>
#include <vector>
#define XCHECKSTATUS(status)                                        \
if (status != ERR_OK) {                                             \
    LOGWARN << " status:" << status;                                \
    apis::ApiException e;                                           \
    switch (status) {                                               \
        case FDSN_StatusEntityDoesNotExist:                         \
        case ERR_NOT_FOUND:                                         \
        case ERR_VOL_NOT_FOUND:                                     \
            e.errorCode = apis::ErrorCode::MISSING_RESOURCE;        \
            break;                                                  \
        case ERR_NOT_READY:                                         \
            e.errorCode = apis::ErrorCode::SERVICE_NOT_READY;       \
            break;                                                  \
        case ERR_VOL_NOT_EMPTY:                                     \
            e.errorCode = apis::ErrorCode::RESOURCE_NOT_EMPTY;      \
            break;                                                  \
        case ERR_DUPLICATE:                                         \
            e.errorCode = apis::ErrorCode::RESOURCE_ALREADY_EXISTS; \
            break;                                                  \
        default:                                                    \
            e.errorCode = apis::ErrorCode::INTERNAL_SERVER_ERROR;   \
    }                                                               \
    e.message = toString(status);                                   \
    throw e;                                                        \
}

namespace fds {

void ResponseHandler::call() {
    switch (type) {
        case HandlerType::WAITEDFOR:
            ready(); break;
        case HandlerType::IMMEDIATE:
            process() ; break;
        case HandlerType::QUEUED:
            fds_panic("QUEUED - not implemented yet");
            break;
    }
}

void ResponseHandler::ready() {
    task.done();
}

void ResponseHandler::wait() {
    task.await();
}

    bool ResponseHandler::waitFor(ulong timeout) {
    return task.await(timeout);
}

ResponseHandler::~ResponseHandler() {
}

//================================================================================

SimpleResponseHandler::SimpleResponseHandler() {
}

SimpleResponseHandler::SimpleResponseHandler(const std::string& name) : name(name) {
}

void SimpleResponseHandler::process() {
    LOGDEBUG << "processing callback for : " << name;
    XCHECKSTATUS(status);
}

SimpleResponseHandler::~SimpleResponseHandler() {
}

//================================================================================

void UpdateBlobResponseHandler::process() {
}

UpdateBlobResponseHandler::~UpdateBlobResponseHandler() {
}
//================================================================================

GetObjectResponseHandler::GetObjectResponseHandler(char *buf) {
    returnBuffer = buf;
}

void GetObjectResponseHandler::process() {
}

GetObjectResponseHandler::~GetObjectResponseHandler() {
}
//================================================================================

ListBucketResponseHandler::ListBucketResponseHandler(std::vector<apis::BlobDescriptor> & vecBlobs)
        : retVecBlobs(vecBlobs) { //NOLINT
}

void ListBucketResponseHandler::process() {
    XCHECKSTATUS(status);

    for (auto const & blobDesc : vecBlobs) {
        retVecBlobs.push_back(blobDesc);
    }
}

ListBucketResponseHandler::~ListBucketResponseHandler() {
}

AsyncListBucketResponseHandler::AsyncListBucketResponseHandler(
    AmAsyncResponseApi::shared_ptr _api,
    boost::shared_ptr<apis::RequestId>& _reqId)
        : respApi(_api),
          requestId(_reqId) {
    type = HandlerType::IMMEDIATE;
}

void AsyncListBucketResponseHandler::process() {
    respApi->volumeContentsResp(error, requestId, vecBlobs);
}

AsyncListBucketResponseHandler::~AsyncListBucketResponseHandler() {
}

//================================================================================

BucketStatsResponseHandler::BucketStatsResponseHandler(
    apis::VolumeDescriptor& volumeDescriptor) : volumeDescriptor(volumeDescriptor) {
}

void BucketStatsResponseHandler::process() {
    XCHECKSTATUS(status);

    if (content_count == 0 || !contents) {
        LOGWARN << "response has no bucket data";
        status = FDSN_StatusErrorBucketNotExists;
        XCHECKSTATUS(status);
    }

    volumeDescriptor.name = contents[0].bucket_name;
    // volumeDescriptor.uuid = 10;
    volumeDescriptor.dateCreated = util::getTimeStampMillis();
    volumeDescriptor.policy.maxObjectSizeInBytes = 2097152;  // 2MB
}

BucketStatsResponseHandler::~BucketStatsResponseHandler() {
    if (contents) {
        delete[] contents;
    }
}

StatBlobResponseHandler::StatBlobResponseHandler(
    apis::BlobDescriptor& retVal) : retBlobDesc(retVal) {
}


void StatBlobResponseHandler::process() {
    if (error == ERR_CAT_ENTRY_NOT_FOUND) {
        apis::ApiException fdsE;
        fdsE.errorCode = apis::MISSING_RESOURCE;
        throw fdsE;
    }
    XCHECKSTATUS(status);

    retBlobDesc.name      = blobDesc.getBlobName();
    retBlobDesc.byteCount = blobDesc.getBlobSize();

    for (const_kv_iterator it = blobDesc.kvMetaBegin();
         it != blobDesc.kvMetaEnd();
         it++) {
        retBlobDesc.metadata[it->first] = it->second;
    }
}

StatBlobResponseHandler::~StatBlobResponseHandler() {
}

AsyncStatBlobResponseHandler::AsyncStatBlobResponseHandler(
    AmAsyncResponseApi::shared_ptr _api,
    boost::shared_ptr<apis::RequestId>& _reqId)
        : respApi(_api),
          requestId(_reqId) {
    type = HandlerType::IMMEDIATE;
}


void AsyncStatBlobResponseHandler::process() {
    retBlobDesc = boost::make_shared<apis::BlobDescriptor>();
    retBlobDesc->name = blobDesc.getBlobName();
    retBlobDesc->byteCount = blobDesc.getBlobSize();

    for (const_kv_iterator it = blobDesc.kvMetaBegin();
         it != blobDesc.kvMetaEnd();
         it++) {
        retBlobDesc->metadata[it->first] = it->second;
    }
    respApi->statBlobResp(error, requestId, retBlobDesc);
}

AsyncStatBlobResponseHandler::~AsyncStatBlobResponseHandler() {
}

StartBlobTxResponseHandler::StartBlobTxResponseHandler(
    apis::TxDescriptor& retVal) : retTxDesc(retVal) {
}

void
StartBlobTxResponseHandler::process() {
    XCHECKSTATUS(status);

    retTxDesc.txId = blobTxId.getValue();
}

StartBlobTxResponseHandler::~StartBlobTxResponseHandler() {
}

AsyncStartBlobTxResponseHandler::AsyncStartBlobTxResponseHandler(
    AmAsyncResponseApi::shared_ptr _api,
    boost::shared_ptr<apis::RequestId>& _reqId)
        : respApi(_api),
          requestId(_reqId) {
    type = HandlerType::IMMEDIATE;
}

void
AsyncStartBlobTxResponseHandler::process() {
    txDesc = boost::make_shared<apis::TxDescriptor>();
    txDesc->txId = blobTxId.getValue();
    respApi->startBlobTxResp(error, requestId, txDesc);
}

AsyncStartBlobTxResponseHandler::~AsyncStartBlobTxResponseHandler() {
}

AsyncAbortBlobTxResponseHandler::AsyncAbortBlobTxResponseHandler(
    AmAsyncResponseApi::shared_ptr _api,
    boost::shared_ptr<apis::RequestId>& _reqId)
        : respApi(_api),
          requestId(_reqId) {
    type = HandlerType::IMMEDIATE;
}

void
AsyncAbortBlobTxResponseHandler::process() {
    respApi->abortBlobTxResp(error, requestId);
}

AsyncAbortBlobTxResponseHandler::~AsyncAbortBlobTxResponseHandler() {
}

AsyncCommitBlobTxResponseHandler::AsyncCommitBlobTxResponseHandler(
    AmAsyncResponseApi::shared_ptr _api,
    boost::shared_ptr<apis::RequestId>& _reqId)
        : respApi(_api),
          requestId(_reqId) {
    type = HandlerType::IMMEDIATE;
}

void
AsyncCommitBlobTxResponseHandler::process() {
    respApi->commitBlobTxResp(error, requestId);
}

AsyncCommitBlobTxResponseHandler::~AsyncCommitBlobTxResponseHandler() {
}

AsyncUpdateBlobResponseHandler::AsyncUpdateBlobResponseHandler(
    AmAsyncResponseApi::shared_ptr _api,
    boost::shared_ptr<apis::RequestId>& _reqId)
        : respApi(_api),
          requestId(_reqId) {
    type = HandlerType::IMMEDIATE;
}

void
AsyncUpdateBlobResponseHandler::process() {
    respApi->updateBlobResp(error, requestId);
}

AsyncUpdateBlobResponseHandler::~AsyncUpdateBlobResponseHandler() {
}

AsyncUpdateBlobOnceResponseHandler::AsyncUpdateBlobOnceResponseHandler(
    AmAsyncResponseApi::shared_ptr _api,
    boost::shared_ptr<apis::RequestId>& _reqId)
        : respApi(_api),
          requestId(_reqId) {
    type = HandlerType::IMMEDIATE;
}

void
AsyncUpdateBlobOnceResponseHandler::process() {
    respApi->updateBlobOnceResp(error, requestId);
}

AsyncUpdateBlobOnceResponseHandler::~AsyncUpdateBlobOnceResponseHandler() {
}

AsyncUpdateMetadataResponseHandler::AsyncUpdateMetadataResponseHandler(
    AmAsyncResponseApi::shared_ptr _api,
    boost::shared_ptr<apis::RequestId>& _reqId)
        : respApi(_api),
          requestId(_reqId) {
    type = HandlerType::IMMEDIATE;
}

void
AsyncUpdateMetadataResponseHandler::process() {
    respApi->updateMetadataResp(error, requestId);
}

AsyncUpdateMetadataResponseHandler::~AsyncUpdateMetadataResponseHandler() {
}

AsyncDeleteBlobResponseHandler::AsyncDeleteBlobResponseHandler(
    AmAsyncResponseApi::shared_ptr _api,
    boost::shared_ptr<apis::RequestId>& _reqId)
        : respApi(_api),
          requestId(_reqId) {
    type = HandlerType::IMMEDIATE;
}

void
AsyncDeleteBlobResponseHandler::process() {
    respApi->deleteBlobResp(error, requestId);
}

AsyncDeleteBlobResponseHandler::~AsyncDeleteBlobResponseHandler() {
}

AttachVolumeResponseHandler::AttachVolumeResponseHandler() {
}

void
AttachVolumeResponseHandler::process() {
    XCHECKSTATUS(status);
}

AttachVolumeResponseHandler::~AttachVolumeResponseHandler() {
}

AsyncAttachVolumeResponseHandler::AsyncAttachVolumeResponseHandler(
    AmAsyncResponseApi::shared_ptr _api,
    boost::shared_ptr<apis::RequestId>& _reqId)
        : respApi(_api),
          requestId(_reqId) {
    type = HandlerType::IMMEDIATE;
}

void
AsyncAttachVolumeResponseHandler::process() {
    respApi->attachVolumeResp(error, requestId);
}

AsyncAttachVolumeResponseHandler::~AsyncAttachVolumeResponseHandler() {
}

StatVolumeResponseHandler::StatVolumeResponseHandler(apis::VolumeStatus& volumeStatus)
        : volumeStatus(volumeStatus) {
}

void StatVolumeResponseHandler::process() {
    XCHECKSTATUS(status);
    volumeStatus.blobCount = volumeMetaData.blobCount;
    volumeStatus.currentUsageInBytes = volumeMetaData.size;
}

AsyncStatVolumeResponseHandler::AsyncStatVolumeResponseHandler(
    AmAsyncResponseApi::shared_ptr _api,
    boost::shared_ptr<apis::RequestId>& _reqId)
        : respApi(_api),
          requestId(_reqId) {
    type = HandlerType::IMMEDIATE;
}

void AsyncStatVolumeResponseHandler::process() {
    volumeStatus = boost::make_shared<apis::VolumeStatus>();
    volumeStatus->blobCount = volumeMetaData.blobCount;
    volumeStatus->currentUsageInBytes = volumeMetaData.size;
    respApi->volumeStatusResp(error, requestId, volumeStatus);
}
}  // namespace fds
