# **********************************************************************
#
# Copyright (c) 2003-2013 ZeroC, Inc. All rights reserved.
#
# This copy of Ice is licensed to you under the terms described in the
# ICE_LICENSE file included in this distribution.
#
# **********************************************************************

top_srcdir	= ..\..\..

TARGETS		= $(OBJS)

OBJS		= Test.obj

SRCS		= $(OBJS:.obj=.cpp)

!include $(top_srcdir)/config/Make.rules.mak

SLICE2CPPFLAGS	= -I. $(SLICE2CPPFLAGS)
CPPFLAGS	= -I. $(CPPFLAGS) -DWIN32_LEAN_AND_MEAN

clean::
	del /q Test.cpp Test.h

!include .depend.mak
