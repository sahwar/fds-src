/*
 * Copyright 2015 Formation Data Systems, Inc.
 */
#include <fds_process.h>
#include <fdsp/PlatNetSvc.h>
#include <net/PlatNetSvcHandler.h>
#include <fdsp/OMSvc.h>
#include <util/timeutils.h>
#include <net/SvcMgr.h>
#include <net/net_utils.h>
#include <net/SvcProcess.h>

namespace fds {

SvcProcess::SvcProcess()
{
}

SvcProcess::SvcProcess(int argc, char *argv[],
                       const std::string &def_cfg_file,
                       const std::string &base_path,
                       const std::string &def_log_file,
                       fds::Module **mod_vec)
{
    auto handler = boost::make_shared<PlatNetSvcHandler>(this);
    auto processor = boost::make_shared<fpi::PlatNetSvcProcessor>(handler);
    init(argc, argv, def_cfg_file, base_path,
         def_log_file, mod_vec, handler, processor);
}

SvcProcess::SvcProcess(int argc, char *argv[],
                               const std::string &def_cfg_file,
                               const std::string &base_path,
                               const std::string &def_log_file,
                               fds::Module **mod_vec,
                               PlatNetSvcHandlerPtr handler,
                               fpi::PlatNetSvcProcessorPtr processor)
{
    init(argc, argv, def_cfg_file, base_path, def_log_file, mod_vec, handler, processor);
}

SvcProcess::~SvcProcess()
{
}

void SvcProcess::init(int argc, char *argv[],
                          const std::string &def_cfg_file,
                          const std::string &base_path,
                          const std::string &def_log_file,
                          fds::Module **mod_vec,
                          PlatNetSvcHandlerPtr handler,
                          fpi::PlatNetSvcProcessorPtr processor)
{
    /* Set up process related services such as logger, timer, etc. */
    FdsProcess::init(argc, argv, def_cfg_file, base_path, def_log_file, mod_vec);

    /* Setup Config db for persistence */
    setupConfigDb_();

    /* Populate service information */
    setupSvcInfo_();

    /* Set up service layer */
    setupSvcMgr_(handler, processor);

    /* Default implementation registers with OM.  Until registration complets
     * this will not return
     */
    registerSvcProcess();

    /* After registraion is complete other modules can proceed.  See FdsProcess::main() */
}

void SvcProcess::registerSvcProcess()
{
    LOGNOTIFY;
    std::vector<fpi::SvcInfo> svcMap;

    do {
        try {
            /* This will block until we get a connection.  All the call below should be
             * idempotent.  In case of failure we just retry until we succeed
             */
            auto omSvcRpc = svcMgr_->getNewOMSvcClient();

            omSvcRpc->registerService(svcInfo_);
            LOGNOTIFY << "registerService() call completed";

            omSvcRpc->getSvcMap(svcMap, 0);
            LOGNOTIFY << "got service map.  size: " << svcMap.size();
            break;
        } catch (Exception &e) {
            LOGWARN << "Failed to register: " << e.what() << ".  Retrying...";
        } catch (...) {
            LOGWARN << "Failed to register: unknown excpeption" << ".  Retrying...";
        }
    } while (true);

    svcMgr_->updateSvcMap(svcMap);
}

SvcMgr* SvcProcess::getSvcMgr() {
    return svcMgr_.get();
}

void SvcProcess::setupConfigDb_()
{
    LOGNOTIFY;
    // TODO(Rao): Set up configdb
    // fds_panic("Unimpl");
}

void SvcProcess::setupSvcInfo_()
{
    auto config = get_conf_helper();
    svcInfo_.svc_id.svc_uuid.svc_uuid = static_cast<int64_t>(config.get<long long>("svc.uuid"));
    svcInfo_.ip = net::get_local_ip(config.get_abs<std::string>("fds.nic_if"));
    svcInfo_.svc_port = config.get<int>("svc.port");

    // TODO(Rao): set up correct incarnation no
    svcInfo_.incarnationNo = util::getTimeStampSeconds();
}

void SvcProcess::setupSvcMgr_(PlatNetSvcHandlerPtr handler,
                              fpi::PlatNetSvcProcessorPtr processor)
{
    LOGNOTIFY;
    svcMgr_.reset(new SvcMgr(this, handler, processor, svcInfo_));
    /* This will start SvcServer instance */
    svcMgr_->mod_init(nullptr);
}

}  // namespace fds
