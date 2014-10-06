/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#ifndef SOURCE_ACCESS_MGR_STOR_HVISOR_HANDLER_HANDLER_H_
#define SOURCE_ACCESS_MGR_STOR_HVISOR_HANDLER_HANDLER_H_

#include <string>

#include <fds_error.h>
#include <fds_types.h>
#include <util/Log.h>
#include <fdsp/FDSP_MetaDataPathReq.h>
#include <fdsp/FDSP_MetaDataPathResp.h>
#include <StorHvVolumes.h>
#include <net/SvcRequestPool.h>
class StorHvCtrl;
#define STORHANDLER(CLASS, IOTYPE) \
    static_cast<CLASS*>(storHvisor->handlers.at(IOTYPE))
namespace fds {
/**
 * ------ NOTE :: IMPORTANT ---
 * do NOT store any state in these classes for now.
 * handler functions should be reentrant 
 */

struct Handler: HasLogger {
    StorHvCtrl* storHvisor = NULL;
    explicit Handler(StorHvCtrl* storHvisor) : storHvisor(storHvisor) {}

    // providing a default empty implmentation to support requests that
    // do not need queuing
    virtual fds::Error handleQueueItem(AmQosReq *qosReq) {
        return ERR_OK;
    }
    virtual ~Handler() {}
};

struct GetVolumeMetaDataHandler : Handler {
    explicit GetVolumeMetaDataHandler(StorHvCtrl* storHvisor) : Handler(storHvisor) {}
    fds::Error handleRequest(const std::string& volumeName, CallbackPtr cb);
    fds::Error handleResponse(fpi::FDSP_MsgHdrTypePtr& header,
                              fpi::FDSP_VolumeMetaDataPtr& volumeMeta);
    fds::Error handleQueueItem(AmQosReq *qosReq);
};

struct StatBlobHandler : Handler {
    explicit StatBlobHandler(StorHvCtrl* storHvisor) : Handler(storHvisor) {}
    fds::Error handleRequest(const std::string& volumeName,
                             const std::string& blobName,
                             CallbackPtr cb);
    fds::Error handleResponse(AmQosReq *qosReq,
                              FailoverSvcRequest* svcReq,
                              const Error& error,
                              boost::shared_ptr<std::string> payload);
    fds::Error handleQueueItem(AmQosReq *qosReq);
};

struct GetBucketHandler : Handler {
    explicit GetBucketHandler(StorHvCtrl* storHvisor) : Handler(storHvisor) {}
    fds::Error handleRequest(BucketContext* bucket_context,
                             fds_uint32_t start,
                             fds_uint32_t maxkeys,
                             CallbackPtr cb);
    fds::Error handleResponse(AmQosReq *qosReq,
                              FailoverSvcRequest* svcReq,
                              const Error& error,
                              boost::shared_ptr<std::string> payload);
    fds::Error handleQueueItem(AmQosReq *qosReq);
};

struct DeleteBlobHandler : Handler {
    explicit DeleteBlobHandler(StorHvCtrl* storHvisor) : Handler(storHvisor) {}
    fds::Error handleRequest(const std::string& volumeName,
                             const std::string& blobName,
                             BlobTxId::ptr blobTxId,
                             CallbackPtr cb);
    fds::Error handleResponse(AmQosReq *qosReq,
                              QuorumSvcRequest* svcReq,
                              const Error& error,
                              boost::shared_ptr<std::string> payload);
    fds::Error handleQueueItem(AmQosReq *qosReq);
};


}  // namespace fds
#endif  // SOURCE_ACCESS_MGR_STOR_HVISOR_HANDLER_HANDLER_H_