# **********************************************************************
#
# Copyright (c) 2003-2013 ZeroC, Inc. All rights reserved.
#
# This copy of Ice is licensed to you under the terms described in the
# ICE_LICENSE file included in this distribution.
#
# **********************************************************************

top_srcdir	= ..

!include $(top_srcdir)\config\Make.rules.mak.cs

SUBDIRS		= Ice IceStorm Glacier2 IcePatch2 IceGrid

!if "$(COMPACT)" != "yes" && "$(SILVERLIGHT)" != "yes"
SUBDIRS		= $(SUBDIRS) IceSSL
!endif

!if "$(SILVERLIGHT)" != "yes"
SUBDIRS		= $(SUBDIRS) IceBox PolicyServer
!endif

$(EVERYTHING)::
	@for %i in ( $(SUBDIRS) ) do \
	    @echo "making $@ in %i" && \
	    cmd /c "cd %i && $(MAKE) -nologo -f Makefile.mak $@" || exit 1
