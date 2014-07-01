/* Copyright 2014 Formation Data Systems, Inc.
 */
#include <string>

#include <net/BaseAsyncSvcHandler.h>
#include <util/Log.h>
#include <net/RpcRequest.h>
#include <net/AsyncRpcRequestTracker.h>
#include <concurrency/SynchronizedTaskExecutor.hpp>

namespace fds {

/**
 *
 */
BaseAsyncSvcHandler::BaseAsyncSvcHandler()
{
}

/**
 *
 */
BaseAsyncSvcHandler::~BaseAsyncSvcHandler() {
}

/**
* @brief 
*
* @param _return
* @param msg
*/
void BaseAsyncSvcHandler::uuidBind(FDS_ProtocolInterface::RespHdr& _return,
                    const FDS_ProtocolInterface::UuidBindMsg& msg)
{
}

/**
* @brief 
*
* @param _return
* @param msg
*/
void BaseAsyncSvcHandler::uuidBind(FDS_ProtocolInterface::RespHdr& _return,
                        boost::shared_ptr<FDS_ProtocolInterface::UuidBindMsg>& msg)
{
}

void BaseAsyncSvcHandler::asyncReqt(const FDS_ProtocolInterface::AsyncHdr& header,
                                    const std::string& payload)
{
}

/**
* @brief Invokes registered request handler
*
* @param header
* @param payload
*/
void BaseAsyncSvcHandler::asyncReqt(boost::shared_ptr<FDS_ProtocolInterface::AsyncHdr>& header,
                                    boost::shared_ptr<std::string>& payload)
{
    GLOGDEBUG << logString(*header);
    try {
        /* Deserialize the message and invoke the handler.  Deserialization is performed
         * by deserializeFdspMsg().
         * For detaails see macro REGISTER_FDSP_MSG_HANDLER()
         */
        fds_assert(header->msg_type_id != fpi::UnknownMsgTypeId);
        asyncReqHandlers_[header->msg_type_id](header, payload);
    } catch(std::out_of_range &e) {
        LOGWARN << "Unknown message type: " << static_cast<int32_t>(header->msg_type_id)
            << " Ignoring";
    }
}

/**
 *
 * @param header
 * @param payload
 */
void BaseAsyncSvcHandler::asyncResp(const FDS_ProtocolInterface::AsyncHdr& header,
                                    const std::string& payload)
{
}

/**
 * Handler function for async responses
 * @param header
 * @param payload
 */
void BaseAsyncSvcHandler::asyncResp(
        boost::shared_ptr<FDS_ProtocolInterface::AsyncHdr>& header,
        boost::shared_ptr<std::string>& payload)
{
    asyncRespHandler(header, payload);
}


/**
* @brief Static async response handler. Making this static so that this function
* is accessible locally(with in the process) to everyone.
*
* @param header
* @param payload
*/
void BaseAsyncSvcHandler::asyncRespHandler(
        boost::shared_ptr<FDS_ProtocolInterface::AsyncHdr>& header,
        boost::shared_ptr<std::string>& payload)
{
    static SynchronizedTaskExecutor<uint64_t>* taskExecutor =
        NetMgr::ep_mgr_singleton()->ep_get_task_executor();

    GLOGDEBUG << logString(*header);

    fds_assert(header->msg_type_id != fpi::UnknownMsgTypeId);

    auto asyncReq = gAsyncRpcTracker->\
            getAsyncRpcRequest(static_cast<AsyncRpcRequestId>(header->msg_src_id));
    if (!asyncReq) {
        GLOGWARN << "Request " << header->msg_src_id << " doesn't exist";
        return;
    }

    /* Execute on synchronized task exector so that handling for requests
     * with same id gets serialized
     */
    taskExecutor->schedule(header->msg_src_id,
                           std::bind(&AsyncRpcRequestIf::handleResponse,
                                     asyncReq.get(), header, payload));

    // asyncReq->handleResponse(header, payload);
}

}  // namespace fds

