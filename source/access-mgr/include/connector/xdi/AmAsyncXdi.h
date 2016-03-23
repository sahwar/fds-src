/*
 * Copyright 2013-2014 Formation Data Systems, Inc.
 */

#ifndef SOURCE_ACCESS_MGR_INCLUDE_AMASYNCXDI_H_
#define SOURCE_ACCESS_MGR_INCLUDE_AMASYNCXDI_H_

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "util/Log.h"

#include "AmAsyncResponseApi.h"
#include "AmAsyncDataApi.h"
#include "fdsp/AsyncXdiServiceResponse.h"
#include "fdsp/AsyncXdiServiceRequest.h"

namespace fds
{

class AmAsyncXdiResponse : public AmAsyncResponseApi {
    using client_handle_type = boost::shared_ptr<apis::RequestId>;
 public:
    using client_type = apis::AsyncXdiServiceResponseClient;
    using client_ptr = std::shared_ptr<client_type>;
    using client_map = std::unordered_map<std::string, client_ptr>;
    using handle_map = std::unordered_map<size_t, client_handle_type>;

    uint64_t register_request(client_handle_type const& handle) {
        std::lock_guard<std::mutex> g(client_lock);
        auto hdl = std::hash<std::string>()(handle->id);
        if (0 < request_id_map.count(hdl)) {
            return -1;
        }
        request_id_map[hdl] = handle;
        LOGTRACE << "handle:" << hdl
                 << " id:" << handle->id << " registering";
        return hdl;
    }

 private:
    using api_type = AmAsyncResponseApi;

    // We use a std rw lock here and vector or client pointers because
    // this lookup only happens once when the handshake is performed
    static std::mutex map_lock;
    static client_map clients;
    static constexpr size_t max_response_retries {1};

    // Each Response has a unique set of outstanding response, here they are
    handle_map request_id_map;

    /// Thrift client to response to XDI
    std::mutex client_lock;
    client_ptr asyncRespClient;
    std::string serverIp;
    uint32_t serverPort;

    void initiateClientConnect();

    client_handle_type complete_request(RequestHandle const& handle)
    {
        auto it = request_id_map.find(handle.handle);
        if (request_id_map.end() == it) {
            LOGNOTIFY << "handle:" << handle.handle << " unknown";
            return nullptr;
        }
        auto request_id = std::move(it->second);
        request_id_map.erase(it);
        LOGTRACE << "id:" << request_id->id << " complete";
        return request_id;
    }

    template<typename ... Args>
    void xdiClientCall(void (client_type::*func)(client_handle_type&, Args...), RequestHandle const& handle, Args&&... args) {
        using transport_exception = apache::thrift::transport::TTransportException;
        std::lock_guard<std::mutex> g(client_lock);
        auto request_id = complete_request(handle);
        if (!request_id) return;
        for (auto i = max_response_retries; i >= 0; --i) {
        try {
            if (!asyncRespClient) {
                initiateClientConnect();
            }
            // Invoke the thrift method on our client
            return ((asyncRespClient.get())->*(func))(request_id, std::forward<Args>(args)...);
        } catch(const transport_exception& e) {
            // Reset the pointer and re-try (if we have any left)
            try {
            initiateClientConnect();
            } catch (const transport_exception& e) {
                // ugh, Xdi probably died or we've become partitioned
                // assume we'll never return this response
                break;
            }
        }
        }
        LOGERROR << "server:tcp://" << serverIp << ":" << serverPort << " unable to respond";
    }

  public:
    explicit AmAsyncXdiResponse(std::string const& server_ip);
    virtual ~AmAsyncXdiResponse();
    typedef boost::shared_ptr<AmAsyncXdiResponse> shared_ptr;

    // This only belongs to the Thrift interface, not the AmAsyncData interface
    // to setup the response port.
    void handshakeComplete(api_type::handle_type const& requestId,
                           boost::shared_ptr<int32_t>& port);

    void attachVolumeResp(const api_type::error_type &error,
                          api_type::handle_type const& requestId,
                          api_type::shared_vol_descriptor_type& volDesc,
                          api_type::shared_vol_mode_type& mode) override;

    void detachVolumeResp(const api_type::error_type &error,
                          api_type::handle_type const& requestId) override;

    void startBlobTxResp(const api_type::error_type &error,
                         api_type::handle_type const& requestId,
                         boost::shared_ptr<apis::TxDescriptor>& txDesc) override;

    void abortBlobTxResp(const api_type::error_type &error,
                         api_type::handle_type const& requestId) override;

    void commitBlobTxResp(const api_type::error_type &error,
                          api_type::handle_type const& requestId) override;

    void updateBlobResp(const api_type::error_type &error,
                        api_type::handle_type const& requestId) override;

    void updateBlobOnceResp(const api_type::error_type &error,
                            api_type::handle_type const& requestId) override;

    void updateMetadataResp(const api_type::error_type &error,
                            api_type::handle_type const& requestId) override;

    void deleteBlobResp(const api_type::error_type &error,
                        api_type::handle_type const& requestId) override;

    void statBlobResp(const api_type::error_type &error,
                      api_type::handle_type const& requestId,
                      api_type::shared_descriptor_type& blobDesc) override;

    void volumeStatusResp(const api_type::error_type &error,
                          api_type::handle_type const& requestId,
                          api_type::shared_status_type& volumeStatus) override;

    void volumeContentsResp(const api_type::error_type &error,
                            api_type::handle_type const& requestId,
                            api_type::shared_descriptor_vec_type& volContents,
                            api_type::shared_string_vec_type& skippedPrefixes) override;

    void setVolumeMetadataResp(const api_type::error_type &error,
                               api_type::handle_type const& requestId) override;

    void getVolumeMetadataResp(const api_type::error_type &error,
                               api_type::handle_type const& requestId,
                               api_type::shared_meta_type& metadata) override;

    void getBlobResp(const api_type::error_type &error,
                     api_type::handle_type const& requestId,
                     const api_type::shared_buffer_array_type& bufs,
                     api_type::size_type& length) override;

    void getBlobWithMetaResp(const api_type::error_type &error,
                             api_type::handle_type const& requestId,
                             const api_type::shared_buffer_array_type& bufs,
                             api_type::size_type& length,
                             api_type::shared_descriptor_type& blobDesc) override;

    void renameBlobResp(const api_type::error_type &error,
                        api_type::handle_type const& requestId,
                        api_type::shared_descriptor_type& blobDesc) override;
};

// This structure provides one feature and is to implement the thrift interface
// which uses apis::RequestId as a handle and has a handshake method. We don't
// want to pollute the other connectors with things they don't want to use or
// need so I'm implemented handshake here and forwarded the rest of the
// requests which will probably be optimized out.
struct AmAsyncXdiRequest
    : public fds::apis::AsyncXdiServiceRequestIf,
      public AmAsyncDataApi
{
    using api_type = AmAsyncDataApi;
    using server_handle_type = boost::shared_ptr<apis::RequestId>;

    explicit AmAsyncXdiRequest(std::shared_ptr<AmProcessor> processor, boost::shared_ptr<AmAsyncResponseApi> response_api):
        api_type(processor, response_api)
    {}

    // This is only a Thrift interface, not a generic AmAsyncData one just to
    // setup the response port.
    void handshakeStart(server_handle_type& requestId, boost::shared_ptr<int32_t>& portNumber)  // NOLINT
    {
        auto handle = get_handle(requestId);
        auto api = boost::dynamic_pointer_cast<AmAsyncXdiResponse>(responseApi);
        if (api)
            api->handshakeComplete(handle, portNumber);
    }

    static
    void logio(char const* op,
          server_handle_type& handle,
          api_type::shared_string_type& volName)
    { LOGIO << "op:" << op << " handle:" << handle->id << " vol:" << *volName; }

    static
    void logio(char const* op,
          server_handle_type& handle,
          api_type::shared_string_type& volName,
          api_type::shared_string_type& blobName)
    { LOGIO << "op:" << op << " handle:" << handle->id << "vol:" << *volName << " blob:" << *blobName; }

    static
    void logio(char const* op,
               server_handle_type& handle,
               api_type::shared_string_type& volName,
               api_type::shared_string_type& blobName,
               api_type::shared_int_type& length,
               api_type::shared_offset_type& offset)
    {
        LOGIO << "op:" << op
              << " handle:" << handle->id
              << " vol:" << *volName
              << " blob:" << *blobName
              << " offset:" << offset->value
              << " length:" << *length;
    }

    api_type::handle_type get_handle(server_handle_type& handle) {
        return api_type::handle_type { boost::dynamic_pointer_cast<AmAsyncXdiResponse>(responseApi)->register_request(handle), 0};
    }

    // These just forward to the generic template implementation in
    // AmAsyncDataApi.cxx
    void abortBlobTx(server_handle_type& requestId, api_type::shared_string_type& domainName, api_type::shared_string_type& volumeName, api_type::shared_string_type& blobName, api_type::shared_tx_ctx_type& txDesc)  // NOLINT
    { logio(__func__, requestId, volumeName, blobName); api_type::abortBlobTx(get_handle(requestId), domainName, volumeName, blobName, txDesc); }  // NOLINT
    void attachVolume(server_handle_type& requestId, api_type::shared_string_type& domainName, api_type::shared_string_type& volumeName, api_type::shared_vol_mode_type& mode)  // NOLINT
    { logio(__func__, requestId, volumeName); api_type::attachVolume(get_handle(requestId), domainName, volumeName, mode); }
    void commitBlobTx(server_handle_type& requestId, api_type::shared_string_type& domainName, api_type::shared_string_type& volumeName, api_type::shared_string_type& blobName, api_type::shared_tx_ctx_type& txDesc)  // NOLINT
    { logio(__func__, requestId, volumeName, blobName); api_type::commitBlobTx(get_handle(requestId), domainName, volumeName, blobName, txDesc); }
    void deleteBlob(server_handle_type& requestId, api_type::shared_string_type& domainName, api_type::shared_string_type& volumeName, api_type::shared_string_type& blobName, api_type::shared_tx_ctx_type& txDesc)  // NOLINT
    { logio(__func__, requestId, volumeName, blobName); api_type::deleteBlob(get_handle(requestId), domainName, volumeName, blobName, txDesc); }
    void getBlob(server_handle_type& requestId, api_type::shared_string_type& domainName, api_type::shared_string_type& volumeName, api_type::shared_string_type& blobName, api_type::shared_int_type& length, api_type::shared_offset_type& offset)  // NOLINT
    { logio(__func__, requestId, volumeName, blobName, length, offset); api_type::getBlob(get_handle(requestId), domainName, volumeName, blobName, length, offset); }  // NOLINT
    void getBlobWithMeta(server_handle_type& requestId, api_type::shared_string_type& domainName, api_type::shared_string_type& volumeName, api_type::shared_string_type& blobName, api_type::shared_int_type& length, api_type::shared_offset_type& offset)  // NOLINT
    { logio(__func__, requestId, volumeName, blobName, length, offset); api_type::getBlobWithMeta(get_handle(requestId), domainName, volumeName, blobName, length, offset); }  // NOLINT
    void renameBlob(server_handle_type& requestId, api_type::shared_string_type& domainName, api_type::shared_string_type& volumeName, api_type::shared_string_type& sourceBlobName, api_type::shared_string_type& destinationBlobName)  // NOLINT
    { logio(__func__, requestId, volumeName, sourceBlobName); api_type::renameBlob(get_handle(requestId), domainName, volumeName, sourceBlobName, destinationBlobName); }  // NOLINT
    void startBlobTx(server_handle_type& requestId, api_type::shared_string_type& domainName, api_type::shared_string_type& volumeName, api_type::shared_string_type& blobName, api_type::shared_int_type& blobMode)  // NOLINT
    { logio(__func__, requestId, volumeName, blobName); api_type::startBlobTx(get_handle(requestId), domainName, volumeName, blobName, blobMode); }
    void statBlob(server_handle_type& requestId, api_type::shared_string_type& domainName, api_type::shared_string_type& volumeName, api_type::shared_string_type& blobName)  // NOLINT
    { logio(__func__, requestId, volumeName, blobName); api_type::statBlob(get_handle(requestId), domainName, volumeName, blobName); }
    void updateBlob(server_handle_type& requestId, api_type::shared_string_type& domainName, api_type::shared_string_type& volumeName, api_type::shared_string_type& blobName, api_type::shared_tx_ctx_type& txDesc, api_type::shared_string_type& bytes, api_type::shared_int_type& length, api_type::shared_offset_type& objectOffset)  // NOLINT
    { logio(__func__, requestId, volumeName, blobName, length, objectOffset); api_type::updateBlob(get_handle(requestId), domainName, volumeName, blobName, txDesc, bytes, length, objectOffset); }   // NOLINT
    void updateBlobOnce(server_handle_type& requestId, api_type::shared_string_type& domainName, api_type::shared_string_type& volumeName, api_type::shared_string_type& blobName, api_type::shared_int_type& blobMode, api_type::shared_string_type& bytes, api_type::shared_int_type& length, api_type::shared_offset_type& objectOffset, api_type::shared_meta_type& metadata)  // NOLINT
    { logio(__func__, requestId, volumeName, blobName, length, objectOffset); api_type::updateBlobOnce(get_handle(requestId), domainName, volumeName, blobName, blobMode, bytes, length, objectOffset, metadata); }   // NOLINT
    void updateMetadata(server_handle_type& requestId, api_type::shared_string_type& domainName, api_type::shared_string_type& volumeName, api_type::shared_string_type& blobName, api_type::shared_tx_ctx_type& txDesc, api_type::shared_meta_type& metadata)  // NOLINT
    { logio(__func__, requestId, volumeName, blobName); api_type::updateMetadata(get_handle(requestId), domainName, volumeName, blobName, txDesc, metadata); }
    void volumeContents(server_handle_type& requestId, api_type::shared_string_type& domainName, api_type::shared_string_type& volumeName, api_type::shared_int_type& count, api_type::shared_size_type& offset, api_type::shared_string_type& pattern, boost::shared_ptr<fpi::PatternSemantics>& patternSems, boost::shared_ptr<fpi::BlobListOrder>& orderBy, api_type::shared_bool_type& descending, api_type::shared_string_type& delimiter)  // NOLINT
    { logio(__func__, requestId, volumeName); api_type::volumeContents(get_handle(requestId), domainName, volumeName, count, offset, pattern, patternSems, orderBy, descending, delimiter); }
    void volumeStatus(server_handle_type& requestId, api_type::shared_string_type& domainName, api_type::shared_string_type& volumeName)  // NOLINT
    { logio(__func__, requestId, volumeName); api_type::volumeStatus(get_handle(requestId), domainName, volumeName); }
    void setVolumeMetadata(server_handle_type& requestId, api_type::shared_string_type& domainName, api_type::shared_string_type& volumeName, api_type::shared_meta_type& metadata)  // NOLINT
    { logio(__func__, requestId, volumeName); api_type::setVolumeMetadata(get_handle(requestId), domainName, volumeName, metadata); }
    void getVolumeMetadata(server_handle_type& requestId, api_type::shared_string_type& domainName, api_type::shared_string_type& volumeName)  // NOLINT
    { logio(__func__, requestId, volumeName); api_type::getVolumeMetadata(get_handle(requestId), domainName, volumeName); }

    // TODO(bszmyd): Tue 13 Jan 2015 04:00:24 PM PST
    // Delete these when we can. These are the synchronous forwarding.
    void you_should_not_be_here()
    { return; }
    void abortBlobTx(const apis::RequestId& requestId, const std::string& domainName, const std::string& volumeName, const std::string& blobName, const apis::TxDescriptor& txDesc)  // NOLINT
    { you_should_not_be_here(); }
    void attachVolume(const apis::RequestId& requestId, const std::string& domainName, const std::string& volumeName, const fpi::VolumeAccessMode& mode)  // NOLINT
    { you_should_not_be_here(); }
    void commitBlobTx(const apis::RequestId& requestId, const std::string& domainName, const std::string& volumeName, const std::string& blobName, const apis::TxDescriptor& txDesc)  // NOLINT
    { you_should_not_be_here(); }
    void deleteBlob(const apis::RequestId& requestId, const std::string& domainName, const std::string& volumeName, const std::string& blobName, const apis::TxDescriptor& txDesc)  // NOLINT
    { you_should_not_be_here(); }
    void getBlob(const apis::RequestId& requestId, const std::string& domainName, const std::string& volumeName, const std::string& blobName, const int32_t length, const apis::ObjectOffset& offset)  // NOLINT
    { you_should_not_be_here(); }
    void getBlobWithMeta(const apis::RequestId& requestId, const std::string& domainName, const std::string& volumeName, const std::string& blobName, const int32_t length, const apis::ObjectOffset& offset)  // NOLINT
    { you_should_not_be_here(); }
    void renameBlob(const apis::RequestId& requestId, const std::string& domainName, const std::string& volumeName, const std::string& sourceBlobName, const std::string& destinationBlobName)  // NOLINT
    { you_should_not_be_here(); }
    void handshakeStart(const apis::RequestId& requestId, const int32_t portNumber)
    { you_should_not_be_here(); }
    void startBlobTx(const apis::RequestId& requestId, const std::string& domainName, const std::string& volumeName, const std::string& blobName, const int32_t blobMode)  // NOLINT
    { you_should_not_be_here(); }
    void statBlob(const apis::RequestId& requestId, const std::string& domainName, const std::string& volumeName, const std::string& blobName)  // NOLINT
    { you_should_not_be_here(); }
    void updateMetadata(const apis::RequestId& requestId, const std::string& domainName, const std::string& volumeName, const std::string& blobName, const apis::TxDescriptor& txDesc, const std::map<std::string, std::string> & metadata)  // NOLINT
    { you_should_not_be_here(); }
    void updateBlob(const apis::RequestId& requestId, const std::string& domainName, const std::string& volumeName, const std::string& blobName, const apis::TxDescriptor& txDesc, const std::string& bytes, const int32_t length, const apis::ObjectOffset& objectOffset)  // NOLINT
    { you_should_not_be_here(); }
    void updateBlobOnce(const apis::RequestId& requestId, const std::string& domainName, const std::string& volumeName, const std::string& blobName, const int32_t blobMode, const std::string& bytes, const int32_t length, const apis::ObjectOffset& objectOffset, const std::map<std::string, std::string> & metadata)  // NOLINT
    { you_should_not_be_here(); }
    void volumeContents(const apis::RequestId& requestId, const std::string& domainName, const std::string& volumeName, const int32_t count, const int64_t offset, const std::string& pattern, const fpi::PatternSemantics paternSems, const fpi::BlobListOrder orderBy, const bool descending, const std::string& delimiter)  // NOLINT
    { you_should_not_be_here(); }
    void volumeStatus(const apis::RequestId& requestId, const std::string& domainName, const std::string& volumeName)  // NOLINT
    { you_should_not_be_here(); }
    void setVolumeMetadata(const apis::RequestId& requestId, const std::string& domainName, const std::string& volumeName, const std::map<std::string, std::string>& metadata)  // NOLINT
    { you_should_not_be_here(); }
    void getVolumeMetadata(const apis::RequestId& requestId, const std::string& domainName, const std::string& volumeName)  // NOLINT
    { you_should_not_be_here(); }
};

}  // namespace fds

#endif  // SOURCE_ACCESS_MGR_INCLUDE_AMASYNCXDI_H_
