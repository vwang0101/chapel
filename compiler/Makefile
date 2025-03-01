# Copyright 2020-2022 Hewlett Packard Enterprise Development LP
# Copyright 2004-2019 Cray Inc.
# Other additional copyright holders may be indicated within.
#
# The entirety of this work is licensed under the Apache License,
# Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.
#
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

#
# Makefile: builds Chapel compiler
#

ifndef CHPL_MAKE_HOME
export CHPL_MAKE_HOME=$(shell pwd)/..
endif


COMPILER_ROOT=.
COMPILER_SUBDIR = .

#
# subdirectories and false subdirectory-oriented targets to force recursion
#
SUBDIRS = \
	adt \
	AST \
	backend \
	codegen \
	llvm \
	main \
	dyno/lib/immediates \
	dyno/lib/parsing \
	dyno/lib/framework \
	dyno/lib/resolution \
	dyno/lib/types \
	dyno/lib/uast \
	dyno/lib/util \
	optimizations \
	parser \
	passes \
	resolution \
	util \


CHPLDOC_SUBDIRS = \
	dyno/lib/immediates \
	dyno/lib/parsing \
	dyno/lib/framework \
	dyno/lib/resolution \
	dyno/lib/types \
	dyno/lib/uast \
	dyno/lib/util \
	dyno/tools/chpldoc \


#
# include standard header for compiler
#
include $(COMPILER_ROOT)/make/Makefile.compiler.head

ALL_HEADERS= */*.h dyno/include/chpl/*/*.h

# Generate tags command, dependent on if Make variable, TAGS == 1
ifeq ($(TAGS), 1)
TAGS_COMMAND=-@(which $(CHPL_TAGS_UTIL) > /dev/null 2>&1 && echo "Updating TAGS..." && $(CHPL_TAGS_UTIL) $(CHPL_TAGS_FLAGS) $(ALL_SRCS) $(ALL_HEADERS)) || echo "$(CHPL_TAGS_UTIL) not available"
EBROWSE_COMMAND=-@(which ebrowse > /dev/null 2>&1 && echo "Updating BROWSE..." && ebrowse $(ALL_SRCS) $(ALL_HEADERS)) || echo "ebrowse not available"
endif

#
# the location of the BUILD_VERSION file
#
BUILD_VERSION_FILE = $(COMPILER_ROOT)/main/BUILD_VERSION
# configured prefix file
CONFIGURED_PREFIX_FILE = $(COMPILER_BUILD)/CONFIGURED_PREFIX

#
# include source subdirectories here
#
include adt/Makefile.include
include AST/Makefile.include
include backend/Makefile.include
include codegen/Makefile.include
include llvm/Makefile.include
include main/Makefile.include
include dyno/lib/immediates/Makefile.include
include dyno/lib/parsing/Makefile.include
include dyno/lib/framework/Makefile.include
include dyno/lib/resolution/Makefile.include
include dyno/lib/types/Makefile.include
include dyno/lib/uast/Makefile.include
include dyno/lib/util/Makefile.include
include optimizations/Makefile.include
include parser/Makefile.include
include passes/Makefile.include
include resolution/Makefile.include
include util/Makefile.include

# sources here are only used to build chpldoc
include dyno/tools/chpldoc/Makefile.include

SRCS =

CHPL_OBJS = \
	$(ADT_OBJS) \
	$(AST_OBJS) \
	$(BACKEND_OBJS) \
	$(CODEGEN_OBJS) \
	$(IFA_OBJS) \
	$(LLVM_OBJS) \
	$(MAIN_OBJS) \
	$(OPTIMIZATIONS_OBJS) \
	$(PARSER_OBJS) \
	$(PASSES_OBJS) \
	$(RESOLUTION_OBJS) \
	$(SYMTAB_OBJS) \
	$(UTIL_OBJS) \
	$(DYNO_FRAMEWORK_OBJS) \
	$(DYNO_IMMEDIATES_OBJS) \
	$(DYNO_PARSING_OBJS) \
	$(DYNO_RESOLUTION_OBJS) \
	$(DYNO_TYPES_OBJS) \
	$(DYNO_UAST_OBJS) \
	$(DYNO_UTIL_OBJS) \


CHPLDOC_OBJS = \
	$(DYNO_FRAMEWORK_OBJS) \
	$(DYNO_IMMEDIATES_OBJS) \
	$(DYNO_PARSING_OBJS) \
	$(DYNO_RESOLUTION_OBJS) \
	$(DYNO_TYPES_OBJS) \
	$(DYNO_UAST_OBJS) \
	$(DYNO_UTIL_OBJS) \
        $(DYNO_CHPLDOC_OBJS) \


EXECS = $(CHPL) $(CHPLDOC)

PRETARGETS = $(BUILD_VERSION_FILE) $(CONFIGURED_PREFIX_FILE)
TARGETS = $(CHPL)

LIBS = -lm

#
# main rules
#

include $(COMPILER_ROOT)/make/Makefile.compiler.rules


#
# target-based rules
#

CHPL_CONFIG_CHECK_PREFIX = $(CHPL_BIN_DIR)/.built-for
CHPL_CONFIG_CHECK_DIR = $(CHPL_CONFIG_CHECK_PREFIX)/$(CHPL_MAKE_COMPILER_SUBDIR)
CHPL_CONFIG_CHECK = $(CHPL_CONFIG_CHECK_DIR)/built-for

UPDATE_BUILD_VERSION = $(CHPL_MAKE_HOME)/util/devel/updateBuildVersion

$(BUILD_VERSION_FILE): FORCE
	@({ test -e $(CHPL_MAKE_HOME)/.git ; } && \
	test -x $(UPDATE_BUILD_VERSION) && $(UPDATE_BUILD_VERSION) $@ $(CHPL_MAKE_HOME)) || \
	test -r $(BUILD_VERSION_FILE) || (echo '"0"' > $@);

$(CHPL_MAKE_HOME)/configured-prefix:
	echo > $(CHPL_MAKE_HOME)/configured-prefix

$(CONFIGURED_PREFIX_FILE): FORCE $(COMPILER_BUILD) $(CHPL_MAKE_HOME)/configured-prefix
	@echo '"'`cat $(CHPL_MAKE_HOME)/configured-prefix`'"' \ > $(CONFIGURED_PREFIX_FILE).incoming
	@$(CHPL_MAKE_PYTHON) $(CHPL_MAKE_HOME)/util/config/update-if-different --update $(CONFIGURED_PREFIX_FILE) $(CONFIGURED_PREFIX_FILE).incoming

$(CHPL_CONFIG_CHECK): | $(CHPL_BIN_DIR)
	rm -rf $(CHPL_CONFIG_CHECK_PREFIX)
	mkdir -p $(CHPL_CONFIG_CHECK_DIR)
	echo $(CHPL_MAKE_COMPILER_SUBDIR) > $(CHPL_CONFIG_CHECK)

$(CHPL): $(CHPL_OBJS) $(CHPL_CONFIG_CHECK) | $(CHPL_BIN_DIR)
	$(CXX) $(LDFLAGS) -o $@ $(CHPL_OBJS) $(CHPL_MAKE_HOST_BUNDLED_LINK_ARGS) $(LIBS) $(LLVM_EXTRA_LIBS) $(CHPL_MAKE_HOST_SYSTEM_LINK_ARGS)
	$(TAGS_COMMAND)
	$(EBROWSE_COMMAND)

MAKEALLCHPLDOCSUBDIRS = $(CHPLDOC_SUBDIRS:%=%.makedir)

$(CHPLDOC): $(MAKEALLCHPLDOCSUBDIRS) $(CHPLDOC_OBJS) | $(CHPL_BIN_DIR)
	$(CXX) $(LDFLAGS) -o $@ $(CHPLDOC_OBJS) $(CHPL_MAKE_HOST_BUNDLED_LINK_ARGS) $(LIBS) $(LLVM_EXTRA_LIBS) $(CHPL_MAKE_HOST_SYSTEM_LINK_ARGS)

$(CHPLDOC)-legacy: | $(CHPL_BIN_DIR)
	rm -f $(CHPLDOC)-legacy
	ln -s $(notdir $(CHPL)) $(CHPLDOC)-legacy

chpldoc: $(CHPLDOC) $(CHPLDOC)-legacy

dyno-chpldoc: chpldoc

$(COMPILER_BUILD):
	mkdir -p $@

$(CHPL_BIN_DIR):
	mkdir -p $@


#
# include standard footer for compiler
#
include $(COMPILER_ROOT)/make/Makefile.compiler.foot

.NOTPARALLEL:
