/*
 * Copyright 2013 Formation Data Systems, Inc.
 */
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string>
#include <iostream>
#include <fds_assert.h>
#include <fds_process.h>
#include <net/net_utils.h>

#include <unistd.h>

namespace fds {

/* Processwide globals from fds_process.h */
FdsProcess *g_fdsprocess = NULL;
fds_log *g_fdslog = NULL;
boost::shared_ptr<FdsCountersMgr> g_cntrs_mgr;
const FdsRootDir                 *g_fdsroot;

void init_process_globals(const std::string &log_name)
{
    fds_log *temp_log = new fds_log(log_name, "logs");
    init_process_globals(temp_log);
}

void init_process_globals(fds_log *log)
{
    fds_verify(g_fdslog == nullptr);
    g_fdslog = log;
    g_cntrs_mgr.reset(new FdsCountersMgr(net::get_my_hostname()+".unknown"));
}

/**
* @brief Constructor.  Keep it as bare shell.  Do the initialization work
* in init()
*/
FdsProcess::FdsProcess()
{
}

/**
* @brief Constructor.  Keep it as bare shell.  Do the initialization work
* in init()
*
* @param argc
* @param argv[]
* @param def_cfg_file
* @param base_path
* @param mod_vec
*/
FdsProcess::FdsProcess(int argc, char *argv[],
                       const std::string &def_cfg_file,
                       const std::string &base_path, Module **mod_vec)
    : FdsProcess(argc, argv, def_cfg_file, base_path, "", mod_vec)
{
}

/**
* @brief Constructor.  Keep it as bare shell.  Do the initialization work
* in init()
*
* @param argc
* @param argv[]
* @param def_cfg_file
* @param base_path
* @param def_log_file
* @param mod_vec
*/
FdsProcess::FdsProcess(int argc, char *argv[],
                       const std::string &def_cfg_file,
                       const std::string &base_path,
                       const std::string &def_log_file, fds::Module **mod_vec)
{
    init(argc, argv, def_cfg_file, base_path, def_log_file, mod_vec);
}

void FdsProcess::init(int argc, char *argv[],
                      const std::string &def_cfg_file,
                      const std::string &base_path,
                      const std::string &def_log_file, fds::Module **mod_vec)
{
    std::string  fdsroot, cfgfile;

    fds_verify(g_fdsprocess == NULL);

    mod_shutdown_invoked_  = false;

    /* Initialize process wide globals */
    g_fdsprocess = this;
    /* Set up the signal handler.  We should do this before creating any threads */
    setup_sig_handler();

    /* Setup module vectors and config */
    mod_vectors_ = new ModuleVector(argc, argv, mod_vec);
    fdsroot      = mod_vectors_->get_sys_params()->fds_root;
    proc_root    = new FdsRootDir(fdsroot);
    proc_thrp    = NULL;

    if (def_cfg_file != "") {
        cfgfile = proc_root->dir_fds_etc() + def_cfg_file;
        setup_config(argc, argv, cfgfile, base_path);
        /*
         * Create a global logger.  Logger is created here because we need the file
         * name from config
         */
        if (def_log_file == "") {
            g_fdslog = new fds_log(proc_root->dir_fds_logs() +
                                   conf_helper_.get<std::string>("logfile"),
                                   proc_root->dir_fds_logs());
        } else {
            g_fdslog = new fds_log(proc_root->dir_fds_logs() + def_log_file,
                                   proc_root->dir_fds_logs());
        }
        /* Process wide counters setup */
        std::string proc_id = argv[0];
        if (conf_helper_.exists("id")) {
            proc_id = conf_helper_.get<std::string>("id");
        }
        setup_cntrs_mgr(net::get_my_hostname() + "."  + proc_id);

        /* Process wide timer service */
        setup_timer_service();

        /* if graphite is enabled, setup graphite task to dump counters */
        if (conf_helper_.get<bool>("enable_graphite")) {
            /* NOTE: Timer service will be setup as well */
            setup_graphite();
        }
        /* If threadpool option is specified, create one. */
        if (conf_helper_.exists("threadpool")) {
            int max_task, spawn_thres, idle_sec, min_thr, max_thr;

            max_task    = conf_helper_.get<int>("threadpool.max_task", 10);
            spawn_thres = conf_helper_.get<int>("threadpool.spawn_thres", 5);
            idle_sec    = conf_helper_.get<int>("threadpool.idle_sec", 3);
            min_thr     = conf_helper_.get<int>("threadpool.min_thread", 3);
            max_thr     = conf_helper_.get<int>("threadpool.max_thread", 8);
            proc_thrp   = new fds_threadpool(max_task, spawn_thres,
                                             idle_sec, min_thr, max_thr);
        }
    } else {
        g_fdslog  = new fds_log(def_log_file, proc_root->dir_fds_logs());
    }
    /* Set the logger level */
    g_fdslog->setSeverityFilter(
        fds_log::getLevelFromName(conf_helper_.get<std::string>("log_severity")));
}

FdsProcess::~FdsProcess()
{
    /* Kill timer service */
    if (timer_servicePtr_) {
        timer_servicePtr_->destroy();
    }
    /* cleanup process wide globals */
    g_fdsprocess = NULL;
    delete g_fdslog;

    /* Terminate signal handling thread */
    int rc = pthread_kill(sig_tid_, SIGTERM);
    fds_assert(rc == 0);
    rc = pthread_join(sig_tid_, NULL);
    fds_assert(rc == 0);

    if (proc_thrp != NULL) {
        delete proc_thrp;
    }
    delete proc_root;
    delete mod_vectors_;
}

void FdsProcess::proc_pre_startup() {}
void FdsProcess::proc_pre_service() {}

/**
 * The main method to coordinate everything in proper sequence.
 */
int FdsProcess::main()
{
    int ret;

    mod_vectors_->mod_init_modules();

    /* The process should have all objects allocated in proper place. */
    proc_pre_startup();

    /* Do FDS process startup sequence. */
    mod_vectors_->mod_startup_modules();
    mod_vectors_->mod_run_locksteps();

    /*  Star to run the main process. */
    proc_pre_service();
    mod_vectors_->mod_start_services();

    /* Run the main loop. */
    ret = run();

    /* Only do module shutdown once.  Module shutdown can happen in interrupt_cb() */
    if (!mod_shutdown_invoked_) {
        /* Do FDS shutdown sequence. */
        mod_vectors_->mod_stop_services();
        mod_vectors_->mod_shutdown_locksteps();
        mod_vectors_->mod_shutdown();
    }
    return ret;
}

void FdsProcess::setup_config(int argc, char *argv[],
                       const std::string &default_config_file,
                       const std::string &base_path)
{
    boost::shared_ptr<FdsConfig> config(new FdsConfig(default_config_file,
                                                      argc, argv));
    conf_helper_.init(config, base_path);
}

FdsConfigAccessor FdsProcess::get_conf_helper() const {
    return conf_helper_;
}

boost::shared_ptr<FdsConfig> FdsProcess::get_fds_config() const
{
    return conf_helper_.get_fds_config();
}

boost::shared_ptr<FdsCountersMgr> FdsProcess::get_cntrs_mgr() const {
    return cntrs_mgrPtr_;
}

FdsTimerPtr FdsProcess::getTimer() const
{
    return timer_servicePtr_;
}

void*
FdsProcess::sig_handler(void* param)
{
    sigset_t ctrl_c_sigs;
    sigemptyset(&ctrl_c_sigs);
    sigaddset(&ctrl_c_sigs, SIGHUP);
    sigaddset(&ctrl_c_sigs, SIGINT);
    sigaddset(&ctrl_c_sigs, SIGTERM);

    while (true)
    {
        int signum = 0;
        int rc = sigwait(&ctrl_c_sigs, &signum);
        if (rc == EINTR)
        {
            /*
             * Some sigwait() implementations incorrectly return EINTR
             * when interrupted by an unblocked caught signal
             */
            continue;
        }
        fds_assert(rc == 0);

        if (g_fdsprocess) {
            g_fdsprocess->interrupt_cb(signum);
            return NULL;
        } else {
            return reinterpret_cast<void*>(NULL);
        }
    }
    return reinterpret_cast<void*>(NULL);
}

/**
 * ICE inspired way of handling signals.  This can possibly be
 * replaced by boost::asio::signal_set.  This needs to be investigated
 */
void FdsProcess::setup_sig_handler()
{
    /*
     * We will block ctrl+c like signals in the main thread.  All
     * other threads will have signals blocked as well.  We will
     * create a thread to listen for signals
     */
    sigset_t ctrl_c_sigs;
    sigemptyset(&ctrl_c_sigs);
    sigaddset(&ctrl_c_sigs, SIGHUP);
    sigaddset(&ctrl_c_sigs, SIGINT);
    sigaddset(&ctrl_c_sigs, SIGTERM);

    int rc = pthread_sigmask(SIG_BLOCK, &ctrl_c_sigs, 0);
    fds_assert(rc == 0);

    // Joinable thread
    rc = pthread_create(&sig_tid_, 0, FdsProcess::sig_handler, 0);
    fds_assert(rc == 0);
}

void FdsProcess::setup_cntrs_mgr(const std::string &mgr_id)
{
    fds_verify(cntrs_mgrPtr_.get() == NULL);
    cntrs_mgrPtr_.reset(new FdsCountersMgr(mgr_id));
    g_cntrs_mgr = cntrs_mgrPtr_;
}

void FdsProcess::setup_timer_service()
{
    if (!timer_servicePtr_) {
        timer_servicePtr_.reset(new FdsTimer());
    }
}

void FdsProcess::setup_graphite()
{
    std::string ip = conf_helper_.get<std::string>("graphite.ip");
    int port = conf_helper_.get<int>("graphite.port");

    graphitePtr_.reset(new GraphiteClient(ip, port,
                                          timer_servicePtr_, cntrs_mgrPtr_));
    int period = conf_helper_.get<int>("graphite.period");
    graphitePtr_->start(period);
    FDS_PLOG(g_fdslog) << "Set up graphite.  ip: " << ip << " port: " << port 
        << " period: " << period;
}

void FdsProcess::interrupt_cb(int signum)
{
    mod_shutdown_invoked_ = true;
    /* Do FDS shutdown sequence. */
    mod_vectors_->mod_stop_services();
    mod_vectors_->mod_shutdown_locksteps();
    mod_vectors_->mod_shutdown();
}

void FdsProcess::daemonize() {
    // adapted from http://www.enderunix.org/docs/eng/daemon.php

    if (getppid() == 1) return; /* already a daemon */
    int childpid = fork();
    if (childpid < 0) {
        LOGERROR << "error forking for daemonize : " << errno;
        exit(1);
    }
    if (childpid > 0) {
        LOGNORMAL << "forked successfully : child pid : " << childpid;
        exit(0);
    }

    // The actual daemon .

    /* obtain a new process group */
    setsid();
    int i;
    for (i = getdtablesize(); i >= 0 ; --i) close(i); /* close all descriptors */
    i = open("/dev/null", O_RDWR); dup(i); dup(i); /* handle standart I/O */
    // umask(027); /* set newly created file permissions */
    // ignore tty signals
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGHUP , SIG_IGN);
    // signal(SIGTERM,signal_handler);
}

fds_log* HasLogger::GetLog() const {
    if (logptr != NULL) return logptr;
    if (g_fdslog) return g_fdslog;
    logptr = new fds_log("log");
    return logptr;
}

fds_log* HasLogger::SetLog(fds_log* logptr) const {
    fds_log* oldlogptr = this->logptr;
    this->logptr = logptr;
    return oldlogptr;
}

// get the global logger
fds_log* GetLog() {
    if (g_fdslog) return g_fdslog;
    // this is a fallback.
    // if the process did not explicity init ..
    init_process_globals("fds");
    return g_fdslog;
}

}  // namespace fds
