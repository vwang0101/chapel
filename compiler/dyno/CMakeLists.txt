# Copyright 2021-2022 Hewlett Packard Enterprise Development LP
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

cmake_minimum_required(VERSION 3.13.4)

project(dyno VERSION 0.1)

option(DYNO_CHPL_HOME "The CHPL_HOME environment variable for tests to use")
# Options to integrate LLVM support library into dyno
# TODO: enable easier cmake builds of dyno (outside of Chapel's Makefiles)
# by falling back on find_package(LLVM) if
# DYNO_LLVM_COMP_ARGS / DYNO_LLVM_LINK_ARGS are empty.
option(DYNO_LLVM_COMP_ARGS "Override compile args for LLVM")
option(DYNO_LLVM_LINK_ARGS "Override linker args for LLVM")
# remove whitespace or you'll get complaints about an error
# according to policy CMP0004
string(STRIP ${DYNO_LLVM_COMP_ARGS} DYNO_LLVM_COMP_ARGS)
string(STRIP ${DYNO_LLVM_LINK_ARGS} DYNO_LLVM_LINK_ARGS)

set(CHPL_MAIN_SRC_DIR     ${CMAKE_CURRENT_SOURCE_DIR})
set(CHPL_MAIN_INCLUDE_DIR ${CHPL_MAIN_SRC_DIR}/include)
set(CHPL_INCLUDE_DIR      ${CMAKE_CURRENT_BINARY_DIR}/include)

# save the version
configure_file(${CHPL_MAIN_INCLUDE_DIR}/chpl/config/config.h.cmake
               ${CHPL_INCLUDE_DIR}/chpl/config/config.h)


# request C++14
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# request C11
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)

# generate the compile_commands.json compilation database
# tools like the fieldsUsed linter require this
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# add an option to enable/disable assertions
# this is based upon LLVM cmake files
string(TOUPPER "${CMAKE_BUILD_TYPE}" uppercase_CMAKE_BUILD_TYPE)
if( NOT uppercase_CMAKE_BUILD_TYPE STREQUAL "DEBUG" )
  option(DYNO_ENABLE_ASSERTIONS "Enable assertions" OFF)
else()
  option(DYNO_ENABLE_ASSERTIONS "Enable assertions" ON)
endif()

# adjust C/C++ flags to remove -DNDEBUG if assertions is enabled
# this is based upon LLVM cmake files
if( DYNO_ENABLE_ASSERTIONS )
  # MSVC doesn't like _DEBUG on release builds. See PR 4379.
  if( NOT MSVC )
    add_definitions( -D_DEBUG )
  endif()
  # On non-Debug builds cmake automatically defines NDEBUG, so we
  # explicitly undefine it:
  if( NOT uppercase_CMAKE_BUILD_TYPE STREQUAL "DEBUG" )
    # NOTE: use `add_compile_options` rather than `add_definitions` since
    # `add_definitions` does not support generator expressions.
    add_compile_options($<$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>:-UNDEBUG>)

    # Also remove /D NDEBUG to avoid MSVC warnings about conflicting defines.
    foreach (flags_var_to_scrub
        CMAKE_CXX_FLAGS_RELEASE
        CMAKE_CXX_FLAGS_RELWITHDEBINFO
        CMAKE_CXX_FLAGS_MINSIZEREL
        CMAKE_C_FLAGS_RELEASE
        CMAKE_C_FLAGS_RELWITHDEBINFO
        CMAKE_C_FLAGS_MINSIZEREL)
      string (REGEX REPLACE "(^| )[/-]D *NDEBUG($| )" " "
        "${flags_var_to_scrub}" "${${flags_var_to_scrub}}")
    endforeach()
  endif()
endif()

# set include directories for -I
#include_directories(include)

# lib/CMakeLists.txt defines the target libdyno
add_subdirectory(lib)
# also make sure headers are included in the target
add_subdirectory(include/chpl)
# Support for documentation of AST header
add_subdirectory(doc)
# Tools like chpldoc
add_subdirectory(tools)
# Utils like C++ linters for this codebase
add_subdirectory(util)

# Check if the directory is there, because in release tarballs, it shouldn't be
if (IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/test")
  # Support for C++ compiler unit tests
  # Needs to happen in this file for ctest to work in this dir
  enable_testing()
  add_subdirectory(test)
endif()
