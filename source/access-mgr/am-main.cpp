/*
 * Copyright 2013 Formation Data Systems, Inc.
 */
#include <string>
#include <fdsn-server.h>
#include <util/fds_stat.h>
#include <native_api.h>
#include <am-platform.h>
#include <net/net-service.h>

extern void CreateSHMode(int argc,
                         char *argv[],
                         fds_bool_t test_mode,
                         fds_uint32_t sm_port,
                         fds_uint32_t dm_port);

namespace fds {

class AMMain : public PlatformProcess
{
  public:
    virtual ~AMMain() {}
    AMMain(int argc, char **argv,
               Platform *platform, Module **mod_vec)
        : PlatformProcess(argc, argv, "fds.am.", "am.log", platform, mod_vec) {}

    void proc_pre_startup() override
    {
        int    argc;
        char **argv;

        PlatformProcess::proc_pre_startup();

        FDS_NativeAPI::ptr api(new FDS_NativeAPI(FDS_NativeAPI::FDSN_AWS_S3));
        gl_FdsnServer.init_server(api);
        if (conf_helper_.get<bool>("testing.enable_probe") == true) {
            // Add the AM probe to the S3 connector probe
            // gl_probeS3Eng.probe_add_adapter(&gl_AmProbe);
            // gl_AmProbe.init_server(api);
        }

        argv = mod_vectors_->mod_argv(&argc);
        CreateSHMode(argc, argv, false, 0 , 0);
    }
    int run() override {
        gl_FdsnServer.deinit_server();
        return 0;
    }
};

}  // namespace fds

extern "C" {
    extern void CreateStorHvisorS3(int argc, char *argv[]);
}

int am_gdb = 1;

int main(int argc, char **argv)
{
    fds::Module *am_mod_vec[] = {
        &fds::gl_fds_stat,
        &fds::gl_AmPlatform,
        &fds::gl_NetService,
        &fds::gl_FdsnServer,
        nullptr
    };
    while (am_gdb == 0) { sleep(1); }

    fds::AMMain amMain(argc, argv, &fds::gl_AmPlatform, am_mod_vec);
    return amMain.main();
}