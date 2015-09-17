/*
 * Copyright 2014 by Formation Data Systems, Inc.
 */
#ifndef SOURCE_ACCESS_MGR_INCLUDE_CONNECTOR_NBD_NBDOPERATIONS_H_
#define SOURCE_ACCESS_MGR_INCLUDE_CONNECTOR_NBD_NBDOPERATIONS_H_

#include <deque>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/enable_shared_from_this.hpp>
#include <boost/lockfree/spsc_queue.hpp>

#include "fds_types.h"
#include "concurrency/RwLock.h"
#include "fdsp/xdi_types.h"
#include "concurrency/Mutex.h"
#include "AmAsyncResponseApi.h"
#include "AmAsyncDataApi.h"
#include "connector/SectorLockMap.h"

namespace fds {

struct NbdResponseVector {
    using buffer_type = std::string;
    using buffer_ptr_type = boost::shared_ptr<buffer_type>;
    enum NbdOperation {
        READ = 0,
        WRITE = 1
    };

    NbdResponseVector(int64_t hdl, NbdOperation op,
                      uint64_t off, uint32_t len, uint32_t maxOSize,
                      uint32_t objCnt = 1)
      : handle(hdl), operation(op), doneCount(0), offset(off), length(len),
        maxObjectSizeInBytes(maxOSize), objCount(objCnt), opError(ERR_OK)
    {
        bufVec.reserve(objCount);
        offVec.reserve(objCount);
    }

    ~NbdResponseVector() {}
    typedef boost::shared_ptr<NbdResponseVector> shared_ptr;

    fds_bool_t isRead() const { return (operation == READ); }
    inline fds_int64_t getHandle() const { return handle; }
    inline Error getError() const { return opError; }
    inline fds_uint64_t getOffset() const { return offset; }
    inline fds_uint32_t getLength() const { return length; }
    inline fds_uint32_t maxObjectSize() const { return maxObjectSizeInBytes; }

    buffer_ptr_type getNextReadBuffer(fds_uint32_t& context) {
        if (context >= bufVec.size()) {
            return nullptr;
        }
        return bufVec[context++];
    }

    void keepBufferForWrite(fds_uint32_t const seqId,
                            fds_uint64_t const objectOff,
                            buffer_ptr_type& buf) {
        fds_assert(WRITE == operation);
        bufVec.emplace_back(buf);
        offVec.emplace_back(objectOff);
    }

    fds_uint64_t getOffset(fds_uint32_t const seqId) const {
        return offVec[seqId];
    }

    buffer_ptr_type getBuffer(fds_uint32_t const seqId) const {
        return bufVec[seqId];
    }

    /**
     * \return true if all responses were received or operation error
     */
    void handleReadResponse(std::vector<buffer_ptr_type>& buffers,
                            fds_uint32_t len);

    /**
     * \return true if all responses were received
     */
    fds_bool_t handleWriteResponse(const Error& err) {
        fds_verify(operation == WRITE);
        if (!err.ok()) {
            // Note, we're always setting the most recent
            // responses's error code.
            opError = err;
        }
        uint32_t doneCnt = doneCount.fetch_add(1);
        return ((doneCnt + 1) == objCount);
    }

    /**
     * Handle read response for read-modify-write
     * \return true if all responses were received or operation error
     */
    std::pair<Error, buffer_ptr_type>
        handleRMWResponse(buffer_ptr_type const& retBuf,
                          fds_uint32_t len,
                          fds_uint32_t seqId,
                          const Error& err);

    fds_int64_t handle;

    // These are the responses we are also in charge of responding to, in order
    // with ourselves being last.
    std::unordered_map<fds_uint32_t, std::deque<NbdResponseVector*>> chained_responses;

  private:
    NbdOperation operation;
    std::atomic_uint doneCount;
    fds_uint32_t objCount;

    // error of the operation
    Error opError;

    // to collect read responses or first and last buffer for write op
    std::vector<buffer_ptr_type> bufVec;
    // when writing, we need to remember the object offsets for rwm buffers
    std::vector<fds_uint64_t> offVec;

    // offset
    fds_uint64_t offset;
    fds_uint32_t length;
    fds_uint32_t maxObjectSizeInBytes;
};

// Response interface for NbdOperations
struct NbdOperationsResponseIface {
    NbdOperationsResponseIface() = default;
    NbdOperationsResponseIface(NbdOperationsResponseIface const&) = delete;
    NbdOperationsResponseIface& operator=(NbdOperationsResponseIface const&) = delete;
    NbdOperationsResponseIface(NbdOperationsResponseIface const&&) = delete;
    NbdOperationsResponseIface& operator=(NbdOperationsResponseIface const&&) = delete;
    virtual ~NbdOperationsResponseIface() = default;

    virtual void readWriteResp(NbdResponseVector* response) = 0;
    virtual void attachResp(Error const& error, boost::shared_ptr<VolumeDesc> const& volDesc) = 0;
    virtual void terminate() = 0;
};

struct HandleSeqPair {
    int64_t handle;
    uint32_t seq;
};

class NbdOperations
    :   public boost::enable_shared_from_this<NbdOperations>,
        public AmAsyncResponseApi<HandleSeqPair>
{
    using req_api_type = AmAsyncDataApi<HandleSeqPair>;
    using resp_api_type = AmAsyncResponseApi<HandleSeqPair>;

    typedef resp_api_type::handle_type handle_type;
    typedef SectorLockMap<handle_type, 1024> sector_type;
    typedef std::unordered_map<fds_int64_t, NbdResponseVector*> response_map_type;
  public:
    explicit NbdOperations(NbdOperationsResponseIface* respIface);
    explicit NbdOperations(NbdOperations const& rhs) = delete;
    NbdOperations& operator=(NbdOperations const& rhs) = delete;
    ~NbdOperations() = default;

    typedef boost::shared_ptr<NbdOperations> shared_ptr;
    void init(req_api_type::shared_string_type vol_name,
              std::shared_ptr<AmProcessor> processor);

    void read(fds_uint32_t length, fds_uint64_t offset, fds_int64_t handle);

    void write(req_api_type::shared_buffer_type& bytes, fds_uint32_t length, fds_uint64_t offset, fds_int64_t handle);

    void attachVolumeResp(const resp_api_type::error_type &error,
                          handle_type& requestId,
                          resp_api_type::shared_vol_descriptor_type& volDesc,
                          resp_api_type::shared_vol_mode_type& mode) override;

    void detachVolumeResp(const resp_api_type::error_type &error,
                          handle_type& requestId) override;

    // The two response types we do support
    void getBlobResp(const resp_api_type::error_type &error,
                     handle_type& requestId,
                     const resp_api_type::shared_buffer_array_type& bufs,
                     resp_api_type::size_type& length) override;

    void updateBlobResp(const resp_api_type::error_type &error, handle_type& requestId) override;

    void shutdown();

  private:
    void finishResponse(NbdResponseVector* response);

    void drainUpdateChain(fds_uint64_t const offset,
                          NbdResponseVector::buffer_ptr_type buf,
                          handle_type* queued_handle_ptr,
                          Error const error);

    fds_uint32_t getObjectCount(fds_uint32_t length,
                                fds_uint64_t offset);

    void detachVolume();

    // api we've built
    std::unique_ptr<req_api_type> amAsyncDataApi;
    boost::shared_ptr<std::string> volumeName;
    fds_uint32_t maxObjectSizeInBytes;

    // interface to respond to nbd passed down in constructor
    std::unique_ptr<NbdOperationsResponseIface> nbdResp;
    bool shutting_down {false};

    // for all reads/writes to AM
    boost::shared_ptr<std::string> blobName;
    boost::shared_ptr<std::string> domainName;
    boost::shared_ptr<fds_int32_t> blobMode;
    boost::shared_ptr< std::map<std::string, std::string> > emptyMeta;

    // for now we are supporting <=4K requests
    // so keep current handles for which we are waiting responses
    fds_mutex respLock;
    response_map_type responses;

    sector_type sector_map;

    // AmAsyncResponseApi un-implemented responses
    void abortBlobTxResp       (const resp_api_type::error_type &, handle_type&) override {}
    void commitBlobTxResp      (const resp_api_type::error_type &, handle_type&) override {}
    void deleteBlobResp        (const resp_api_type::error_type &, handle_type&) override {}
    void getBlobWithMetaResp   (const resp_api_type::error_type &, handle_type&, const resp_api_type::shared_buffer_array_type&, resp_api_type::size_type&, resp_api_type::shared_descriptor_type&) override {}  // NOLINT
    void startBlobTxResp       (const resp_api_type::error_type &, handle_type&, resp_api_type::shared_tx_ctx_type&) override {}  // NOLINT
    void statBlobResp          (const resp_api_type::error_type &, handle_type&, shared_descriptor_type&) override {}  // NOLINT
    void updateBlobOnceResp    (const resp_api_type::error_type &, handle_type&) override {}
    void updateMetadataResp    (const resp_api_type::error_type &, handle_type&) override {}
    void renameBlobResp        (const resp_api_type::error_type &, handle_type&, shared_descriptor_type&) override {}
    void volumeContentsResp    (const resp_api_type::error_type &, handle_type&, shared_descriptor_vec_type&) override {}  // NOLINT
    void volumeStatusResp      (const resp_api_type::error_type &, handle_type&, shared_status_type&) override {}  // NOLINT
    void setVolumeMetadataResp (const resp_api_type::error_type &, handle_type&) override {}  // NOLINT
    void getVolumeMetadataResp (const resp_api_type::error_type &, handle_type&, shared_meta_type&) override {}  // NOLINT
};

}  // namespace fds

#endif  // SOURCE_ACCESS_MGR_INCLUDE_CONNECTOR_NBD_NBDOPERATIONS_H_