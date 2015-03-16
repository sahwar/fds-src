topdir            := $(test_topdir)/..
user_target       := test
user_std_libs     := true
user_rtime_env    := user

user_incl_dir     += \
    $(topdir)/platform/include \
    $(topdir)/data-mgr/include \
    $(topdir)/lib \
    $(topdir)/util \
    $(topdir)

user_cpp_flags    +=
user_fds_so_libs  +=
user_fds_ar_libs  +=  \
    fds-test \
    fds-dm-lib \
    fds-dsk-mgnt \
    fds-net-svc \
    fds-fdsp \
    fds-dm-lib

user_non_fds_libs +=  \
    boost_timer \
    crypt crypto \
    z \
    ssl \
    gmock \
    leveldb \
    profiler

include $(topdir)/Makefile.incl
