/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

#ifndef TOKENCOPYSENDER_H_
#define TOKENCOPYSENDER_H_

#include <string>
#include <boost/msm/back/state_machine.hpp>

#include <fds_assert.h>
#include <concurrency/fds_actor.h>
#include <fdsp/FDSP_types.h>
#include <fds_base_migrators.h>
#include <util/Log.h>

namespace fds {

using namespace  ::FDS_ProtocolInterface;

struct TokenCopySenderFSM_;
typedef boost::msm::back::state_machine<TokenCopySenderFSM_> TokenCopySenderFSM;

class TokenCopySender : public MigrationSender,
                        public FdsRequestQueueActor {
public:
    TokenCopySender(fds_threadpoolPtr threadpool, fds_logPtr log);
    virtual ~TokenCopySender();

    fds_log* get_log() {
        return log_.get();
    }

    /* For logging */
    std::string log_string() {
        return "TokenCopySender";
    }

    /* Overrides from FdsRequestQueueActor */
    virtual Error handle_actor_request(FdsActorRequestPtr req) override;

private:
    TokenCopySenderFSM   *sm_;
    fds_logPtr log_;
};

} /* namespace fds */

#endif /* TOKENCOPYSENDER_H_ */
