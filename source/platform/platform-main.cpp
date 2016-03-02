/*
 * Copyright 2013 Formation Data Systems, Inc.
 */

#include <stdlib.h>
#include <string>
#include <iostream>  // NOLINT

#include "fds_process.h"
#include "net/SvcProcess.h"
#include "net/SvcMgr.h"

#include "disk_plat_module.h"
#include "platform/platform_manager.h"
#include "platform/svc_handler.h"
#include "fdsp/common_constants.h"

namespace fds
{
    class PlatformMain : public SvcProcess
    {
        public:
            PlatformMain (int argc, char *argv[])
            {
                platform = new fds::pm::PlatformManager();

                auto handler = boost::make_shared <fds::pm::SvcHandler> (this, platform);
                auto processor = boost::make_shared <fpi::PlatNetSvcProcessor> (handler);
                // Note on Thrift service compatibility:
                // If a backward incompatible change arises, pass additional pairs of
                // processor and Thrift service name to SvcProcess::init(). Similarly,
                // if the Thrift service API wants to be broken up.
                init (argc, argv, "platform.conf", "fds.pm.", "pm.log", nullptr, handler, processor,
                    fpi::commonConstants().PLATNET_SERVICE_NAME);

                // init above calls setupSvcInfo_, so by the time we get here, data in platform should be set
                gl_DiskPlatMod.set_node_uuid(platform->getUUID());
                gl_DiskPlatMod.set_largest_disk_index(platform->getLargestDiskIndex());
                gl_DiskPlatMod.mod_startup();
                platform->persistLargestDiskIndex(gl_DiskPlatMod.get_largest_disk_index());;

#ifdef DEBUG
                bool fDumpDiskMap = get_fds_config()->get<fds_bool_t>("fds.pm.dump_diskmap",false);

                if (fDumpDiskMap) {
                    GLOGWARN << "exiting as fds.pm.dump_diskmap is true";
                    exit(0);
                }
#endif

                platform->updateServiceInfoProperties(&svcInfo_.props);
            }

            void setupSvcInfo_()
            {
                gl_DiskPlatMod.mod_init (nullptr);
                platform->mod_init (nullptr);
                SvcProcess::setupSvcInfo_();

                const fpi::NodeInfo& nodeInfo = platform->getNodeInfo();
                svcInfo_.svc_id.svc_uuid.svc_uuid = nodeInfo.uuid;
                LOGNOTIFY << "Svc info overrriden to: " << fds::logString(svcInfo_);
            }

            int run()
            {
                platform->run();

                return 0;
            }

        protected:
            fds::pm::PlatformManager* platform;
    };

} // namespace fds

int main (int argc, char *argv[])
{
    /* Based on command line arg --foreground is set, don't daemonize the process */
    fds::FdsProcess::checkAndDaemonize (argc, argv);

    auto pmMain = new fds::PlatformMain (argc, argv);
    auto ret = pmMain->main();
    delete pmMain;
    return ret;
}
