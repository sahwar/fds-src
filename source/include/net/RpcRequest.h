/* Copyright 2014 Formation Data Systems, Inc.
 */
#ifndef SOURCE_INCLUDE_NET_RPCREQUEST_H_
#define SOURCE_INCLUDE_NET_RPCREQUEST_H_

#include <string>
#include <vector>
#include <functional>
#include <boost/shared_ptr.hpp>

#include <fds_typedefs.h>
#include <fdsp/fds_service_types.h>
#include <fds_error.h>
#include <net/RpcFunc.h>

namespace fds {
/* Forward declarations */
class FailoverRpcRequest;

/* Async rpc request identifier */
typedef uint64_t AsyncRpcRequestId;

/* Async rpc request callback types */
typedef std::function<void(boost::shared_ptr<std::string>)> RpcRequestSuccessCb;
typedef std::function<void(const Error&,
        boost::shared_ptr<std::string>)> RpcRequestErrorCb;
typedef std::function<void(const Error&,
        boost::shared_ptr<std::string>, bool&)> RpcRequestFailoverCb;
typedef std::function<void(const AsyncRpcRequestId&)> RpcRequestCompletionCb;

/* Async rpc request states */
enum AsyncRpcState {
    PRIOR_INVOCATION,
    INVOCATION_PROGRESS,
    RPC_COMPLETE
};

/**
 * Base class for async rpc requests
 */
class AsyncRpcRequestIf {
 public:
    AsyncRpcRequestIf();

    explicit AsyncRpcRequestIf(const AsyncRpcRequestId &id);

    virtual ~AsyncRpcRequestIf();

    virtual void invoke() = 0;

    virtual void handleResponse(boost::shared_ptr<fpi::AsyncHdr>& header,
            boost::shared_ptr<std::string>& payload) = 0;

    virtual void complete(const Error& error);

    virtual bool isComplete();

    void setRpcFunc(RpcFuncPtr rpc);

    void setTimeoutMs(const uint32_t &timeout_ms);

    uint32_t getTimeout();

    AsyncRpcRequestId getRequestId();

    void setRequestId(const AsyncRpcRequestId &id);

    void setCompletionCb(RpcRequestCompletionCb &completionCb);

 protected:
    void invokeCommon_(const fpi::SvcUuid &epId);

    /* Lock for synchronizing response handling */
    fds_mutex respLock_;
    /* Wrapper around rpc function call */
    RpcFuncPtr rpc_;
    /* Request Id */
    AsyncRpcRequestId id_;
    /* Async rpc state */
    AsyncRpcState state_;
    /* Error if any */
    Error error_;
    /* Timeout */
    uint32_t timeoutMs_;
    /* Completion cb */
    RpcRequestCompletionCb completionCb_;
};
typedef boost::shared_ptr<AsyncRpcRequestIf> AsyncRpcRequestIfPtr;

/**
 * Wrapper around asynchronous rpc request
 */
class EPAsyncRpcRequest : public AsyncRpcRequestIf {
 public:
    EPAsyncRpcRequest();

    EPAsyncRpcRequest(const AsyncRpcRequestId &id,
            const fpi::SvcUuid &svcUuids);

    void onSuccessCb(RpcRequestSuccessCb &cb);

    void onErrorCb(RpcRequestErrorCb &cb);

    virtual void invoke() override;

    virtual void handleResponse(boost::shared_ptr<fpi::AsyncHdr>& header,
            boost::shared_ptr<std::string>& payload) override;

 protected:
    fpi::SvcUuid epId_;
    /* Response callbacks.  If set they are invoked in handleResponse() */
    RpcRequestSuccessCb successCb_;
    RpcRequestErrorCb errorCb_;

    friend class FailoverRpcRequest;
};
typedef boost::shared_ptr<EPAsyncRpcRequest> EPAsyncRpcRequestPtr;

/**
 *
 */
class FailoverRpcRequest : public AsyncRpcRequestIf {
 public:
    FailoverRpcRequest();

    FailoverRpcRequest(const AsyncRpcRequestId& id,
            const std::vector<fpi::SvcUuid>& uuid);

    void addEndpoint(const fpi::SvcUuid& uuid);

    void onFailoverCb(RpcRequestFailoverCb& cb);

    void onSuccessCb(RpcRequestSuccessCb& cb);

    void onErrorCb(RpcRequestErrorCb& cb);

    virtual void invoke() override;

    virtual void handleResponse(boost::shared_ptr<fpi::AsyncHdr>& header,
            boost::shared_ptr<std::string>& payload) override;

 protected:
    void epReqSuccessCb_(boost::shared_ptr<std::string> payload);

    void epReqErrorCb_(const Error& e, boost::shared_ptr<std::string> payload);

    bool moveToNextHealthyEndpoint_();

    void invokeInternal_();


    /* Next endpoint to invoke the request on */
    uint8_t curEpIdx_;

    /* Endpoint request collection */
    std::vector<EPAsyncRpcRequestPtr> epReqs_;

    /* Callback to invoke before failing over to the next endpoint */
    RpcRequestFailoverCb failoverCb_;
    /* Callback to invoke when response is succesfully received */
    RpcRequestSuccessCb successCb_;
    /* Callback to invoke when all the endpoints have failed */
    RpcRequestErrorCb errorCb_;
};
typedef boost::shared_ptr<FailoverRpcRequest> FailoverRpcRequestPtr;

#if 0
/**
 * Helper macro for invoking an rpc
 * req - RpcRequest context object
 * SvcType - class type of the service
 * method - method to invoke
 * ... - arguments to the method
 */
#define INVOKE_RPC(req, SvcType, method, ...)                               \
    do {                                                                    \
        boost::shared_ptr<SvcType> client;                                  \
        auto ep = NetMgr::ep_mgr_singleton()->\
            svc_lookup((req).getEndpointId(), 0 , 0);                       \
        Error status = ep->ep_get_status();                                 \
        if (status != ERR_OK) {                                             \
            (req).setError(status);                                         \
            GLOGWARN << "Not invoking the method: " ## method;              \
            break;                                                          \
        } else {                                                            \
            client = ep->svc_rpc<SvcType>();                                \
        }                                                                   \
        try {                                                               \
            client->method(__VA_ARGS__);                                    \
        } catch(...) {                                                      \
            (req).handleError(ERR_INVALID, VoidPtr(nullptr));               \
        }                                                                   \
    } while (false)



/**
 * Base class for rpc request
 */
template <class ServiceT, class ArgT>
class RpcRequestIf {
 public:
    virtual ~RpcRequestIf() {}

    void setRpc(std::function<void(ArgT)> rpc, ArgT& arg) {
        rpc_ = rpc;
        rpcArg_ = arg;
    }

    virtual void handleError(const Error&e, VoidPtr resp) = 0;
    virtual void invoke() = 0;

    void setError(const Error &e) {
        error_ = e;
    }

    Error getError() const {
        return error_;
    }

 protected:
    /* RPC Request error if any */
    Error error_;
    /* Rpc to invoke */
    std::function<void(ArgT)> rpc_;
    /* Argument to rpc call */
    ArgT rpcArg_;
};


/**
 * RPC request for single endpoint
 */
template <class ServiceT, class ArgT>
class EPRpcRequest : public RpcRequestIf<ServiceT, ArgT> {
 public:
    EPRpcRequest()
     : EPRpcRequest(fpi::SvcUuid()) {
    }

    explicit EPRpcRequest(const fpi::SvcUuid &uuid) {
        epId_ = uuid;
    }

    virtual ~EPRpcRequest() {
    }

    void setEndpointId(const fpi::SvcUuid &uuid) {
        epId_ = uuid;
    }

    fpi::SvcUuid getEndpointId() const {
        return epId_;
    }

    virtual void handleError(const Error&e, VoidPtr resp) override {
        // TODO(Rao): Handle error
    }

    virtual void invoke() {
        // TODO(Rao): Implement
    }

    /**
     * Wrapper for invoking the member function on an endpoint
     * Specialization for void return type
     * @param mf - member function
     * @param args - arguments to member function
     */
    template <typename T, typename ...Args>
    void invoke(void (T::*mf)(Args...), Args &&... args)
    {
        T *client = nullptr;
        Error e;
        // TODO(Rao): Get client from ep id
        try {
            (client->*mf)(std::forward<Args>(args)...);
        } catch(...) {
            handleError(e, VoidPtr(nullptr));
        }
    }



    /**
     * Wrapper for invoking the member function on an endpoint
     * @param mf - member function
     * @param args - arguments to member function
     */
    template <typename T, typename R, typename ...Args>
    R invoke(R (T::*mf)(Args...), Args &&... args)
    {
        T *client = nullptr;
        Error e;
        R ret;
        // TODO(Rao): Get client from ep id
        try {
            ret = (client->*mf)(std::forward<Args>(args)...);
        } catch(...) {
            handleError(e, VoidPtr(nullptr));
        }
        return ret;
    }

 protected:
    /* Endpoint id */
    fpi::SvcUuid epId_;
    /* Error if any was set */
    Error error_;
};
// typedef boost::shared_ptr<EPRpcRequest> EPRpcRequestPtr;
#endif



}  // namespace fds

#endif  // SOURCE_INCLUDE_NET_RPCREQUEST_H_
