/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

#include <string>

#include "fds_process.h"

#include "AmCache.h"
#include "AccessMgr.h"
#include "StorHvCtrl.h"
#include "StorHvQosCtrl.h"
#include "StorHvVolumes.h"

#include "connector/xdi/AmAsyncService.h"
#include "connector/xdi/fdsn-server.h"
#include "connector/block/NbdConnector.h"

namespace fds {

AccessMgr::AccessMgr(const std::string &modName,
                     CommonModuleProviderIf *modProvider)
        : Module(modName.c_str()),
          modProvider_(modProvider),
          shuttingDown(false) {
}

AccessMgr::~AccessMgr() {
}

int
AccessMgr::mod_init(SysParams const *const param) {
    Module::mod_init(param);
    // Init the storHvisor global object. It takes a bunch of arguments
    // but doesn't really need them so we just create stock values.
    fds::Module *io_dm_vec[] = {
        nullptr
    };
    FdsConfigAccessor conf(g_fdsprocess->get_fds_config(), "fds.am.");
    instanceId = conf.get<fds_uint32_t>("instanceId");
    LOGNOTIFY << "Initializing AM instance " << instanceId;

    int argc = 0;
    char **argv = NULL;
    fds::ModuleVector io_dm(argc, argv, io_dm_vec);
    storHvisor = new StorHvCtrl(argc, argv, io_dm.get_sys_params(),
                                StorHvCtrl::NORMAL, instanceId);
    dataApi = boost::make_shared<AmDataApi>();

    // Init the FDSN server to serve XDI data requests
    fdsnServer = FdsnServer::unique_ptr(
        new FdsnServer("AM FDSN Server", dataApi, instanceId));
    fdsnServer->init_server();

    // Init the async server
    asyncServer = AsyncDataServer::unique_ptr(
        new AsyncDataServer("AM Async Server", instanceId));
    asyncServer->init_server();

    if (!conf.get<bool>("testing.standalone")) {
        omConfigApi = boost::make_shared<OmConfigApi>();
    }

    blkConnector = std::unique_ptr<NbdConnector>(new NbdConnector(omConfigApi));

    return 0;
}

void
AccessMgr::mod_startup() {
}

void
AccessMgr::mod_shutdown() {
    delete storHvisor;
}

void
AccessMgr::mod_lockstep_start_service() {
    storHvisor->StartOmClient();
    storHvisor->qos_ctrl->runScheduler();

    this->mod_lockstep_done();
}

void
AccessMgr::run() {
    // Run until the data server stops
    fdsnServer->deinit_server();
    asyncServer->deinit_server();
}

Error
AccessMgr::registerVolume(const VolumeDesc& volDesc) {
    // TODO(Andrew): Create cache separately since
    // the volume data doesn't do it. We should converge
    // on a single volume add location.
    storHvisor->amCache->createCache(volDesc);
    return storHvisor->vol_table->registerVolume(volDesc);
}

// Set AM in shutdown mode.
void
AccessMgr::setShutDown() {
    shuttingDown = true;

    /*
     * If there are no
     * more outstanding requests, tell the QoS
     * Dispatcher that we're shutting down.
     */
    if (storHvisor->qos_ctrl->htb_dispatcher->num_outstanding_ios == 0)
    {
        stop();
    }
}

void
AccessMgr::stop() {
    LOGDEBUG << "Shutting down and no outstanding I/O's. Stop dispatcher and server.";
    storHvisor->qos_ctrl->htb_dispatcher->stop();
    asyncServer->getTTServer()->stop();
    fdsnServer->getNBServer()->stop();
}

// Check whether AM is in shutdown mode.
bool
AccessMgr::isShuttingDown() {
    return shuttingDown;
}

}  // namespace fds
