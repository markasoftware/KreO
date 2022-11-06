#
# Copyright (C) 2004-2013 Intel Corporation.
# SPDX-License-Identifier: MIT
#

ifeq ($(PIN_ROOT),)
$(error PIN_ROOT environment variable must be set)
endif
ifeq ($(findstring \,$(PIN_ROOT)),\) # this is one of the few occasions in my life I've been able to get away without escaping a backslash
$(error PIN_ROOT should not contain backslashes -- use forward slashes as directory separators, even on Windows)
endif
CONFIG_ROOT := $(PIN_ROOT)/source/tools/Config
include $(CONFIG_ROOT)/makefile.config
include makefile.rules
include $(TOOLS_ROOT)/Config/makefile.default.rules
