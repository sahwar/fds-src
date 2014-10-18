/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#ifndef SOURCE_ACCESS_MGR_INCLUDE_AMDISPATCHER_H_
#define SOURCE_ACCESS_MGR_INCLUDE_AMDISPATCHER_H_

#include <string>
#include <fds_module.h>
#include <AmCache.h>
#include <StorHvVolumes.h>
#include <net/SvcRequest.h>

namespace fds {

/**
 * AM FDSP request dispatcher and reciever. The dispatcher
 * does the work to send and receive AM network messages over
 * the service layer. The layer is stateless and does not
 * internal locking.
 */
class AmDispatcher : public Module, public boost::noncopyable {
  public:
    /**
     * The dispatcher takes a shared ptr to the DMT manager
     * which it uses when deciding who to dispatch to. The
     * DMT manager is still owned and updated to omClient.
     * TODO(Andrew): Make the dispatcher own this piece or
     * iterface with platform lib.
     */
    AmDispatcher(const std::string &modName,
                 DLTManagerPtr _dltMgr,
                 DMTManagerPtr _dmtMgr);
    ~AmDispatcher();
    typedef std::unique_ptr<AmDispatcher> unique_ptr;
    typedef boost::shared_ptr<AmDispatcher> shared_ptr;

    /**
     * Module methods
     */
    int mod_init(SysParams const *const param);
    void mod_startup();
    void mod_shutdown();

    /**
     * Dispatches a get volume metadata request.
     */
    void dispatchGetVolumeMetadata(AmQosReq *qosReq);

    /**
     * Callback for get volume metadata responses.
     */
    void getVolumeMetadataCb(AmQosReq* qosReq,
                             FailoverSvcRequest* svcReq,
                             const Error& error,
                             boost::shared_ptr<std::string> payload);

    /**
     * Aborts a blob transaction request.
     */
    void dispatchAbortBlobTx(AmQosReq *qosReq);

    /**
     * Dipatches a start blob transaction request.
     */
    void dispatchStartBlobTx(AmQosReq *qosReq);

    /**
     * Callback for start blob transaction responses.
     */
    void startBlobTxCb(AmQosReq* qosReq,
                       QuorumSvcRequest* svcReq,
                       const Error& error,
                       boost::shared_ptr<std::string> payload);

    /**
     * Dispatches a commit blob transaction request.
     */
    void dispatchCommitBlobTx(AmQosReq *qosReq);

    /**
     * Callback for commit blob transaction responses.
     */
    void commitBlobTxCb(AmQosReq* qosReq,
                       QuorumSvcRequest* svcReq,
                       const Error& error,
                       boost::shared_ptr<std::string> payload);

    /**
     * Dispatches an update catalog request.
     */
    void dispatchUpdateCatalog(AmQosReq *qosReq);

    /**
     * Dispatches an update catalog once request.
     */
    void dispatchUpdateCatalogOnce(AmQosReq *qosReq);

    /**
     * Dipatches a put object request.
     */
    void dispatchPutObject(AmQosReq *qosReq);

    /**
     * Dipatches a get object request.
     */
    void dispatchGetObject(AmQosReq *qosReq);

    /**
     * Dispatches a delete blob transaction request.
     */
    void dispatchDeleteBlob(AmQosReq *qosReq);

    /**
     * Dipatches a query catalog request.
     */
    void dispatchQueryCatalog(AmQosReq *qosReq);

    /**
     * Dispatches a stat blob transaction request.
     */
    void dispatchStatBlob(AmQosReq *qosReq);

  private:
    /// Shared ptrs to the DLT and DMT managers used
    /// for deciding who to dispatch to
    DLTManagerPtr dltMgr;
    DMTManagerPtr dmtMgr;

    /**
     * Callback for delete blob responses.
     */
    void abortBlobTxCb(AmQosReq *qosReq,
                       QuorumSvcRequest* svcReq,
                       const Error& error,
                       boost::shared_ptr<std::string> payload);

    /**
     * Callback for delete blob responses.
     */
    void deleteBlobCb(AmQosReq *qosReq,
                      QuorumSvcRequest* svcReq,
                      const Error& error,
                      boost::shared_ptr<std::string> payload);

    /**
     * Callback for get blob responses.
     */
    void getObjectCb(AmQosReq* qosReq,
                     FailoverSvcRequest* svcReq,
                     const Error& error,
                     boost::shared_ptr<std::string> payload);

    /**
     * Callback for catalog query responses.
     */
    void getQueryCatalogCb(AmQosReq* qosReq,
                           FailoverSvcRequest* svcReq,
                           const Error& error,
                           boost::shared_ptr<std::string> payload);

    /**
     * Callback for stat blob responses.
     */
    void statBlobCb(AmQosReq *qosReq,
                    FailoverSvcRequest* svcReq,
                    const Error& error,
                    boost::shared_ptr<std::string> payload);

    /**
     * Callback for update blob responses.
     */
    void updateCatalogCb(AmQosReq* qosReq,
                         QuorumSvcRequest* svcReq,
                         const Error& error,
                         boost::shared_ptr<std::string> payload);

    /**
     * Callback for put object responses.
     */
    void putObjectCb(AmQosReq* qosReq,
                     QuorumSvcRequest* svcReq,
                     const Error& error,
                     boost::shared_ptr<std::string> payload);
};

}  // namespace fds

#endif  // SOURCE_ACCESS_MGR_INCLUDE_AMDISPATCHER_H_
