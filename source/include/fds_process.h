/*
 * Copyright 2013 Formation Data Systems, Inc.
 */

#ifndef SOURCE_INCLUDE_FDS_PROCESS_H_
#define SOURCE_INCLUDE_FDS_PROCESS_H_

#include <pthread.h>
#include <csignal>

#include <string>
#include <boost/utility.hpp>
#include <boost/shared_ptr.hpp>
#include <fds_module_provider.h>
#include <fds_globals.h>
#include <fds_module.h>
#include <fds_config.hpp>
#include <fds_counters.h>
#include <graphite_client.h>
#include <util/Log.h>
#include <concurrency/ThreadPool.h>

namespace fds {

/* Forward declarations */
class FlagsMap;

/* These are exposed to make it easy to access them */
extern FdsProcess* g_fdsprocess;
extern fds_log* g_fdslog;
extern boost::shared_ptr<FdsCountersMgr> g_cntrs_mgr;
extern fds_log* GetLog();

/* Helper functions to init process globals. Only invoke these if you
 * aren't deriving from fds_process 
 */
void init_process_globals(const std::string &log_name);
void init_process_globals(fds_log *log);

/**
 * Generic process class.  It provides the following capabilities
 * 1. Signal handling.  Can be overridden.
 * 2. Configuration from file and command line
 * 3. Module vector based initialization
 * 4. Main function to co-ordinate the init, setup and run methods.
 */
class FdsProcess : public boost::noncopyable,
                   public HasLogger,
                   virtual public CommonModuleProviderIf {
 public:
    FdsProcess();
    FdsProcess(int argc, char *argv[],
               const std::string &def_cfg_file,
               const std::string &base_path, Module **mod_vec);
    FdsProcess(int argc, char *argv[],
               const std::string &def_cfg_file,
               const std::string &base_path,
               const std::string &def_log_file,  Module **mod_vec);

    /* Constructs FdsProcess object */
    /**
     * @param argc
     * @param argv
     * @param def_cfg_file - default configuration file path
     * @param base_path - base path to stanza to describing the process config
     * @param def_log_file - default log file path
     * @param mod_vec - module vectors for the process.
     */
    void init(int argc, char *argv[],
              const std::string &def_cfg_file,
              const std::string &base_path,
              const std::string &def_log_file,  Module **mod_vec);

    virtual ~FdsProcess();

    /**
     * Override this method to provide your setup.
     * By default module vector based startup sequence is performed here.  
     * When you override make sure to invoke base class setup to ensure
     * module vector is executed.
     */
    virtual void proc_pre_startup();
    virtual void proc_pre_service();

    /**
     * Main processing loop goes here
     */
    virtual int run() = 0;

    /**
     * main method to coordinate the init of vector modules in various steps, setup
     * and run.  Return the value from the run() method.
     * Sequence in main:
     *    For each module, call mod_init().
     *    Call proc_pre_startup()
     *    For each module, call mod_startup().
     *    For each module, call mod_lockstep()
     *    Call proc_pre_service()
     *    For each module, call mod_enable_service()
     *    Call run()
     *    For each module, call mod_stop_services()
     *    For each module, call mod_shutdown_locksteps()
     *    For each module, call shutdown().
     */
    virtual int main();

    void daemonize();

    /**
     * Handler function for Ctrl+c like signals.  Default implementation
     * just calls exit(0).
     * @param signum
     */
    virtual void interrupt_cb(int signum);

    /* Moduel provider overrides */
    /**
     * Returns the global fds config helper object
     */
    virtual FdsConfigAccessor get_conf_helper() const override;

    /**
     * Returns config object
     * @return
     */
    virtual boost::shared_ptr<FdsConfig> get_fds_config() const override;

    /**
    * @brief Returns process wide timer
    *
    * @return 
    */
    virtual FdsTimerPtr getTimer() const override;

    /**
     * Return global counter manager
     * @return
     */
    virtual boost::shared_ptr<FdsCountersMgr> get_cntrs_mgr() const override;

    /**
     * Return the fds root directory obj.
     */
    virtual const FdsRootDir *proc_fdsroot() const  override
    {
        return proc_root;
    }

    virtual fds_threadpool *proc_thrpool() const override
    {
        return proc_thrp;
    }


 protected:
    // static members/methods
    static void* sig_handler(void* param);

 protected:
    virtual void setup_config(int argc, char *argv[],
               const std::string &default_config_path,
               const std::string &base_path);

    virtual void setup_sig_handler();
    virtual void setup_cntrs_mgr(const std::string &mgr_id);
    virtual void setup_timer_service();
    virtual void setup_graphite();

    /* Signal handler thread */
    pthread_t sig_tid_;

    /* Process wide config accessor */
    FdsConfigAccessor conf_helper_;

    /* Process wide counters manager */
    boost::shared_ptr<FdsCountersMgr> cntrs_mgrPtr_;

    /* Process wide timer service.  Not enabled by default.  It needs
     * to be explicitly enabled
     */
    boost::shared_ptr<FdsTimer> timer_servicePtr_;

    /* Graphite client for exporting stats.  Only enabled based on config and a
     * compiler directive DEV_BUILD (this isn't done yet)
     */
    boost::shared_ptr<GraphiteClient> graphitePtr_;

    /* Module vectors. */
    ModuleVector *mod_vectors_;

    /* Flag to indicate whether modulue shutdown has been invoked or not */
    std::atomic<bool>mod_shutdown_invoked_;

    /* FdsRootDir globals. */
    FdsRootDir   *proc_root;

    /* Process wide thread pool. */
    fds_threadpool *proc_thrp;
};

}  // namespace fds

#endif  // SOURCE_INCLUDE_FDS_PROCESS_H_
