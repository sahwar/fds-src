probe_topdir      := ..
source_dir        := $(probe_dir)/../..
user_incl_dir     :=

user_fds_so_libs  :=
user_fds_ar_libs  := 
user_non_fds_libs :=
user_no_style     :=
user_hh           := $(wildcard *.h)
user_cc           := ngx_modules.c
user_cpp          := \
    test-app.cpp

user_bin_exe      := example-svc
example-svc       := $(user_cpp) $(user_cc)

include $(probe_topdir)/Makefile.svc
