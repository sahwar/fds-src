topdir            := ..
user_target       := exe
user_std_libs     := true
speedup           = -j $(threads)
user_rtime_env    := user
user_build_dir    := include dm-lib utils

user_incl_dir     := include \
                     $(topdir)/include \
                     $(topdir)/lib \
                     $(topdir)
user_cpp_flags    :=
user_fds_so_libs  :=
user_fds_ar_libs  := \
    fds-qos-lib \
    fds-dm-lib \
    fds-dsk-mgnt \
    fds-fdsp

# boost_chrono and rabbitmq are used in the SimpleAmqpClient library
# The SimpleAmqpClient is used in the statsclient library
user_non_fds_libs := \
    fdsStatsUtil-debug \
    statsclient \
    SimpleAmqpClient \
    rabbitmq \
    jemalloc \
    leveldb \
    ssl \
    crypto \
    pcrecpp \
    sqlite3 \
    boost_chrono \
    boost_system

user_bin_exe      := DataMgr DmChecker
DataMgr           := dm-main.cpp
DmChecker         := DmCheckerMain.cpp

include $(topdir)/Makefile.incl
