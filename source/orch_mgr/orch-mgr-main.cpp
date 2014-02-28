/*
 * Copyright 2013 Formation Data Systems, Inc.
 */

#include <orchMgr.h>
#include <iostream>  // NOLINT(*)
#include <string>
#include <vector>
#include <orch-mgr/om-service.h>
#include <thread>
#include <jni.h>

namespace fds {

extern OrchMgr *orchMgr;
OM_Module       gl_OMModule("OM");

OM_Module *
OM_Module::om_singleton()
{
    return &gl_OMModule;
}

}  // namespace fds

void start_jvm() {
    JavaVM *javaVM;
    JNIEnv* env;
    JavaVMInitArgs args;
    JavaVMOption options[2];

    args.version = JNI_VERSION_1_8;
    args.nOptions = 1;
    options[0].optionString = "-Djava.class.path=classes/";
    // options[1].optionString = "-verbose:jni";
    args.options = options;
    args.ignoreUnrecognized = JNI_FALSE;
    JNI_CreateJavaVM(&javaVM, reinterpret_cast<void **>(&env), &args);
    cout << "Started JVM" << endl;
}

int main(int argc, char *argv[]) {
    fds::orchMgr = new fds::OrchMgr(argc, argv, "orch_mgr.conf", "fds.om.");

    fds::gl_orch_mgr = fds::orchMgr;

    fds::Module *omVec[] = {
        fds::orchMgr,
        &fds::gl_OMModule,
        nullptr};

    fds::orchMgr->setup(argc, argv, omVec);

    std::thread vmThread(start_jvm);
    fds::orchMgr->run();

    delete fds::orchMgr;

    return 0;
}

