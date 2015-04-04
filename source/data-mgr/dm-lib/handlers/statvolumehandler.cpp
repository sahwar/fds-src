/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

#include <dmhandler.h>
#include <DMSvcHandler.h>  // This shouldn't be necessary, included because of
                           // incomplete type errors in PlatNetSvcHandler.h
#include <net/PlatNetSvcHandler.h>
#include <util/Log.h>
#include <DmIoReq.h>
#include <PerfTrace.h>
#include <fds_assert.h>

namespace fds {
namespace dm {

StatVolumeHandler::StatVolumeHandler() {
    if (!dataMgr->features.isTestMode()) {
        REGISTER_DM_MSG_HANDLER(fpi::StatVolumeMsg, handleRequest);
    }
}

void StatVolumeHandler::handleRequest(
        boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
        boost::shared_ptr<fpi::StatVolumeMsg>& message) {
    LOGTRACE << "Received a statVolume request for volume "
             << message->volume_id;

    auto dmReq = new DmIoStatVolume(message);
    dmReq->cb = BIND_MSG_CALLBACK(StatVolumeHandler::handleResponse, asyncHdr, message);

    PerfTracer::tracePointBegin(dmReq->opReqLatencyCtx);

    addToQueue(dmReq);
}

void StatVolumeHandler::handleQueueItem(dmCatReq* dmRequest) {
    QueueHelper helper(dmRequest);
    DmIoStatVolume* typedRequest = static_cast<DmIoStatVolume*>(dmRequest);

    helper.err = dataMgr->timeVolCat_->queryIface()->statVolume(
            typedRequest->getVolId(),
            // FIXME(DAC): These casts are poster-children for inappropriate usage of
            //             reinterpret_cast.
            reinterpret_cast<fds_uint64_t*>(&typedRequest->msg->volumeStatus.size),
            reinterpret_cast<fds_uint64_t*>(&typedRequest->msg->volumeStatus.blobCount),
            reinterpret_cast<fds_uint64_t*>(&typedRequest->msg->volumeStatus.objectCount));
}

void StatVolumeHandler::handleResponse(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                                       boost::shared_ptr<fpi::StatVolumeMsg>& message,
                                       Error const& e, dmCatReq* dmRequest) {
    LOGTRACE << "Finished stat for volume " << message->volume_id;
    DBG(GLOGDEBUG << logString(*asyncHdr) << logString(*message));

    asyncHdr->msg_code = e.GetErrno();
    DM_SEND_ASYNC_RESP(*asyncHdr, fpi::StatVolumeMsgTypeId, *message);

    delete dmRequest;
}

}  // namespace dm
}  // namespace fds
