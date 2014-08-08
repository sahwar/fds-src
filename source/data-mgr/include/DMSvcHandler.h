/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#ifndef SOURCE_DATA_MGR_INCLUDE_DMSVCHANDLER_H_
#define SOURCE_DATA_MGR_INCLUDE_DMSVCHANDLER_H_

#include <fdsp/fds_service_types.h>
#include <net/PlatNetSvcHandler.h>
#include <fdsp/DMSvc.h>
// TODO(Rao): Don't include DataMgr here.  The only reason we include now is
// b/c dmCatReq is subclass in DataMgr and can't be forward declared
#include <DataMgr.h>

namespace fds {

class DMSvcHandler : virtual public DMSvcIf, public PlatNetSvcHandler {
 public:
    DMSvcHandler();

    void startBlobTx(const fpi::AsyncHdr& asyncHdr,
                       const fpi::StartBlobTxMsg& startBlob) {
        // Don't do anything here. This stub is just to keep cpp compiler happy
    }
    void queryCatalogObject(const fpi::AsyncHdr& asyncHdr,
                            const fpi::QueryCatalogMsg& queryMsg) {
        // Don't do anything here. This stub is just to keep cpp compiler happy
    }
    void updateCatalog(const fpi::AsyncHdr& asyncHdr,
                       const fpi::UpdateCatalogMsg& updcatMsg) {
        // Don't do anything here. This stub is just to keep cpp compiler happy
    }

    void deleteCatalogObject(const fpi::AsyncHdr& asyncHdr,
                             const fpi::DeleteCatalogObjectMsg& delcatMsg) {
        // Don't do anything here. This stub is just to keep cpp compiler happy
    }

    void commitBlobTx(const fpi::AsyncHdr& asyncHdr,
                             const fpi::CommitBlobTxMsg& commitBlbMsg) {
        // Don't do anything here. This stub is just to keep cpp compiler happy
    }

    void abortBlobTx(const fpi::AsyncHdr& asyncHdr,
                             const fpi::AbortBlobTxMsg& abortBlbMsg) {
        // Don't do anything here. This stub is just to keep cpp compiler happy
    }

    void volSyncState(const fpi::AsyncHdr& asyncHdr,
                      const fpi::VolSyncStateMsg& syncMsg) {
        // Don't do anything here. This stub is just to keep cpp compiler happy
    }

    void fwdCatalogUpdateMsg(const fpi::AsyncHdr& AsyncHdr,
                             const fpi::ForwardCatalogMsg& fwdMsg) {
        // Don't do anything here. This stub is just to keep cpp compiler happy
    }

    void registerStreaming(const fpi::AsyncHdr& asyncHdr,
                           const fpi::StreamingRegistrationMsg & streamRegstrMsg) {
        // Don't do anything here. This stub is just to keep cpp compiler happy
    }

    void deregisterStreaming(const fpi::AsyncHdr& asyncHdr,
                           const fpi::StreamingDeregistrationMsg & streamDeregstrMsg) {
        // Don't do anything here. This stub is just to keep cpp compiler happy
    }

    void volSyncState(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                      boost::shared_ptr<fpi::VolSyncStateMsg>& syncMsg);

    void fwdCatalogUpdateMsg(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                             boost::shared_ptr<fpi::ForwardCatalogMsg>& fwdCatMsg);

    void fwdCatalogUpdateMsg(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                             boost::shared_ptr<fpi::ForwardCatalogMsg>& fwdCatMsg,
                             const Error &err, DmIoFwdCat* req);

    void fwdCatalogUpdateCb(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                            boost::shared_ptr<fpi::ForwardCatalogMsg>& fwdCatMsg,
                            const Error &err, DmIoFwdCat *req);

    void startBlobTx(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                       boost::shared_ptr<fpi::StartBlobTxMsg>& startBlob);
    void startBlobTxCb(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                         const Error &e, DmIoStartBlobTx *req);

    void queryCatalogObject(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                            boost::shared_ptr<fpi::QueryCatalogMsg>& queryMsg);
    void queryCatalogObjectCb(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                              boost::shared_ptr<fpi::QueryCatalogMsg>& queryMsg,
                              const Error &e, dmCatReq *req);

    void updateCatalog(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                       boost::shared_ptr<fpi::UpdateCatalogMsg>& updcatMsg);
    void updateCatalogOnce(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                           boost::shared_ptr<
                           fpi::UpdateCatalogOnceMsg>& updcatMsg);
    void updateCatalogCb(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                         const Error &e, DmIoUpdateCat *req);
    void updateCatalogOnceCb(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                             const Error &e,
                             DmIoUpdateCatOnce *req);
    void commitBlobOnceCb(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                          const Error &e,
                          DmIoCommitBlobTx *req);

    void deleteCatalogObject(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                       boost::shared_ptr<fpi::DeleteCatalogObjectMsg>& delcatMsg);
    void deleteCatalogObjectCb(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                         const Error &e, DmIoDeleteCat *req);

    void commitBlobTx(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                       boost::shared_ptr<fpi::CommitBlobTxMsg>& commitBlbTx);
    void commitBlobTxCb(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                         const Error &e, DmIoCommitBlobTx *req);

    void abortBlobTx(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                       boost::shared_ptr<fpi::AbortBlobTxMsg>& abortBlbTx);
    void abortBlobTxCb(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                         const Error &e, DmIoAbortBlobTx *req);
    void getBlobMetaData(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                         boost::shared_ptr<fpi::GetBlobMetaDataMsg>& message);
    void getBlobMetaDataCb(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                           boost::shared_ptr<fpi::GetBlobMetaDataMsg>& message,
                           const Error &e, DmIoGetBlobMetaData *req);
    void setBlobMetaData(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                         boost::shared_ptr<fpi::SetBlobMetaDataMsg>& setBlobMD);
    void setBlobMetaDataCb(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                           const Error &e, DmIoSetBlobMetaData *req);

    void getVolumeMetaData(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                           boost::shared_ptr<fpi::GetVolumeMetaDataMsg>& message);
    void getVolumeMetaDataCb(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                           boost::shared_ptr<fpi::GetVolumeMetaDataMsg>& message,
                           const Error &e, DmIoGetVolumeMetaData *req);

    void handleStatStream(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                          boost::shared_ptr<fpi::StatStreamMsg>& message);
    void handleStatStreamCb(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                            boost::shared_ptr<fpi::StatStreamMsg>& message,
                            const Error &e, DmIoStatStream *req);

    void registerStreaming(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                           boost::shared_ptr<fpi::StreamingRegistrationMsg>& streamRegstrMsg);
    void registerStreamingCb(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                             fpi::StreamingRegistrationMsgPtr & message,
                             const Error &e, DmIoStreamingRegstr * req);

    void deregisterStreaming(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                        boost::shared_ptr<fpi::StreamingDeregistrationMsg>& streamDeregstrMsg);
    void deregisterStreamingCb(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                               fpi::StreamingDeregistrationMsgPtr & message,
                               const Error &e, DmIoStreamingDeregstr * req);
};

}  // namespace fds

#endif  // SOURCE_DATA_MGR_INCLUDE_DMSVCHANDLER_H_
