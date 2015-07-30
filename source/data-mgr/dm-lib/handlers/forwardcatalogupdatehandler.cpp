/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

#include <dmhandler.h>
#include <DmIoReq.h>
#include <DMSvcHandler.h>  // This shouldn't be necessary, included because of
                           // incomplete type errors in PlatNetSvcHandler.h
#include <net/PlatNetSvcHandler.h>
#include <util/Log.h>
#include <fds_assert.h>

#include <functional>

namespace fds {
namespace dm {

ForwardCatalogUpdateHandler::ForwardCatalogUpdateHandler(DataMgr& dataManager)
    : Handler(dataManager)
{
    if (!dataManager.features.isTestModeEnabled()) {
        REGISTER_DM_MSG_HANDLER(fpi::ForwardCatalogMsg, handleRequest);
    }
}

void ForwardCatalogUpdateHandler::handleRequest(
        boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
        boost::shared_ptr<fpi::ForwardCatalogMsg>& message) {
    auto dmReq = new DmIoFwdCat(message);
    DBG(LOGMIGRATE << "Enqueued new forward request " << logString(*asyncHdr)
        << " " << *reinterpret_cast<DmIoFwdCat*>(dmReq));
    /* Route the message to right executor.  If static migration is in progress
     * the message will be buffered.  Otherwise it will be handed to qos ctrlr
     * to be applied immediately
     */
    auto error = dataManager.dmMigrationMgr->handleForwardedCommits(dmReq);

    /* Ack back immediately */
    asyncHdr->msg_code = error.GetErrno();
    DM_SEND_ASYNC_RESP(*asyncHdr, fpi::ForwardCatalogRspMsgTypeId, fpi::ForwardCatalogRspMsg());
}

void ForwardCatalogUpdateHandler::handleQueueItem(DmRequest* dmRequest) {
    DmIoFwdCat* typedRequest = static_cast<DmIoFwdCat*>(dmRequest);
    QueueHelper helper(dataManager, typedRequest);

    LOGMIGRATE << "Will commit fwd blob " << *typedRequest << " to tvc";

    if (typedRequest->fwdCatMsg->lastForward &&
        typedRequest->fwdCatMsg->blob_name.size() == 0) {
            return;
    }

    helper.err = dataManager.timeVolCat_->updateFwdCommittedBlob(
            static_cast<fds_volid_t>(typedRequest->fwdCatMsg->volume_id),
            typedRequest->fwdCatMsg->blob_name,
            typedRequest->fwdCatMsg->blob_version,
            typedRequest->fwdCatMsg->obj_list,
            typedRequest->fwdCatMsg->meta_list,
            typedRequest->fwdCatMsg->sequence_id);
}

}  // namespace dm
}  // namespace fds
