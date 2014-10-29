/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

#include <map>
#include <string>
#include <vector>
#include <AmAsyncDataApi.h>
#include <handlermappings.h>
#include <responsehandler.h>
#include <StorHvisorNet.h>

namespace fds {

AmAsyncDataApi::AmAsyncDataApi() {
    FdsConfigAccessor conf(g_fdsprocess->get_conf_helper());
    testUturnAll = conf.get_abs<bool>("fds.am.testing.uturn_amserv_all");
    if (true == testUturnAll) {
        LOGDEBUG << "Enabling uturn testing for all AM service APIs";
    }

    responseApi = boost::make_shared<AmAsyncXdiResponse>();
}

AmAsyncDataApi::~AmAsyncDataApi() {
}

void
AmAsyncDataApi::setResponseApi(AmAsyncResponseApi::shared_ptr respApi) {
    responseApi = respApi;
}

void
AmAsyncDataApi::attachVolume(const apis::RequestId& requestId,
                             const std::string& domainName,
                             const std::string& volumeName) {
    fds_panic("You shouldn't be here.");
}

void
AmAsyncDataApi::attachVolume(boost::shared_ptr<apis::RequestId>& requestId,
                             boost::shared_ptr<std::string>& domainName,
                             boost::shared_ptr<std::string>& volumeName) {
}

void
AmAsyncDataApi::volumeStatus(const apis::RequestId& requestId,
                             const std::string& domainName,
                             const std::string& volumeName) {
    fds_panic("You shouldn't be here.");
}

void
AmAsyncDataApi::volumeStatus(boost::shared_ptr<apis::RequestId>& requestId,
                             boost::shared_ptr<std::string>& domainName,
                             boost::shared_ptr<std::string>& volumeName) {
}

void
AmAsyncDataApi::volumeContents(const apis::RequestId& requestId,
                               const std::string& domainName,
                               const std::string& volumeName,
                               const int32_t count,
                               const int64_t offset) {
    fds_panic("You shouldn't be here.");
}

void
AmAsyncDataApi::volumeContents(boost::shared_ptr<apis::RequestId>& requestId,
                               boost::shared_ptr<std::string>& domainName,
                               boost::shared_ptr<std::string>& volumeName,
                               boost::shared_ptr<int32_t>& count,
                               boost::shared_ptr<int64_t>& offset) {
}

void
AmAsyncDataApi::statBlob(const apis::RequestId& requestId,
                         const std::string& domainName,
                         const std::string& volumeName,
                         const std::string& blobName) {
    fds_panic("You shouldn't be here.");
}

void
AmAsyncDataApi::statBlob(boost::shared_ptr<apis::RequestId>& requestId,
                         boost::shared_ptr<std::string>& domainName,
                         boost::shared_ptr<std::string>& volumeName,
                         boost::shared_ptr<std::string>& blobName) {
}

void
AmAsyncDataApi::startBlobTx(const apis::RequestId& requestId,
                            const std::string& domainName,
                            const std::string& volumeName,
                            const std::string& blobName,
                            const fds_int32_t blobMode) {
    fds_panic("You shouldn't be here.");
}

void
AmAsyncDataApi::startBlobTx(boost::shared_ptr<apis::RequestId>& requestId,
                            boost::shared_ptr<std::string>& domainName,
                            boost::shared_ptr<std::string>& volumeName,
                            boost::shared_ptr<std::string>& blobName,
                            boost::shared_ptr<fds_int32_t>& blobMode) {
    AsyncStartBlobTxResponseHandler::ptr handler(
        new AsyncStartBlobTxResponseHandler(responseApi,
                                            requestId));

    FdsBlobReq *blobReq = new StartBlobTxReq(invalid_vol_id,
                                             *volumeName,
                                             *blobName,
                                             *blobMode,
                                             SHARED_DYN_CAST(Callback, handler));
    storHvisor->enqueueBlobReq(blobReq);
}

void
AmAsyncDataApi::commitBlobTx(const apis::RequestId& requestId,
                             const std::string& domainName,
                             const std::string& volumeName,
                             const std::string& blobName,
                             const apis::TxDescriptor& txDesc) {
    fds_panic("You shouldn't be here.");
}

void
AmAsyncDataApi::commitBlobTx(boost::shared_ptr<apis::RequestId>& requestId,
                             boost::shared_ptr<std::string>& domainName,
                             boost::shared_ptr<std::string>& volumeName,
                             boost::shared_ptr<std::string>& blobName,
                             boost::shared_ptr<apis::TxDescriptor>& txDesc) {
}

void
AmAsyncDataApi::abortBlobTx(const apis::RequestId& requestId,
                            const std::string& domainName,
                            const std::string& volumeName,
                            const std::string& blobName,
                            const apis::TxDescriptor& txDesc) {
    fds_panic("You shouldn't be here.");
}

void
AmAsyncDataApi::abortBlobTx(boost::shared_ptr<apis::RequestId>& requestId,
                            boost::shared_ptr<std::string>& domainName,
                            boost::shared_ptr<std::string>& volumeName,
                            boost::shared_ptr<std::string>& blobName,
                            boost::shared_ptr<apis::TxDescriptor>& txDesc) {
}

void
AmAsyncDataApi::getBlob(const apis::RequestId& requestId,
                        const std::string& domainName,
                        const std::string& volumeName,
                        const std::string& blobName,
                        const int32_t length,
                        const apis::ObjectOffset& offset) {
    fds_panic("You shouldn't be here.");
}

void
AmAsyncDataApi::getBlob(boost::shared_ptr<apis::RequestId>& requestId,
                        boost::shared_ptr<std::string>& domainName,
                        boost::shared_ptr<std::string>& volumeName,
                        boost::shared_ptr<std::string>& blobName,
                        boost::shared_ptr<int32_t>& length,
                        boost::shared_ptr<apis::ObjectOffset>& objectOffset) {
}

void
AmAsyncDataApi::updateMetadata(const apis::RequestId& requestId,
                               const std::string& domainName,
                               const std::string& volumeName,
                               const std::string& blobName,
                               const apis::TxDescriptor& txDesc,
                               const std::map<std::string, std::string> & metadata) {
    fds_panic("You shouldn't be here.");
}

void
AmAsyncDataApi::updateMetadata(boost::shared_ptr<apis::RequestId>& requestId,
                               boost::shared_ptr<std::string>& domainName,
                               boost::shared_ptr<std::string>& volumeName,
                               boost::shared_ptr<std::string>& blobName,
                               boost::shared_ptr<apis::TxDescriptor>& txDesc,
                               boost::shared_ptr< std::map<std::string, std::string> >& metadata) {
}

void
AmAsyncDataApi::updateBlobOnce(const apis::RequestId& requestId,
                               const std::string& domainName,
                               const std::string& volumeName,
                               const std::string& blobName,
                               const fds_int32_t blobMode,
                               const std::string& bytes,
                               const int32_t length,
                               const apis::ObjectOffset& objectOffset,
                               const std::map<std::string, std::string>& metadata) {
    fds_panic("You shouldn't be here.");
}

void
AmAsyncDataApi::updateBlobOnce(boost::shared_ptr<apis::RequestId>& requestId,
                               boost::shared_ptr<std::string>& domainName,
                               boost::shared_ptr<std::string>& volumeName,
                               boost::shared_ptr<std::string>& blobName,
                               boost::shared_ptr<fds_int32_t>& blobMode,
                               boost::shared_ptr<std::string>& bytes,
                               boost::shared_ptr<int32_t>& length,
                               boost::shared_ptr<apis::ObjectOffset>& objectOffset,
                               boost::shared_ptr< std::map<std::string, std::string> >& metadata) {
    fds_verify(*length >= 0);
    fds_verify(objectOffset->value >= 0);

    AsyncUpdateBlobOnceResponseHandler::ptr putHandler(
        boost::make_shared<AsyncUpdateBlobOnceResponseHandler>(responseApi,
                                                               requestId));

    FdsBlobReq *blobReq = new PutBlobReq(invalid_vol_id,
                                         *volumeName,
                                         *blobName,
                                         static_cast<fds_uint64_t>(objectOffset->value),
                                         *length,
                                         const_cast<char *>(bytes->c_str()),
                                         *blobMode,
                                         metadata,
                                         putHandler);
    storHvisor->enqueueBlobReq(blobReq);
}

void
AmAsyncDataApi::updateBlob(const apis::RequestId& requestId,
                           const std::string& domainName,
                           const std::string& volumeName,
                           const std::string& blobName,
                           const apis::TxDescriptor& txDesc,
                           const std::string& bytes,
                           const int32_t length,
                           const apis::ObjectOffset& objectOffset,
                           const bool isLast) {
    fds_panic("You shouldn't be here.");
}

void
AmAsyncDataApi::updateBlob(boost::shared_ptr<apis::RequestId>& requestId,
                           boost::shared_ptr<std::string>& domainName,
                           boost::shared_ptr<std::string>& volumeName,
                           boost::shared_ptr<std::string>& blobName,
                           boost::shared_ptr<apis::TxDescriptor>& txDesc,
                           boost::shared_ptr<std::string>& bytes,
                           boost::shared_ptr<int32_t>& length,
                           boost::shared_ptr<apis::ObjectOffset>& objectOffset,
                           boost::shared_ptr<bool>& isLast) {
}

void
AmAsyncDataApi::deleteBlob(const apis::RequestId& requestId,
                           const std::string& domainName,
                           const std::string& volumeName,
                           const std::string& blobName) {
    fds_panic("You shouldn't be here.");
}

void
AmAsyncDataApi::deleteBlob(boost::shared_ptr<apis::RequestId>& requestId,
                           boost::shared_ptr<std::string>& domainName,
                           boost::shared_ptr<std::string>& volumeName,
                           boost::shared_ptr<std::string>& blobName) {
}
}  // namespace fds