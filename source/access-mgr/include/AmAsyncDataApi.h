/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#ifndef SOURCE_ACCESS_MGR_INCLUDE_AMASYNCDATAAPI_H_
#define SOURCE_ACCESS_MGR_INCLUDE_AMASYNCDATAAPI_H_

#include <map>
#include <string>
#include <vector>
#include <AmAsyncResponseApi.h>
#include <AsyncResponseHandlers.h>

namespace fds {

struct AmProcessor;

/**
 * AM's async data API that is exposed to XDI. This interface is the
 * basic data API that XDI and connectors are programmed to.
 */

template<typename T, typename C>
CallbackPtr
create_async_handler(C&& c)
{ return std::make_shared<AsyncResponseHandler<T, C>>(std::forward<C>(c)); }

class AmAsyncDataApi {
 public:
    template <typename M> using sp = boost::shared_ptr<M>;

    typedef RequestHandle handle_type;
    typedef sp<bool> shared_bool_type;
    typedef sp<int32_t> shared_int_type;
    typedef sp<int64_t> shared_size_type;
    typedef sp<std::string> shared_buffer_type;
    typedef sp<std::string> shared_string_type;
    typedef sp<apis::ObjectOffset> shared_offset_type;
    typedef sp<apis::TxDescriptor> shared_tx_ctx_type;
    typedef sp<fpi::VolumeAccessMode> shared_vol_mode_type;
    typedef sp<std::map<std::string, std::string>> shared_meta_type;

 private:
    typedef std::shared_ptr<AmProcessor> processor_type;
    typedef AmAsyncResponseApi response_api_type;
    typedef typename boost::shared_ptr<response_api_type> response_ptr;

    processor_type amProcessor;

  protected:
    /// Response client to use in response handler
    response_ptr responseApi;

  public:
    explicit AmAsyncDataApi(processor_type processor, response_ptr response_api);
    explicit AmAsyncDataApi(processor_type processor, response_api_type* response_api);

    ~AmAsyncDataApi() {}

    void attachVolume(handle_type const& requestId,
                      shared_string_type& domainName,
                      shared_string_type& volumeName,
                      shared_vol_mode_type& mode);

    void detachVolume(handle_type const& requestId,
                      shared_string_type& domainName,
                      shared_string_type& volumeName);

    void volumeStatus(handle_type const& requestId,
                      shared_string_type& domainName,
                      shared_string_type& volumeName);

    void volumeContents(handle_type const& requestId,
                        shared_string_type& domainName,
                        shared_string_type& volumeName,
                        shared_int_type& count,
                        shared_size_type& offset,
                        shared_string_type& pattern,
                        boost::shared_ptr<fpi::PatternSemantics>& patternSems,
                        boost::shared_ptr<fpi::BlobListOrder>& orderBy,
                        shared_bool_type& descending,
                        shared_string_type& delimiter);

    void setVolumeMetadata(handle_type const& requestId,
                           shared_string_type& domainName,
                           shared_string_type& volumeName,
                           shared_meta_type& metadata);

    void getVolumeMetadata(handle_type const& requestId,
                           shared_string_type& domainName,
                           shared_string_type& volumeName);

    void statBlob(handle_type const& requestId,
                  shared_string_type& domainName,
                  shared_string_type& volumeName,
                  shared_string_type& blobName);

    void startBlobTx(handle_type const& requestId,
                     shared_string_type& domainName,
                     shared_string_type& volumeName,
                     shared_string_type& blobName,
                     shared_int_type& blobMode);

    void commitBlobTx(handle_type const& requestId,
                      shared_string_type& domainName,
                      shared_string_type& volumeName,
                      shared_string_type& blobName,
                      shared_tx_ctx_type& txDesc);

    void abortBlobTx(handle_type const& requestId,
                     shared_string_type& domainName,
                     shared_string_type& volumeName,
                     shared_string_type& blobName,
                     shared_tx_ctx_type& txDesc);

    void getBlob(handle_type const& requestId,
                 shared_string_type& domainName,
                 shared_string_type& volumeName,
                 shared_string_type& blobName,
                 shared_int_type& length,
                 shared_offset_type& objectOffset);

    void getBlobWithMeta(handle_type const& requestId,
                         shared_string_type& domainName,
                         shared_string_type& volumeName,
                         shared_string_type& blobName,
                         shared_int_type& length,
                         shared_offset_type& objectOffset);

    void renameBlob(handle_type const& requestId,
                    shared_string_type& domainName,
                    shared_string_type& volumeName,
                    shared_string_type& sourceBlobName,
                    shared_string_type& destinationBlobName);

    void updateMetadata(handle_type const& requestId,
                        shared_string_type& domainName,
                        shared_string_type& volumeName,
                        shared_string_type& blobName,
                        shared_tx_ctx_type& txDesc,
                        shared_meta_type& metadata);

    void updateBlobOnce(handle_type const& requestId,
                        shared_string_type& domainName,
                        shared_string_type& volumeName,
                        shared_string_type& blobName,
                        shared_int_type& blobMode,
                        shared_buffer_type& bytes,
                        shared_int_type& length,
                        shared_offset_type& objectOffset,
                        shared_meta_type& metadata);

    void updateBlob(handle_type const& requestId,
                    shared_string_type& domainName,
                    shared_string_type& volumeName,
                    shared_string_type& blobName,
                    shared_tx_ctx_type& txDesc,
                    shared_buffer_type& bytes,
                    shared_int_type& length,
                    shared_offset_type& objectOffset);

    void deleteBlob(handle_type const& requestId,
                    shared_string_type& domainName,
                    shared_string_type& volumeName,
                    shared_string_type& blobName,
                    shared_tx_ctx_type& txDesc);
};

}  // namespace fds

#endif  // SOURCE_ACCESS_MGR_INCLUDE_AMASYNCDATAAPI_H_
