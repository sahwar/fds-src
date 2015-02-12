/* Copyright 2015 Formation Data Systems, Inc.
 */

#include <vector>
#include <string>
#include <sstream>
#include <concurrency/Mutex.h>
#include <arpa/inet.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <fdsp/PlatNetSvc.h>
#include <net/PlatNetSvcHandler.h>
#include <fdsp/OMSvc.h>
#include <net/fdssocket.h>
#include <fdsp/fds_service_types.h>
#include <fdsp_utils.h>
#include <fds_module_provider.h>
#include <net/SvcRequestPool.h>
#include <net/SvcServer.h>
#include <net/SvcMgr.h>

namespace fds {

std::size_t SvcUuidHash::operator()(const fpi::SvcUuid& svcId) const {
    return svcId.svc_uuid;
}

template<class T>
boost::shared_ptr<T> allocRpcClient(const std::string ip, const int &port,
                                    const bool &blockOnConnect)
{
    auto sock = bo::make_shared<net::Socket>(ip, port);
    auto trans = bo::make_shared<tt::TFramedTransport>(sock);
    auto proto = bo::make_shared<tp::TBinaryProtocol>(trans);
    boost::shared_ptr<T> client = bo::make_shared<T>(proto);
    if (!blockOnConnect) {
        /* This may throw an exception when trying to open.  Caller is expected to handle
         * the exception
         */
        sock->open();
    } else {
        sock->connect();
    }
    return client;
}

SvcMgr::SvcMgr(CommonModuleProviderIf *moduleProvider,
               PlatNetSvcHandlerPtr handler,
               fpi::PlatNetSvcProcessorPtr processor,
               const fpi::SvcInfo &svcInfo)
    : HasModuleProvider(moduleProvider),
    Module("SvcMgr")
{
    auto config = MODULEPROVIDER()->get_conf_helper();
    omIp_ = config.get_abs<std::string>("fds.common.om_ip_list");
    omPort_ = config.get_abs<int>("fds.common.om_port");
    omSvcUuid_.svc_uuid = static_cast<int64_t>(config.get_abs<long long>("fds.common.om_uuid"));

    svcRequestHandler_ = handler;
    svcInfo_ = svcInfo;
    /* Create the server */
    svcServer_ = boost::make_shared<SvcServer>(svcInfo_.svc_port, processor);

    taskExecutor_ = new SynchronizedTaskExecutor<uint64_t>(*MODULEPROVIDER()->proc_thrpool());

    svcRequestMgr_ = new SvcRequestPool(MODULEPROVIDER(), getSelfSvcUuid(), handler);
    gSvcRequestPool = svcRequestMgr_;
}

SvcMgr::~SvcMgr()
{
    delete taskExecutor_;
    delete svcRequestMgr_;
    svcRequestMgr_ = gSvcRequestPool = nullptr;
}

int SvcMgr::mod_init(SysParams const *const p)
{
    GLOGNOTIFY;

    svcRequestHandler_->setTaskExecutor(taskExecutor_);
    svcServer_->start();

    return 0;
}

void SvcMgr::mod_startup()
{
    GLOGNOTIFY;
}

void SvcMgr::mod_enable_service()
{
    GLOGNOTIFY;
}

void SvcMgr::mod_shutdown()
{
    GLOGNOTIFY;
}

SvcRequestPool* SvcMgr::getSvcRequestMgr() const {
    return svcRequestMgr_;
}

SvcRequestCounters* SvcMgr::getSvcRequestCntrs() const {
    return svcRequestMgr_->getSvcRequestCntrs();
}

SvcRequestTracker* SvcMgr::getSvcRequestTracker() const {
    return svcRequestMgr_->getSvcRequestTracker();
}

void SvcMgr::updateSvcMap(const std::vector<fpi::SvcInfo> &entries)
{
    GLOGDEBUG << "Updating service map. Incoming entries size: " << entries.size();

    fds_scoped_lock lock(svcHandleMapLock_);
    for (auto &e : entries) {
        /* Ignore self service map entry.  We don't need a handle for self yet */
        if (e.svc_id.svc_uuid == svcInfo_.svc_id.svc_uuid) {
            continue;
        }

        auto svcHandleItr = svcHandleMap_.find(e.svc_id.svc_uuid);
        if (svcHandleItr == svcHandleMap_.end()) {
            /* New service handle entry.  Note, we don't allocate rpcClient.  We do this lazily
             * when needed to not incur the cost of socket creation.
             */
            auto svcHandle = boost::make_shared<SvcHandle>(MODULEPROVIDER(), e);
            svcHandleMap_.emplace(std::make_pair(e.svc_id.svc_uuid, svcHandle));
            GLOGDEBUG << "svcmap update.  svcuuid: " << e.svc_id.svc_uuid.svc_uuid
                    << " is new.  Incarnation: " << e.incarnationNo;
        } else {
            svcHandleItr->second->updateSvcHandle(e);
        }
    }
    GLOGDEBUG << "After update.  Servic map size: " << svcHandleMap_.size();
}

bool SvcMgr::getSvcHandle_(const fpi::SvcUuid &svcUuid, SvcHandlePtr& handle) const
{
    auto itr = svcHandleMap_.find(svcUuid);
    if (itr == svcHandleMap_.end()) {
        return false;
    }
    handle = itr->second;
    return true;
}

void SvcMgr::sendAsyncSvcReqMessage(fpi::AsyncHdrPtr &header,
                                 StringPtr &payload)
{
    SvcHandlePtr svcHandle;
    fpi::SvcUuid &svcUuid = header->msg_dst_uuid;

    do {
        fds_scoped_lock lock(svcHandleMapLock_);
        if (!getSvcHandle_(svcUuid, svcHandle)) {
            GLOGWARN << "Svc handle for svcuuid: " << svcUuid.svc_uuid << " not found";
            break;
        }
    } while (false);

    if (svcHandle) {
        svcHandle->sendAsyncSvcReqMessage(header, payload);
    } else {
        postSvcSendError(header);
    }
}

void SvcMgr::sendAsyncSvcRespMessage(fpi::AsyncHdrPtr &header,
                                     StringPtr &payload)
{
    SvcHandlePtr svcHandle;
    fpi::SvcUuid &svcUuid = header->msg_dst_uuid;

    do {
        fds_scoped_lock lock(svcHandleMapLock_);
        if (!getSvcHandle_(svcUuid, svcHandle)) {
            GLOGWARN << "Svc handle for svcuuid: " << svcUuid.svc_uuid << " not found";
            break;
        }
    } while (false);

    if (svcHandle) {
        svcHandle->sendAsyncSvcRespMessage(header, payload);
    } else {
        GLOGWARN << "Failed to get svc handle: " << fds::logString(*header)
                << ".  Dropping the respone";
    }
}

void SvcMgr::broadcastAsyncSvcReqMessage(fpi::AsyncHdrPtr &h,
                                      StringPtr &payload,
                                      const SvcInfoPredicate& predicate)
{
    SvcHandleMap map;

    {
        /* Make a copy of the service map */
        fds_scoped_lock lock(svcHandleMapLock_);
        map = svcHandleMap_;
    }

    for (auto &kv : map) {
        /* Create header for the target */
        auto header = boost::make_shared<fpi::AsyncHdr>(*h);
        header->msg_dst_uuid = kv.first;

        kv.second->sendAsyncSvcReqMessageOnPredicate(header, payload, predicate);
    }
}

void SvcMgr::postSvcSendError(fpi::AsyncHdrPtr &header)
{
    swapAsyncHdr(header);
    header->msg_code = ERR_SVC_REQUEST_INVOCATION;
    svcRequestMgr_->postError(header);
}

fpi::SvcUuid SvcMgr::getSelfSvcUuid() const
{
    return svcInfo_.svc_id.svc_uuid;
}

int SvcMgr::getSvcPort() const
{
    return svcInfo_.svc_port;
}

void SvcMgr::getOmIPPort(std::string &omIp, int &port) const
{
    omIp = omIp_;
    port = omPort_;
}

fpi::SvcUuid SvcMgr::getOmSvcUuid() const
{
    return omSvcUuid_;
}

fpi::OMSvcClientPtr SvcMgr::getNewOMSvcClient() const
{
    fpi::OMSvcClientPtr omClient;
    while (true) {
        try {
            omClient = allocRpcClient<fpi::OMSvcClient>(omIp_, omPort_, true);
            break;
        } catch (std::exception &e) {
            GLOGWARN << "allocRpcClient failed.  Exception: " << e.what()
                << ".  ip: "  << omIp_ << " port: " << omPort_;
        } catch (...) {
            GLOGWARN << "allocRpcClient failed.  Unknown exception. ip: "
                << omIp_ << " port: " << omPort_;
        }
    }

    return omClient;
}

SynchronizedTaskExecutor<uint64_t>*
SvcMgr::getTaskExecutor() {
    return taskExecutor_;
}

bool SvcMgr::isSvcActionableError(const Error &e)
{
    // TODO(Rao): Implement
    return false;
}

void SvcMgr::handleSvcError(const fpi::SvcUuid &srcSvc, const Error &e)
{
    // TODO(Rao): Implement
}

SvcHandle::SvcHandle(CommonModuleProviderIf *moduleProvider,
                     const fpi::SvcInfo &info)
: HasModuleProvider(moduleProvider)
{
    svcInfo_ = info;
    GLOGDEBUG << "Operation: new service handle";
    GLOGDEBUG << logString();
}

SvcHandle::~SvcHandle()
{
    GLOGDEBUG << logString();
}

void SvcHandle::sendAsyncSvcReqMessage(fpi::AsyncHdrPtr &header,
                                    StringPtr &payload)
{
    bool bSendSuccess = true;
    {
        fds_scoped_lock lock(lock_);
        bSendSuccess  = sendAsyncSvcMessageCommon_(true, header, payload);
    }
    if (!bSendSuccess) {
        MODULEPROVIDER()->getSvcMgr()->postSvcSendError(header);
    }
}

void SvcHandle::sendAsyncSvcRespMessage(fpi::AsyncHdrPtr &header,
                                        StringPtr &payload)
{
    bool bSendSuccess = true;
    {
        fds_scoped_lock lock(lock_);
        bSendSuccess  = sendAsyncSvcMessageCommon_(false, header, payload);
    }
    if (!bSendSuccess) {
        MODULEPROVIDER()->getSvcMgr()->postSvcSendError(header);
    }
}

void SvcHandle::sendAsyncSvcReqMessageOnPredicate(fpi::AsyncHdrPtr &header,
                                               StringPtr &payload,
                                               const SvcInfoPredicate& predicate)
{
    bool bSendSuccess = true;
    {
        fds_scoped_lock lock(lock_);
        /* Only send to services matching predicate */
        if (predicate(svcInfo_)) {
            bSendSuccess  = sendAsyncSvcMessageCommon_(true, header, payload);
        }
    }

    if (!bSendSuccess) {
        MODULEPROVIDER()->getSvcMgr()->postSvcSendError(header);
    }
}

bool SvcHandle::sendAsyncSvcMessageCommon_(bool isAsyncReqt,
                                           fpi::AsyncHdrPtr &header,
                                           StringPtr &payload)
{
    /* NOTE: This code assumes lock is held */

    if (isSvcDown_()) {
        /* No point trying to send when service is down */
        return false;
    }
    try {
        if (!svcClient_) {
            GLOGDEBUG << "Allocating PlatNetSvcClient for: " << logString();
            svcClient_ = allocRpcClient<fpi::PlatNetSvcClient>(svcInfo_.ip,
                                                               svcInfo_.svc_port,
                                                               false);
        }
        if (isAsyncReqt) {
            svcClient_->asyncReqt(*header, *payload);
        } else {
            svcClient_->asyncResp(*header, *payload);
        }
        return true;
    } catch (std::exception &e) {
        GLOGWARN << "allocRpcClient failed.  Exception: " << e.what() << ".  "  << header;
        markSvcDown_();
    } catch (...) {
        GLOGWARN << "allocRpcClient failed.  Unknown exception." << header;
        markSvcDown_();
    }
    return false;
}

void SvcHandle::updateSvcHandle(const fpi::SvcInfo &newInfo)
{
    GLOGDEBUG << "Svchandle incoming update: " << fds::logString(newInfo);

    fds_scoped_lock lock(lock_);
    if (svcInfo_.incarnationNo < newInfo.incarnationNo) {
        /* Update to new incaration information.  Invalidate the old rpc client */
        svcInfo_ = newInfo;
        svcClient_.reset();
        GLOGDEBUG << "Operation: update to new incarnation";
    } else if (svcInfo_.incarnationNo == newInfo.incarnationNo &&
               newInfo.svc_status == fpi::SVC_STATUS_INACTIVE) {
        /* Mark current incaration inactivnewInfo.  Invalidate the rpc client */
        svcInfo_.svc_status = fpi::SVC_STATUS_INACTIVE; 
        svcClient_.reset();
        GLOGDEBUG << "Operation: set current incarnation as down";
    } else {
        GLOGDEBUG << "Operation: not applied";
    }

    GLOGDEBUG << "After update: " << logString();
}

std::string SvcHandle::logString() const
{
    /* NOTE: not protected by lock */
    return fds::logString(svcInfo_);
}

bool SvcHandle::isSvcDown_() const
{
    /* NOTE: Assumes this function is invoked under lock */
    return svcInfo_.svc_status == fpi::SVC_STATUS_INACTIVE;
}

void SvcHandle::markSvcDown_()
{
    /* NOTE: Assumes this function is invoked under lock */
    svcInfo_.svc_status = fpi::SVC_STATUS_INACTIVE;
    svcClient_.reset();
    GLOGDEBUG << logString();
}


}  // namespace fds
