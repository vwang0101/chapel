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

#ifndef _CHPL_GPU_H_
#define _CHPL_GPU_H_

#include <stdbool.h>
#include "chpl-tasks.h"
#include "chpl-mem-desc.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAS_GPU_LOCALE

static inline void CHPL_GPU_DEBUG(const char *str, ...) {
  if (verbosity >= 2) {
    va_list args;
    va_start(args, str);
    vfprintf(stdout, str, args);
    va_end(args);
    fflush(stdout);
  }
}

static inline bool chpl_gpu_running_on_gpu_locale(void) {
  return chpl_task_getRequestedSubloc()>=0;
}

void chpl_gpu_init(void);
void chpl_gpu_on_std_modules_finished_initializing(void);

void chpl_gpu_launch_kernel(int ln, int32_t fn,
                            const char* fatbinData, const char* name,
                            int grd_dim_x, int grd_dim_y, int grd_dim_z,
                            int blk_dim_x, int blk_dim_y, int blk_dim_z,
                            int nargs, ...);
void chpl_gpu_launch_kernel_flat(int ln, int32_t fn,
                                 const char* fatbinPath, const char* name,
                                 int num_threads, int blk_dim,
                                 int nargs, ...);

void* chpl_gpu_mem_array_alloc(size_t size, chpl_mem_descInt_t description,
                                   int32_t lineno, int32_t filename);
void* chpl_gpu_mem_alloc(size_t size, chpl_mem_descInt_t description,
                         int32_t lineno, int32_t filename);
void* chpl_gpu_mem_calloc(size_t number, size_t size,
                          chpl_mem_descInt_t description,
                          int32_t lineno, int32_t filename);
void* chpl_gpu_mem_realloc(void* memAlloc, size_t size,
                           chpl_mem_descInt_t description,
                           int32_t lineno, int32_t filename);
void* chpl_gpu_mem_memalign(size_t boundary, size_t size,
                            chpl_mem_descInt_t description,
                            int32_t lineno, int32_t filename);
void chpl_gpu_mem_free(void* memAlloc, int32_t lineno, int32_t filename);

void* chpl_gpu_memmove(void* dst, const void* src, size_t n);
void chpl_gpu_copy_device_to_host(void* dst, const void* src, size_t n);
void chpl_gpu_copy_host_to_device(void* dst, const void* src, size_t n);

bool chpl_gpu_is_device_ptr(const void* ptr);
bool chpl_gpu_is_host_ptr(const void* ptr);

// TODO do we really need to expose this?
size_t chpl_gpu_get_alloc_size(void* ptr);
#endif // HAS_GPU_LOCALE

#ifdef __cplusplus
}
#endif

#endif
