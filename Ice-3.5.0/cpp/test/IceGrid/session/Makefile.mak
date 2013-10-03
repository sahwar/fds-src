# **********************************************************************
#
# Copyright (c) 2003-2013 ZeroC, Inc. All rights reserved.
#
# This copy of Ice is licensed to you under the terms described in the
# ICE_LICENSE file included in this distribution.
#
# **********************************************************************

top_srcdir	= ..\..\..

CLIENT		= client.exe
SERVER		= server.exe
VERIFIER        = verifier.exe

TARGETS		= $(CLIENT) $(SERVER) $(VERIFIER)

COBJS		= Client.obj \
		  AllTests.obj \
		  Test.obj

SOBJS		= Server.obj \
		  Test.obj

VOBJS		= PermissionsVerifier.obj \
		  Test.obj

SRCS		= $(COBJS:.obj=.cpp) \
		  $(SOBJS:.obj=.cpp) \
		  $(VOBJS:.obj=.cpp)

!include $(top_srcdir)/config/Make.rules.mak

CPPFLAGS	= -I. -I../../include $(CPPFLAGS) -DWIN32_LEAN_AND_MEAN
LINKWITH	= $(LIBS) glacier2$(LIBSUFFIX).lib

!if "$(GENERATE_PDB)" == "yes"
CPDBFLAGS        = /pdb:$(CLIENT:.exe=.pdb)
SPDBFLAGS        = /pdb:$(SERVER:.exe=.pdb)
VPDBFLAGS        = /pdb:$(VERIFIER:.exe=.pdb)
!endif

$(CLIENT): $(COBJS)
	$(LINK) $(LD_EXEFLAGS) $(CPDBFLAGS) $(SETARGV) $(COBJS) $(PREOUT)$@ $(PRELIBS)$(LINKWITH) icegrid$(LIBSUFFIX).lib
	@if exist $@.manifest echo ^ ^ ^ Embedding manifest using $(MT) && \
	    $(MT) -nologo -manifest $@.manifest -outputresource:$@;#1 && del /q $@.manifest

$(VERIFIER): $(VOBJS)
	$(LINK) $(LD_EXEFLAGS) $(VPDBFLAGS) $(SETARGV) $(VOBJS) $(PREOUT)$@ $(PRELIBS)$(LINKWITH) icessl$(LIBSUFFIX).lib
	@if exist $@.manifest echo ^ ^ ^ Embedding manifest using $(MT) && \
	    $(MT) -nologo -manifest $@.manifest -outputresource:$@;#1 && del /q $@.manifest

$(SERVER): $(SOBJS)
	$(LINK) $(LD_EXEFLAGS) $(SPDBFLAGS) $(SETARGV) $(SOBJS) $(PREOUT)$@ $(PRELIBS)$(LINKWITH) icessl$(LIBSUFFIX).lib
	@if exist $@.manifest echo ^ ^ ^ Embedding manifest using $(MT) && \
	    $(MT) -nologo -manifest $@.manifest -outputresource:$@;#1 && del /q $@.manifest

clean::
	del /q Test.cpp Test.h
	if exist db\node rmdir /s /q db\node 
	if exist db\registry rmdir /s /q db\registry 
	if exist db\node-1 rmdir /s /q db\node-1 
	if exist db\replica-1 rmdir /s /q db\replica-1

!include .depend.mak
