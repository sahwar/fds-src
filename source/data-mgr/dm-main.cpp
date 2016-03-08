/*
 * Copyright 2013 Formation Data Systems, Inc.
 */

#include <memory>

#include <unistd.h>
#include <DataMgr.h>

#include <net/SvcProcess.h>

#include <fdsp/DMSvc.h>
#include "fdsp/common_constants.h"
#include <DMSvcHandler.h>

#include "util/ExecutionGate.h"
#include "util/Log.h"

template<class T, class... ArgTs> std::unique_ptr<T> make_unique(ArgTs&&... args)
{
    return std::unique_ptr<T>(new T(std::forward<ArgTs>(args)...));
}

int gdb_stop = 0;

using std::move;
using std::unique_ptr;

using fpi::DMSvcProcessor;

using fds::DataMgr;
using fds::DMSvcHandler;
using fds::Module;

using fds::util::ExecutionGate;

class DMMain : public SvcProcess
{
 public:
    using DMPointer = unique_ptr<DataMgr>;

    DMMain() {}

    virtual ~DMMain() {}

    virtual void interrupt_cb(int signum) override
    {
        SvcProcess::interrupt_cb(signum);

        _shutdownGate.open();
    }

    virtual int run()
    {
        int retval = _dm->run();

        _shutdownGate.waitUntilOpened();

        return retval;
    }

    static unique_ptr<DMMain> build(int argc, char* argv[])
    {
        auto retval = make_unique<DMMain>();
        auto dm = make_unique<DataMgr>(retval.get());

        retval->internal_constructor(move(dm), argc, argv);

        return retval;
    }

 private:

    DMPointer _dm;

    Module* _dmVec[2];

    ExecutionGate _shutdownGate;

    void internal_constructor(DMPointer dm, int argc, char* argv[])
    {
        _dm = move(dm);

        _dmVec[0] = _dm.get();
        _dmVec[1] = NULL;

        // FIXME: This is not DI.
        auto handler = boost::make_shared<DMSvcHandler>(this, *_dm);
        auto processor = boost::make_shared<DMSvcProcessor>(handler);

        /**
         * Note on Thrift service compatibility:
         * Because asynchronous service requests are routed manually, any new
         * PlatNetSvc version MUST extend a previous PlatNetSvc version.
         * Only ONE version of PlatNetSvc API can be included in the list of
         * multiplexed services.
         *
         * For other new major service API versions (not PlatNetSvc), pass
         * additional pairs of processor and Thrift service name.
         */
        TProcessorMap processors;
        processors.insert(std::make_pair<std::string,
            boost::shared_ptr<apache::thrift::TProcessor>>(
                fpi::commonConstants().DM_SERVICE_NAME, processor));

        init(argc, argv, "platform.conf", "fds.dm.", "dm.log", _dmVec, handler, processors);
    }
};

int main(int argc, char *argv[])
{
    /* Based on command line arg --foreground is set, don't daemonize the process */
    fds::FdsProcess::checkAndDaemonize(argc, argv);

    auto dmMain = DMMain::build(argc, argv);

    auto ret = dmMain->main();

    LOGNORMAL << "Normal exit.";

    return ret;
}
