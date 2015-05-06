/*
 * Copyright 2015 Formation Data Systems, Inc.
 */
#ifndef SOURCE_INCLUDE_NET_SVCPROCESS_H_
#define SOURCE_INCLUDE_NET_SVCPROCESS_H_

#include <boost/shared_ptr.hpp>

#include <fds_process.h>
#include <fds_dmt.h>
#include <dlt.h>
#include <fdsp/svc_types_types.h>
#include <net/SvcServer.h>

/* Forward declarations */
namespace FDS_ProtocolInterface {
    class PlatNetSvcProcessor;
    using PlatNetSvcProcessorPtr = boost::shared_ptr<PlatNetSvcProcessor>;
}  // namespace FDS_ProtocolInterface
namespace fpi = FDS_ProtocolInterface;

namespace fds {

/* Forward declarations */
class FdsProcess;
struct SvcMgr; 
class PlatNetSvcHandler;
using PlatNetSvcHandlerPtr = boost::shared_ptr<PlatNetSvcHandler>;

/**
* @brief Base class for all Services
* On top of what fds_process, it provides
* 1. SvcMgr (Responsible for managing service layer)
* 2. Config db for persistence
* 2. Service registration
*/
struct SvcProcess : FdsProcess, SvcServerListener {
    SvcProcess();
    SvcProcess(int argc, char *argv[],
                   const std::string &def_cfg_file,
                   const std::string &base_path,
                   const std::string &def_log_file,
                   fds::Module **mod_vec);
    SvcProcess(int argc, char *argv[],
                   const std::string &def_cfg_file,
                   const std::string &base_path,
                   const std::string &def_log_file,
                   fds::Module **mod_vec,
                   PlatNetSvcHandlerPtr handler,
                   fpi::PlatNetSvcProcessorPtr processor);
    virtual ~SvcProcess();

    /**
    * @brief Initializes the necessary services.
    * Init process is
    * 1. Set up configdb
    * 2. Set up service layer
    * 3. Do the necessary registration workd
    * 4. Startup the passedin modules
    *
    * @param argc
    * @param argv[]
    * @param def_cfg_file
    * @param base_path
    * @param def_log_file
    * @param mod_vec
    * @param processor
    */
    void init(int argc, char *argv[],
              const std::string &def_cfg_file,
              const std::string &base_path,
              const std::string &def_log_file,
              fds::Module **mod_vec,
              PlatNetSvcHandlerPtr handler,
              fpi::PlatNetSvcProcessorPtr processor);

    // XXX: Handler should be of type PlatNetSvcHandler and Processor
    //      should be of type fpi::PlatNetSvcProcessor
    template<typename Handler, typename Processor>
    void init(int argc, char *argv[],
              const std::string &def_cfg_file,
              const std::string &base_path,
              const std::string &def_log_file,
              fds::Module **mod_vec) {
        auto handler = boost::make_shared<Handler>(this);
        auto processor = boost::make_shared<Processor>(handler);
        init(argc, argv, def_cfg_file, base_path, def_log_file, mod_vec,
                handler, processor);
    }

    virtual void start_modules() override;

    /**
    * @brief Registers the service.  Default implementation will register the service
    * with OM.
    * Override this behavior depedning on the service type.
    */
    virtual void registerSvcProcess();

    virtual SvcMgr* getSvcMgr() override;

    /* SvcServerListener notifications */
    virtual void notifyServerDown(const Error &e) override;

 protected:
    /**
    * @brief Sets up configdb used for persistence
    */
    virtual void setupConfigDb_();

    /**
    * @brief Populates service information
    */
    virtual void setupSvcInfo_();

    /**
    * @brief Sets up service layer manager
    *
    * @param processor
    */
    virtual void setupSvcMgr_(PlatNetSvcHandlerPtr handler,
                              fpi::PlatNetSvcProcessorPtr processor);

    /* TODO(Rao): Include persistence as well */
    boost::shared_ptr<SvcMgr> svcMgr_;
    fpi::SvcInfo              svcInfo_;
    std::string               svcName_;
};
}  // namespace fds
#endif  // SOURCE_INCLUDE_NET_SVCPROCESS_H_
