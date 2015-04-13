/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

#include <dmhandler.h>
#include <fds_assert.h>
#include <util/Log.h>
#include <DmIoReq.h>
#include <PerfTrace.h>
#include <fds_error.h>
#include <DMSvcHandler.h>

namespace fds {
namespace dm {

QueryCatalogHandler::QueryCatalogHandler(DataMgr& dataManager)
    : Handler(dataManager)
{
    if (!dataManager.features.isTestMode()) {
        REGISTER_DM_MSG_HANDLER(fpi::QueryCatalogMsg, handleRequest);
    }
}

void QueryCatalogHandler::handleRequest(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                                        boost::shared_ptr<fpi::QueryCatalogMsg>& message) {
    DBG(GLOGDEBUG << logString(*asyncHdr) << logString(*message));

    auto err = dataManager.validateVolumeIsActive(message->volume_id);
    if (!err.OK())
    {
        handleResponse(asyncHdr, message, err, nullptr);
        return;
    }

    auto dmReq = new DmIoQueryCat(message);
    dmReq->cb = BIND_MSG_CALLBACK(QueryCatalogHandler::handleResponse, asyncHdr, message);

    PerfTracer::tracePointBegin(dmReq->opReqLatencyCtx);

    addToQueue(dmReq);
}

void QueryCatalogHandler::handleQueueItem(dmCatReq* dmRequest) {
    QueueHelper helper(dataManager, dmRequest);
    DmIoQueryCat* typedRequest = static_cast<DmIoQueryCat*>(dmRequest);

    // TODO(Andrew): Should add request flag so that we don't always
    // pass the metadata and size over the network, unless it's needed
    helper.err = dataManager.timeVolCat_->queryIface()->getBlob(
        typedRequest->volId,
        typedRequest->blob_name,
        typedRequest->queryMsg->start_offset,
        typedRequest->queryMsg->end_offset,
        &typedRequest->blob_version,
        &typedRequest->queryMsg->meta_list,
        &typedRequest->queryMsg->obj_list,
        reinterpret_cast<fds_uint64_t *>(&typedRequest->queryMsg->byteCount));
    if (!helper.err.ok()) {
        PerfTracer::incr(typedRequest->opReqFailedPerfEventType, typedRequest->getVolId(),
                typedRequest->perfNameStr);
    }
}

void QueryCatalogHandler::handleResponse(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                                         boost::shared_ptr<fpi::QueryCatalogMsg>& message,
                                         Error const& e, dmCatReq* dmRequest) {
    asyncHdr->msg_code = e.GetErrno();
    DBG(GLOGDEBUG << logString(*asyncHdr) << logString(*message));

    // TODO(Rao): We should have a separate response message for QueryCatalogMsg for
    // consistency
    DM_SEND_ASYNC_RESP(*asyncHdr, fpi::QueryCatalogMsgTypeId, *message);

    delete dmRequest;
}

}  // namespace dm
}  // namespace fds
