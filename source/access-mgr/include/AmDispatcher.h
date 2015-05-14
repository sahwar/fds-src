/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#ifndef SOURCE_ACCESS_MGR_INCLUDE_AMDISPATCHER_H_
#define SOURCE_ACCESS_MGR_INCLUDE_AMDISPATCHER_H_

#include <string>
#include <fds_volume.h>
#include <net/SvcRequest.h>
#include "AmRequest.h"

namespace fds {

/* Forward declaarations */
class MockSvcHandler;
class OMgrClient;

/**
 * AM FDSP request dispatcher and reciever. The dispatcher
 * does the work to send and receive AM network messages over
 * the service layer.
 */
struct AmDispatcher : HasModuleProvider
{
    /**
     * The dispatcher takes a shared ptr to the DMT manager
     * which it uses when deciding who to dispatch to. The
     * DMT manager is still owned and updated to omClient.
     * TODO(Andrew): Make the dispatcher own this piece or
     * iterface with platform lib.
     */
    explicit AmDispatcher(CommonModuleProviderIf *modProvider);
    AmDispatcher(AmDispatcher const&)               = delete;
    AmDispatcher& operator=(AmDispatcher const&)    = delete;
    AmDispatcher(AmDispatcher &&)                   = delete;
    AmDispatcher& operator=(AmDispatcher &&)        = delete;
    ~AmDispatcher();

    /**
     * Initialize the OM client, and retrieve Dlt/Dmt managers
     */
    void start();

    /**
     * Dlt/Dmt updates
     */
    Error updateDlt(bool dlt_type, std::string& dlt_data, OmDltUpdateRespCbType cb);
    Error updateDmt(bool dmt_type, std::string& dmt_data);

    /**
     * Dispatches a test volume request to OM.
     */
    Error attachVolume(std::string const& volume_name);
    void dispatchAttachVolume(AmRequest *amReq);

    /**
     * Dispatches an open volume request to DM.
     */
    void dispatchOpenVolume(fds_volid_t const vol_id,
                            fds_int64_t const token,
                            std::function<void(fds_int64_t const, Error const)> cb);

    /**
     * Dispatches an open volume request to DM.
     */
    void dispatchCloseVolume(fds_int64_t vol_id, fds_int64_t token);

    /**
     * Dispatches a stat volume request.
     */
    void dispatchStatVolume(AmRequest *amReq);

    /**
     * Callback for stat volume responses.
     */
    void statVolumeCb(AmRequest* amReq,
                      FailoverSvcRequest* svcReq,
                      const Error& error,
                      boost::shared_ptr<std::string> payload);

    /**
     * Dispatches a set volume metadata request.
     */
    void dispatchSetVolumeMetadata(AmRequest *amReq);

    /**
     * Callback for set volume metadata responses.
     */
    void setVolumeMetadataCb(AmRequest* amReq,
                             QuorumSvcRequest* svcReq,
                             const Error& error,
                             boost::shared_ptr<std::string> payload);

    /**
     * Dispatches a get volume metadata request.
     */
    void dispatchGetVolumeMetadata(AmRequest *amReq);

    /**
     * Callback for get volume metadata responses.
     */
    void getVolumeMetadataCb(AmRequest* amReq,
                             FailoverSvcRequest* svcReq,
                             const Error& error,
                             boost::shared_ptr<std::string> payload);

    /**
     * Aborts a blob transaction request.
     */
    void dispatchAbortBlobTx(AmRequest *amReq);

    /**
     * Dipatches a start blob transaction request.
     */
    void dispatchStartBlobTx(AmRequest *amReq);

    /**
     * Callback for start blob transaction responses.
     */
    void startBlobTxCb(AmRequest* amReq,
                       QuorumSvcRequest* svcReq,
                       const Error& error,
                       boost::shared_ptr<std::string> payload);

    /**
     * Dispatches a commit blob transaction request.
     */
    void dispatchCommitBlobTx(AmRequest *amReq);

    /**
     * Callback for commit blob transaction responses.
     */
    void commitBlobTxCb(AmRequest* amReq,
                       QuorumSvcRequest* svcReq,
                       const Error& error,
                       boost::shared_ptr<std::string> payload);

    /**
     * Dispatches an update catalog request.
     */
    void dispatchUpdateCatalog(AmRequest *amReq);

    /**
     * Dispatches an update catalog once request.
     */
    void dispatchUpdateCatalogOnce(AmRequest *amReq);

    /**
     * Dipatches a put object request.
     */
    void dispatchPutObject(AmRequest *amReq);

    /**
     * Dipatches a get object request.
     */
    void dispatchGetObject(AmRequest *amReq);

    /**
     * Dispatches a delete blob transaction request.
     */
    void dispatchDeleteBlob(AmRequest *amReq);

    /**
     * Dipatches a query catalog request.
     */
    void dispatchQueryCatalog(AmRequest *amReq);

    /**
     * Dispatches a stat blob transaction request.
     */
    void dispatchStatBlob(AmRequest *amReq);

    /**
     * Dispatches a set metadata on blob transaction request.
     */
    void dispatchSetBlobMetadata(AmRequest *amReq);

    /**
     * Dispatches a volume contents (list bucket) transaction request.
     */
    void dispatchVolumeContents(AmRequest *amReq);

  private:
    /**
     * Shared ptrs to the DLT and DMT managers used
     * for deciding who to dispatch to.
     */
    boost::shared_ptr<DLTManager> dltMgr;
    boost::shared_ptr<DMTManager> dmtMgr;

    template<typename Msg>
    QuorumSvcRequestPtr createQuorumRequest(fds_volid_t const vol_id,
                                            boost::shared_ptr<Msg> const& payload) const;

    template<typename Msg>
    FailoverSvcRequestPtr createFailoverRequest(fds_volid_t const vol_id,
                                                boost::shared_ptr<Msg> const& payload) const;

    /**
     * Callback for delete blob responses.
     */
    void abortBlobTxCb(AmRequest *amReq,
                       QuorumSvcRequest* svcReq,
                       const Error& error,
                       boost::shared_ptr<std::string> payload);

    /**
     * Callback for delete blob responses.
     */
    void deleteBlobCb(AmRequest *amReq,
                      QuorumSvcRequest* svcReq,
                      const Error& error,
                      boost::shared_ptr<std::string> payload);

    /**
     * Callback for get blob responses.
     */
    void getObjectCb(AmRequest* amReq,
                     fds_uint64_t dltVersion,
                     FailoverSvcRequest* svcReq,
                     const Error& error,
                     boost::shared_ptr<std::string> payload);

    /**
     * Callback for catalog query responses.
     */
    void getQueryCatalogCb(AmRequest* amReq,
                           FailoverSvcRequest* svcReq,
                           const Error& error,
                           boost::shared_ptr<std::string> payload);

    /**
     * Callback for catalog query error checks from service layer.
     */
    fds_bool_t getQueryCatalogAppStatusCb(AmRequest* amReq,
                                          const Error& error,
                                          boost::shared_ptr<std::string> payload);

    /**
     * Callback for set metadata on blob responses.
     */
    void setBlobMetadataCb(AmRequest *amReq,
                           QuorumSvcRequest* svcReq,
                           const Error& error,
                           boost::shared_ptr<std::string> payload);

    /**
     * Callback for stat blob responses.
     */
    void statBlobCb(AmRequest *amReq,
                    FailoverSvcRequest* svcReq,
                    const Error& error,
                    boost::shared_ptr<std::string> payload);

    /**
     * Callback for update blob responses.
     */
    void updateCatalogOnceCb(AmRequest* amReq,
                             QuorumSvcRequest* svcReq,
                             const Error& error,
                             boost::shared_ptr<std::string> payload);
    void updateCatalogCb(AmRequest* amReq,
                         QuorumSvcRequest* svcReq,
                         const Error& error,
                         boost::shared_ptr<std::string> payload);

    /**
     * Callback for put object responses.
     */
    void putObjectCb(AmRequest* amReq,
                     fds_uint64_t dltVersion,
                     QuorumSvcRequest* svcReq,
                     const Error& error,
                     boost::shared_ptr<std::string> payload);

    /**
     * Callback for stat blob responses.
     */
    void volumeContentsCb(AmRequest *amReq,
                          FailoverSvcRequest* svcReq,
                          const Error& error,
                          boost::shared_ptr<std::string> payload);

    /**
     * Configurable timeouts and defaults (ms)
     */
    uint32_t message_timeout_io { 0 };

    boost::shared_ptr<MockSvcHandler> mockHandler_;
    uint64_t mockTimeoutUs_  = 200;
    bool mockTimeoutEnabled_ = false;
};

}  // namespace fds

#endif  // SOURCE_ACCESS_MGR_INCLUDE_AMDISPATCHER_H_
