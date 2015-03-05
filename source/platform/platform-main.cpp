/*
 * Copyright 2013 Formation Data Systems, Inc.
 */

#include <stdlib.h>
#include <string>
#include <iostream>  // NOLINT
#include <fds_process.h>

#include <fdsp/fds_service_types.h>
#include <net/SvcProcess.h>
#include <platform/platformmanager.h>
#include <platform/svchandler.h>
#include <disk_plat_module.h>
namespace fds {

class PlatformMain : public SvcProcess {
  public:
    PlatformMain(int argc, char *argv[]) {
        platform = new fds::pm::PlatformManager();

        closeAllFDs();

        auto handler = boost::make_shared<fds::pm::SvcHandler>(this, platform);
        auto processor = boost::make_shared<fpi::PlatNetSvcProcessor>(handler);
        init(argc, argv, "platform.conf", "fds.pm.", "pm.log",
             nullptr, handler, processor);

        util::Properties& props = platform->getProperties();
        props.setData(&svcInfo_.props);
    }

    virtual void setupSvcInfo_() {
        platform->mod_init(nullptr);
        gl_DiskPlatMod.mod_init(nullptr);

        SvcProcess::setupSvcInfo_();
        const fpi::NodeInfo& nodeInfo = platform->getNodeInfo();
        svcInfo_.svc_id.svc_uuid.svc_uuid = nodeInfo.uuid;
        platform->loadProperties();
    }

    virtual int run() {
        return platform->run();
    }
  protected:
    fds::pm::PlatformManager* platform;
};
} // namespace fds

int main(int argc, char **argv) {
    auto pmMain = new fds::PlatformMain(argc, argv);
    auto ret = pmMain->main();
    delete pmMain;
    return ret;
}
