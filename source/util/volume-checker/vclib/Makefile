topdir            := ../../../
user_target       := lib
user_rtime_env    := user
speedup           = -j $(threads)
user_build_dir    :=

user_incl_dir     := \
                     $(topdir) \
                     ../include

user_cpp_flags    :=
user_fds_so_libs  :=
user_fds_ar_libs  :=  \
    fds-svc-request \
    fds-umod \
    fds-lib \
    fds-util \
    fds-fdsp \
    fds-hash \
    fds-thread \
    fds-osdep


user_static_libs  := ev

user_non_fds_libs :=  \
    boost_log \
    boost_system \
    boost_thread \
    boost_date_time \
    boost_program_options \
    boost_filesystem \
    boost_regex \
    fdsStatsUtil-debug \
    statsclient \
    thrift \
    jansson \
    config++ \
    crypto \
    hiredis \
    event \
    rt \
    fiu \
    dl \
    pthread \
    tcmalloc

user_cc           :=
user_cpp        := $(wildcard *.cpp)

user_no_style     := $(user_cc) $(wildcard com_*.h)
user_ar_libs      := fds-volume-checker
fds-volume-checker := $(user_cpp)

include $(topdir)/Makefile.incl
