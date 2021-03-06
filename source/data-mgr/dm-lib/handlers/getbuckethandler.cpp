/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#include <stack>
#include <string>
#include <tuple>
#include <list>
#include <algorithm>

#include <pcrecpp.h>

#include <DataMgr.h>
#include <dmhandler.h>
#include <DMSvcHandler.h>

using std::get;
using std::string;
using std::stack;
using std::tuple;

using FDS_ProtocolInterface::PatternSemantics;

namespace fds {
namespace dm {

GetBucketHandler::GetBucketHandler(DataMgr& dataManager)
    : Handler(dataManager)
{
    if (!dataManager.features.isTestModeEnabled()) {
        REGISTER_DM_MSG_HANDLER(fpi::GetBucketMsg, handleRequest);
    }
}

void GetBucketHandler::handleRequest(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                                     boost::shared_ptr<fpi::GetBucketMsg>& message) {
    fds_volid_t volId(message->volume_id);
    LOGDEBUG << "volume: " << volId;

    Error err(ERR_OK);
    // if (!dataManager.amIPrimaryGroup(volId)) {err = ERR_DM_NOT_PRIMARY;}
    if (err.OK()) {
    	err = dataManager.validateVolumeIsActive(volId);
    }
    if (!err.OK())
    {
        auto dummyResponse = boost::make_shared<fpi::GetBucketRspMsg>();
        handleResponse(asyncHdr, dummyResponse, err, nullptr);
        return;
    }

    // setup the request
    auto dmRequest = new DmIoGetBucket(message);

    // setup callback
    dmRequest->cb = BIND_MSG_CALLBACK(GetBucketHandler::handleResponse, asyncHdr, dmRequest->response);

    addToQueue(dmRequest);
}

void GetBucketHandler::handleQueueItem(DmRequest *dmRequest) {
    QueueHelper helper(dataManager, dmRequest);  // this will call the callback
    DmIoGetBucket *request = static_cast<DmIoGetBucket*>(dmRequest);

    fpi::BlobDescriptorListType& blobVec = request->response->blob_descr_list;
    auto& skippedPrefixes = request->response->skipped_prefixes;
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wswitch-enum"
    switch (request->message->patternSemantics)
    {

    case PatternSemantics::PCRE:
    {
        helper.err = dataManager.timeVolCat_->queryIface()->listBlobs(dmRequest->volId, &blobVec);

        auto& patternString = request->message->pattern;
        if (!patternString.empty())
        {
            pcrecpp::RE pattern { patternString, pcrecpp::UTF8() };
            if (!pattern.error().empty())
            {
                LOGWARN << "Error initializing pattern: " << quoteString(patternString)
                        << " " << pattern.error();
                helper.err = ERR_DM_INVALID_REGEX;
                return;
            }

            auto iterEnd = std::remove_if(
                    blobVec.begin(),
                    blobVec.end(),
                    [&pattern](fpi::BlobDescriptor const& blobDescriptor)
                    {
                        return !pattern.PartialMatch(blobDescriptor.name);
                    });
            blobVec.erase(iterEnd, blobVec.end());
        }

        break;
    }

    case PatternSemantics::PREFIX:
        helper.err = dataManager.timeVolCat_
                               ->queryIface()
                               ->listBlobsWithPrefix(dmRequest->volId,
                                                     request->message->pattern,
                                                     "",
                                                     blobVec,
                                                     skippedPrefixes);
        break;

    case PatternSemantics::PREFIX_AND_DELIMITER:
        helper.err = dataManager.timeVolCat_
                               ->queryIface()
                               ->listBlobsWithPrefix(dmRequest->volId,
                                                     request->message->pattern,
                                                     request->message->delimiter,
                                                     blobVec,
                                                     skippedPrefixes);
        break;

    default:
        helper.err = ERR_DM_UNRECOGNIZED_PATTERN_SEMANTICS;
        LOGWARN << "Pattern semantics "
                << std::to_string(static_cast<int>(request->message->patternSemantics))
                << " not recognized.";
        return;

    }
#pragma GCC diagnostic pop

    // sort if required
    if (request->message->orderBy) {
        const fpi::BlobListOrder orderColumn = request->message->orderBy;
        const bool descending = request->message->descending;
        std::sort(blobVec.begin(), blobVec.end(),
                [orderColumn, descending](const fpi::BlobDescriptor & lhs,
                                          const fpi::BlobDescriptor & rhs) {
                    bool ret = fpi::BLOBSIZE == orderColumn ?  lhs.byteCount < rhs.byteCount :
                            lhs.name < rhs.name;
                    return descending ? !ret : ret;
                });
    }

    // based on user's request process results
    if (request->message->startPos) {
        if (static_cast<fds_uint64_t>(request->message->startPos) < blobVec.size()) {
            blobVec.erase(blobVec.begin(), blobVec.begin() + request->message->startPos);
        } else {
            blobVec.clear();
        }
    }
    if (!blobVec.empty() && blobVec.size() > static_cast<fds_uint32_t>(request->message->count)) {
        blobVec.erase(blobVec.begin() + request->message->count, blobVec.end());
    }

    LOGDEBUG << " volid: " << dmRequest->volId
             << " numblobs: " << request->response->blob_descr_list.size();
}

void GetBucketHandler::handleResponse(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                                      boost::shared_ptr<fpi::GetBucketRspMsg>& message,
                                      const Error &e, DmRequest *dmRequest) {
    LOGDEBUG << " volid: " << (dmRequest ? dmRequest->volId : invalid_vol_id) << " err: " << e;
    asyncHdr->msg_code = static_cast<int32_t>(e.GetErrno());
    DM_SEND_ASYNC_RESP(asyncHdr, fpi::GetBucketRspMsgTypeId, message);
    delete dmRequest;
}

}  // namespace dm
}  // namespace fds
