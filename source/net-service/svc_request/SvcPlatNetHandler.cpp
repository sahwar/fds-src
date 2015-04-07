/*
 * Copyright 2014 by Formation Data Systems, Inc.
 */

#include <string>
#include <map>
#include <vector>
#include <csignal>
#include <unordered_map>

#include <boost/tokenizer.hpp>
#include <boost/algorithm/string.hpp>

#include <net/PlatNetSvcHandler.h>
#include <util/Log.h>
#include <net/SvcRequest.h>
#include <net/SvcRequestTracker.h>
#include <net/SvcRequestPool.h>
#include <net/SvcMgr.h>
#include <concurrency/SynchronizedTaskExecutor.hpp>
#include <util/fiu_util.h>
#include <fiu-control.h>
#include <fds_process.h>
#include <fdsp/svc_api_types.h>

namespace fds {
PlatNetSvcHandler::PlatNetSvcHandler(CommonModuleProviderIf *provider)
: HasModuleProvider(provider),
Module("PlatNetSvcHandler")
{
    deferRequests_ = false;
    REGISTER_FDSP_MSG_HANDLER(fpi::GetSvcStatusMsg, getStatus);
    REGISTER_FDSP_MSG_HANDLER(fpi::UpdateSvcMapMsg, updateSvcMap);
}

PlatNetSvcHandler::~PlatNetSvcHandler()
{
}
int PlatNetSvcHandler::mod_init(SysParams const *const param)
{
    return 0;
}

void PlatNetSvcHandler::mod_startup()
{
}

void PlatNetSvcHandler::mod_shutdown()
{
}

void PlatNetSvcHandler::deferRequests(bool defer)
{
    /* NOTE: For now the supported deferring sequence is to start with 
     * defer set to true and at a later point set defer to false.
     */

    deferRequests_ = defer;

    if (deferRequests_ == false) {
        /* Drain all the deferred requests.  NOTE it's possible to drain them
         * on a threadpool as well
         */
        fds_scoped_lock l(lock_);
        for (auto &reqPair: deferredReqs_) {
            asyncReqt(reqPair.first, reqPair.second);        
        }
     }
}


/**
 * @brief 
 *
 * @param taskExecutor
 */
void PlatNetSvcHandler::setTaskExecutor(SynchronizedTaskExecutor<uint64_t>  *taskExecutor)
{
    taskExecutor_ = taskExecutor;
}

/**
 * @brief
 *
 * @param _return
 * @param msg
 */
void PlatNetSvcHandler::uuidBind(fpi::AsyncHdr &_return, const fpi::UuidBindMsg& msg)
{
}

/**
 * @brief
 *
 * @param _return
 * @param msg
 */
void PlatNetSvcHandler::uuidBind(fpi::AsyncHdr &_return,
                                   boost::shared_ptr<fpi::UuidBindMsg>& msg)
{
}

void PlatNetSvcHandler::asyncReqt(const FDS_ProtocolInterface::AsyncHdr& header,
                                       const std::string& payload)
{
}

/**
  * @brief Invokes registered request handler
  *
  * @param header
  * @param payload
  */
void PlatNetSvcHandler::asyncReqt(boost::shared_ptr<FDS_ProtocolInterface::AsyncHdr>& header,
                                     boost::shared_ptr<std::string>& payload)
{
    SVCPERF(header->rqRcvdTs = util::getTimeStampNanos());
    fiu_do_on("svc.uturn.asyncreqt", header->msg_code = ERR_INVALID;
    sendAsyncResp(*header, fpi::EmptyMsgTypeId, fpi::EmptyMsg()); return; );

    while (deferRequests_) {
        fds_scoped_lock l(lock_);
        if (!deferRequests_) {
            break;
         }
         GLOGNOTIFY << "Deferred: " << logString(*header);
         deferredReqs_.push_back(std::make_pair(header, payload));
         return;
    }

    GLOGDEBUG << logString(*header);
    try
    {
        /* Deserialize the message and invoke the handler.  Deserialization is performed
         * by deserializeFdspMsg().
         * For detaails see macro REGISTER_FDSP_MSG_HANDLER()
         */
         fds_assert(header->msg_type_id != fpi::UnknownMsgTypeId);
         asyncReqHandlers_.at(header->msg_type_id) (header, payload);
    }
    catch(std::out_of_range &e)
    {
        fds_assert(!"Unregistered fdsp message type");
        LOGWARN << "Unknown message type: " << static_cast<int32_t>(header->msg_type_id)
                << " Ignoring";
    }
}

/**
  *
  * @param header
  * @param payload
  */
void PlatNetSvcHandler::asyncResp(const FDS_ProtocolInterface::AsyncHdr& header,
                                    const std::string& payload)
{
}

/**
  * Handler function for async responses
  * @param header
  * @param payload
  */
void PlatNetSvcHandler::asyncResp(boost::shared_ptr<FDS_ProtocolInterface::AsyncHdr>& header,
                                    boost::shared_ptr<std::string>& payload)
{
    SVCPERF(header->rspRcvdTs = util::getTimeStampNanos());
    fiu_do_on("svc.disable.schedule", asyncRespHandler(\
    MODULEPROVIDER()->getSvcMgr()->getSvcRequestTracker(), header, payload); return; );
    // fiu_do_on("svc.use.lftp", asyncResp2(header, payload); return; );


    GLOGDEBUG << logString(*header);

    fds_assert(header->msg_type_id != fpi::UnknownMsgTypeId);

    /* Execute on synchronized task exector so that handling for requests
     * with same id gets serialized
     */
     taskExecutor_->schedule(header->msg_src_id,
                           std::bind(&PlatNetSvcHandler::asyncRespHandler,
                                     MODULEPROVIDER()->getSvcMgr()->getSvcRequestTracker(),
                                     header, payload));
}


/**
  * @brief Static async response handler. Making this static so that this function
  * is accessible locally(with in the process) to everyone.
  *
  * @param header
  * @param payload
*/
void PlatNetSvcHandler::asyncRespHandler(SvcRequestTracker* reqTracker,
     boost::shared_ptr<FDS_ProtocolInterface::AsyncHdr>& header,
     boost::shared_ptr<std::string>& payload)
{
     auto asyncReq = reqTracker->getSvcRequest(static_cast<SvcRequestId>(header->msg_src_id));
     if (!asyncReq)
     {
         GLOGWARN << logString(*header) << " Request doesn't exist (timed out?)";
         return;
     }

     SVCPERF(asyncReq->ts.rqRcvdTs = header->rqRcvdTs);
     SVCPERF(asyncReq->ts.rqHndlrTs = header->rqHndlrTs);
     SVCPERF(asyncReq->ts.rspSerStartTs = header->rspSerStartTs);
     SVCPERF(asyncReq->ts.rspSendStartTs = header->rspSendStartTs);
     SVCPERF(asyncReq->ts.rspRcvdTs = header->rspRcvdTs);

     asyncReq->handleResponse(header, payload);
}

 void PlatNetSvcHandler::sendAsyncResp_(const fpi::AsyncHdr& reqHdr,
                     const fpi::FDSPMsgTypeId &msgTypeId,
                     StringPtr &payload)
{
     auto respHdr = boost::make_shared<fpi::AsyncHdr>(
          std::move(SvcRequestPool::swapSvcReqHeader(reqHdr)));
     respHdr->msg_type_id = msgTypeId;

     MODULEPROVIDER()->getSvcMgr()->sendAsyncSvcRespMessage(respHdr, payload);
}


void PlatNetSvcHandler::allUuidBinding(const fpi::UuidBindMsg& mine)
{
}

void
PlatNetSvcHandler::allUuidBinding(boost::shared_ptr<fpi::UuidBindMsg>& mine)  // NOLINT
{
}

void
PlatNetSvcHandler::notifyNodeActive(const fpi::FDSP_ActivateNodeType &info)
{
}

void
PlatNetSvcHandler::notifyNodeActive(boost::shared_ptr<fpi::FDSP_ActivateNodeType> &info)
{
    LOGDEBUG << "Boost notifyNodeActive";
}

void PlatNetSvcHandler::notifyNodeInfo(
    std::vector<fpi::NodeInfoMsg> & _return,      // NOLINT
    const fpi::NodeInfoMsg& info, const bool bcast)
{
}

void PlatNetSvcHandler::notifyNodeInfo(
    std::vector<fpi::NodeInfoMsg> & _return,      // NOLINT
    boost::shared_ptr<fpi::NodeInfoMsg>& info,                       // NOLINT
    boost::shared_ptr<bool>& bcast)
{
}

void PlatNetSvcHandler::getProperty(std::string& _return, const std::string& name) {
    // Don't do anything here. This stub is just to keep cpp compiler happy
}


void PlatNetSvcHandler::getProperty(std::string& _return, boost::shared_ptr<std::string>& name) {
    if (!MODULEPROVIDER() || !(MODULEPROVIDER()->getProperties())) return;
    _return = MODULEPROVIDER()->getProperties()->get(*name);
}

void PlatNetSvcHandler::getProperties(std::map<std::string, std::string> & _return, const int32_t nullarg) {
    // Don't do anything here. This stub is just to keep cpp compiler happy
}


void PlatNetSvcHandler::getProperties(std::map<std::string, std::string> & _return, boost::shared_ptr<int32_t>& nullarg) {
    if (!MODULEPROVIDER() || !(MODULEPROVIDER()->getProperties())) return;
    _return = MODULEPROVIDER()->getProperties()->getAllProperties();
}

/**
 * Return list of domain nodes in the node inventory.
 */
void
PlatNetSvcHandler::getDomainNodes(fpi::DomainNodes &ret, const fpi::DomainNodes &domain)
{
}

void
PlatNetSvcHandler::getDomainNodes(fpi::DomainNodes                    &ret,
                                  boost::shared_ptr<fpi::DomainNodes> &domain)
{
#if 0
    /*  Only do the local domain for now */
    DomainContainer::pointer    local;

    local = Platform::platf_singleton()->plf_node_inventory();
    local->dc_node_svc_info(ret);
#endif
}

fpi::ServiceStatus PlatNetSvcHandler::getStatus(const int32_t nullarg)
{
    return fpi::SVC_STATUS_INVALID;
}

void PlatNetSvcHandler::getCounters(std::map<std::string, int64_t> & _return,    // NOLINT
                                    const std::string& id)
{
}

void PlatNetSvcHandler::resetCounters(const std::string& id) // NOLINT
{
}

void PlatNetSvcHandler::getFlags(std::map<std::string, int64_t> & _return,
                                 const int32_t nullarg)  // NOLINT
{
}

void PlatNetSvcHandler::setConfigVal(const std::string& id, const int64_t val)
{
}

void PlatNetSvcHandler::setFlag(const std::string& id, const int64_t value)
{
}

int64_t PlatNetSvcHandler::getFlag(const std::string& id)
{
    return 0;
}
bool PlatNetSvcHandler::setFault(const std::string& command)
{
    // Don't do anything here. This stub is just to keep cpp compiler happy
    return false;
}

/**
 * Returns the status of the service
 * @param nullarg
 * @return
 */
fpi::ServiceStatus PlatNetSvcHandler::
getStatus(boost::shared_ptr<int32_t>& nullarg)  // NOLINT
{
    if (!MODULEPROVIDER())
    {
        return fpi::SVC_STATUS_INVALID;
    }
    return fpi::SVC_STATUS_ACTIVE;
}

/**
 * Returns the stats associated with counters identified by id
 * @param _return
 * @param id
 */
void PlatNetSvcHandler::getCounters(std::map<std::string, int64_t> & _return,
                                    boost::shared_ptr<std::string>& id)
{
    if (!MODULEPROVIDER())
    {
        return;
    }

    if (*id == "*")
    {
        /* Request to return all counters */
        MODULEPROVIDER()->get_cntrs_mgr()->toMap(_return);
        return;
    }

    auto    cntrs = MODULEPROVIDER()->get_cntrs_mgr()->get_counters(*id);

    if (cntrs == nullptr)
    {
        return;
    }
    cntrs->toMap(_return);
}

/**
 * Reset counters identified by id
 * @param id
 */
void PlatNetSvcHandler::resetCounters(boost::shared_ptr<std::string>& id)
{
    if (!MODULEPROVIDER())
    {
        return;
    }

    if (*id == "*")
    {
        /* Request to return all counters */
        MODULEPROVIDER()->get_cntrs_mgr()->reset();
    }
}
/**
 * For setting a flag dynamically
 * @param id
 * @param val
 */
void PlatNetSvcHandler::
setConfigVal(boost::shared_ptr<std::string>& id,  // NOLINT
             boost::shared_ptr<int64_t>& val)
{
    if (!MODULEPROVIDER())
    {
        return;
    }

    try
    {
        MODULEPROVIDER()->get_fds_config()->set(*id, static_cast<fds_uint64_t>(*val));
    } catch(...)
    {
        // TODO(Rao): Only ignore SettingNotFound exception
        /* Ignore the error */
    }
}

/**
 * @brief
 *
 * @param id
 * @param value
 */
void PlatNetSvcHandler::setFlag(boost::shared_ptr<std::string>& id,  // NOLINT
                                boost::shared_ptr<int64_t>& value)
{
}


/**
 * @brief
 *
 * @param id
 *
 * @return
 */
int64_t PlatNetSvcHandler::getFlag(boost::shared_ptr<std::string>& id)  // NOLINT
{
    return 0;
}

/**
 * @brief map of all the flags
 *
 * @param _return
 * @param nullarg
 */
void PlatNetSvcHandler::getFlags(std::map<std::string, int64_t> & _return,  // NOLINT
                                 boost::shared_ptr<int32_t>& nullarg)
{
}

/**
 * @brief
 *
 * @param command
 */
bool PlatNetSvcHandler::setFault(boost::shared_ptr<std::string>& cmdline)  // NOLINT
{
    boost::char_separator<char>                       sep(", ");
    /* Parse the cmd line */
    boost::tokenizer<boost::char_separator<char> >    toknzr(*cmdline, sep);
    std::unordered_map<std::string, std::string>      args;

    for (auto t : toknzr)
    {
        if (args.size() == 0)
        {
            args["cmd"] = t;
            continue;
        }

        std::vector<std::string>    parts;
        boost::split(parts, t, boost::is_any_of("= "));

        if (parts.size() != 2)
        {
            return false;
        }

        args[parts[0]] = parts[1];
    }

    if (args.count("cmd") == 0 || args.count("name") == 0)
    {
        return false;
    }

    /* Execute based on the cmd */
    auto    cmd = args["cmd"];
    auto    name = args["name"];

    if (cmd == "enable")
    {
        fiu_enable(name.c_str(), 1, NULL, 0);
        LOGDEBUG << "Enable fault: " << name;
    }else if (cmd == "enable_random") {
        // TODO(Rao): add support for this
    }else if (cmd == "disable") {
        fiu_disable(name.c_str());
        LOGDEBUG << "Disable fault: " << name;
    }
    return true;
}

void PlatNetSvcHandler::getStatus(fpi::AsyncHdrPtr &header,
                                  fpi::GetSvcStatusMsgPtr &statusMsg)
{
    static boost::shared_ptr<int32_t> nullarg;
    fpi::GetSvcStatusRespMsgPtr respMsg = boost::make_shared<fpi::GetSvcStatusRespMsg>();
    respMsg->status = getStatus(nullarg);
    sendAsyncResp(*header, FDSP_MSG_TYPEID(fpi::GetSvcStatusRespMsg), *respMsg);
}

void PlatNetSvcHandler::updateSvcMap(fpi::AsyncHdrPtr &header,
                                     fpi::UpdateSvcMapMsgPtr &svcMapMsg)
{
    MODULEPROVIDER()->getSvcMgr()->updateSvcMap(svcMapMsg->updates);
}

}  // namespace fds