topdir            := ../../..
user_target       := lib
user_rtime_env    := user
user_build_dir    :=

# TODO: remove this once we have correct placement of header API.
#
user_incl_dir     := $(topdir) ../../include
user_cpp_flags    :=
user_cpp          := \
	DmVolumeCatalog.cpp \
	DmPersistVolCat.cpp \
	DmPersistVolDB.cpp
#	DmOIDArrayMmap.cpp \
#	DmPersistVolFile.cpp

user_ar_libs      := fds-dm-lib
user_so_libs      := fds-dm-vol-cat
fds-dm-lib        := $(user_cpp)
fds-dm-vol-cat    := $(user_cpp)

user_fds_ar_libs := fds-lib

include $(topdir)/Makefile.incl
