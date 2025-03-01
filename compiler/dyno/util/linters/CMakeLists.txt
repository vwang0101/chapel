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

add_executable(fieldsUsed "fieldsUsed.cpp")

# This option is set from the call to CMake from compiler/dyno/Makefile.help
# TODO: use DYNO_LLVM_COMP_ARGS DYNO_LLVM_LINK_ARGS instead since
# these should include clang arguments for CHPL_LLVM=system / CHPL_LLVM=bundled.
# TODO: don't try to `find_package` if CHPL_LLVM=none
option(DYNO_LINTERS_CLANG_CMAKE_PATH "Override the path to find a ClangConfig.cmake")

if(DYNO_LINTERS_CLANG_CMAKE_PATH)
  # this finds the target clang-cpp and also finds an appropriate LLVM version
  # Not marked as required since this linter is optional
  find_package(Clang PATHS ${DYNO_LINTERS_CLANG_CMAKE_PATH} NO_DEFAULT_PATH)
else()
  find_package(Clang)
endif()

message("Using libclang from ${Clang_DIR}")

target_link_libraries(fieldsUsed clang-cpp ${DYNO_LLVM_LINK_ARGS})
