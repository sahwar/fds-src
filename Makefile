topdir         := .
user_ext_build := true

user_build_dir := \
    thrift-0.9.0  \
    jansson-2.5   \
    gmock-1.7.0   \
    cmdconsole    \
	gcovr         \
    source

include $(topdir)/Makefile.incl

.PHONY: coverage

all: generate-config-files

coverage:
	cd source && make coverage
