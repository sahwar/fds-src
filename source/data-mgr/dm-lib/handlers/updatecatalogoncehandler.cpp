/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

#include <dmhandler.h>
#include <DMSvcHandler.h>  // This shouldn't be necessary, included because of
                           // incomplete type errors in BaseAsyncSvcHandler.h
#include <net/BaseAsyncSvcHandler.h>
#include <util/Log.h>
#include <fds_assert.h>
#include <dm-platform.h>
#include <DmIoReq.h>
#include <PerfTrace.h>

namespace fds {
namespace dm {

UpdateCatalogOnceHandler::UpdateCatalogOnceHandler() {
    if (!dataMgr->feature.isTestMode()) {
        REGISTER_DM_MSG_HANDLER(fpi::UpdateCatalogOnceMsg, handleRequest);
    }
}

void UpdateCatalogOnceHandler::handleRequest(
        boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
        boost::shared_ptr<fpi::UpdateCatalogOnceMsg>& message) {
    if (dataMgr->testUturnAll || dataMgr->testUturnUpdateCat) {
        GLOGDEBUG << "Uturn testing update catalog once " << logString(*asyncHdr)
                  << logString(*message);
        handleResponse(asyncHdr, message, ERR_OK, nullptr);
        return;
    }

    DBG(GLOGDEBUG << logString(*asyncHdr) << logString(*message));

    // Allocate a commit request structure because it is needed by the
    // commit call that will be executed during update processing.
    auto dmCommitBlobOnceReq = new DmIoCommitBlobOnce(message->volume_id,
                                                      message->blob_name,
                                                      message->blob_version,
                                                      message->dmt_version);
    dmCommitBlobOnceReq->cb =
            BIND_MSG_CALLBACK(UpdateCatalogOnceHandler::handleCommitBlobOnceResponse, asyncHdr);
    PerfTracer::tracePointBegin(dmCommitBlobOnceReq->opReqLatencyCtx);

    // allocate a new query cat log  class  and  queue  to per volume queue.
    auto dmUpdCatReq = new DmIoUpdateCatOnce(message, dmCommitBlobOnceReq);
    dmUpdCatReq->cb =
            BIND_MSG_CALLBACK(UpdateCatalogOnceHandler::handleResponse, asyncHdr, message);
    dmCommitBlobOnceReq->parent = dmUpdCatReq;
    PerfTracer::tracePointBegin(dmUpdCatReq->opReqLatencyCtx);

    addToQueue(dmUpdCatReq);
}

void UpdateCatalogOnceHandler::handleQueueItem(dmCatReq* dmRequest) {
    QueueHelper helper(static_cast<DmIoUpdateCatOnce*>(dmRequest)->commitBlobReq);
    DmIoUpdateCatOnce* typedRequest = static_cast<DmIoUpdateCatOnce*>(dmRequest);

    // Start the transaction
    helper.err = dataMgr->timeVolCat_->startBlobTx(typedRequest->volId,
                                                   typedRequest->blob_name,
                                                   typedRequest->updcatMsg->blob_mode,
                                                   typedRequest->ioBlobTxDesc);
    if (helper.err != ERR_OK) {
        LOGERROR << "Failed to start transaction" << typedRequest->ioBlobTxDesc << ": "
                 << helper.err;
        return;
    }

    // Apply the offset updates
    helper.err = dataMgr->timeVolCat_->updateBlobTx(typedRequest->volId,
                                                    typedRequest->ioBlobTxDesc,
                                                    typedRequest->updcatMsg->obj_list);
    if (helper.err != ERR_OK) {
        LOGERROR << "Failed to update object offsets for transaction "
                 << *typedRequest->ioBlobTxDesc << ": " << helper.err;
        helper.err = dataMgr->timeVolCat_->abortBlobTx(typedRequest->volId,
                                                       typedRequest->ioBlobTxDesc);
        if (!helper.err.ok()) {
            LOGERROR << "Failed to abort transaction " << *typedRequest->ioBlobTxDesc;
        }
        return;
    }

    // Apply the metadata updates
    helper.err = dataMgr->timeVolCat_->updateBlobTx(typedRequest->volId,
                                                    typedRequest->ioBlobTxDesc,
                                                    typedRequest->updcatMsg->meta_list);
    if (helper.err != ERR_OK) {
        LOGERROR << "Failed to update metadata for transaction "
                 << *typedRequest->ioBlobTxDesc << ": " << helper.err;
        helper.err = dataMgr->timeVolCat_->abortBlobTx(typedRequest->volId,
                                                       typedRequest->ioBlobTxDesc);
        if (!helper.err.ok()) {
            LOGERROR << "Failed to abort transaction " << *typedRequest->ioBlobTxDesc;
        }
        return;
    }

    // Commit the metadata updates
    // The commit callback we pass in will actually call the
    // final service callback
    PerfTracer::tracePointBegin(typedRequest->commitBlobReq->opLatencyCtx);
    helper.err = dataMgr->timeVolCat_->commitBlobTx(
            typedRequest->volId, typedRequest->blob_name, typedRequest->ioBlobTxDesc,
            std::bind(&CommitBlobTxHandler::volumeCatalogCb,
                      static_cast<CommitBlobTxHandler*>(dataMgr->handlers[FDS_COMMIT_BLOB_TX]),
                      std::placeholders::_1, std::placeholders::_2,
                      std::placeholders::_3, std::placeholders::_4, std::placeholders::_5,
                      typedRequest->commitBlobReq));
    if (helper.err != ERR_OK) {
        LOGERROR << "Failed to commit transaction " << *typedRequest->ioBlobTxDesc << ": "
                 << helper.err;
        helper.err = dataMgr->timeVolCat_->abortBlobTx(typedRequest->volId,
                                                       typedRequest->ioBlobTxDesc);
        if (!helper.err.ok()) {
            LOGERROR << "Failed to abort transaction " << typedRequest->ioBlobTxDesc;
        }
        return;
    } else {
        // We don't want to mark the I/O done or call handleResponse yet, work is now passed off to
        // CommitBlobTxHandler::volumeCatalogCb.
        helper.cancel();
    }
}

void UpdateCatalogOnceHandler::handleCommitBlobOnceResponse(
        boost::shared_ptr<fpi::AsyncHdr>& asyncHdr, Error const& e, dmCatReq* dmRequest) {
    DmIoCommitBlobOnce* commitOnceReq = static_cast<DmIoCommitBlobOnce*>(dmRequest);
    DmIoUpdateCatOnce* parent = commitOnceReq->parent;
    parent->cb(e, dmRequest);
    delete parent;
}

void UpdateCatalogOnceHandler::handleResponse(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                                              boost::shared_ptr<fpi::UpdateCatalogOnceMsg>& message,
                                              Error const& e, dmCatReq* dmRequest) {
    asyncHdr->msg_code = e.GetErrno();
    DBG(GLOGDEBUG << logString(*asyncHdr));

    // Build response
    fpi::UpdateCatalogOnceRspMsg updcatRspMsg;
    auto commitOnceReq = static_cast<DmIoCommitBlobOnce*>(dmRequest);
    updcatRspMsg.byteCount = commitOnceReq->rspMsg.byteCount;
    updcatRspMsg.meta_list.swap(commitOnceReq->rspMsg.meta_list);

    DM_SEND_ASYNC_RESP(*asyncHdr, fpi::UpdateCatalogOnceRspMsgTypeId, updcatRspMsg);

    if (dataMgr->testUturnAll || dataMgr->testUturnUpdateCat) {
        fds_verify(dmRequest == nullptr);
    } else {
        delete dmRequest;
    }
}

}  // namespace dm
}  // namespace fds
