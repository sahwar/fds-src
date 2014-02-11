/*
 * Copyright 2013 Formation Data Systems, Inc.
 */

#ifndef SOURCE_UTIL_ACTOR_H_
#define SOURCE_UTIL_ACTOR_H_

#include <boost/shared_ptr.hpp>
#include <memory>
#include <fds_err.h>
#include <concurrency/fds_actor_request.h>
#include <concurrency/ThreadPool.h>
#include <concurrency/Mutex.h>

namespace fds {
class FdsActor {
public:
    virtual ~FdsActor() {}
    virtual Error send_actor_request(FdsActorRequestPtr req) = 0;
    virtual Error handle_actor_request(FdsActorRequestPtr req) = 0;
    virtual int get_queue_size() = 0;
    virtual std::string log_string() {
        return "FdsActor";
    }
};
typedef boost::shared_ptr<FdsActor> FdsActorPtr;
typedef std::unique_ptr<FdsActor> FdsActorUPtr;

class FdsRequestQueueActor : public FdsActor {
public:
    FdsRequestQueueActor(fds_threadpoolPtr threadpool);
    virtual Error send_actor_request(FdsActorRequestPtr req) override;
    virtual int get_queue_size() override;
    virtual std::string log_string() {
        return "FdsRequestQueueActor";
    }

protected:
    virtual void request_loop();

    fds_threadpoolPtr threadpool_;
    fds_spinlock lock_;
    bool scheduled_;
    // TODO(rao):  Change to lockfree queue
    FdsActorRequestQueue queue_;
};
}  // namespace fds

#endif  // SOURCE_UTIL_ACTOR_H_
