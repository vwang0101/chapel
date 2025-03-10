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

#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include "chplcgfns.h"
#include "chpllaunch.h"
#include "chpl-mem.h"
#include "chpltypes.h"
#include "error.h"

#define LAUNCH_PATH_HELP WRAP_TO_STR(LAUNCH_PATH)
#define WRAP_TO_STR(x) TO_STR(x)
#define TO_STR(x) #x

#define basePBSFilename ".chpl-pbs-qsub-"
#define baseExpectFilename ".chpl-expect-"
#define baseSysFilename ".chpl-sys-"

char pbsFilename[FILENAME_MAX];
char expectFilename[FILENAME_MAX];
char sysFilename[FILENAME_MAX];

/* copies of binary to run per node */
#define procsPerNode 1

#define launcherAccountEnvvar "CHPL_LAUNCHER_ACCOUNT"

typedef enum {
  pbspro,
  nccs,
  torque,
  unknown
} qsubVersion;

static qsubVersion determineQsubVersion(void) {
  const int buflen = 256;
  char version[buflen];
  char *argv[3];
  argv[0] = (char *) "qsub";
  argv[1] = (char *) "--version";
  argv[2] = NULL;

  memset(version, 0, buflen);
  if (chpl_run_utility1K("qsub", argv, version, buflen) <= 0) {
    chpl_error("Error trying to determine qsub version", 0, 0);
  }

  if (strstr(version, "NCCS")) {
    return nccs;
  } else if (strstr(version, "PBSPro")) {
    return pbspro;
  } else if (strstr(version, "version:") || strstr(version, "Version:")) {
    return torque;
  } else {
    return unknown;
  }
}

static int getNumCoresPerLocale(void) {
  int numCores = 0;
  char* numCoresString = getenv("CHPL_LAUNCHER_CORES_PER_LOCALE");

  if (numCoresString) {
    numCores = atoi(numCoresString);
    if (numCores != 0)
      return numCores;
  }

  return numCores;
}

static void genNumLocalesOptions(FILE* pbsFile, qsubVersion qsub,
                                 int32_t numLocales,
                                 int32_t numCoresPerLocale) {
  char* queue = getenv("CHPL_LAUNCHER_QUEUE");
  char* walltime = getenv("CHPL_LAUNCHER_WALLTIME");

  if (queue)
    fprintf(pbsFile, "#PBS -q %s\n", queue);
  if (walltime)
    fprintf(pbsFile, "#PBS -l walltime=%s\n", walltime);
  switch (qsub) {
  case pbspro:
  case unknown:
    fprintf(pbsFile, "#PBS -l mppwidth=%d\n", numLocales);
    fprintf(pbsFile, "#PBS -l mppnppn=%d\n", procsPerNode);
    if (numCoresPerLocale)
      fprintf(pbsFile, "#PBS -l mppdepth=%d\n", numCoresPerLocale);
    break;
  case torque:
    fprintf(pbsFile, "#PBS -l nodes=%d\n", numLocales);
    break;
  case nccs:
    if (!queue && !walltime)
      chpl_error("An execution time must be specified for the NCCS launcher if no queue is\n"
                 "specified -- use the CHPL_LAUNCHER_WALLTIME and/or CHPL_LAUNCHER_QUEUE\n"
                 "environment variables", 0, 0);
    if (numCoresPerLocale)
      fprintf(pbsFile, "#PBS -l nodes=%d\n", numLocales);
    break;
  }
}

static char* chpl_launch_create_command(int argc, char* argv[],
                                        int32_t numLocales) {
  int i;
  FILE* pbsFile, *expectFile;
  char* projectString = getenv(launcherAccountEnvvar);
  char* basenamePtr = strrchr(argv[0], '/');
  pid_t mypid;
  char  jobName[128];

  chpl_launcher_get_job_name(basenamePtr, jobName, sizeof(jobName));

  chpl_compute_real_binary_name(argv[0]);

#ifndef DEBUG_LAUNCH
  mypid = getpid();
#else
  mypid = 0;
#endif
  sprintf(sysFilename, "%s%d", baseSysFilename, (int)mypid);
  sprintf(expectFilename, "%s%d", baseExpectFilename, (int)mypid);
  sprintf(pbsFilename, "%s%d", basePBSFilename, (int)mypid);

  pbsFile = fopen(pbsFilename, "w");
  fprintf(pbsFile, "#!/bin/sh\n\n");
  fprintf(pbsFile, "#PBS -N %s\n", jobName);
  genNumLocalesOptions(pbsFile, determineQsubVersion(), numLocales, getNumCoresPerLocale());
  if (projectString && strlen(projectString) > 0)
    fprintf(pbsFile, "#PBS -A %s\n", projectString);
  fclose(pbsFile);

  expectFile = fopen(expectFilename, "w");
  if (verbosity < 2) {
    fprintf(expectFile, "log_user 0\n");
  }
  fprintf(expectFile, "set timeout -1\n");
  fprintf(expectFile, "set prompt \"(%%|#|\\\\$|>) $\"\n");
  fprintf(expectFile, "spawn qsub -z ");
  fprintf(expectFile, "-V "); // pass through all environment variables
  fprintf(expectFile, "-I %s\n", pbsFilename);
  fprintf(expectFile, "expect -re $prompt\n");
  fprintf(expectFile, "send \"cd \\$PBS_O_WORKDIR\\n\"\n");
  fprintf(expectFile, "expect -re $prompt\n");
  fprintf(expectFile, "send \"%s/%s/gasnetrun_ibv -n %d -N %d",
          CHPL_THIRD_PARTY, WRAP_TO_STR(LAUNCH_PATH), numLocales, numLocales);
  fprintf(expectFile, " %s ", chpl_get_real_binary_name());
  for (i=1; i<argc; i++) {
    fprintf(expectFile, " '%s'", argv[i]);
  }
  fprintf(expectFile, "\\n\"\n");
  fprintf(expectFile, "interact -o -re $prompt {return}\n");
  fprintf(expectFile, "send_user \"\\n\"\n");
  fprintf(expectFile, "send \"exit\\n\"\n");
  fclose(expectFile);

  const char *cmd_fmt = "expect %s";
  const int cmd_len
            = strlen(cmd_fmt)           // 'expect' command printf() format
              - 2                       //   length of "%s" specifier
              + strlen(expectFilename)  //   length of expectFilename
              + 1;                      //   length of trailing '\0'
  char* command = chpl_mem_allocMany(cmd_len, sizeof(command[0]),
                                     CHPL_RT_MD_COMMAND_BUFFER, -1, 0);
  if (snprintf(command, cmd_len, cmd_fmt, expectFilename) >= cmd_len) {
    chpl_internal_error("buffer overflow");
  }

  return command;
}

static void chpl_launch_cleanup(void) {
#ifndef DEBUG_LAUNCH
  if (!chpl_doDryRun()) {
    {
      char command[sizeof(pbsFilename) + 4];
      (void) snprintf(command, sizeof(command), "rm %s", pbsFilename);
      system(command);
    }

    {
      char command[sizeof(expectFilename) + 4];
      (void) snprintf(command, sizeof(command), "rm %s", expectFilename);
      system(command);
    }

    {
      char command[sizeof(sysFilename) + 4];
      (void) snprintf(command, sizeof(command), "rm %s", sysFilename);
      system(command);
    }
  }
#endif
}


int chpl_launch(int argc, char* argv[], int32_t numLocales) {
  int retcode =
    chpl_launch_using_system(chpl_launch_create_command(argc, argv, numLocales),
                             argv[0]);
  chpl_launch_cleanup();
  return retcode;
}


int chpl_launch_handle_arg(int argc, char* argv[], int argNum,
                           int32_t lineno, int32_t filename) {
  return 0;
}


const argDescTuple_t* chpl_launch_get_help(void) {
  return NULL;
}
