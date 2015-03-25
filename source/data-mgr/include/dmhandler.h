/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#ifndef SOURCE_DATA_MGR_INCLUDE_DMHANDLER_H_
#define SOURCE_DATA_MGR_INCLUDE_DMHANDLER_H_

#include <string>
#include <fds_error.h>
#include <fds_types.h>
#include <util/Log.h>
#include <net/PlatNetSvcHandler.h>
#include <net/SvcRequestPool.h>
#include <net/SvcMgr.h>
#include <fds_module_provider.h>
#include <DmIoReq.h>
#include <DmBlobTypes.h>

#define DMHANDLER(CLASS, IOTYPE) \
    static_cast<CLASS*>(dataMgr->handlers.at(IOTYPE))

#define REGISTER_DM_MSG_HANDLER(FDSPMsgT, func) \
    REGISTER_FDSP_MSG_HANDLER_GENERIC(MODULEPROVIDER()->getSvcMgr()->getSvcRequestHandler(), \
            FDSPMsgT, func)

#define DM_SEND_ASYNC_RESP(...) \
    MODULEPROVIDER()->getSvcMgr()->getSvcRequestHandler()->sendAsyncResp(__VA_ARGS__)

#define HANDLE_INVALID_TX_ID() \
    if (BlobTxId::txIdInvalid == message->txId) { \
        LOGWARN << "Received invalid tx id with" << logString(*message); \
        handleResponse(asyncHdr, message, ERR_DM_INVALID_TX_ID, nullptr); \
        return; \
    }

#define HANDLE_U_TURN() \
    if (dataMgr->testUturnAll) { \
        LOGNOTIFY << "Uturn testing" << logString(*message); \
        handleResponse(asyncHdr, message, ERR_OK, nullptr); \
        return; \
    }

namespace fds { namespace dm {
/**
 * ------ NOTE :: IMPORTANT ---
 * do NOT store any state in these classes for now.
 * handler functions should be reentrant 
 */
struct RequestHelper {
    dmCatReq *dmRequest;
    explicit RequestHelper(dmCatReq *dmRequest);
    ~RequestHelper();
};

struct QueueHelper {
    fds_bool_t ioIsMarkedAsDone;
    fds_bool_t cancelled;
    fds_bool_t skipImplicitCb;
    dmCatReq *dmRequest;
    Error err = ERR_OK;
    explicit QueueHelper(dmCatReq *dmRequest);
    ~QueueHelper();
    void markIoDone();
    void cancel();
};


struct Handler: HasLogger {
    // providing a default empty implmentation to support requests that
    // do not need queuing
    virtual void handleQueueItem(dmCatReq *dmRequest);
    virtual void addToQueue(dmCatReq *dmRequest);
    virtual ~Handler();
};

struct GetBucketHandler : Handler {
    GetBucketHandler();
    void handleRequest(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                       boost::shared_ptr<fpi::GetBucketMsg>& message);
    void handleQueueItem(dmCatReq *dmRequest);
    void handleResponse(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                        boost::shared_ptr<fpi::GetBucketRspMsg>& message,
                        const Error &e, dmCatReq *dmRequest);
};

struct DmSysStatsHandler : Handler {
    DmSysStatsHandler();
    void handleRequest(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                       boost::shared_ptr<fpi::GetDmStatsMsg>& message);
    void handleQueueItem(dmCatReq *dmRequest);
    void handleResponse(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                        boost::shared_ptr<fpi::GetDmStatsMsg>& message,
                        const Error &e, dmCatReq *dmRequest);
};

struct DeleteBlobHandler : Handler {
    DeleteBlobHandler();
    void handleRequest(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                       boost::shared_ptr<fpi::DeleteBlobMsg>& message);
    void handleQueueItem(dmCatReq *dmRequest);
    void handleResponse(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                        boost::shared_ptr<fpi::DeleteBlobMsg>& message,
                        const Error &e, dmCatReq *dmRequest);
};

struct UpdateCatalogHandler : Handler {
    UpdateCatalogHandler();
    void handleRequest(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                       boost::shared_ptr<fpi::UpdateCatalogMsg> & message);
    void handleQueueItem(dmCatReq * dmRequest);
    void handleResponse(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                        boost::shared_ptr<fpi::UpdateCatalogMsg> & message,
                        const Error &e, dmCatReq *dmRequest);
};

struct GetBlobMetaDataHandler : Handler {
    GetBlobMetaDataHandler();
    void handleRequest(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                       boost::shared_ptr<fpi::GetBlobMetaDataMsg>& message);
    void handleQueueItem(dmCatReq* dmRequest);
    void handleResponse(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                        boost::shared_ptr<fpi::GetBlobMetaDataMsg> & message,
                        Error const& e, dmCatReq* dmRequest);
};

struct QueryCatalogHandler : Handler {
    QueryCatalogHandler();
    void handleRequest(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                       boost::shared_ptr<fpi::QueryCatalogMsg>& message);
    void handleQueueItem(dmCatReq* dmRequest);
    void handleResponse(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                        boost::shared_ptr<fpi::QueryCatalogMsg>& message,
                        Error const& e, dmCatReq* dmRequest);
};

struct StartBlobTxHandler : Handler {
    StartBlobTxHandler();
    void handleRequest(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                       boost::shared_ptr<fpi::StartBlobTxMsg>& message);
    void handleQueueItem(dmCatReq* dmRequest);
    void handleResponse(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                        boost::shared_ptr<fpi::StartBlobTxMsg>& message,
                        Error const& e, dmCatReq* dmRequest);
};

struct StatStreamHandler : Handler {
    StatStreamHandler();
    void handleRequest(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                       boost::shared_ptr<fpi::StatStreamMsg>& message);
    void handleQueueItem(dmCatReq* dmRequest);
    void handleResponse(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                        boost::shared_ptr<fpi::StatStreamMsg>& message,
                        Error const& e, dmCatReq* dmRequest);
};

struct CommitBlobTxHandler : Handler {
    CommitBlobTxHandler();
    void handleRequest(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                       boost::shared_ptr<fpi::CommitBlobTxMsg>& message);
    void handleQueueItem(dmCatReq* dmRequest);
    void volumeCatalogCb(Error const& e, blob_version_t blob_version,
                         BlobObjList::const_ptr const& blob_obj_list,
                         MetaDataList::const_ptr const& meta_list,
                         fds_uint64_t const blobSize,
                         DmIoCommitBlobTx* commitBlobReq);
    void handleResponse(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                        boost::shared_ptr<fpi::CommitBlobTxMsg>& message,
                        Error const& e, dmCatReq* dmRequest);
};

struct UpdateCatalogOnceHandler : Handler {
    UpdateCatalogOnceHandler();
    void handleRequest(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                       boost::shared_ptr<fpi::UpdateCatalogOnceMsg>& message);
    void handleQueueItem(dmCatReq* dmRequest);
    void handleCommitBlobOnceResponse(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                                      Error const& e, dmCatReq* dmRequest);
    void handleResponse(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                        boost::shared_ptr<fpi::UpdateCatalogOnceMsg>& message,
                        Error const& e, dmCatReq* dmRequest);
};

struct SetBlobMetaDataHandler : Handler {
    SetBlobMetaDataHandler();
    void handleRequest(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                       boost::shared_ptr<fpi::SetBlobMetaDataMsg>& message);
    void handleQueueItem(dmCatReq* dmRequest);
    void handleResponse(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                        boost::shared_ptr<fpi::SetBlobMetaDataMsg>& message,
                        Error const& e, dmCatReq* dmRequest);
};

struct AbortBlobTxHandler : Handler {
    AbortBlobTxHandler();
    void handleRequest(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                       boost::shared_ptr<fpi::AbortBlobTxMsg>& message);
    void handleQueueItem(dmCatReq* dmRequest);
    void handleResponse(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                        boost::shared_ptr<fpi::AbortBlobTxMsg>& message,
                        Error const& e, dmCatReq* dmRequest);
};

struct ForwardCatalogUpdateHandler : Handler {
    ForwardCatalogUpdateHandler();
    void handleRequest(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                       boost::shared_ptr<fpi::ForwardCatalogMsg>& message);
    void handleQueueItem(dmCatReq* dmRequest);
    void handleUpdateFwdCommittedBlob(Error const& e, DmIoFwdCat* fwdCatReq);
    void handleResponse(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                        boost::shared_ptr<fpi::ForwardCatalogMsg>& message,
                        Error const& e, dmCatReq* dmRequest);
    void handleCompletion(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                          boost::shared_ptr<fpi::ForwardCatalogMsg>& message,
                          Error const& e, dmCatReq* dmRequest);
};

struct StatVolumeHandler : Handler {
    StatVolumeHandler();
    void handleRequest(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                       boost::shared_ptr<fpi::StatVolumeMsg>& message);
    void handleQueueItem(dmCatReq* dmRequest);
    void handleResponse(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                        boost::shared_ptr<fpi::StatVolumeMsg>& message,
                        Error const& e, dmCatReq* dmRequest);
};

struct SetVolumeMetadataHandler : Handler {
    SetVolumeMetadataHandler();
    void handleRequest(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                       boost::shared_ptr<fpi::SetVolumeMetadataMsg>& message);
    void handleQueueItem(dmCatReq* dmRequest);
    void handleResponse(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                        boost::shared_ptr<fpi::SetVolumeMetadataMsg>& message,
                        Error const& e, dmCatReq* dmRequest);
};

struct ReloadVolumeHandler : Handler {
    ReloadVolumeHandler();
    void handleRequest(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                       boost::shared_ptr<fpi::ReloadVolumeMsg>& message);
    void handleQueueItem(dmCatReq* dmRequest);
    void handleResponse(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                        boost::shared_ptr<fpi::ReloadVolumeMsg>& message,
                        Error const& e, dmCatReq* dmRequest);
};

}  // namespace dm
}  // namespace fds
#endif  // SOURCE_DATA_MGR_INCLUDE_DMHANDLER_H_
