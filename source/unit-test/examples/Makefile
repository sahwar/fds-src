topdir            := ../..
user_target       := test
user_std_libs     := true
user_rtime_env    := user
user_install_loc  := test

user_incl_dir     := \
	$(topdir) \
	$(topdir)/stor-mgr/include \
	$(topdir)/stor-mgr/migrations \
	$(topdir)/lib

user_cpp_flags    :=
user_fds_so_libs  :=
user_fds_ar_libs  := \
    fds-test \
    fds-am-engine \
    fds-am-plat \
    fds-dsk-io \
    fds-dsk-mgnt \
    fds-hash \
    fds-fdsp \
    fds-smlib     \
    fds-hash      \
    fds-migrations \
    fds-odb       \
    fds-sm-plat   \
    fds-smlib

user_non_fds_libs :=  \
    jansson \
	udev \
	parted \
    config++    \
    boost_timer \
    leveldb \
    ssl \
    crypt \
    pcre \
    crypto \
    z \
    leveldb \
    gmock \
    profiler \
    pthread


user_cpp          := \
	goog-pprof.cpp

user_no_style     :=

user_bin_exe      := \
	goog-pprof

goog-pprof := goog-pprof.cpp

include $(topdir)/Makefile.incl
