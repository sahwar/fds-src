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

#define GROUPHANDLE_FUNCTIONAL_CHECK_CB(cb) \
    if (state_ != fpi::ResourceState::Active) { \
        LOGWARN << logString() << " Unavailable"; \
        cb(ERR_VOLUMEGROUP_DOWN, nullptr); \
        return; \
    }
#define GROUPHANDLE_FUNCTIONAL_CHECK() \
    if (state_ != fpi::ResourceState::Active) { \
        LOGWARN << logString() << " Unavailable"; \
        return; \
    }


namespace fds {
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
        appliedCommitId = commitId;
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

/**
* @brief Base class for group requests that are related to VolumeGrouping
* All volume request track op id and commit ids for making sure operations
* applied in order.
*/
struct VolumeGroupRequest : MultiEpSvcRequest {
    VolumeGroupRequest(CommonModuleProviderIf* provider,
                        const SvcRequestId &id,
                        const fpi::SvcUuid &myEpId,
                        VolumeGroupHandle *groupHandle);
    virtual std::string logString() override;

    VolumeGroupHandle          *groupHandle_; 
    fpi::VolumeIoHdr            volumeIoHdr_; 
    VolumeResponseCb            responseCb_;
    uint8_t                     nAcked_;
    uint8_t                     nSuccessAcked_;
    
    friend class VolumeGroupHandle;
};


/**
* @brief Request for broadcasting the payload to all replicas
*/
struct VolumeGroupBroadcastRequest : VolumeGroupRequest {
    /* Constructors inherited */
    using VolumeGroupRequest::VolumeGroupRequest;
    virtual void invoke() override;
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
    virtual void invoke() override;
    virtual void handleResponse(SHPTR<fpi::AsyncHdr>& header,
                                SHPTR<std::string>& payload) override;
 protected:
    virtual void invokeWork_() override;
};

/**
* @brief VolumeGroupHandle listener.
*/
struct VolumeGroupHandleListener {
    virtual bool isError(const fpi::FDSPMsgTypeId &reqMsgTypeId, const Error &e) = 0;
};

/**
* @brief VolumeGroupHandle provides access to a group of volumes.
* It manages io coordination/replication to a group of volumes. Think
* of it as a file handle to a group of volumes.
* -Ensures IO is replicated in order to all the volumes in the group.
* -Handles faults in a volume group
* -Manages sync/repair of a failed volume replica.
*/
struct VolumeGroupHandle : HasModuleProvider {
    using VolumeReplicaHandleList       = std::vector<VolumeReplicaHandle>;
    using VolumeReplicaHandleItr        = VolumeReplicaHandleList::iterator;
    using WriteOpsBuffer                = boost::circular_buffer<std::pair<fpi::FDSPMsgTypeId, StringPtr>>;

    VolumeGroupHandle(CommonModuleProviderIf* provider,
                      const fds_volid_t& volId,
                      uint32_t quorumCnt);

    void open(const SHPTR<fpi::OpenVolumeMsg>& msg, const StatusCb &cb);

    void close();

    template<class MsgT>
    void sendReadMsg(const fpi::FDSPMsgTypeId &msgTypeId,
                     SHPTR<MsgT> &msg, const VolumeResponseCb &cb);

    template<class MsgT>
    void sendWriteMsg(const fpi::FDSPMsgTypeId &msgTypeId,
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
        const std::function<void(const Error&, const fpi::AddToVolumeGroupRespCtrlMsgPtr&)> &cb);

    virtual void handleVolumeResponse(const fpi::SvcUuid &srcSvcUuid,
                                      const int32_t &replicaVersion,
                                      const fpi::VolumeIoHdr &hdr,
                                      const fpi::FDSPMsgTypeId &msgTypeId,
                                      const bool writeReq,
                                      const Error &inStatus,
                                      uint8_t &successAcks);

    std::vector<VolumeReplicaHandle*> getIoReadyReplicaHandles();
    VolumeReplicaHandle* getFunctionalReplicaHandle();
    std::vector<fpi::SvcUuid> getAllReplicas() const;

    inline const uint32_t& getQuorumCnt() const { return quorumCnt_; }
    inline bool isFunctional() const { return state_ == fpi::ResourceState::Active; }
    inline int64_t getGroupId() const { return groupId_; }
    inline int32_t getDmtVersion() const { return dmtVersion_; }
    inline int32_t size() const {
        return functionalReplicas_.size() +
            nonfunctionalReplicas_.size() +
            syncingReplicas_.size();
    }
    inline uint32_t getFunctionalReplicasCnt() const { return functionalReplicas_.size(); }
    std::string logString() const;

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
        req->responseCb_ = cb;
        req->setTaskExecutorId(groupId_);
        req->invoke();

        return req;
    }

    bool replayFromWriteOpsBuffer_(const fpi::SvcUuid &svcUuid, const int64_t fromOpId);
    void toggleWriteOpsBuffering_(bool enable);
    void resetGroup_();
    EPSvcRequestPtr createSetVolumeGroupCoordinatorMsgReq_();
    QuorumSvcRequestPtr createPreareOpenVolumeGroupMsgReq_();
    void determineFunctaionalReplicas_(QuorumSvcRequest* openReq);
    QuorumSvcRequestPtr createBroadcastGroupInfoReq_();
    void changeState_(const fpi::ResourceState &targetState, const std::string& logCtx);
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

    SynchronizedTaskExecutor<uint64_t>  *taskExecutor_;
    SvcRequestPool                      *requestMgr_;
    VolumeGroupHandleListener           *listener_ {nullptr};
    VolumeReplicaHandleList             functionalReplicas_;
    VolumeReplicaHandleList             nonfunctionalReplicas_;
    VolumeReplicaHandleList             syncingReplicas_;
    /* # of volume replicas that need to respond success before client is acked with success */
    uint32_t                            quorumCnt_;
    /* State of the volume group */
    fpi::ResourceState                  state_;
    /* ID of the volume group.  This is same as volume id */
    int64_t                             groupId_;
    /* Version # for group handle.  This is different from VolumeReplicaHandle version # */
    fpi::VolumeGroupVersion             version_;
    /* Every write operation is given a sequence #. The first # is OPSTARTID+1 */
    int64_t                             opSeqNo_;
    /* Every commit operation is given a sequence #. The first # is COMMITSTARTID+1 */
    int64_t                             commitNo_;
    uint64_t                            dmtVersion_;
    /* When buffering is enabled, every write operation up last n(buffer size)
     * operations are buffered here
     */
    std::unique_ptr<WriteOpsBuffer>     writeOpsBuffer_;

    static const uint32_t               WRITEOPS_BUFFER_SZ = 1024;

    friend class VolumeGroupBroadcastRequest;
};

template<class MsgT>
void VolumeGroupHandle::sendReadMsg(const fpi::FDSPMsgTypeId &msgTypeId,
                                    SHPTR<MsgT> &msg, const VolumeResponseCb &cb)
{
    runSynchronized([this, msgTypeId, msg, cb]() mutable {
        GROUPHANDLE_FUNCTIONAL_CHECK_CB(cb);

        /* Create a request and send */
        auto req = requestMgr_->newSvcRequest<VolumeGroupFailoverRequest>(this);
        req->setPayload(msgTypeId, msg);
        req->responseCb_ = cb;
        req->setTaskExecutorId(groupId_);
        req->invoke();
    });
}

template<class MsgT>
void VolumeGroupHandle::sendModifyMsg(const fpi::FDSPMsgTypeId &msgTypeId,
                                      SHPTR<MsgT> &msg, const VolumeResponseCb &cb) {
    runSynchronized([this, msgTypeId, msg, cb]() mutable {
        GROUPHANDLE_FUNCTIONAL_CHECK_CB(cb);

        opSeqNo_++;
        sendWriteReq_<MsgT, VolumeGroupBroadcastRequest>(msgTypeId, msg, cb);
    });
}

template<class MsgT>
void VolumeGroupHandle::sendWriteMsg(const fpi::FDSPMsgTypeId &msgTypeId,
                                     SHPTR<MsgT> &msg, const VolumeResponseCb &cb) {
    runSynchronized([this, msgTypeId, msg, cb]() mutable {
        GROUPHANDLE_FUNCTIONAL_CHECK_CB(cb);

        opSeqNo_++;
        commitNo_++;
        sendWriteReq_<MsgT, VolumeGroupBroadcastRequest>(msgTypeId, msg, cb);
    });
}


}  // namespace fds

#endif   // SOURCE_INCLUDE_NET_VOLUMEGROUPHANDLE_H_
