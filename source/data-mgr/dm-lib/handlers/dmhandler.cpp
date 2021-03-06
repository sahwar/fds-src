/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#include <DataMgr.h>
#include <dmhandler.h>
#include <util/Log.h>

namespace fds { namespace dm {

RequestHelper::RequestHelper(DataMgr& dataManager, DmRequest *dmRequest)
    : dmRequest(dmRequest),
      _dataManager(dataManager)
{}

RequestHelper::~RequestHelper() {
    if (_dataManager.features.isQosEnabled()) {
        Error err = _dataManager.qosCtrl->enqueueIO(dmRequest->getVolId(), dmRequest);
        if (err != ERR_OK) {
            LOGWARN << "Unable to enqueue request for volid:" << dmRequest->getVolId();

            if (dmRequest->cb) {
                dmRequest->cb(err, dmRequest);
            } else {
                delete dmRequest;
            }
        }
    }
}

QueueHelper::QueueHelper(DataMgr& dataManager, DmRequest *dmRequest)
        : dmRequest(dmRequest),
          ioIsMarkedAsDone(false),
          cancelled(false),
          skipImplicitCb(false),
          dataManager_(dataManager)
{}

QueueHelper::~QueueHelper() {
    if (!cancelled) {
        markIoDone();
        if (!skipImplicitCb) {
            LOGDEBUG << "calling cb for volid: " << dmRequest->volId;
            dmRequest->cb(err, dmRequest);
        }
    }
}

void QueueHelper::markIoDone() {
    if (!ioIsMarkedAsDone) {
        if (dataManager_.features.isQosEnabled()) dataManager_.qosCtrl->markIODone(*dmRequest);
        ioIsMarkedAsDone = true;
    }
}

void QueueHelper::cancel() {
    cancelled = true;
}

Handler::Handler(DataMgr& dataManager)
    : dataManager(dataManager)
{}

void Handler::handleQueueItem(DmRequest *dmRequest) {
}

void Handler::addToQueue(DmRequest *dmRequest) {
    if (!dataManager.features.isQosEnabled()) {
        LOGWARN << "qos disabled .. not queuing";
        return;
    }
    const VolumeDesc * voldesc = dataManager.getVolumeDesc(dmRequest->getVolId());

    // Start timing the request so that we can track queueing latency.
    PerfTracer::tracePointBegin(dmRequest->opQoSWaitCtx);
    Error err = dataManager.qosCtrl->enqueueIO(voldesc && voldesc->isSnapshot()
                                               ? voldesc->qosQueueId
                                               : dmRequest->getVolId(),
                                               dmRequest);
    if (err != ERR_OK) {
        LOGWARN << "Unable to enqueue request for volid:" << dmRequest->getVolId();
        if (dmRequest->cb) {
            dmRequest->cb(err, dmRequest);
        } else {
            delete dmRequest;
        }
    } else {
        LOGTRACE << "dmrequest " << dmRequest << " added to queue successfully";
    }
}

Handler::~Handler() {
}

Error Handler::preEnqueueWriteOpHandling(const fds_volid_t &volId,
                                         const int64_t &opId,
                                         const fpi::AsyncHdrPtr &hdr,
                                         const SHPTR<std::string> &payload)
{
    // TODO(Rao): volMeta should be accessed under lock
    auto volMeta = dataManager.getVolumeMeta(volId);
    if (volMeta == nullptr || volMeta->vol_desc == nullptr) {
        return ERR_VOL_NOT_FOUND;
    } else if (dataManager.features.isVolumegroupingEnabled() &&
               !volMeta->isReplayOp(hdr) &&
               hdr->msg_src_uuid != volMeta->getCoordinatorId()) {
        /* It's better if this check is done under qos context.  For now this
         * should be ok.
         */
        LOGWARN << volMeta->logString() << " - coordinator mismatch"
                << " cached:" << volMeta->getCoordinatorId()
                << " from:" << hdr->msg_src_uuid;
        return ERR_INVALID_COORDINATOR;
    }
    if (volMeta->isActive()) {
        return ERR_OK;
    } else if (volMeta->isSyncing()) {
        /* When sync in progress we will receive active io as well as replay
         * for buffered io here.  Active io will need to be buffered while
         * replay is in progress, where as replay io will need to be applied
         */
        if (!volMeta->isReplayOp(hdr))  {
            Error e = volMeta->initializer->tryAndBufferIo(
                BufferReplay::Op{opId, hdr->msg_type_id, payload});
            if (e.ok()) {
                return ERR_WRITE_OP_BUFFERED;
            } else if (e == ERR_UNAVAILABLE) {
                /* Buffering is not required.  We've replayed all the buffered ops.
                 * Go through normal handling for active io
                 */
                return ERR_OK;
            }
            return e;
        }
        return ERR_OK;
    }
    return ERR_DM_VOL_NOT_ACTIVATED;
}

}  // namespace dm
}  // namespace fds
