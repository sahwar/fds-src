/* Copyright 2015 Formation Data Systems, Inc.
 */
#include <vector>
#include <net/VolumeGroupHandle.h>
#include <net/SvcRequestPool.h>
#include <json/json.h>

namespace fds {

#define NONE_RET

#define ASSERT_SYNCHRONIZED()

#define INVALID_REAPLICA_HANDLE() functionalReplicas_.end()

#define GET_VOLUMEREPLICA_HANDLE_RETURN(volumeHandle__, svcUuid__, ret__) \
    auto volumeHandle__ = getVolumeReplicaHandle_(svcUuid__); \
    do { \
    if (volumeHandle__ == INVALID_REAPLICA_HANDLE()) { \
        LOGWARN << "Failed to lookup replica handle for: " << svcUuid__.svc_uuid; \
        return ret__; \
    } \
    } while (false)

#define GET_VOLUMEREPLICA_HANDLE(volumeHandle__, svcUuid__) \
        GET_VOLUMEREPLICA_HANDLE_RETURN(volumeHandle__, svcUuid__, NONE_RET)

uint32_t VolumeGroupHandle::GROUPCHECK_INTERVAL_SEC = 30;

std::ostream& operator << (std::ostream &out, const fpi::VolumeIoHdr &h)
{
    out << " groupid:" << h.groupId 
        << " svcUuid:" << SvcMgr::mapToSvcUuidAndName(h.svcUuid)
        << " opid:" << h.opId
        << " commitid:" << h.commitId;
    return out;
}

std::string VolumeReplicaHandle::logString() const
{
    std::stringstream ss;
    ss << " [VolumeReplicaHandle" 
        << " svcUuid:" << SvcMgr::mapToSvcUuidAndName(svcUuid)
        << " version:" << version
        << " state:" << fpi::_ResourceState_VALUES_TO_NAMES.at(static_cast<int>(state))
        << " lasterr:" << lastError
        << " appliedOp:" << appliedOpId
        << " appliedCommit:" << appliedCommitId
        << "] ";

    return ss.str();
}

VolumeGroupHandle::VolumeGroupHandle(CommonModuleProviderIf* provider,
                                     const fds_volid_t& volId,
                                     uint32_t quorumCnt)
: HasModuleProvider(provider)
{
    taskExecutor_ = MODULEPROVIDER()->getSvcMgr()->getTaskExecutor();
    requestMgr_ = MODULEPROVIDER()->getSvcMgr()->getSvcRequestMgr();
    groupSize_ = 0;
    quorumCnt_ = quorumCnt;

    state_ = fpi::ResourceState::Unknown;   // same as closed
    groupId_ = volId.get();
    version_ = VolumeGroupConstants::VERSION_INVALID;

    stateProviderId_ = "volumegrouphandle." + std::to_string(groupId_);

    opSeqNo_ = VolumeGroupConstants::OPSTARTID;
    commitNo_ = VolumeGroupConstants::COMMITSTARTID;

    dmtVersion_ = DMT_VER_INVALID;

    refCnt_ = 0;

    closeCb_ = nullptr;
    checkOnNonFunctionalScheduled_ = false;

    isCoordinator_ = false;

    if (MODULEPROVIDER()->get_cntrs_mgr()) {
        MODULEPROVIDER()->get_cntrs_mgr()->add_for_export(this);
    }
}

VolumeGroupHandle::~VolumeGroupHandle()
{
    if (MODULEPROVIDER()->get_cntrs_mgr()) {
        MODULEPROVIDER()->get_cntrs_mgr()->remove_from_export(this);
    }
}

void VolumeGroupHandle::setListener(VolumeGroupHandleListener *listener)
{
    listener_ = listener;
}

void VolumeGroupHandle::toggleWriteOpsBuffering_(bool enable)
{
    if (enable) {
        writeOpsBuffer_.reset(new WriteOpsBuffer(WRITEOPS_BUFFER_SZ));
        LOGNORMAL << logString() << " Enabled write ops buffering";
    } else {
        writeOpsBuffer_.reset();
        LOGNORMAL << logString() << " Disabled write ops buffering";
    }
}

bool VolumeGroupHandle::replayFromWriteOpsBuffer_(const VolumeReplicaHandle &handle,
                                                  const int64_t replayStartOpId)
{
    fds_assert(opSeqNo_ >= static_cast<int64_t>(writeOpsBuffer_->size()));
    /* Valid request range is a subset range of (OPSTARTID, opSeqNo_+1]
     * NOTE: the first opid is OPSTARTID+1
     */
    fds_assert(replayStartOpId > VolumeGroupConstants::OPSTARTID &&
               replayStartOpId <= opSeqNo_+1);
    if (replayStartOpId == opSeqNo_+1) {
        /* Requester already has all the ops.  There is nothing to replay */
        return true;
    }

    /* Figure out the range for ops buffered */
    int64_t fromOpId = opSeqNo_ - writeOpsBuffer_->size() + 1;
    int64_t toOpId = opSeqNo_;
    if (replayStartOpId < fromOpId || replayStartOpId > toOpId) {
        LOGWARN << logString() << handle.logString()
            << "replay start opid: " << replayStartOpId << " doesn't fall in the range ["
            << fromOpId << ", " << toOpId << "]";
        return false;
    }

    LOGNORMAL << logString() << handle.logString()
        << " replaying writeOpsBuffer from opid: " << replayStartOpId
        << " to opid: " << opSeqNo_;

    int32_t idx = replayStartOpId - fromOpId;
    for (; idx < static_cast<int64_t>(writeOpsBuffer_->size()); idx++) {
        auto req = requestMgr_->newEPSvcRequest(handle.svcUuid);
        req->setTaskExecutorId(groupId_);
        req->setPayloadBuf((*writeOpsBuffer_)[idx].first, (*writeOpsBuffer_)[idx].second);
        /* NOTE: For now send as fire and forget.  This is fine because we haven't considered
         * the replica to be functional yet.  Also, on the DM side incase opid doens't match
         * DM will reject messages
         */
        req->invokeDirect();
    }
    return true;
}

void VolumeGroupHandle::resetGroup_(fpi::ResourceState state)
{
    opSeqNo_ = VolumeGroupConstants::OPSTARTID;
    commitNo_ = VolumeGroupConstants::COMMITSTARTID;

    /* Make all the replica handles non-functional */
    auto svcMgr = MODULEPROVIDER()->getSvcMgr();
    dmtVersion_ = svcMgr->getDMTVersion();
    auto svcs = svcMgr->getDMTNodesForVolume(fds_volid_t(groupId_))->toSvcUuids();
    functionalReplicas_.clear();
    nonfunctionalReplicas_.clear();
    syncingReplicas_.clear();
    groupSize_ = svcs.size();

    if (isCoordinator_) {
        for (const auto &svcUuId : svcs) {
            nonfunctionalReplicas_.push_back(VolumeReplicaHandle(svcUuId));
        }
    } else {
        /* When group handle isn't coordinator, only reads are allowed.  We assume everyone
         * is functional..and just failover as errors are encountered
         */
        for (const auto &svcUuId : svcs) {
            functionalReplicas_.push_back(VolumeReplicaHandle(svcUuId));
            functionalReplicas_.back().setState(fpi::ResourceState::Active);
        }
    }

    changeState_(state,
                 false, /* don't clear replica lists */
                 __FUNCTION__);

}

void VolumeGroupHandle::open(const SHPTR<fpi::OpenVolumeMsg>& msg,
                             const OpenResponseCb &cb)
{
    /* Wrapper to decrement refcount before invoking client callback */
    auto clientCb = [this, cb](const Error &e, const fpi::OpenVolumeRspMsgPtr &resp) {
        decRef();
        cb(e, resp);
    };

    runSynchronized([this, msg, clientCb]() mutable {
        incRef();

        if (state_ == fpi::ResourceState::Active) {
            LOGWARN << logString() << " - Trying open an already opened volume";
            clientCb(ERR_INVALID, nullptr);
            return;
        }

        if (msg->mode.can_write) {
            isCoordinator_ = true;
        }

        try {
            /* In the increase version # and reset replica handles and look up the group
             * from DMT
             */
            resetGroup_(fpi::Loading);
        } catch (const Exception &e) {
            LOGWARN << logString() << " - Failed to get nodes from DMT";
            clientCb(e.getError(), nullptr);
            return;
        }

        /* Send open volume against the group to prepare for open */
        auto openReq = createPreareOpenVolumeGroupMsgReq_();
        openReq->onResponseCb([this, clientCb](QuorumSvcRequest* openReq,
                                              const Error& e_,
                                              boost::shared_ptr<std::string> payload) {
         Error e = e_;
         auto responseMsg = fds::deserializeFdspMsg<fpi::OpenVolumeRspMsg>(e, payload);
         if (e != ERR_OK) {
             LOGWARN << logString() << "Prepare for open failed: " << e;
             clientCb(e, nullptr);
             return;
         }

         if (!isCoordinator_) {
            handleOpenResponseForNonCoordinator_(openReq, clientCb);
            return;
         }

         auto openResp = determineFunctaionalReplicas_(openReq);
         if (functionalReplicas_.size() == 0) {
             LOGWARN << logString()
                 << " Not enough members with latest state to start a group";
             clientCb(ERR_VOLUMEGROUP_DOWN, nullptr);
             return;
         }
         /* Functional group is determined.  Broadcase the group info so that functaional
          * group members can set their state to functional
          */
         auto broadcastGroupReq = createBroadcastGroupInfoReq_();
         broadcastGroupReq->onResponseCb([this, openResp, clientCb](QuorumSvcRequest*,
                                              const Error& e_,
                                              boost::shared_ptr<std::string> payload) {
             /* NOTE: We don't care about the returned error here.  We want to be sure
              * the broadcasted group information reached the group members
              */
             changeState_(fpi::ResourceState::Active,
                          false, /* This value is noop */
                          "Open volume");
             clientCb(ERR_OK, openResp);
         });
         LOGNORMAL << logString() << " - Broadcast group info";
         broadcastGroupReq->invoke();
       });
       LOGNORMAL << logString() << " - Prepare for open request to group";
       openReq->invoke();
    });
}

void VolumeGroupHandle::close(const VoidCb &closeCb)
{
    runSynchronized([this, closeCb]() {
        groupSize_ = 0;
        changeState_(fpi::ResourceState::Unknown,
                     true,  /* Clear replica lists */
                     "Close");

        /* Notify OM to clear the coordinator */
        auto clearCoordinatorreq = createSetVolumeGroupCoordinatorMsgReq_(true);
        clearCoordinatorreq->invokeDirect();

        if (refCnt_ == 0) {
            closeCb_ = nullptr;
            closeCb();
        } else if (!closeCb_) {
            closeCb_ = closeCb;
        }
    });
}

std::string VolumeGroupHandle::logString() const
{
    std::stringstream ss;
    ss << " [VolumeGroupHandle:" << groupId_
        << (!isCoordinator_ ? ":R" : "")
        << " version:" << version_
        << " state:" << fpi::_ResourceState_VALUES_TO_NAMES.at(static_cast<int>(state_))
        << " up:" << functionalReplicas_.size()
        << " sync:" << syncingReplicas_.size()
        << " down:" << nonfunctionalReplicas_.size()
        << " opid:" << opSeqNo_
        << " sequenceid:" << commitNo_
        << "] ";
    return ss.str();
}

std::string VolumeGroupHandle::getStateInfo()
{
    /* NOTE: Getting the stateinfo isn't synchronized.  It may be a bit stale */
    Json::Value state;
    state["state"] = fpi::_ResourceState_VALUES_TO_NAMES.at(static_cast<int>(state_));
    state["version"] = static_cast<Json::Value::Int64>(version_);
    state["quorumcnt"] = quorumCnt_;
    state["up"] = static_cast<Json::Value::UInt>(functionalReplicas_.size());
    state["syncing"] = static_cast<Json::Value::UInt>(syncingReplicas_.size());
    state["down"] = static_cast<Json::Value::UInt>(nonfunctionalReplicas_.size());
    state["opid"] = static_cast<Json::Value::Int64>(opSeqNo_);
    state["sequenceid"] = static_cast<Json::Value::Int64>(commitNo_);

    auto f = [&state](const VolumeReplicaHandle &h) {
        std::string id = std::to_string(h.svcUuid.svc_uuid);
        state[id]["state"] = fpi::_ResourceState_VALUES_TO_NAMES.at(static_cast<int>(h.state));
        state[id]["version"] = h.version;
        state[id]["lasterror"] = h.lastError.GetErrName();
        state[id]["appliedopid"] = static_cast<Json::Value::Int64>(h.appliedOpId);
#ifdef COMMITID_SUPPORTED
        state[id]["appliedsequenceid"] = static_cast<Json::Value::Int64>(h.appliedCommitId);
#endif
    };
    for_each(functionalReplicas_.begin(), functionalReplicas_.end(), f);
    for_each(syncingReplicas_.begin(), syncingReplicas_.end(), f);

    std::stringstream ss;
    ss << state;
    return ss.str();
}

std::string VolumeGroupHandle::getStateProviderId()
{
    return stateProviderId_;
}

void VolumeGroupHandle::incRef()
{
    ++refCnt_;
}

void VolumeGroupHandle::decRef()
{
    --refCnt_;
    fds_assert(refCnt_ >= 0);
    if (refCnt_ <= 0 && closeCb_) {
        auto cb = std::move(closeCb_);
        closeCb_ = nullptr;
        cb();
    }
}

void VolumeGroupHandle::scheduleCheckOnNonfunctionalReplicas_()
{
    ASSERT_SYNCHRONIZED();
    fds_verify(isCoordinator_);

    if (checkOnNonFunctionalScheduled_) {
        return;
    }

    incRef();
    checkOnNonFunctionalScheduled_ = true;
    MODULEPROVIDER()->getTimer()->scheduleFunction(
        std::chrono::seconds(GROUPCHECK_INTERVAL_SEC),
        [this]() {
           checkOnNonFunctaionalReplicas_();
       });
}

void VolumeGroupHandle::checkOnNonFunctaionalReplicas_()
{
    runSynchronized([this]() mutable {
        checkOnNonFunctionalScheduled_ = false;

        if (closeCb_) {
            decRef();
            return;
        }

        /* Only schedule if we still have non-functional replicas */
        if (nonfunctionalReplicas_.size() > 0) {
            auto broadcastGroupReq = createBroadcastGroupInfoReq_();
            broadcastGroupReq->onResponseCb([](QuorumSvcRequest*, const Error&, StringPtr) {} );
            broadcastGroupReq->invoke();
            /* schedule the next round */
            scheduleCheckOnNonfunctionalReplicas_();
        }
        decRef();
    });
}

EPSvcRequestPtr
VolumeGroupHandle::createSetVolumeGroupCoordinatorMsgReq_(bool clearCoordinator)
{
    auto msg = MAKE_SHARED<fpi::SetVolumeGroupCoordinatorMsg>();
    if (clearCoordinator) {
        msg->coordinator.id.svc_uuid = 0;
    } else {
        msg->coordinator.id =  MODULEPROVIDER()->getSvcMgr()->getSelfSvcUuid();
    }
    msg->coordinator.version = version_;
    msg->volumeId = groupId_;
    auto omUuid = MODULEPROVIDER()->getSvcMgr()->getOmSvcUuid();
    auto req = requestMgr_->newEPSvcRequest(omUuid);
    req->setTaskExecutorId(groupId_);
    req->setPayload(FDSP_MSG_TYPEID(fpi::SetVolumeGroupCoordinatorMsg), msg);
    return req;
}

QuorumSvcRequestPtr
VolumeGroupHandle::createPreareOpenVolumeGroupMsgReq_()
{
    std::vector<fpi::SvcUuid> replicas;
    if (isCoordinator_) {
        fds_assert(functionalReplicas_.size() == 0 &&
                   syncingReplicas_.size() == 0 &&
                   nonfunctionalReplicas_.size() > 0);
        std::for_each(nonfunctionalReplicas_.begin(),
                      nonfunctionalReplicas_.end(),
                      [&replicas](const VolumeReplicaHandle &h) { replicas.push_back(h.svcUuid); });
    } else {
        fds_assert(functionalReplicas_.size() > 0 &&
                   syncingReplicas_.size() == 0 &&
                   nonfunctionalReplicas_.size() == 0);
        std::for_each(functionalReplicas_.begin(),
                      functionalReplicas_.end(),
                      [&replicas](const VolumeReplicaHandle &h) { replicas.push_back(h.svcUuid); });
    }

    auto prepareMsg = boost::make_shared<fpi::OpenVolumeMsg>();
    prepareMsg->volume_id = groupId_;
    prepareMsg->coordinator.id = MODULEPROVIDER()->getSvcMgr()->getSelfSvcUuid();
    prepareMsg->coordinator.version = version_;
    prepareMsg->mode.can_write = isCoordinator_;
    // TODO(Rao): Set the token.  Set cooridnator as well
    // prepareMsg->token = volReq->token;
    // prepareMsg->mode = volReq->mode;

    auto req = requestMgr_->newSvcRequest<QuorumSvcRequest>(getDmtVersion(), replicas);
    req->setTaskExecutorId(groupId_);
    req->setPayload(FDSP_MSG_TYPEID(fpi::OpenVolumeMsg), prepareMsg);
    req->setQuorumCnt(quorumCnt_);
    req->setWaitForAllResponses(true);
    return req;
}

fpi::OpenVolumeRspMsgPtr
VolumeGroupHandle::determineFunctaionalReplicas_(QuorumSvcRequest* openReq)
{
    using Response = std::pair<fpi::SvcUuid, fpi::OpenVolumeRspMsgPtr>;
    std::vector<Response> successSvcs;
    std::vector<fpi::SvcUuid> errdSvcs;

    /* Split services that responded succusfully from the ones that failed */
    auto sz = size();    
    for (int32_t i = 0; i < sz; i++) {
        const auto &ep = openReq->ep(i);
        if (ep->responseStatus() == ERR_OK) {
            Error e;
            auto resp = deserializeFdspMsg<fpi::OpenVolumeRspMsg>(e, ep->responsePayload());
            if (e != ERR_OK) {
                fds_assert(!"Deserialization shouldn't fail here");
                errdSvcs.push_back(ep->getPeerEpId());
                continue;
            }
            successSvcs.push_back(std::make_pair(ep->getPeerEpId(), resp));
            LOGNORMAL << logString()
                << " open success svc: " << fds::logString(ep->getPeerEpId())
                << " sequenceid: " << resp->sequence_id;
        } else {
            errdSvcs.push_back(ep->getPeerEpId());
            LOGNORMAL << logString()
                << " open errored svc: " << fds::logString(ep->getPeerEpId())
                << ep->responseStatus();
        }
    }

    /* Sort open responses by sequence id in descending order */
    std::sort(successSvcs.begin(),
              successSvcs.end(),
              [](const Response &lhs, const Response &rhs) {
                  return lhs.second->sequence_id > rhs.second->sequence_id;
              });

    /* Figure out if have quorum # of responses with latest state */
    uint32_t latestStateReplicas = 1;
    for (uint32_t i = 1; i < successSvcs.size(); ++i) {
        if (successSvcs[i-1].second->sequence_id != successSvcs[i].second->sequence_id) {
            break;
        }
        ++latestStateReplicas;
    }
    if (latestStateReplicas < quorumCnt_) {
        LOGWARN << logString() << " Not enough members with latest state to start a group."
            << " latest state replicas count: " << latestStateReplicas
            << " quorum count: " << quorumCnt_;
        return nullptr;
    }
    for (uint32_t i = 0; i < latestStateReplicas; i++) {
        auto volumeHandle = getVolumeReplicaHandle_(successSvcs[i].first);
        /* For replica handle to be functional transition from syncing to functional so that
         * all the accounting is taken care for ids appropriately
         */
        changeVolumeReplicaState_(volumeHandle,
                                  successSvcs[i].second->replicaVersion,
                                  fpi::ResourceState::Syncing,
                                  opSeqNo_,
                                  ERR_OK,
                                  "Open");
        changeVolumeReplicaState_(volumeHandle,
                                  successSvcs[i].second->replicaVersion,
                                  fpi::ResourceState::Active,
                                  opSeqNo_,
                                  ERR_OK,
                                  "Open");
    }

    commitNo_ = successSvcs[0].second->sequence_id;
    version_ = commitNo_;

    return successSvcs[0].second;
}

QuorumSvcRequestPtr
VolumeGroupHandle::createBroadcastGroupInfoReq_()
{
    auto msg = MAKE_SHARED<fpi::VolumeGroupInfoUpdateCtrlMsg>();
    msg->group = getGroupInfoForExternalUse_();
    auto req = requestMgr_->newSvcRequest<QuorumSvcRequest>(
        getDmtVersion(),
        getAllReplicas());
    req->setTaskExecutorId(groupId_);
    req->setPayload(FDSP_MSG_TYPEID(fpi::VolumeGroupInfoUpdateCtrlMsg), msg);
    return req;
}

void
VolumeGroupHandle::handleOpenResponseForNonCoordinator_(QuorumSvcRequest* openReq,
                                                        const OpenResponseCb &cb)
{
    int nOks = 0;
    int nNotActives = 0;
    auto sz = size();    
    for (int32_t i = 0; i < sz; i++) {
        const auto &ep = openReq->ep(i);
        if (ep->responseStatus() == ERR_OK) {
#if 0
            Error e;
            auto resp = deserializeFdspMsg<fpi::OpenVolumeRspMsg>(e, ep->responsePayload());
            if (e != ERR_OK) {
                fds_assert(!"Deserialization shouldn't fail here");
                continue;
            }
#endif
            ++nOks;
            LOGNORMAL << logString()
                << " open success svc: " << fds::logString(ep->getPeerEpId());
        } else {
            if (ep->responseStatus() == ERR_DM_VOL_NOT_ACTIVATED) {
                ++nNotActives;
            }
            LOGNORMAL << logString()
                << " open errored svc: " << fds::logString(ep->getPeerEpId())
                << ep->responseStatus();
        }
    }

    if (nOks > 0) {
        /* At least one replica is active, which means it can be read */
        changeState_(fpi::ResourceState::Active,
                     false, /* This value is noop */
                     "Open volume");
        cb(ERR_OK, nullptr);
    } else if (nNotActives == sz) {
        /* Returning this error means, coordinator hasn't been set yet.
         * For this volume group to be active a coordinator is required.
         * Returning this error is an indication to the caller to become
         * a coordinator inorder to read.
         */
        cb(ERR_DM_VOL_NOT_ACTIVATED, nullptr);
    } else {
        /* None of the replicas are available for read.  It's possible to issue reads
         * after open call, but most likely they will fail until a coordinator sucessfully
         * opens the volume group.
         */
        changeState_(fpi::ResourceState::Active,
                     false, /* This value is noop */
                     "Open volume");
        cb(ERR_VOLUMEGROUP_DOWN, nullptr);
    }
}

void VolumeGroupHandle::changeState_(const fpi::ResourceState &targetState,
                                     bool cleanReplicas,
                                     const std::string& logCtx)
{
    if (targetState == fpi::ResourceState::Active) {
        fds_assert(functionalReplicas_.size() >= quorumCnt_);
    } else {
        if (cleanReplicas) {
            functionalReplicas_.clear();
            nonfunctionalReplicas_.clear();
            syncingReplicas_.clear();
        }
    }
    state_ = targetState;

    fds_assert(groupSize_ == (functionalReplicas_.size() +
                              nonfunctionalReplicas_.size() +
                              syncingReplicas_.size()));

    LOGNORMAL << logString() << " - State changed.  Context: " << logCtx;
}

void VolumeGroupHandle::handleAddToVolumeGroupMsg(
    const fpi::AddToVolumeGroupCtrlMsgPtr &addMsg,
    const AddToVolumeGroupCb &cb)

{
    runSynchronized([this, addMsg, cb]() mutable {
        /* Make sure we are not in the middle of open or prior open */
        if (state_ == fpi::ResourceState::Unknown ||
            state_ == fpi::ResourceState::Loading) {
            LOGDEBUG << "Rejecting AddToVolumeGroupCtrlMsg from : "
                << SvcMgr::mapToSvcUuidAndName(addMsg->svcUuid)
                << " as group handle is not in active state";
            cb(ERR_NOT_READY, MAKE_SHARED<fpi::AddToVolumeGroupRespCtrlMsg>());
            return;
        } else if (!isCoordinator_) {
            LOGDEBUG << "Rejecting AddToVolumeGroupCtrlMsg from : "
                << SvcMgr::mapToSvcUuidAndName(addMsg->svcUuid)
                << " as group handle is not a coordinator";
            cb(ERR_INVALID_COORDINATOR, MAKE_SHARED<fpi::AddToVolumeGroupRespCtrlMsg>());
            return;
        }
        fds_assert(state_ == fpi::ResourceState::Active ||
                   state_ == fpi::ResourceState::Offline);

        auto respMsg = MAKE_SHARED<fpi::AddToVolumeGroupRespCtrlMsg>();
        auto volumeHandle = getVolumeReplicaHandle_(addMsg->svcUuid);
        if (volumeHandle == INVALID_REAPLICA_HANDLE()) {
            LOGWARN << "Failed to lookup replica handle for: "
                << SvcMgr::mapToSvcUuidAndName(addMsg->svcUuid);
            cb(ERR_INVALID, respMsg);
            return;
        }

        Error err(ERR_OK);
        err = changeVolumeReplicaState_(volumeHandle,
                                        addMsg->replicaVersion,
                                        addMsg->targetState,
                                        addMsg->lastOpId,
                                        ERR_OK,
                                        "AddToVolumeGroupMsg");

        respMsg->group = getGroupInfoForExternalUse_();
        cb(err, respMsg);
    });
}

void VolumeGroupHandle::handleVolumeResponse(const fpi::SvcUuid &srcSvcUuid,
                                             const int32_t &replicaVersion,
                                             const fpi::VolumeIoHdr &hdr,
                                             const fpi::FDSPMsgTypeId &msgTypeId,
                                             const bool writeReq,
                                             const Error &inStatus,
                                             VolumeGroupRequest::Acks &successAcks)
{
    ASSERT_SYNCHRONIZED();

    fds_assert(isCoordinator_ || !writeReq);

    /* Make sure we are not in the middle of open or prior open */
    if (state_ == fpi::ResourceState::Unknown ||
        state_ == fpi::ResourceState::Loading) {
        LOGWARN << "VolumeGroupHandle not active: "
            << logString()
            << " Rejecting response from svcuuid: "
            << SvcMgr::mapToSvcUuidAndName(srcSvcUuid)
            << " opid: " << hdr.opId << " version: " << replicaVersion << inStatus;
        return;
    }

    LOGDEBUG << "svcuuid: " << SvcMgr::mapToSvcUuidAndName(srcSvcUuid)
        << " opid: " << hdr.opId << " version: " << replicaVersion << inStatus;

    auto volumeHandle = getVolumeReplicaHandle_(srcSvcUuid);

    /* Do a version check to ignore responses from previous incarnation of volume replica */
    if (replicaVersion != volumeHandle->version) {
        LOGWARN << "Ignoring response.  Version check failed. svcuuid: "
            << SvcMgr::mapToSvcUuidAndName(srcSvcUuid)
            << volumeHandle->logString()
            << " incoming version:  " << replicaVersion
            << " incoming opid: " << hdr.opId << inStatus;
        return;
    }

    if (volumeHandle->isFunctional() ||
        volumeHandle->isSyncing()) {
        bool isError = isVolumeGroupError(inStatus);

        if (!isError) {
            /* Do opid sequence checks only for requests that mutate state on replica */
            if (writeReq) {
                fds_verify(volumeHandle->appliedOpId+1 == hdr.opId);
                volumeHandle->appliedOpId = hdr.opId;
#ifdef COMMITID_SUPPORTED
                fds_verify(volumeHandle->appliedCommitId == hdr.commitId ||
                           volumeHandle->appliedCommitId+1 == hdr.commitId);
                volumeHandle->appliedCommitId = hdr.commitId;
#endif
            }
            if (volumeHandle->isFunctional()) {
                successAcks.emplace_back(std::make_pair(srcSvcUuid, inStatus));
            }
        } else {
            LOGWARN << "Received an error.  svcuuid: "
                << SvcMgr::mapToSvcUuidAndName(srcSvcUuid)
                << " opid: " << hdr.opId << " version: " << replicaVersion << inStatus;
            if (!isCoordinator_) {
                /* When group handle isn't a coordinator it doesn't manage states about
                 * replicas.  Only reads are allowed and it just cycles through replicas
                 * in a failover manner.
                 * We move the failed replica to end of functional list as an optimiation
                 * to not send reads immediately.
                 */
                fds_verify(volumeHandle == functionalReplicas_.begin());
                /* Left shift and rotate by one */
                std::rotate(functionalReplicas_.begin(), functionalReplicas_.begin()+1,
                            functionalReplicas_.end());
                return;
            }
            auto changeErr = changeVolumeReplicaState_(volumeHandle,
                                                       volumeHandle->version,
                                                       fpi::ResourceState::Offline,
                                                       opSeqNo_,
                                                       inStatus,
                                                       __FUNCTION__);
            fds_verify(changeErr == ERR_OK);
        }
    } else {
        fds_assert(isCoordinator_);
        /* When replica isn't functional we don't expect subsequent IO to return with
         * success status
         */
        if (inStatus == ERR_OK) {
            LOGWARN << "ERR_OK response from non-functional replica.  svcuuid: "
                << SvcMgr::mapToSvcUuidAndName(srcSvcUuid)
                << " opid: " << hdr.opId << " version: " << replicaVersion
                << volumeHandle->logString();
        }
    }
}

std::vector<VolumeReplicaHandle*> VolumeGroupHandle::getWriteableReplicaHandles()
{
    std::vector<VolumeReplicaHandle*> replicas;
    for (auto &r : functionalReplicas_) {
        replicas.push_back(&r);
    }
    for (auto &r : syncingReplicas_) {
        replicas.push_back(&r);
    }
    return replicas;
}

VolumeReplicaHandle* VolumeGroupHandle::getFunctionalReplicaHandle()
{
    return &(functionalReplicas_.front());
}

std::vector<fpi::SvcUuid> VolumeGroupHandle::getAllReplicas() const
{
    std::vector<fpi::SvcUuid> svcs;
    auto appendF = [&svcs](const VolumeReplicaHandle& h) { svcs.push_back(h.svcUuid); };
    std::for_each(functionalReplicas_.begin(), functionalReplicas_.end(), appendF);
    std::for_each(nonfunctionalReplicas_.begin(), nonfunctionalReplicas_.end(), appendF);
    std::for_each(syncingReplicas_.begin(), syncingReplicas_.end(), appendF);
    return svcs;
}

VolumeGroupHandle::VolumeReplicaHandleList&
VolumeGroupHandle::getVolumeReplicaHandleList_(const fpi::ResourceState& s)
{
    if (VolumeReplicaHandle::isFunctional(s)) {
        return functionalReplicas_;
    } else if (VolumeReplicaHandle::isSyncing(s)) {
        return syncingReplicas_;
    } else {
        fds_verify(VolumeReplicaHandle::isNonFunctional(s));
        return nonfunctionalReplicas_;
    }
}

/**
* @brief 
* NOTE: Ensure all state changes for volume go through this function so that we have
* central function to coordinate state changes
*
* @param volumeHandle
* @param targetState
*
* @return 
*/
Error VolumeGroupHandle::changeVolumeReplicaState_(VolumeReplicaHandleItr &volumeHandle,
                                                   const int32_t &replicaVersion,
                                                   const fpi::ResourceState &targetState,
                                                   const int64_t &opId,
                                                   const Error &e,
                                                   const std::string &context)
{
    fds_assert(isCoordinator_);

    /* VolumeReplicaHandle state transitions
     * Transition cycle is expected to be Loading->Syncing -> Active -> Offline
     * Loading doesn't require any state change.  It's more of a query to figure
     * out the current group state.
     * On every new transition cycle starting with Syncing, version # is expected
     * to be incremented.
     * On a particual version # states transtions can only take place in the above cycle order.
     * For ex. at replica handle at version 1, can't go from state Active to Syncing on same
     * version #.
     */
    auto srcState = volumeHandle->state;

    if (targetState == fpi::ResourceState::Loading) {
        /* Set the replica handle to offline.  This marks the end of current version.
         * Version is incremented when volume comes back with target state of syncing.
         * Now, we just need to respond back with current group information so it can copy 
         * active transactions from a peer.
         * We will start buffering writes so that when replica handles come back again
         * after copying active transactions, we will replay buffered writes.
         */
        volumeHandle->setState(fpi::Offline);
        toggleWriteOpsBuffering_(true);

    } else if (VolumeReplicaHandle::isSyncing(targetState)) {
        /* We expect the replica version to always go up */
        if (replicaVersion != VolumeGroupConstants::VERSION_START &&
            replicaVersion <= volumeHandle->version) {
            fds_assert(!"Invalid version");
            return ERR_INVALID_VERSION;
        }
        if (writeOpsBuffer_) {
            /* Replica has/will get to active state upto opId, we will replay
             * range [opId+1, opSeqNo_]
             */
            replayFromWriteOpsBuffer_(*volumeHandle, opId+1);
            toggleWriteOpsBuffering_(false);
        }
        /* Transition to sync state.  From this point Replica will receive
         * write ops from opSeqNo_.
         */
        volumeHandle->setInfo(replicaVersion, targetState, opSeqNo_, commitNo_);

    } else if (VolumeReplicaHandle::isNonFunctional(targetState)) {
        /* Loading is already taken care of above.  At the moment only offline is the
         * other non-functional state
         */
        fds_assert(targetState == fpi::ResourceState::Offline);
        volumeHandle->setState(targetState);
        volumeHandle->setError(e);

        scheduleCheckOnNonfunctionalReplicas_();

    } else if (VolumeReplicaHandle::isFunctional(targetState)){
        if (replicaVersion != volumeHandle->version) {
            fds_assert(!"Invalid version: expecting %d got %d");
            return ERR_INVALID_VERSION;
        }
        /* When transition to functional we must transition from sync state and latest opids
         * must match.  NOTE: We only asssert because opid checks will fail at volume replica.
         */
        fds_assert(volumeHandle->isSyncing() && volumeHandle->appliedOpId == opId);
        volumeHandle->setVersion(replicaVersion);
        volumeHandle->setState(targetState);
        volumeHandle->setError(ERR_OK);

    }

    /* Move the handle from the appropriate replica list
     * NOTE: we move volumeHandle to the end in the destination list
     */
    auto &srcList = getVolumeReplicaHandleList_(srcState);
    /* Ensure volumeHandle exists in srcList */
    fds_assert(std::find_if(srcList.begin(), \
                            srcList.end(), \
                            [&volumeHandle](const VolumeReplicaHandle &h) \
                            {return volumeHandle->svcUuid == h.svcUuid;}) != srcList.end());
    auto &dstList = getVolumeReplicaHandleList_(targetState);
    auto dstItr = std::move(volumeHandle, volumeHandle+1, std::back_inserter(dstList));
    srcList.erase(volumeHandle, volumeHandle+1);
    volumeHandle = dstList.end() - 1;

    /* After moving ensure volume handle state maches the container list it is supposed to be in */
    fds_assert(&(getVolumeReplicaHandleList_(volumeHandle->state)) == &dstList);

    LOGNORMAL << logString() << volumeHandle->logString()
        << " state changed. Context - " << context;

    /* Volumegroup state change actions */
    if (VolumeReplicaHandle::isFunctional(targetState)) {
        /* Check if offline volumegroup needs to become functional again */
        if (state_ == fpi::ResourceState::Offline &&
            functionalReplicas_.size() == quorumCnt_) {
            changeState_(fpi::ResourceState::Active, false, /* This is a noop */
                         " - funcationl again.  Met the quorum count");
        }
    } else if (VolumeReplicaHandle::isNonFunctional(targetState)) {
        /* Check if the group needs to become offline */
        if (state_ == fpi::ResourceState::Active &&
            functionalReplicas_.size() < quorumCnt_) {
            changeState_(fpi::ResourceState::Offline,
                         false,  /* Don't clear replica lists */
                         " not enough active replicas");
        } 
        if (state_ == fpi::ResourceState::Offline && functionalReplicas_.size() == 0) {
            /* When we have zero functional replicas we reset opSeqNo_ */
            opSeqNo_ = VolumeGroupConstants::OPSTARTID;
            LOGNORMAL << logString()
                << " - # functional replicas is zero.  Resetting opid";
        }
    }

    return ERR_OK;
}

fpi::VolumeGroupInfo VolumeGroupHandle::getGroupInfoForExternalUse_()
{
    fpi::VolumeGroupInfo groupInfo; 
    groupInfo.groupId = groupId_;
    groupInfo.coordinator.id = MODULEPROVIDER()->getSvcMgr()->getSelfSvcUuid();
    groupInfo.coordinator.version = version_;
    groupInfo.lastOpId = opSeqNo_;
    groupInfo.lastCommitId = commitNo_;
    for (const auto &replica : functionalReplicas_) {
        groupInfo.functionalReplicas.push_back(replica.svcUuid); 
    }
    for (const auto &replica : syncingReplicas_) {
        groupInfo.syncingReplicas.push_back(replica.svcUuid); 
    }
    for (const auto &replica : nonfunctionalReplicas_) {
        groupInfo.nonfunctionalReplicas.push_back(replica.svcUuid); 
    }
    return groupInfo;
}

void VolumeGroupHandle::setGroupInfo_(const fpi::VolumeGroupInfo &groupInfo)
{
    groupId_ = groupInfo.groupId;
    version_ = groupInfo.coordinator.version;
    opSeqNo_ = groupInfo.lastOpId;
    commitNo_ = groupInfo.lastCommitId;
    functionalReplicas_.clear();
    for (const auto &svcUuId : groupInfo.functionalReplicas) {
        functionalReplicas_.push_back(VolumeReplicaHandle(svcUuId));
    }
    nonfunctionalReplicas_.clear();
    for (const auto &svcUuId : groupInfo.nonfunctionalReplicas) {
        nonfunctionalReplicas_.push_back(VolumeReplicaHandle(svcUuId));
    }
}

VolumeGroupHandle::VolumeReplicaHandleItr
VolumeGroupHandle::getVolumeReplicaHandle_(const fpi::SvcUuid &svcUuid)
{
#define CHECKIN(__list) \
    for (auto itr = __list.begin(); itr != __list.end(); itr++) { \
        if (itr->svcUuid == svcUuid) { \
            return itr; \
        } \
    }
    
    CHECKIN(functionalReplicas_);
    CHECKIN(nonfunctionalReplicas_);
    CHECKIN(syncingReplicas_);
    return INVALID_REAPLICA_HANDLE();
}

VolumeGroupRequest::VolumeGroupRequest(CommonModuleProviderIf* provider,
                                       const SvcRequestId &id,
                                       const fpi::SvcUuid &myEpId,
                                       VolumeGroupHandle *groupHandle)
: MultiEpSvcRequest(provider, id, myEpId, 0, {}),
    groupHandle_(groupHandle),
    nAcked_(0)
{
    /* NOTE: This should be under coordinator synchronized context */
    groupHandle_->incRef();
}

VolumeGroupRequest::~VolumeGroupRequest()
{
    /* NOTE: This should be under coordinator synchronized context */
    groupHandle_->decRef();
}

std::string VolumeGroupRequest::logString()
{
    std::stringstream ss;
    // ss << volumeIoHdr_;
    return ss.str();
}

void VolumeGroupBroadcastRequest::invoke()
{
    invokeWork_();
}

void VolumeGroupBroadcastRequest::invokeWork_()
{
    auto replicas = groupHandle_->getWriteableReplicaHandles();
    state_ = SvcRequestState::INVOCATION_PROGRESS;
    for (const auto &r : replicas) {
        addEndpoint(r->svcUuid,
                    groupHandle_->getDmtVersion(),
                    groupHandle_->getGroupId(),
                    r->version);
        auto &ep = epReqs_.back();
        ep->setPayloadBuf(msgTypeId_, payloadBuf_);
        ep->setTimeoutMs(timeoutMs_);
        ep->invokeWork_();
    }
}

void VolumeGroupBroadcastRequest::handleResponse(SHPTR<fpi::AsyncHdr>& header,
                                                  SHPTR<std::string>& payload)
{
    DBG(GLOGDEBUG << fds::logString(*header));
    auto epReq = getEpReq_(header->msg_src_uuid);
    if (isComplete()) {
        /* Drop completed requests */
        GLOGWARN << logString() << " Already completed";
        return;
    } else if (!epReq) {
        /* Drop responses from uknown endpoint src ids */
        GLOGWARN << fds::logString(*header) << " Unknown EpId";
        return;
    } else if (epReq->isComplete()) {
        GLOGWARN << fds::logString(*header) << " Already completed";
        return;
    }
    epReq->completeReq(header->msg_code, header, payload);

    auto prevSuccessCnt = successAcks_.size();

    /* Have coordinator handle the response */
    groupHandle_->handleVolumeResponse(header->msg_src_uuid,
                                       header->replicaVersion,
                                       volumeIoHdr_,
                                       msgTypeId_,
                                       true,
                                       header->msg_code,
                                       successAcks_);

    if (prevSuccessCnt == successAcks_.size() && /* VolumeGroup considered it an error */
        response_.ok()) {
        /* Caching first error code. Used when quorum isn't met */
        if (header->msg_code == ERR_OK) {  /* when group is down this can happen */
            response_ = ERR_VOLUMEGROUP_DOWN;
        } else {
            response_ = header->msg_code;
        }
    }

    if (successAcks_.size() == groupHandle_->getQuorumCnt() &&
        responseCb_) {
        /* Met the quorum count.  Returning the last error code */
        // TODO(Rao): Ensure all replicas returned the same error code
        responseCb_(successAcks_[0].second, payload); 
        responseCb_ = 0;
    }
    ++nAcked_;
    if (nAcked_ == epReqs_.size()) {
        if (responseCb_) {
            /* Haven't met the quorum count */
            fds_assert(response_ != ERR_OK);
            responseCb_(response_, payload);
            responseCb_ = 0;
            complete(response_);
        } else {
            /* We've already met the quorum and responded to client.  Just complete with ok */
            complete(ERR_OK);
        }
    }
}

void VolumeGroupFailoverRequest::invoke()
{
    invokeWork_();
}

void VolumeGroupFailoverRequest::invokeWork_()
{
    auto replica = groupHandle_->getFunctionalReplicaHandle();
    addEndpoint(replica->svcUuid,
                groupHandle_->getDmtVersion(),
                groupHandle_->getGroupId(),
                replica->version);
    auto &ep = epReqs_.back();
    ep->setPayloadBuf(msgTypeId_, payloadBuf_);
    ep->setTimeoutMs(timeoutMs_);
    ep->invokeWork_();
}

void VolumeGroupFailoverRequest::handleResponse(SHPTR<fpi::AsyncHdr>& header,
                                                  SHPTR<std::string>& payload)
{
    DBG(GLOGDEBUG << fds::logString(*header));
    auto epReq = getEpReq_(header->msg_src_uuid);
    if (isComplete()) {
        /* Drop completed requests */
        GLOGWARN << logString() << " Already completed";
        return;
    } else if (!epReq) {
        /* Drop responses from uknown endpoint src ids */
        GLOGWARN << fds::logString(*header) << " Unknown EpId";
        return;
    } else if (epReq->isComplete()) {
        GLOGWARN << fds::logString(*header) << " Already completed";
        return;
    }
    epReq->completeReq(header->msg_code, header, payload);

    ++nAcked_;
    fds_assert(nAcked_ <= groupHandle_->size());

    /* Have coordinator handle the response */
    groupHandle_->handleVolumeResponse(header->msg_src_uuid,
                                       header->replicaVersion,
                                       volumeIoHdr_,
                                       msgTypeId_,
                                       false,
                                       header->msg_code,
                                       successAcks_);
    if (successAcks_.size() == 1) {
        /* Atleast one replica succeeded */
        fds_assert(groupHandle_->getFunctionalReplicasCnt() > 0);
        responseCb_(header->msg_code, payload); 
        responseCb_ = 0;
        complete(ERR_OK);
    } else {
        /* We continue as long as we haven't tried against all replicas (NOTE: When not
         * coordinator i.e read only, all replicas are considered functional)
         */
        if (nAcked_ < groupHandle_->size() &&
            groupHandle_->getFunctionalReplicasCnt() > 0) {
            /* Try against another replica */
            invokeWork_();
        } else {
            /* All replicas have failed..return error */
            responseCb_(header->msg_code, payload); 
            responseCb_ = 0;
            complete(header->msg_code);
        }
    }
}

}  // namespace fds
