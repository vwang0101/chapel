/*
 * Copyright 2020-2022 Hewlett Packard Enterprise Development LP
 * Copyright 2004-2019 Cray Inc.
 * Other additional copyright holders may be indicated within.
 *
 * The entirety of this work is licensed under the Apache License,
 * Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.
 *
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _CHPL_INIT_H_
#define _CHPL_INIT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#ifndef LAUNCHER

void chpl_rt_preUserCodeHook(void);
void chpl_rt_postUserCodeHook(void);
const char* allocate_string_literals_buf(int64_t s);
void deallocate_string_literals_buf(void);

#endif // ifndef LAUNCHER

void chpl_rt_init(int argc, char* argv[]);
void chpl_rt_finalize(int return_value);

void chpl_executable_init(void);
void chpl_execute_module_deinit(c_fn_ptr deinitFun);

void chpl_library_init(int argc, char* argv[]);
void chpl_library_finalize(void);

void chpl_std_module_init(void);
void chpl_std_module_finalize(void);

#ifdef __cplusplus
}
#endif

#endif // _CHPL_INIT_H_
