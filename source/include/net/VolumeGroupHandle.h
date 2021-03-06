/* Copyright 2015 Formation Data Systems, Inc.
 */
#ifndef SOURCE_INCLUDE_NET_VOLUMEGROUPHANDLE_H_
#define SOURCE_INCLUDE_NET_VOLUMEGROUPHANDLE_H_

#include <net/SvcMgr.h>
#include <net/SvcRequest.h>
#include <net/SvcRequestPool.h>
#include <fdsp/volumegroup_types.h>
#include <fdsp/dm_api_types.h>
#include <net/volumegroup_extensions.h>
#include <boost/circular_buffer.hpp>

#define GROUPHANDLE_ACCESS_CHECK_CB(isWrite, cb, msg) \
    if (state_ != fpi::ResourceState::Active) { \
        if (state_ == fpi::ResourceState::Loading) { \
            LOGWARN << logString() << fds::logString(*msg) << " open in progress"; \
            cb(ERR_VOLUMEGROUP_NOT_OPEN, nullptr); \
            return; \
        } else { \
            if (functionalReplicas_.size() == 0) { \
                LOGWARN << logString() << fds::logString(*msg) << " invalid volumegroup"; \
                cb(ERR_VOLUMEGROUP_INVALID, nullptr); \
                return; \
            } else if (isWrite || state_ != fpi::ResourceState::Offline) { \
                LOGWARN << logString() << fds::logString(*msg) << " unavailable"; \
                cb(ERR_VOLUMEGROUP_DOWN, nullptr); \
                return; \
            } \
            /* Read request when state is offline with at least one functional replica is ok */ \
        } \
    } else if (isWrite && !isCoordinator_) { \
        LOGWARN << logString() << fds::logString(*msg) << " must be a coordinator to do write"; \
        cb(ERR_INVALID, nullptr); \
        return; \
    } else if (closeCb_) { \
        LOGWARN << logString() << fds::logString(*msg) << " invalid request on a closed handle"; \
        cb(ERR_INVALID, nullptr); \
        return; \
    }

namespace fds {
// Some logging routines have external linkage
extern std::string logString(const fpi::AbortBlobTxMsg&);
extern std::string logString(const fpi::CommitBlobTxMsg&);
extern std::string logString(const fpi::QueryCatalogMsg&);
extern std::string logString(const fpi::StartBlobTxMsg&);
extern std::string logString(const fpi::UpdateCatalogMsg&);
extern std::string logString(const fpi::UpdateCatalogOnceMsg&);
extern std::string logString(const fpi::DeleteBlobMsg&);
extern std::string logString(const fpi::GetVolumeMetadataMsg&);
extern std::string logString(const fpi::RenameBlobMsg&);
extern std::string logString(const fpi::SetBlobMetaDataMsg&);
extern std::string logString(const fpi::SetVolumeMetadataMsg&);
extern std::string logString(const fpi::GetBlobMetaDataMsg&);
extern std::string logString(const fpi::StatVolumeMsg&);
extern std::string logString(const fpi::GetBucketMsg&);

struct VolumeGroupHandle;

std::ostream& operator << (std::ostream &out, const fpi::VolumeIoHdr &h);

/**
* @brief Used for keeping state information about an individual volume replica.
* Acts as a proxy for the volume replica residing on a DM.
* VolumeGroupHandle manages a collection of VolumeReplicaHandles
*/
struct VolumeReplicaHandle {
    explicit VolumeReplicaHandle(const fpi::SvcUuid &id)
        : version(VolumeGroupConstants::VERSION_INVALID),
        svcUuid(id),
        state(fpi::ResourceState::Offline),
        lastError(ERR_OK),
        appliedOpId(VolumeGroupConstants::OPSTARTID),
        appliedCommitId(VolumeGroupConstants::COMMITSTARTID)
    {
    }
    inline static bool isFunctional(const fpi::ResourceState& s)
    {
        return s == fpi::ResourceState::Active;
    }
    inline static bool isSyncing(const fpi::ResourceState& s)
    {
        return s == fpi::ResourceState::Syncing;
    }
    inline static bool isNonFunctional(const fpi::ResourceState& s)
    {
        return s == fpi::ResourceState::Offline || s == fpi::ResourceState::Loading;
    }
    inline bool isFunctional() const {
        return isFunctional(state);
    }
    inline bool isSyncing() const {
        return isSyncing(state);
    }
    inline bool isNonFunctional() const {
        return isNonFunctional(state);
    }
    inline void setState(const fpi::ResourceState &state)
    {
        this->state = state;
    }
    inline void setError(const Error &e)
    {
        this->lastError = e;
    }
    inline void setVersion(const int32_t &version)
    {
        this->version = version;
    }
    inline void setInfo(const int32_t &version,
                        const fpi::ResourceState &state,
                        int64_t opId, int64_t commitId)
    {
        setState(state);
        this->version = version;
        appliedOpId = opId;
#ifdef COMMITID_SUPPORTED
        appliedCommitId = commitId;
#endif
    }
    inline std::string logString() const;

    /* Version of the volume replica.  As volume goes up/down this # is incremented */
    int32_t                 version;
    /* Service where this replica is hosted */
    fpi::SvcUuid            svcUuid;
    /* State of the volume replica */
    fpi::ResourceState      state;
    /* Last error that caused this volume replica to become non-functional */
    Error                   lastError;
    /* Last succesfully applied operation id */
    int64_t                 appliedOpId;
    /* Last succesfully applied commit id */
    int64_t                 appliedCommitId;
};
std::ostream& operator << (std::ostream &out, const VolumeReplicaHandle &h);

using VolumeResponseCb = std::function<void(const Error&, StringPtr)>;
using OpenResponseCb = std::function<void(const Error&, const fpi::OpenVolumeRspMsgPtr&)>;
using AddToVolumeGroupCb = std::function<void(const Error&,
                                              const fpi::AddToVolumeGroupRespCtrlMsgPtr&)>;

/**
* @brief Base class for group requests that are related to VolumeGrouping
* All volume request track op id and commit ids for making sure operations
* applied in order.
*/
struct VolumeGroupRequest : MultiEpSvcRequest {
    using Acks = std::vector<std::pair<fpi::SvcUuid, Error>>;
    VolumeGroupRequest(CommonModuleProviderIf* provider,
                        const SvcRequestId &id,
                        const fpi::SvcUuid &myEpId,
                        VolumeGroupHandle *groupHandle);
    virtual ~VolumeGroupRequest();

    virtual void invoke() override;
    void complete(const Error& error) override;

    virtual std::string logString() override;

    VolumeGroupHandle          *groupHandle_; 
    fpi::VolumeIoHdr            volumeIoHdr_; 
    VolumeResponseCb            responseCb_;
    uint8_t                     nAcked_;
    Acks                        successAcks_;
    
    friend class VolumeGroupHandle;
};


/**
* @brief Request for broadcasting the payload to all replicas
*/
struct VolumeGroupBroadcastRequest : VolumeGroupRequest {
    /* Constructors inherited */
    using VolumeGroupRequest::VolumeGroupRequest;
    virtual void handleResponse(SHPTR<fpi::AsyncHdr>& header,
                                SHPTR<std::string>& payload) override;
 protected:
    virtual void invokeWork_() override;
};

/**
* @brief Failover style request for volume group
*/
struct VolumeGroupFailoverRequest : VolumeGroupRequest {
    /* Constructors inherited */
    using VolumeGroupRequest::VolumeGroupRequest;
    virtual void handleResponse(SHPTR<fpi::AsyncHdr>& header,
                                SHPTR<std::string>& payload) override;
    inline void setAvailableReplicas(const std::vector<VolumeReplicaHandle> &replicas) {
        availableReplicas_ = replicas;
    }

 protected:
    virtual void invokeWork_() override;

    std::vector<VolumeReplicaHandle>   availableReplicas_;
};

/**
* @brief VolumeGroupHandle listener.
*/
struct VolumeGroupHandleListener {
    virtual bool isError(const fpi::FDSPMsgTypeId &reqMsgTypeId, const Error &e) = 0;
};

struct OpenRetryCtx {
    /* # of times switch was attempted */
    int                     triesCnt {0};
    fpi::SvcUuid            currentCoordinator;
    bool                    doSwitch {false};
};

/**
* @brief VolumeGroupHandle provides access to a group of volumes.
* It manages io coordination/replication to a group of volumes. Think
* of it as a file handle to a group of volumes.
* -Ensures IO is replicated in order to all the volumes in the group.
* -Handles faults in a volume group
* -Manages sync/repair of a failed volume replica.
*
* Following are states for VolumeGroupHandle
* Unknown - Prior open is called
* Initing - Open is in progress
* Active - # of functional replicas >= quorum count.  When active, if close is called, we drain
* all the pending requests before calling close cb.  After close is called, the expectation is,
* no new requests can come in.
* Offline - # of function replicas < qourm count
*/
struct VolumeGroupHandle : HasModuleProvider, StateProvider {
    using VolumeReplicaHandleList       = std::vector<VolumeReplicaHandle>;
    using VolumeReplicaHandleItr        = VolumeReplicaHandleList::iterator;
    using WriteOpsBuffer                = boost::circular_buffer<std::pair<fpi::FDSPMsgTypeId, StringPtr>>;

    /* Ping to the group when non-functional members exist is done on this interval 
     * Exposed as public/non-const so that it can be tuned for unit testing
     */
    static uint32_t                     GROUPCHECK_INTERVAL_SEC;
    static uint32_t                     IO_TIMEOUT_MS;
    static uint32_t                     COORDINATOR_SWITCH_TIMEOUT_MS;

    VolumeGroupHandle(CommonModuleProviderIf* provider,
                      const fds_volid_t& volId,
                      uint32_t quorumCnt);
    virtual ~VolumeGroupHandle();

    /**
    * @brief Opens volume handle.  After opening, messages can be sent to the group handle.
    *
    * @param msg
    * @param cb
    */
    void open(const SHPTR<fpi::OpenVolumeMsg>& msg, const OpenResponseCb &cb);

    /**
    * @brief Closes volume group handle.  After close is called, once all the pedning messages
    * are responded to closeCb is invoked.
    * NOTE: After calling close don't send any more new messages.
    *
    * @param closeCb
    */
    void close(const VoidCb &closeCb);

    template<class MsgT>
    void sendReadMsg(const fpi::FDSPMsgTypeId &msgTypeId,
                     SHPTR<MsgT> &msg, const VolumeResponseCb &cb);

    template<class MsgT>
    void sendCommitMsg(const fpi::FDSPMsgTypeId &msgTypeId,
                      SHPTR<MsgT> &msg, const VolumeResponseCb &cb);

    template<class MsgT>
    void sendModifyMsg(const fpi::FDSPMsgTypeId &msgTypeId,
                      SHPTR<MsgT> &msg, const VolumeResponseCb &cb);

    void setListener(VolumeGroupHandleListener *listener);

    template<class F>
    void runSynchronized(F&& f)
    {
        // TODO(Rao): Use threadpool directly
        taskExecutor_->scheduleOnHashKey(groupId_, std::forward<F>(f));
    }


    virtual void handleAddToVolumeGroupMsg(
        const fpi::AddToVolumeGroupCtrlMsgPtr &addMsg,
        const AddToVolumeGroupCb &cb);

    virtual void handleVolumeResponse(const fpi::SvcUuid &srcSvcUuid,
                                      const int32_t &replicaVersion,
                                      const fpi::VolumeIoHdr &hdr,
                                      const fpi::FDSPMsgTypeId &msgTypeId,
                                      const bool writeReq,
                                      const Error &inStatus,
                                      VolumeGroupRequest::Acks &successAcks);

    std::string getStateInfo() override;
    std::string getStateProviderId() override;

    void incRef();
    void decRef();

    std::vector<VolumeReplicaHandle*> getWriteableReplicaHandles();
    VolumeReplicaHandle* getFunctionalReplicaHandle();
    std::vector<fpi::SvcUuid> getAllReplicas() const;
    std::vector<fpi::SvcUuid> getFunctionalReplicas() const;

    inline const uint32_t& getQuorumCnt() const { return quorumCnt_; }
    inline bool isFunctional() const { return state_ == fpi::ResourceState::Active; }
    inline int64_t getGroupId() const { return groupId_; }
    inline int32_t getDmtVersion() const { return dmtVersion_; }
    inline int64_t getVersion() const { return version_; }
    inline int32_t size() const {
        return functionalReplicas_.size() +
            nonfunctionalReplicas_.size() +
            syncingReplicas_.size();
    }
    inline uint32_t getFunctionalReplicasCnt() const { return functionalReplicas_.size(); }
    std::string logString() const;
    inline int32_t getRefCnt() const { return refCnt_; }
    inline bool isSynchronized() const {
        return std::this_thread::get_id() == threadId_;
    }

 protected:
    template<class MsgT, class ReqT>
    SHPTR<ReqT> sendWriteReq_(const fpi::FDSPMsgTypeId &msgTypeId,
                              SHPTR<MsgT> &msg,
                              const VolumeResponseCb &cb) {
        /* Create a request and send */
        auto req = requestMgr_->newSvcRequest<ReqT>(this);
        /* First update opId in message bound for volume and cache the same opid in the
         * request
         */
        msg->opId = opSeqNo_;
        req->volumeIoHdr_.opId = msg->opId;
        // TODO(Rao): Set commit/sequence id once enabled
        auto payload = serializeFdspMsg(*msg);
        if (writeOpsBuffer_) {
            writeOpsBuffer_->push_back(std::make_pair(msgTypeId, payload));
        }
        req->setPayloadBuf(msgTypeId, payload);
        req->setTimeoutMs(IO_TIMEOUT_MS);
        req->responseCb_ = cb;
        req->setTaskExecutorId(groupId_);
        req->invoke();

        return req;
    }

    void runOpenProtocol_(const OpenResponseCb &cb);
    void runCoordinatorSwitchProtocol_(const OpenResponseCb &cb);

    bool replayFromWriteOpsBuffer_(const VolumeReplicaHandle &handle, const int64_t fromOpId);
    void toggleWriteOpsBuffering_(bool enable);
    void resetGroup_(fpi::ResourceState state);
    EPSvcRequestPtr createSetVolumeGroupCoordinatorMsgReq_(bool clearCoordinator = false);
    QuorumSvcRequestPtr createPreareOpenVolumeGroupMsgReq_();
    void determineFunctaionalReplicas_(QuorumSvcRequest* openReq);
    void handleOpenResponseForNonCoordinator_(QuorumSvcRequest* openReq,
                                              const OpenResponseCb &cb);
    QuorumSvcRequestPtr createBroadcastGroupInfoReq_();
    void changeState_(const fpi::ResourceState &targetState,
                      bool cleanReplicas,
                      const std::string& logCtx);
    Error changeVolumeReplicaState_(VolumeReplicaHandleItr &volumeHandle,
                                    const int32_t &replicaVersion,
                                    const fpi::ResourceState &targetState,
                                    const int64_t &opId,
                                    const Error &e,
                                    const std::string &context);
    void setGroupInfo_(const fpi::VolumeGroupInfo &groupInfo);
    fpi::VolumeGroupInfo getGroupInfoForExternalUse_();
    VolumeReplicaHandleItr getVolumeReplicaHandle_(const fpi::SvcUuid &svcUuid);
    VolumeReplicaHandleList& getVolumeReplicaHandleList_(const fpi::ResourceState& s);
    void scheduleCheckOnNonfunctionalReplicas_();
    void checkOnNonFunctaionalReplicas_();
    void closeHandle_();

    SynchronizedTaskExecutor<uint64_t>  *taskExecutor_;
    /* ID of the thread on which all work related to this handle is done on.
     * Cached here for ensuring all synchronized tasks are done on this thread id 
     */
    std::thread::id                     threadId_;

    SvcRequestPool                      *requestMgr_;
    VolumeGroupHandleListener           *listener_ {nullptr};
    VolumeReplicaHandleList             functionalReplicas_;
    VolumeReplicaHandleList             nonfunctionalReplicas_;
    VolumeReplicaHandleList             syncingReplicas_;
    uint32_t                            groupSize_;
    /* # of volume replicas that need to respond success before client is acked with success */
    uint32_t                            quorumCnt_;
    /* State of the volume group */
    fpi::ResourceState                  state_;
    /* ID of the volume group.  This is same as volume id */
    int64_t                             groupId_;
    /* Version # for group handle.  This is different from VolumeReplicaHandle version # */
    int64_t                             version_;
    /* Id used when exporting state */
    std::string                         stateProviderId_;
    /* Every write operation is given a sequence #. The first # is OPSTARTID+1 */
    int64_t                             opSeqNo_;
    /* Every commit operation is given a sequence #. The first # is COMMITSTARTID+1 */
    int64_t                             commitNo_;
    uint64_t                            dmtVersion_;
    /* When buffering is enabled, every write operation up last n(buffer size)
     * operations are buffered here
     */
    std::unique_ptr<WriteOpsBuffer>     writeOpsBuffer_;
    /* # of references.  Every time an async msg is sent out, this # is incremented.  On
     * responses this # is decremented
     */
    int32_t                             refCnt_;
    /* Close callback.  Invoked when close(cb) is called && # of references on the
     * VolumeGroupHandle is zero
     */
    VoidCb                              closeCb_;
    /* Whether check on non-functional replicas is in progress */
    bool                                checkOnNonFunctionalScheduled_;
    /* Whether the group handle is coordinator or not.  Coordinator has write access/
     * as well as responsibility to coordinate replication
     */
    bool                                isCoordinator_;

    /* Context kept around when we retry open.  Open is retried when
     * 1. coordinator switch is required.  We run switch protocol when >= quorum
     *    # of replicas return ERR_INVALID_COORDINATOR.
     * 2. force open is required.  We may need force open when less than quorum
     *    # of replicas return ERR_INVALID_COORDINATOR
     */
    std::unique_ptr<OpenRetryCtx>       openRetryCtx_;

    static const uint32_t               WRITEOPS_BUFFER_SZ = 1024;

    friend class VolumeGroupBroadcastRequest;
    friend class VolumeGroupFailoverRequest;
};

template<class MsgT>
void VolumeGroupHandle::sendReadMsg(const fpi::FDSPMsgTypeId &msgTypeId,
                                    SHPTR<MsgT> &msg, const VolumeResponseCb &cb)
{
    fds_assert(!closeCb_);

    runSynchronized([this, msgTypeId, msg, cb]() mutable {
        GROUPHANDLE_ACCESS_CHECK_CB(false, cb, msg);

        /* Create a request and send */
        auto req = requestMgr_->newSvcRequest<VolumeGroupFailoverRequest>(this);
        req->setPayload(msgTypeId, msg);
        req->setTimeoutMs(IO_TIMEOUT_MS);
        req->responseCb_ = cb;
        req->setTaskExecutorId(groupId_);
        if (!isCoordinator_) {
            req->setAvailableReplicas(functionalReplicas_);
        }
        req->invoke();
    });
}

template<class MsgT>
void VolumeGroupHandle::sendModifyMsg(const fpi::FDSPMsgTypeId &msgTypeId,
                                      SHPTR<MsgT> &msg, const VolumeResponseCb &cb) {
    fds_assert(!closeCb_);

    runSynchronized([this, msgTypeId, msg, cb]() mutable {
        GROUPHANDLE_ACCESS_CHECK_CB(true, cb, msg);

        opSeqNo_++;
        sendWriteReq_<MsgT, VolumeGroupBroadcastRequest>(msgTypeId, msg, cb);
    });
}

template<class MsgT>
void VolumeGroupHandle::sendCommitMsg(const fpi::FDSPMsgTypeId &msgTypeId,
                                     SHPTR<MsgT> &msg, const VolumeResponseCb &cb) {
    fds_assert(!closeCb_);

    runSynchronized([this, msgTypeId, msg, cb]() mutable {
        GROUPHANDLE_ACCESS_CHECK_CB(true, cb, msg);

        opSeqNo_++;
        commitNo_++;
        msg->sequence_id = commitNo_;
        sendWriteReq_<MsgT, VolumeGroupBroadcastRequest>(msgTypeId, msg, cb);
    });
}


}  // namespace fds

#endif   // SOURCE_INCLUDE_NET_VOLUMEGROUPHANDLE_H_
