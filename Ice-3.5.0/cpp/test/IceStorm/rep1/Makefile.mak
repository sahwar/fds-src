# **********************************************************************
#
# Copyright (c) 2003-2013 ZeroC, Inc. All rights reserved.
#
# This copy of Ice is licensed to you under the terms described in the
# ICE_LICENSE file included in this distribution.
#
# **********************************************************************

top_srcdir	= ..\..\..

PUBLISHER	= publisher.exe
SUBSCRIBER	= subscriber.exe
SUB		= sub.exe

TARGETS		= $(PUBLISHER) $(SUBSCRIBER) $(SUB)

OBJS		= Single.obj

POBJS		= Publisher.obj

SOBJS		= Subscriber.obj

SUB_OBJS	= Sub.obj

SRCS		= $(OBJS:.obj=.cpp) \
		  $(POBJS:.obj=.cpp) \
		  $(SOBJS:.obj=.cpp) \
		  $(SUB_OBJS:.obj=.cpp)

!include $(top_srcdir)/config/Make.rules.mak

CPPFLAGS	= -I. -I../../include $(CPPFLAGS) -DWIN32_LEAN_AND_MEAN
LIBS		= icestorm$(LIBSUFFIX).lib $(LIBS)

!if "$(GENERATE_PDB)" == "yes"
PPDBFLAGS        = /pdb:$(PUBLISHER:.exe=.pdb)
SPDBFLAGS        = /pdb:$(SUBSCRIBER:.exe=.pdb)
SUB_PDBFLAGS     = /pdb:$(SUB:.exe=.pdb)
!endif

$(PUBLISHER): $(OBJS) $(POBJS)
	$(LINK) $(LD_EXEFLAGS) $(PPDBFLAGS) $(SETARGV) $(OBJS) $(POBJS) $(PREOUT)$@ $(PRELIBS)$(LIBS)
	@if exist $@.manifest echo ^ ^ ^ Embedding manifest using $(MT) && \
	    $(MT) -nologo -manifest $@.manifest -outputresource:$@;#1 && del /q $@.manifest

$(SUBSCRIBER): $(OBJS) $(SOBJS)
	$(LINK) $(LD_EXEFLAGS) $(SPDBFLAGS) $(SETARGV) $(OBJS) $(SOBJS) $(PREOUT)$@ $(PRELIBS)$(LIBS)
	@if exist $@.manifest echo ^ ^ ^ Embedding manifest using $(MT) && \
	    $(MT) -nologo -manifest $@.manifest -outputresource:$@;#1 && del /q $@.manifest

$(SUB): $(OBJS) $(SUB_OBJS)
	$(LINK) $(LD_EXEFLAGS) $(SUB_PDBFLAGS) $(SETARGV) $(OBJS) $(SUB_OBJS) $(PREOUT)$@ $(PRELIBS)$(LIBS)
	@if exist $@.manifest echo ^ ^ ^ Embedding manifest using $(MT) && \
	    $(MT) -nologo -manifest $@.manifest -outputresource:$@;#1 && del /q $@.manifest

!if "$(OPTIMIZE)" == "yes"

all::
	@echo release > build.txt

!else

all::
	@echo debug > build.txt

!endif

clean::
	del /q build.txt
	del /q Single.cpp Single.h
	-if exist 0.db\__Freeze rmdir /q /s 0.db\__Freeze
	-for %f in (0.db\*) do if not %f == 0.db\.gitignore del /q %f
	-if exist 1.db\__Freeze rmdir /q /s 1.db\__Freeze
	-for %f in (1.db\*) do if not %f == 1.db\.gitignore del /q %f
	-if exist 2.db\__Freeze rmdir /q /s 2.db\__Freeze
	-for %f in (2.db\*) do if not %f == 2.db\.gitignore del /q %f

!include .depend.mak
