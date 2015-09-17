/*
 * Copyright 2015 Formation Data Systems, Inc.
 */

#ifndef SOURCE_ACCESSMGR_INCLUDE_CONNECTOR_SCST_SCSTTASK_H_
#define SOURCE_ACCESSMGR_INCLUDE_CONNECTOR_SCST_SCSTTASK_H_

#include <atomic>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/shared_ptr.hpp>

#include "fds_error.h"
#include "fds_assert.h"

#include "connector/scst/scst_user.h"

namespace fds
{

struct ScstTask {
    using buffer_type = std::string;
    using buffer_ptr_type = boost::shared_ptr<buffer_type>;
    enum ScstOperation {
        OTHER = 0,
        READ = 1,
        WRITE = 2
    };

    ScstTask(uint32_t hdl, uint32_t sc);
    ~ScstTask() = default;

    bool isRead() const { return (operation == READ); }
    inline uint32_t getHandle() const { return reply.cmd_h; }
    inline uint32_t getSubcode() const { return reply.subcode; }
    inline Error getError() const { return opError; }
    inline uint64_t getOffset() const { return offset; }
    inline uint32_t getLength() const { return length; }
    inline uint32_t maxObjectSize() const { return maxObjectSizeInBytes; }
    inline void setObjectCount(size_t const count) {
        objCount = count;
        bufVec.reserve(count);
        offVec.reserve(count);
    }

    void setRead(uint64_t const off, uint32_t const bytes) {
        operation = READ;
        offset = off;
        length = bytes;
    }

    void setWrite(uint64_t const off, uint32_t const bytes) {
        operation = WRITE;
        offset = off;
        length = bytes;
    }

    void setMaxObjectSize(uint32_t const size) { maxObjectSizeInBytes = size; };

    void checkCondition(uint8_t const key, uint8_t const asc, uint8_t const ascq)
    {
        sense_buffer[0] = 0x70;
        sense_buffer[2] = key;
        sense_buffer[7] = 0x0a;
        sense_buffer[12] = asc;
        sense_buffer[13] = ascq;

        reply.exec_reply.status = SAM_STAT_CHECK_CONDITION;
        reply.exec_reply.sense_len = 18;
        reply.exec_reply.psense_buffer = (unsigned long)&sense_buffer;
    }

    void setResponseBuffer(std::unique_ptr<uint8_t[]>& buf, size_t buf_len)
    {
        fds_assert(buf);
        pbuffer.swap(buf);
        reply.exec_reply.pbuf = (unsigned long)pbuffer.get();
        reply.exec_reply.resp_data_len = buf_len;
    }

    void setResult(int32_t result) { reply.result = result; }

    unsigned long getReply() { return (unsigned long)&reply; } 

    buffer_ptr_type getNextReadBuffer(uint32_t& context) {
        if (context >= bufVec.size()) {
            return nullptr;
        }
        return bufVec[context++];
    }

    void keepBufferForWrite(uint32_t const seqId,
                            uint64_t const objectOff,
                            buffer_ptr_type& buf) {
        fds_assert(WRITE == operation);
        bufVec.emplace_back(buf);
        offVec.emplace_back(objectOff);
    }

    uint64_t getOffset(uint32_t const seqId) const {
        return offVec[seqId];
    }

    buffer_ptr_type getBuffer(uint32_t const seqId) const {
        return bufVec[seqId];
    }

    /**
     * \return true if all responses were received or operation error
     */
    void handleReadResponse(std::vector<buffer_ptr_type>& buffers,
                            uint32_t len);

    /**
     * \return true if all responses were received
     */
    bool handleWriteResponse(const Error& err) {
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
                          uint32_t len,
                          uint32_t seqId,
                          const Error& err);

    // These are the responses we are also in charge of responding to, in order
    // with ourselves being last.
    std::unordered_map<uint32_t, std::deque<ScstTask*>> chained_responses;

  private:
    scst_user_reply_cmd reply {};

    ScstOperation operation {OTHER};
    std::atomic_uint doneCount {0};
    uint32_t objCount {1};

    // error of the operation
    Error opError {ERR_OK};

    // pbuf pointer for cmds (RAII)
    uint8_t sense_buffer[18] {};
    std::unique_ptr<uint8_t[]> pbuffer;

    // to collect read responses or first and last buffer for write op
    std::vector<buffer_ptr_type> bufVec;
    // when writing, we need to remember the object offsets for rwm buffers
    std::vector<uint64_t> offVec;

    // offset
    uint64_t offset {0};
    uint32_t length {0};
    uint32_t maxObjectSizeInBytes {0};
};

}  // namespace fds

#endif  // SOURCE_ACCESSMGR_INCLUDE_CONNECTOR_SCST_SCSTTASK_H_