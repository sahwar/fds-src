/**
 * Copyright 2014 by Formation Data Systems, Inc.
 */

#include <string>
#include <NbdOperations.h>

namespace fds {

NbdOperations::NbdOperations(AmAsyncDataApi::shared_ptr& amApi,
                             NbdOperationsResponseIface* respIface)
        : amAsyncDataApi(amApi),
          responseApi(this),
          nbdResp(respIface),
          domainName(new std::string("TestDomain")),
          blobName(new std::string("BlockBlob")) {
    amApi->setResponseApi(responseApi);
}

NbdOperations::~NbdOperations() {
}

void
NbdOperations::read(boost::shared_ptr<std::string>& volumeName,
                    fds_uint32_t maxObjectSizeInBytes,
                    fds_uint32_t length,
                    fds_uint64_t offset,
                    fds_int64_t handle) {
    // calculate how many object we will get from AM
    fds_uint32_t objCount = length / maxObjectSizeInBytes;
    if ((length % maxObjectSizeInBytes) != 0) {
        ++objCount;
    }

    LOGDEBUG << "Will read " << length << " bytes " << offset
             << " offset for volume " << *volumeName
             << " object count " << objCount
             << " handle " << handle
             << " max object size " << maxObjectSizeInBytes << " bytes";

    // we will wait for responses
    NbdResponseVector* resp = new NbdResponseVector(handle,
                                                    NbdResponseVector::READ,
                                                    objCount);

    {   // add response that we will fill in with data
        fds_mutex::scoped_lock l(respLock);
        fds_verify(responses.count(handle) == 0);
        responses[handle] = resp;
    }

    // break down request into max obj size chunks and send to AM
    fds_uint32_t amBytesRead = 0;
    fds_int32_t seqId = 0;
    while (amBytesRead < length) {
        fds_uint64_t curOffset = offset + amBytesRead;
        fds_uint64_t objectOff = curOffset / maxObjectSizeInBytes;
        fds_uint32_t curLength = length - amBytesRead;
        if (curLength > maxObjectSizeInBytes) {
            curLength = maxObjectSizeInBytes;
        }
        boost::shared_ptr<int32_t> blobLength = boost::make_shared<int32_t>(curLength);
        boost::shared_ptr<apis::ObjectOffset> off(new apis::ObjectOffset());
        off->value = objectOff;

        boost::shared_ptr<apis::RequestId> reqId(
            boost::make_shared<apis::RequestId>());
        // request id is 64 bit of handle + 32 bit of sequence Id
        reqId->id = std::to_string(handle) + ":" + std::to_string(seqId);

        // blob name for block?
        LOGDEBUG << "getBlob length " << curLength << " offset " << curOffset
                 << " object offset " << off
                 << " volume " << volumeName << " reqId " << reqId->id;
        try {
            amAsyncDataApi->getBlob(reqId,
                                    domainName,
                                    volumeName,
                                    blobName,
                                    blobLength,
                                    off);
            amBytesRead += curLength;
            ++seqId;
        } catch(apis::ApiException fdsE) {
            // return error
            nbdResp->readResp(ERR_DISK_READ_FAILED, handle, NULL);
            fds_mutex::scoped_lock l(respLock);
            if (responses.count(handle) > 0) {
                NbdResponseVector* delResp = responses[handle];
                responses[handle] = NULL;
                delete delResp;
                responses.erase(handle);
            }
            return;
        }
    }
}

/*
void
NbdOperations::write(boost::shared_ptr<std::string>& volumeName,
                     boost::shared_ptr<std::string>& bytes,
                     fds_uint32_t length,
                     fds_uint64_t offset,
                     fds_int64_t handle) {
    // hardcoding domain name
    boost::shared_ptr<std::string> domainName(new std::string("TestDomain"));
    fds_uint32_t objectSize = 4096;  // hardcoding 4K max obj size

    // calculate how many object we will put
    fds_uint32_t objCount = length / objectSize;
    if ((length % objectSize) != 0) {
        ++objCount;
    }

    LOGDEBUG << "Will write " << length << " bytes " << offset
             << " offset for volume " << *volumeName
             << " object count " << objCount
             << " handle " << handle;

    fds_uint32_t amBytesWritten = 0;
    while (amBytesWritten < len) {
        fds_uint64_t curOffset = offset + amBytesWritten;
        fds_uint64_t objectOff = curOffset / objectSize;
        fds_uint32_t curLength = length - amBytesRead;
        if (curLength > objectSize) {
            curLength = objectSize;
        }
        boost::shared_ptr<int32_t> blobLength = boost::make_shared<int32_t>(curLength);
        boost::shared_ptr<apis::ObjectOffset> off(new apis::ObjectOffset());
        off->value = objectOff;

        // see if we need to read-modify-write this object
    }
}
*/

void
NbdOperations::getBlobResp(const Error &error,
                           boost::shared_ptr<apis::RequestId>& requestId,
                           char* buf,
                           fds_uint32_t& length) {
    NbdResponseVector* resp = NULL;
    fds_int64_t handle = 0;
    fds_int32_t seqId = 0;
    parseRequestId(requestId, &handle, &seqId);

    LOGDEBUG << "Reponse for getBlob, " << length << " bytes "
             << error << ", handle " << handle
             << " seqId " << seqId << " ( "
             << requestId->id << " )";

    {
        fds_mutex::scoped_lock l(respLock);
        // if we are not waiting for this response, we probably already
        // returned an error
        if (responses.count(handle) == 0) {
            LOGWARN << "Not waiting for response for handle " << handle
                    << ", check if we returned an error";
            return;
        }
        // get response
        resp = responses[handle];
    }

    // add buffer to the response list
    fds_verify(resp);
    fds_bool_t done = resp->handleReadResponse(buf, length, seqId);
    if (done) {
        // we are done collecting responses for this handle, notify nbd connector
        nbdResp->readResp(error, handle, resp);
        // nbd connector will free resp
        // remove from the wait list
        fds_mutex::scoped_lock l(respLock);
        responses.erase(handle);
    }
}

void
NbdOperations::parseRequestId(boost::shared_ptr<apis::RequestId>& requestId,
                              fds_int64_t* handle,
                              fds_int32_t* seqId) {
    std::string delim(":");
    size_t start = 0;
    size_t end = (requestId->id).find(delim, start);
    fds_verify(end != std::string::npos);
    std::string handleStr = (requestId->id).substr(start,
                                                   end - start);
    std::string seqIdStr = (requestId->id).substr(end + delim.size());

    // return
    *handle = std::stoll(handleStr);
    *seqId = std::stol(seqIdStr);
}

}  // namespace fds
