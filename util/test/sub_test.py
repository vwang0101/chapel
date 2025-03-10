#!/usr/bin/env python3
#
# Rewrite of sub_test by Sung-Eun Choi (sungeun@cray.com)
#  sub_test is used by start_test in the Chapel Testing system
#  August 2009
#
# This script can be overridden with a script by the same name
#  placed in the test directory.
#
# The use and behavior of the various environment variables,
#  settings, and files were copied straight from sub_test.  They
#  were added/modified to sub_test over the years, and their use
#  is inconsistent and a bit of a mess.  I like to think that this
#  is due to the fact that the original sub_test was written in csh,
#  which was probably pretty novel at the time but is quite limited
#  by today's standards.  In addition, I implemented the timeout
#  mechanism directly rather than calling out to the timedexec
#  (perl) script.
#
# For compatibility reasons, I have maintained the behavior of the
#  original sub_test.  Any new features (e.g., internal timeout
#  mechanism) or modified behaviors (e.g., multiple .compopts,
#  multiple .execopts, custom .good files) will not interfere with
#  the expected behavior of tests that do not use the features or
#  behaviors.
#
#
# ENVIRONMENT VARIABLES:
#
# CHPL_HOME: Grabbed from the environment or deduced based on the path to
#    the compiler.
# CHPL_TEST_VGRND_COMP: Use valgrind on the compiler
# CHPL_TEST_VGRND_EXE: Use valgrind on the test program
# CHPL_VALGRIND_OPTS: Options to valgrind
# CHPL_TEST_FUTURES: 2 == test futures only
#                    1 == test futures and non-futures
#                    0 == test non-futures only
# CHPL_TEST_NOTESTS: Test the tests that are marked "notest" (see below)
# LAUNCHCMD: Uses this command to launch the test program
# CHPL_TEST_INTERP: DEPRECATED
# CHPL_TEST_PERF: Run as a performance test (same as -performance flag)
# CHPL_TEST_PERF_LABEL: The performance label, e.g. "perf"
# CHPL_TEST_PERF_DIR: Scratch directory for performance data
# CHPL_TEST_PERF_TRIALS: Default number of trials for perf tests
# CHPL_ONETEST: Name of the one test in this directory to run
# CHPL_TEST_SINGLES: If false, test the entire directory
# CHPL_SYSTEM_PREEXEC: If set, run comma-separated scripts on test output prior to execution
# CHPL_SYSTEM_PREDIFF: If set, run comma-separated scripts on each test output
# CHPL_COMM: Chapel communication layer
# CHPL_COMPONLY: Only build the test (same as -noexec flag)
# CHPL_TEST_NUM_LOCALES_AVAILABLE: same as CHPL_TEST_MAX_LOCALES (for backwards compatibility)
# CHPL_TEST_MAX_LOCALES: Maximum number of locales to use (overrides above)
# CHPL_TEST_MULTILOCALE_ONLY: Only run tests that use numlocales > 1
# CHPL_NO_STDIN_REDIRECT: do not redirect stdin when running tests
#                         also, skip tests with .stdin files
# CHPL_LAUNCHER_TIMEOUT: if defined, pass an option/options to the executable
#                        for it to enforce timeout instead of using timedexec;
#                        the value of the variable determines the option format.
# CHPL_TEST_TIMEOUT: The default global timeout to use.
# CHPL_TEST_UNIQUIFY_EXE: Uniquify the name of the test executable in the test
#                         system. CAUTION: This wont necessarily work for all
#                         tests, but can allow for running multiple start_tests
#                         over a directory in parallel.
# CHPL_TEST_ROOT_DIR: Absolute path to the test/ dir. Useful when test dir is
#                     not under $CHPL_HOME. Should not be set when test/ is
#                     under $CHPL_HOME. When it is set and the path prefixes a
#                     test in the logs, it will be removed (from the logs).
#
#
# DIRECTORY-WIDE FILES:  These settings are for the entire directory and
#  in many cases can be overridden or augmented with test-specific settings.
#
# NOEXEC: Do not execute tests in this directory
# NOVGRBIN: Do not execute valgrind
# COMPSTDIN: Get stdin from this file (default /dev/null)
# COMPOPTS: Compiler flags
# LASTCOMPOPTS: Compiler flags to be put at the end of the command line
# CHPLDOCOPTS: chpldoc flags
# EXECENV: Environment variables to be applied to the entire directory
# EXECOPTS: Test program flags to be applied to the entire directory
# LASTEXECOPTS: Test program flags to be put at the end of the command line
# NUMLOCALES: Number of locales to use
# CATFILES: List of files whose contents are added to end of test output
# PREDIFF: Script to execute before diff'ing output (arguments: <test
#    executable>, <log>, <compiler executable>)
# PREEXEC: Script to execute before executing test program (arguments: <test
#    executable>, <log>, <compiler executable>)
# PRECOMP: Script to execute before running the compiler (arguments: <test
#    executable>, <log>, <compiler executable>).
# PERFNUMTRIALS: Number of trials to run for performance testing
#
#
# TEST-SPECIFIC FILES:  These setting override or augment the directory-wide
#  settings.  Unless otherwise specified, these files are named
#  <test executable>.suffix (where suffix is one of the following).
#
# .good: "Golden" output file (can have different basenames)
# .compenv: Additional environment variables for the compile
# .compopts: Additional compiler options
# .perfcompenv: Additional environment variables for performance compiling
# .perfcompopts: Additional compiler options for performance testing
# .lastcompopts: Additional compiler options to be added at the end of the
#    command line
# .chpldocopts: Additional chpldoc options.
# .execenv: Additional environment variables for the test
# .execopts: Additional test options
# .perfexecenv: Additional environment variables for performance testing
# .perfexecopts: Additional test options for performance testing
# .perfnumtrials: Number of trials to run for performance testing
# .notest: Do not run this test
# .numlocales: Number of locales to use (overrides NUMLOCALES)
# .future: Future test
# .ifuture: Future test
# .noexec: Do not execute this test
# .skipif: Skip this test if certain environment conditions hold true
# .suppressif: Suppress this test if certain environment conditions hold true
# .timeout: Test timeout (overrides TIMEOUT)
# .perftimeout: Performance test timeout
# .killtimeout: Kill timeout (overrides KILLTIMEOUT)
# .catfiles: Additional list of files whose contents are added to end of
#    test output
# .precomp: Additional script to execute before compiling the test
# .prediff: Additional script to execute before diff'ing output
# .preexec: Additional script to execute before executing test program
# .perfkeys: Existence indicates a performance test.  Contents specifies
#    performance "keys"
#
# In general, the performance label from CHPL_TEST_PERF_LABEL is used
# instead of "perf" in the above suffixes, and its all-caps version is used
# in the all-caps file names instead "PERF".

from __future__ import with_statement

import execution_limiter
import py3_compat
import sys, os, subprocess, string, signal
import operator
import select, fcntl
import fnmatch, time
import re
import shlex
import datetime
import errno
from functools import reduce

# update path to allow 'import fixpath' below
util_dir = os.path.join(os.path.dirname(__file__), '..', '..', 'util')
config_dir = os.path.join(util_dir, 'config')
sys.path.insert(0, os.path.abspath(config_dir))

# update PATH to remove CHPL_HOME paths if any
# This essentially deactivates the venv that sub_test is run in,
# because the test venv is built inside of CHPL_HOME.
# We do this so tests/prediffs can use the system python instead of the venv
import fixpath
fixpath.update_path_env()

localdir = ''
sub_test_start_time = time.time()
def elapsed_sub_test_time():
    """Print elapsed time for sub_test call to console."""
    global sub_test_start_time, localdir
    elapsed_sec = time.time() - sub_test_start_time

    test_name = localdir
    if 'CHPL_ONETEST' in os.environ:
        chpl_name = os.environ.get('CHPL_ONETEST')
        base_name = os.path.splitext(chpl_name)[0]
        test_name = os.path.join(test_name, base_name)

    print('[Finished subtest "{0}" - {1:.3f} seconds]\n'.format(test_name, elapsed_sec))

import atexit
atexit.register(elapsed_sub_test_time)

#
# Time out class:  Read from a stream until time out
#  A little ugly but sending SIGALRM (or any other signal) to Python
#   can be unreliable (will not respond if holding certain locks).
#
class ReadTimeoutException(Exception): pass


def SetNonBlock(stream):
    flags = fcntl.fcntl(stream.fileno(), fcntl.F_GETFL)
    flags |= os.O_NONBLOCK
    fcntl.fcntl(stream.fileno(), fcntl.F_SETFL, flags)

def SuckOutputWithTimeout(stream, timeout):
    SetNonBlock(stream)
    buffer = b''
    end_time = time.time() + timeout
    while True:
        now = time.time()
        if end_time <= now:
            # Maybe return partial result instead?
            raise ReadTimeoutException('Teh tiem iz out!');
        ready_set = select.select([stream], [], [], end_time - now)[0]
        if stream in ready_set:
            bytes = stream.read()
            if len(bytes) == 0:
                break           # EOF
            buffer += bytes     # Inefficient way to accumulate bytes.
            # len(ready_set) == 0 is also an indication of timeout. However,
            # if we relied on that, we would require no data ready in order
            # to timeout  which doesn't seem quite right either.
    return buffer

def LauncherTimeoutArgs(seconds):
    if useLauncherTimeout == 'pbs' or useLauncherTimeout == 'slurm':
        # --walltime=hh:mm:ss
        m, s = divmod(seconds, 60)
        h, m = divmod(m, 60)
        fmttime = '--walltime={0:02d}:{1:02d}:{2:02d}'.format(h, m, s)
        return [fmttime]
    else:
        Fatal('LauncherTimeoutArgs encountered an unknown format spec: ' + \
              useLauncherTimeout)


#
# Auxiliary functions
#

# Escape all special characters
def ShellEscape(arg):
    return re.sub(r'([\\!@#$%^&*()?\'"|<>[\]{} ])', r'\\\1', arg)

# Escape all special characters but leave spaces alone
def ShellEscapeCommand(arg):
    return re.sub(r'([\\!@#$%^&*()?\'"|<>[\]{}])', r'\\\1', arg)


# Grabs the start and end of the output and replaces non-printable chars with ~
def trim_output(output):
    max_size = 256*1024 # ~1/4 MB
    if len(output) > max_size:
        new_output = output[:max_size//2]
        new_output += output[-max_size//2:]
        output = new_output
    return ''.join(s if s in string.printable else "~" for s in output)


# return True if f has .chpl, .test.c or .ml-test.c extension
def hasTestableExtension(f):
    if f.endswith(('.chpl',
                   '.test.c', '.test.cpp',
                   '.ml-test.c', '.ml-test.cpp')):
        return True
    else:
        return False

perflabel = '' # declare it for the following functions

# file suffix: 'keys' -> '.perfkeys' etc.
def PerfSfx(s):
    return '.' + perflabel + s

# directory-wide file: 'COMPOPTS' or 'compopts' -> './PERFCOMPOPTS' etc.
def PerfDirFile(s):
    return './' + perflabel.upper() + s.upper()

# test-specific file: (foo,keys) -> foo.perfkeys etc.
def PerfTFile(test_filename, sfx):
    return test_filename + '.' + perflabel + sfx

def get_chplenv():
    env_cmd = [os.path.join(utildir, 'printchplenv'), '--all', '--simple', '--no-tidy', '--internal']
    chpl_env = py3_compat.Popen(env_cmd, stdout=subprocess.PIPE).communicate()[0]
    chpl_env = dict(map(lambda l: l.split('=', 1), chpl_env.splitlines()))
    return chpl_env

# Similar to os.path.expandvars but stuff chplenv into it too
def expandvars_chpl(path):
    expand_env = os.environ.copy()
    expand_env.update(get_chplenv())
    return string.Template(path).safe_substitute(expand_env)

# Wait up to timeout seconds for files to exist. Useful on systems where file
# IO may only complete after the launcher returns
def WaitForFiles(files, timeout=int(os.getenv('CHPL_TEST_WAIT_FOR_FILES_TIMEOUT', '10'))):
    waited = 0
    for f in files:
        while not os.path.exists(f) and waited < timeout:
            time.sleep(1)
            waited += 1

# Read a file or if the file is executable read its output. If the file is
# executable, the current chplenv is copied into the env before executing.
# Expands shell and chplenv variables and strip out comments/whitespace.
# Returns a list of string, one per line in the file.
def ReadFileWithComments(f, ignoreLeadingSpace=True):
    mylines = ""
    # if the file is executable, run it and grab the output. If we get an
    # OSError while trying to run, report it and try to keep going
    if os.access(f, os.X_OK):
        try:
            # grab the chplenv so it can be stuffed into the subprocess env
            chpl_env = get_chplenv()
            file_env = os.environ.copy()
            file_env.update(chpl_env)

            # execute the file and grab its output
            cmd = py3_compat.Popen([os.path.abspath(f)], stdout=subprocess.PIPE, env=file_env)
            mylines = cmd.communicate()[0].splitlines()

        except OSError as e:
            global localdir
            f_name = os.path.join(localdir, f)
            sys.stdout.write("[Error trying to execute '{0}': {1}]\n".format(f_name, str(e)))

    # otherwise, just read the file
    else:
        with open(f, 'r') as myfile:
            mylines = myfile.readlines()

    mylist=list()
    for line in mylines:
        line = line.rstrip()
        # ignore blank lines
        if not line.strip(): continue
        # ignore comments
        if ignoreLeadingSpace:
            if line.lstrip()[0] == '#': continue
        else:
            if line[0] == '#': continue
        # expand shell variables
        line = expandvars_chpl(line)
        mylist.append(line)
    return mylist

# diff 2 files
def DiffFiles(f1, f2):
    sys.stdout.write('[Executing diff %s %s]\n'%(f1, f2))
    p = py3_compat.Popen(['diff',f1,f2],
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    myoutput = p.communicate()[0] # grab stdout to avoid potential deadlock
    if p.returncode != 0:
        sys.stdout.write(trim_output(myoutput))
    return p.returncode

def DiffBinaryFiles(f1, f2):
    sys.stdout.write('[Executing binary diff %s %s]\n'%(f1, f2))
    p = py3_compat.Popen(['diff', '-a', f1,f2],
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    myoutput = p.communicate()[0] # grab stdout to avoid potential deadlock
    if p.returncode != 0:
        sys.stdout.write('Binary files differed\n')
    return p.returncode

# diff output vs. .bad file, filtering line numbers out of error messages that arise
# in module files.
def DiffBadFiles(f1, f2):
    sys.stdout.write('[Executing diff-ignoring-module-line-numbers %s %s]\n'%(f1, f2))
    p = py3_compat.Popen([utildir+'/test/diff-ignoring-module-line-numbers', f1, f2],
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    myoutput = p.communicate()[0] # grab stdout to avoid potential deadlock
    if p.returncode != 0:
        sys.stdout.write(myoutput)
    return p.returncode

# kill process
def KillProc(p, timeout):
    k = py3_compat.Popen(['kill',str(p.pid)])
    k.wait()
    now = time.time()
    end_time = now + timeout # give it a little time
    while end_time > now:
        if p.poll():
            return
        now = time.time()
    # use the big hammer (and don't bother waiting)
    py3_compat.Popen(['kill','-9', str(p.pid)])
    return

# clean up after the test has been built
def cleanup(execname, test_ran_and_more_compopts=False):
    try:
        if execname is not None:
            if os.path.isfile(execname):
                os.unlink(execname)
            if os.path.isfile(execname+'_real'):
                os.unlink(execname+'_real')
            # Hopefully short term workaround on cygwin where we've been seeing
            # an issue where after a test is run, some other process has a
            # handle on the executable and we can't create a new executable
            # with the same name for a bit. If a test ran and we have
            # additional compopts (which means the exec name will be reused)
            # wait a second while cleaning up the executable to give the other
            # process time to release its handle.
            if test_ran_and_more_compopts and 'cygwin' in platform:
                time.sleep(1)
    except (IOError, OSError) as ex:
        # If the error is "Device or resource busy", call lsof on the file (or
        # handle for windows) to see what is holding the file handle, to help
        # debug the issue.
        if isinstance(ex, OSError) and ex.errno == 16:
            handle = which('handle')
            lsof = which('lsof')
            if handle is not None:
                sys.stdout.write('[Inspecting open file handles with: {0}\n'.format(handle))
                py3_compat.Popen([handle]).communicate()
            elif lsof is not None:
                cmd = [lsof, execname]
                sys.stdout.write('[Inspecting open file handles with: {0}\n'.format(' '.join(cmd)))
                py3_compat.Popen(cmd).communicate()

        # Do not print the warning for cygwin32 when errno is 16 (Device or resource busy).
        if not (getattr(ex, 'errno', 0) == 16 and platform == 'cygwin32'):
            sys.stdout.write('[Warning: could not remove {0}: {1}]\n'.format(execname, ex))

def which(program):
    """Returns absolute path to program, if it exists in $PATH. If not found,
    returns None.

    From: http://stackoverflow.com/a/377028
    """
    def is_exe(fpath):
        return os.path.isfile(fpath) and os.access(fpath, os.X_OK)

    fpath, fname = os.path.split(program)
    if fpath:
        if is_exe(program):
            return program
    else:
        for path in os.environ.get('PATH', '').split(os.pathsep):
            path = path.strip('"')
            exe_file = os.path.join(path, program)
            if is_exe(exe_file):
                return exe_file
    return None


# print (compopts: XX, execopts: XX) for later decoding of failed tests
def printTestVariation(compoptsnum, compoptslist,
                       execoptsnum=0, execoptslist=[] ):
    printCompOpts = True
    printExecOpts = True
    if compoptsnum==0 or len(compoptslist) <= 1:
        printCompOpts = False
    if execoptsnum==0 or len(execoptslist) <= 1:
        printExecOpts = False

    if (not printCompOpts) and (not printExecOpts):
        return;

    sys.stdout.write(' (')
    if printCompOpts:
        sys.stdout.write('compopts: %d'%(compoptsnum))
    if printExecOpts:
        if printCompOpts:
            sys.stdout.write(', ')
        sys.stdout.write('execopts: %d'%(execoptsnum))
    sys.stdout.write(')')
    return

# print '[Elapsed time to compile and execute all versions of ...'
#   in the format expected by convert_start_test_log_to_junit_xml.py.

def printEndOfTestMsg(test_name, elapsedTime):
    sys.stdout.flush()
    print('[Elapsed time to compile and execute all versions of "{0}" - '
        '{1:.3f} seconds]'.format(test_name, elapsedTime))


# return true if string is an integer
def IsInteger(str):
    try:
        int(str)
        return True
    except ValueError:
        return False

# read integer value from a file
def ReadIntegerValue(f, localdir):
    to = ReadFileWithComments(f)
    if to:
        for l in to:
            if l[0] == '#':
                continue
            if IsInteger(l):
                return int(l)
            else:
                break
    Fatal('Invalid integer value in '+f+' ('+localdir+')')

# report an error message and exit
def Fatal(message):
    sys.stdout.write('[Error (sub_test): '+message+']\n')
    magic_exit_code = reduce(operator.add, map(ord, 'CHAPEL')) % 256
    sys.exit(magic_exit_code)

# Attempts to find an appropriate timer to use. The timer must be in
# util/test/timers/. Expects to be passed a file containing only the name of
# the timer script. If the file is improperly formatted the default timer is
# used, and if the timer is not executable or can't be found 'time -p' is used
def GetTimer(f):
    timersdir = os.path.join(utildir, 'test', 'timers')
    defaultTimer = os.path.join(timersdir, 'defaultTimer')

    lines = ReadFileWithComments(f)
    if len(lines) != 1:
        sys.stdout.write('[Error "%s" must contain exactly one non-comment line '
            'with the name of the timer located in %s to use. Using default ' 
            'timer %s.]\n' %(f, timersdir, defaultTimer))
        timer = defaultTimer
    else:
        timer = os.path.join(timersdir, lines[0])

    if not os.access(timer,os.R_OK|os.X_OK):
        sys.stdout.write('[Error cannot execute timer "%s", using "time -p"]\n' %(timer))
        return 'time -p'

    return timer

# attempts to find an appropriate .good file. Good files are expected to be of
# the form basename.<configuration>.<commExecNums>.good. Where configuration
# options are one of the below configuration specific parameters that are
# checked for. E.G the current comm layer. commExecNums are the optional
# compopt and execopt number to enable different .good files for different
# compopts/execopts with explicitly specifying name. 
def FindGoodFile(basename, commExecNums=['']):
    
    goodfile = ''
    for commExecNum in commExecNums:
        # Try the machine specific .good
        if not os.path.isfile(goodfile):
            goodfile = basename+'.'+machine+commExecNum+'.good'
        # Else if --no-local try the no-local .good file.
        if not os.path.isfile(goodfile):
            if '--no-local' in envCompopts:
                goodfile=basename+'.no-local'+commExecNum+'.good'
        # Else try comm-, networkAtomics-, and localeModel-specific .good
        # files.  All 3, any 2, or just 1 of these may be used, but if more
        # then 1 they must be in the above order.
        # Also tries tasks- by itself. If this grows any larger, we should probably
        # list all files with basename prefix and parse them out to avoid the combination
        # blowup and large number of stat calls
        if not os.path.isfile(goodfile):
            for specstr in [ chplcommstr+chplnastr+chpllmstr,
                             chplcommstr+chplnastr,
                             chplcommstr+chpllmstr,
                             chplcommstr+chpltasksstr,
                             chplnastr+chpllmstr,
                             chplcommstr,
                             chpltasksstr,
                             chplnastr,
                             chpllmstr ]:
                goodfile=basename+specstr+commExecNum+'.good'
                if os.path.isfile(goodfile):
                    break
        # Else try the platform-specific .good file.
        if not os.path.isfile(goodfile):
            goodfile=basename+'.'+platform+commExecNum+'.good'
        # Else use the execopts-specific .good file.
        if not os.path.isfile(goodfile):
            goodfile=basename+commExecNum+'.good'

    return goodfile

def get_exec_log_name(execname, comp_opts_count=None, exec_opts_count=None):
    """Returns the execution output log name based on number of comp and exec opts."""
    suffix = '.exec.out.tmp'
    if comp_opts_count is None and exec_opts_count is None:
        return '{0}{1}'.format(execname, suffix)
    else:
        return '{0}.{1}-{2}{3}'.format(execname, comp_opts_count, exec_opts_count, suffix)

# Use testEnv to process skipif files, it works for executable and
# non-executable versions
def runSkipIf(skipifName):
    p = py3_compat.Popen([utildir+'/test/testEnv', './'+skipifName], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    (stdout, stderr) = p.communicate()
    status = p.returncode

    stderr = stderr.strip()
    errmsg = ""
    if stderr:
        errmsg = stderr
    if status:
        if stderr:
            errmsg += '\n'
        errmsg += 'exit status %d' % (status)
    if stderr or status:
        raise RuntimeError(errmsg)

    return stdout

# Translate some known failures into more easily understood forms
def translateOutput(output_in):
    xlates = (('slurmstepd: Munge decode failed: Expired credential',
               'Jira 18 -- Expired slurm credential for'),
              ('output file from job .* does not exist',
               'private issue #906 -- Missing output file for'),
              ('aprun: Unexpected close of the apsys control connection',
               'Jira 193 -- Unexpected close of apsys for'),
              ('qsub: cannot connect to server sdb',
               'Jira 283 -- Sporadic: qstat failed to connect to server sdb'),
              ('Failed to recv data from background qsub',
               'Jira 260 -- Failed to recv data from background qsub for'),
              (r'\d+ Killed /var/spool/PBS/mom_priv/jobs',
               'Jira 324 -- PBS job killed for'),
              ('Fatal MPP reservation error on confirm',
               'Jira 329 -- Fatal MPP reservation error for'),
              ('Fatal MPP reservation error on create',
               'Jira 329 -- Fatal MPP reservation error for'),
              ('Text file busy',
               'private issue #474 -- Text file busy for'),
              ('Socket timed out on send/recv operation',
               'private issue #579 -- Slurm socket timed out on send/recv'))

    known_error = ''
    was_timeout = False

    output = output_in
    if isinstance(output, bytes):
        output = str(output, encoding='utf-8', errors='surrogateescape')

    for x in xlates:
        if re.search(x[0], output, re.IGNORECASE) != None:
            known_error = x[1]
            break
    else:
        if (re.search('PBS: job killed: walltime', output, re.IGNORECASE) != None or
              re.search('slurm.* CANCELLED .* DUE TO TIME LIMIT', output, re.IGNORECASE) != None):
            known_error = 'Timed out executing program'
            was_timeout = True

    return known_error, was_timeout

# Remove innocuous warnings about clock skew from when we `make` the generated code
def remove_clock_skew_warning(output):
    output = re.sub(r'g?make.*: Warning: File .* has modification time .* s in the future *\n', '', output)
    output = re.sub(r'g?make.*: warning:  Clock skew detected\.  Your build may be incomplete\. *\n', '', output)
    return output

def filter_compiler_errors(compiler_output):
    """Identify common errors that occur in compilation.
    Return message to emit when error found."""

    error_msg = ''
    err_strings = ['could not checkout FLEXlm license']
    for s in err_strings:
        if re.search(s, compiler_output, re.IGNORECASE) != None:
            error_msg = '(private issue #398) '
            break

    return error_msg

def filter_errors(output_in, pre_exec_output, execgoodfile, execlog):
    """Identify common errors that occur in runtime or compiler warnings.
    Return message to emit when error found."""

    # If the output contains messages consistent with JIRA 154, add a
    # hint to the error summary and force a text base diff since
    # sometimes the output contains a null character
    extra_msg = ''
    err_strings = ['got exn while reading exit code: connection closed',
                   'slave got an unknown command on coord socket:',
                   'Slave got an xSocket: connection closed on recv',
                   'recursive failure in AMUDP_SPMDShutdown',
                   'AM_ERR_RESOURCE \(Problem with requested resource\)']

    output = output_in
    if isinstance(output, bytes):
        output = str(output, encoding='utf-8', errors='surrogateescape')

    for s in err_strings:
        if re.search(s, output, re.IGNORECASE) != None:
            extra_msg = '(possible JIRA 154) '
            DiffBinaryFiles(execgoodfile, execlog)
            break

    err_strings = ['Master got an xSocket: error in sendAll']
    for s in err_strings:
        if re.search(s, output, re.IGNORECASE) != None:
            extra_msg = '(possible JIRA 274) '
            break

    err_strings = ['Clock skew detected']
    for s in err_strings:
        # NOTE: checking pre_exec (compiler) output
        if re.search(s, pre_exec_output, re.IGNORECASE) != None:
            extra_msg = '(private issue #482) '
            break

    err_strings = ['could not checkout FLEXlm license']
    for s in err_strings:
        if (re.search(s, output, re.IGNORECASE) != None or
            re.search(s, pre_exec_output, re.IGNORECASE) != None):
            extra_msg = '(private issue #398) '
            break

    err_strings = ['=* Memory Leaks =*']
    for s in err_strings:
        if (re.search(s, output, re.IGNORECASE) != None):
            extra_msg = '(memory leak) '
            break

    # detect cases of 'GASNet timer calibration on %s detected non-linear
    # timer behavior:' messages which we can't do much about
    err_strings = ['GASNet timer calibration on']
    for s in err_strings:
        if (re.search(s, output, re.IGNORECASE) != None):
            extra_msg = '(private issue #480) '
            break

    return extra_msg


# Start of sub_test proper
#

if len(sys.argv)!=2:
    print('usage: sub_test COMPILER')
    sys.exit(0)

# Find the base installation
compiler=sys.argv[1]
if not os.access(compiler,os.R_OK|os.X_OK):
    Fatal('Cannot execute compiler \''+compiler+'\'')

is_chpldoc = compiler.endswith('chpldoc')

path_to_compiler=os.path.abspath(os.path.dirname(compiler))
# Assume chpl binary is 2 directory levels down in the base installation
(chpl_base, tmp) = os.path.split(path_to_compiler)
(chpl_base, tmp) = os.path.split(chpl_base)
chpl_base=os.path.normpath(chpl_base)
# sys.stdout.write('CHPL_BASE='+chpl_base+'\n')

# If $CHPL_HOME is not set, use the base installation of the compiler
chpl_home=os.getenv('CHPL_HOME', chpl_base);
chpl_home=os.path.normpath(chpl_home)
# sys.stdout.write('CHPL_HOME='+chpl_home+'\n');

# Find the test util directory -- set this in start_test to permit
# a version of start_test other than the one in CHPL_HOME to be used
utildir=os.getenv('CHPL_TEST_UTIL_DIR');
if utildir is None or not os.path.isdir(utildir):
    Fatal('Cannot find test util directory {0}'.format(utildir))

# Needed for MacOS mount points
utildir = os.path.realpath(utildir)
# sys.stdout.write('utildir='+utildir+'\n');

# Find the c compiler
# We open the compileline inside of CHPL_HOME rather than CHPL_TEST_UTIL_DIR on
# purpose. compileline will not work correctly in some configurations when run
# outside of its directory tree.
compileline = os.path.join(chpl_home, 'util', 'config', 'compileline')
p = py3_compat.Popen([compileline, '--compile'],
                     stdout=subprocess.PIPE, stderr=subprocess.PIPE)
c_compiler = p.communicate()[0].rstrip()
if p.returncode != 0:
  Fatal('Cannot find c compiler')
p = py3_compat.Popen([compileline, '--compile-c++'],
                     stdout=subprocess.PIPE, stderr=subprocess.PIPE)
cpp_compiler = p.communicate()[0].rstrip()
if p.returncode != 0:
  Fatal('Cannot find c++ compiler')

p = py3_compat.Popen([compileline, '--host-c-compiler'],
                     stdout=subprocess.PIPE, stderr=subprocess.PIPE)
host_c_compiler = p.communicate()[0].rstrip()
if p.returncode != 0:
  Fatal('Cannot find host c compiler')

p = py3_compat.Popen([compileline, '--host-cxx-compiler'],
                     stdout=subprocess.PIPE, stderr=subprocess.PIPE)
host_cpp_compiler = p.communicate()[0].rstrip()
if p.returncode != 0:
  Fatal('Cannot find host c++ compiler')

# Get runtime includes and defines
p = py3_compat.Popen([compileline, '--includes-and-defines'],
                     stdout=subprocess.PIPE, stderr=subprocess.PIPE)
runtime_includes_and_defines= p.communicate()[0].rstrip()
if p.returncode != 0:
  Fatal('Cannot find runtime includes and defines')

# Find the test directory
testdir=chpl_home+'/test'
if os.path.isdir(testdir)==0:
    testdir=chpl_home+'/examples'
    if os.path.isdir(testdir)==0:
        Fatal('Cannot find test directory '+chpl_home+'/test or '+testdir)
# Needed for MacOS mount points
testdir = os.path.realpath(testdir)
# sys.stdout.write('testdir='+testdir+'\n');

# If user specified a different test directory (e.g. with --test-root flag on
# start_test), use it instead.
test_root_dir = os.environ.get('CHPL_TEST_ROOT_DIR')
if test_root_dir is not None:
    testdir = test_root_dir

# Use timedexec
# As much as I hate calling out to another script for the time out stuff,
#  subprocess doesn't quite cut it for this kind of stuff
useTimedExec=True
if useTimedExec:
    timedexec=utildir+'/test/timedexec'
    if not os.access(timedexec,os.R_OK|os.X_OK):
        Fatal('Cannot execute timedexec script \''+timedexec+'\'')
# sys.stdout.write('timedexec='+timedexec+'\n');

# HW platform
platform=py3_compat.Popen([utildir+'/chplenv/chpl_platform.py', '--target'], stdout=subprocess.PIPE).communicate()[0]
platform = platform.strip()
# sys.stdout.write('platform='+platform+'\n')

# Machine name we are running on
machine=os.uname()[1].split('.', 1)[0]
# sys.stdout.write('machine='+machine+'\n')

# Get the system-wide preexec(s)
systemPreexecs = os.getenv('CHPL_SYSTEM_PREEXEC')
if systemPreexecs:
    systemPreexecs = systemPreexecs.strip().split(',')
    for spreexec in systemPreexecs:
        if not os.access(spreexec, os.R_OK|os.X_OK):
            Fatal('Cannot execute system-wide preexec \''+spreexec+'\'')

# Get the system-wide prediff(s)
systemPrediffs = os.getenv('CHPL_SYSTEM_PREDIFF')
if systemPrediffs:
    systemPrediffs = systemPrediffs.strip().split(',')
    for sprediff in systemPrediffs:
        if not os.access(sprediff,os.R_OK|os.X_OK):
            Fatal('Cannot execute system-wide prediff \''+sprediff+'\'')

# Use the launcher walltime option for timeout
useLauncherTimeout = os.getenv('CHPL_LAUNCHER_TIMEOUT')

uniquifyTests = False
if os.getenv('CHPL_TEST_UNIQUIFY_EXE') != None:
    uniquifyTests = True

# Get the current directory (normalize for MacOS case-sort-of-sensitivity)
localdir = os.path.normpath(os.getcwd()).replace(testdir, '.')
# sys.stdout.write('localdir=%s\n'%(localdir))

if localdir.find('./') == 0:
    # strip off the leading './'
    localdir = localdir.lstrip('.')
    localdir = localdir.lstrip('/')
# sys.stdout.write('localdir=%s\n'%(localdir))

# CHPL_COMM
chplcomm=os.getenv('CHPL_COMM','none').strip()
chplcommstr='.comm-'+chplcomm
# sys.stdout.write('chplcomm=%s\n'%(chplcomm))

# CHPL_NETWORK_ATOMICS
chplna=os.getenv('CHPL_NETWORK_ATOMICS','none').strip()
chplnastr='.na-'+chplna
#sys.stdout.write('chplna=%s\n'%(chplna))

# CHPL_LAUNCHER
chpllauncher=os.getenv('CHPL_LAUNCHER','none').strip()

# CHPL_LOCALE_MODEL
chpllm=os.getenv('CHPL_LOCALE_MODEL','flat').strip()
chpllmstr='.lm-'+chpllm
#sys.stdout.write('lm=%s\n'%(chpllm))

# CHPL_TASKS
chpltasks=os.getenv('CHPL_TASKS', 'none').strip()
chpltasksstr='.tasks-'+chpltasks

#
# Test options for all tests in this directory
#

if os.getenv('CHPL_TEST_PERF')!=None:
    perftest=True
    perflabel=os.getenv('CHPL_TEST_PERF_LABEL')
    perfdir=os.getenv('CHPL_TEST_PERF_DIR')
    perfdescription = os.getenv('CHPL_TEST_PERF_DESCRIPTION')
    if perfdescription != None:
        sys.stdout.write('Setting perfdir to %s from %s because of additional perf description\n' %(os.path.join(perfdir, perfdescription), perfdir))
        perfdir = os.path.join(perfdir, perfdescription)
    else:
        perfdescription= ''
    if perflabel==None or perfdir==None:
        Fatal('$CHPL_TEST_PERF_DIR and $CHPL_TEST_PERF_LABEL must be set for performance testing')
else:
    perftest=False
    perflabel=''

compoptssuffix = PerfSfx('compopts')  # .compopts or .perfcompopts or ...

# If compiler is chpldoc, use .chpldocopts for options.
chpldocsuffix = '.chpldocopts'

compenvsuffix  = PerfSfx('compenv')   # compenv  or .perfcompenv  or ...
execenvsuffix  = PerfSfx('execenv')   # .execenv  or .perfexecenv  or ...
execoptssuffix = PerfSfx('execopts')  # .execopts or .perfexecopts or ...
timeoutsuffix  = PerfSfx('timeout')   # .timeout  or .perftimeout  or ...
# sys.stdout.write('perftest=%d perflabel=%s\n'%(perftest,perflabel))

# Get global timeout
if os.getenv('CHPL_TEST_VGRND_COMP')=='on' or os.getenv('CHPL_TEST_VGRND_EXE')=='on':
    defaultTimeout=1000
else:
    defaultTimeout=300
globalTimeout = int(os.getenv('CHPL_TEST_TIMEOUT', defaultTimeout))

# get a threshold for which to report long running tests
if os.getenv("CHPL_TEST_EXEC_TIME_WARN_LIMIT"):
    execTimeWarnLimit = int(os.getenv('CHPL_TEST_EXEC_TIME_WARN_LIMIT', '0'))
else:
    execTimeWarnLimit = 0

# get a threshold for which to skip any remaining trials
if os.getenv("CHPL_TEST_EXEC_TIME_LIMIT_NUM_TRIALS"):
    execTimeSkipTrials = int(os.getenv('CHPL_TEST_EXEC_TIME_LIMIT_NUM_TRIALS', '0'))
else:
    execTimeSkipTrials = 0

# directory level timeout
directoryTimeout = globalTimeout
if os.access('./TIMEOUT',os.R_OK):
    fileTimeout = ReadIntegerValue('./TIMEOUT', localdir)
    if fileTimeout < defaultTimeout or fileTimeout > globalTimeout:
        directoryTimeout = fileTimeout
# sys.stdout.write('globalTimeout=%d\n'%(globalTimeout))

# Check for global PERFTIMEEXEC option
timerFile = PerfDirFile('TIMEEXEC') # e.g. ./PERFTIMEEXEC
if os.access(timerFile, os.R_OK):
    globalTimer = GetTimer(timerFile)
else:
    globalTimer = None

# Get global timeout for kill
if os.access('./KILLTIMEOUT',os.R_OK):
    globalKillTimeout = ReadIntegerValue('./KILLTIMEOUT', localdir)
else:
    globalKillTimeout=10
# sys.stdout.write('globalKillTimeout=%d\n'%(globalKillTimeout))

if os.access('./NOEXEC',os.R_OK):
    execute=False
else:
    execute=True
# sys.stdout.write('execute=%d\n'%(execute))

if os.access('./NOVGRBIN',os.R_OK):
    vgrbin=False
else:
    vgrbin=True
# sys.stdout.write('vgrbin=%d\n'%(vgrbin))

if os.access('./COMPSTDIN',os.R_OK):
    compstdin='./COMPSTDIN'
else:
    compstdin='/dev/null'
# sys.stdout.write('compstdin=%s\n'%(compstdin))

globalLastcompopts=list();
if os.access('./LASTCOMPOPTS',os.R_OK):
    globalLastcompopts+=py3_compat.Popen(['cat', './LASTCOMPOPTS'], stdout=subprocess.PIPE).communicate()[0].strip().split()
# sys.stdout.write('globalLastcompopts=%s\n'%(globalLastcompopts))

globalLastexecopts=list();
if os.access('./LASTEXECOPTS',os.R_OK):
    globalLastexecopts+=py3_compat.Popen(['cat', './LASTEXECOPTS'], stdout=subprocess.PIPE).communicate()[0].strip().split()
# sys.stdout.write('globalLastexecopts=%s\n'%(globalLastexecopts))

if os.access(PerfDirFile('NUMLOCALES'),os.R_OK):
    globalNumlocales=ReadIntegerValue(PerfDirFile('NUMLOCALES'), localdir)
    # globalNumlocales.strip(globalNumlocales)
else:
    # start_test sets this, so we'll assume it's right :)
    globalNumlocales=int(os.getenv('NUMLOCALES', '0'))
# sys.stdout.write('globalNumlocales=%s\n'%(globalNumlocales))

maxLocalesAvailable = os.getenv('CHPL_TEST_MAX_LOCALES')
if maxLocalesAvailable == None:
    maxLocalesAvailable = os.getenv('CHPL_TEST_NUM_LOCALES_AVAILABLE')
if maxLocalesAvailable is not None:
    maxLocalesAvailable = int(maxLocalesAvailable)

if os.access('./CATFILES',os.R_OK):
    globalCatfiles=py3_compat.Popen(['cat', './CATFILES'], stdout=subprocess.PIPE).communicate()[0]
    globalCatfiles.strip(globalCatfiles)
else:
    globalCatfiles=None
# sys.stdout.write('globalCatfiles=%s\n'%(globalCatfiles))


#
# valgrind stuff
#
chpl_valgrind_opts=os.getenv('CHPL_VALGRIND_OPTS', '--tool=memcheck')
# sys.stdout.write('chpl_valgrind_opts=%s\n'%(chpl_valgrind_opts))

if os.getenv('CHPL_TEST_VGRND_COMP')=='on':
    valgrindcomp = 'valgrind'
    valgrindcompopts=chpl_valgrind_opts.split()
    valgrindcompopts+=['--gen-suppressions=all']
    valgrindcompopts+=['--suppressions=%s/compiler/etc/valgrind.suppressions'%(chpl_home)]
    valgrindcompopts+=['-q']
else:
    valgrindcomp = None
    valgrindcompopts = None
# sys.stdout.write('valgrindcomp=%s %s\n'%(valgrindcomp, valgrindcompopts))

if (os.getenv('CHPL_TEST_VGRND_EXE')=='on' and vgrbin):
    valgrindbin = 'valgrind'
    valgrindbinopts = chpl_valgrind_opts.split()+['-q']
    if (chplcomm!='none'):
        valgrindbinopts+=['--trace-children=yes']
        if (chplcomm=='gasnet'):
            valgrindbinopts+=['--suppressions=%s/third-party/gasnet/gasnet-src/other/valgrind/gasnet.supp'%(chpl_home)]
else:
    valgrindbin = None
    valgrindbinopts = None
# sys.stdout.write('valgrindbin=%s %s\n'%(valgrindbin, valgrindbinopts))


#
# Misc set up
#

testfutures=int(os.getenv('CHPL_TEST_FUTURES','0'))
# sys.stdout.write('testfutures=%s\n'%(testfutures))

testnotests=os.getenv('CHPL_TEST_NOTESTS')
# sys.stdout.write('testnotests=%s\n'%(testnotests))

launchcmd=os.getenv('LAUNCHCMD')
# sys.stdout.write('launchcmd=%s\n'%(launchcmd))

if os.getenv('CHPL_TEST_INTERP')=='on':
    execute=False
    futureSuffix='.ifuture'
else:
    futureSuffix='.future'
# sys.stdout.write('futureSuffix=%s\n'%(futureSuffix))

printpassesfile = None
if os.getenv('CHPL_TEST_COMP_PERF')!=None:
    compperftest=True

    # check for the main compiler performance directory
    if os.getenv('CHPL_TEST_COMP_PERF_DIR')!=None:
        compperfdir=os.getenv('CHPL_TEST_COMP_PERF_DIR')
    else:
        compperfdir=chpl_home+'/test/compperfdat/' 

    # The env var CHPL_PRINT_PASSES_FILE will cause the 
    # compiler to save the pass timings to specified file.
    if os.getenv('CHPL_PRINT_PASSES_FILE')!=None:
        printpassesfile=os.getenv('CHPL_PRINT_PASSES_FILE')
    else:
        printpassesfile='timing.txt'
        os.environ['CHPL_PRINT_PASSES_FILE'] = 'timing.txt'

    # check for the perfkeys file
    if os.getenv('CHPL_TEST_COMP_PERF_KEYS')!=None:
        keyfile=os.getenv('CHPL_TEST_COMP_PERF_KEYS')
    else:
        keyfile=chpl_home+'/test/performance/compiler/compilerPerformance.perfkeys'

    # Check for the directory to store the temporary .dat files that will get
    # combined into one.
    if os.getenv('CHPL_TEST_COMP_PERF_TEMP_DAT_DIR')!=None:
        tempDatFilesDir = os.getenv('CHPL_TEST_COMP_PERF_TEMP_DAT_DIR')
    else:
        tempDatFilesDir = compperfdir + 'tempCompPerfDatFiles/'

else:
    compperftest=False

#
# Global COMPOPTS/PERFCOMPOPTS:
#
#   Prefer PERFCOMPOPTS if doing performance testing; otherwise, use
#   COMPOPTS.  Note that COMPOPTS is used for performance testing
#   currently in the absence of a PERFCOMPOPTS file.  Not sure whether
#   or not this is a good idea, but preserving it for now for backwards
#   compatibility.
#

directoryCompopts = list(' ')
if (perftest and os.access(PerfDirFile('COMPOPTS'),os.R_OK)): # ./PERFCOMPOPTS
    directoryCompopts=ReadFileWithComments(PerfDirFile('COMPOPTS'))
elif os.access('./COMPOPTS',os.R_OK):
    directoryCompopts=ReadFileWithComments('./COMPOPTS')

envCompopts = os.getenv('COMPOPTS')
if envCompopts is not None:
    envCompopts = shlex.split(envCompopts)
else:
  envCompopts = []

# Global CHPLDOCOPTS
if os.access('./CHPLDOCOPTS', os.R_OK):
    dirChpldocOpts = shlex.split(ReadFileWithComments('./CHPLDOCOPTS')[0])
else:
    dirChpldocOpts = []

# Env CHPLDOCOPTS
envChpldocOpts = os.getenv('CHPLDOCOPTS')
if envChpldocOpts is not None:
    envChpldocOpts = shlex.split(envChpldocOpts)
else:
    envChpldocOpts = []

# Global chpldoc options.
globalChpldocOpts = dirChpldocOpts + envChpldocOpts

#
# Global PERFNUMTRIALS
#
if os.access(PerfDirFile('NUMTRIALS'), os.R_OK): # ./PERFNUMTRIALS
    globalNumTrials = ReadIntegerValue(PerfDirFile('NUMTRIALS'), localdir)
else:
    globalNumTrials=int(os.getenv('CHPL_TEST_NUM_TRIALS', '1'))

#
# Global EXECENV
#
if os.access('./EXECENV',os.R_OK):
    globalExecenv=ReadFileWithComments('./EXECENV')
else:
    globalExecenv=list()

#
# Global COMPENV
#
if os.access('./COMPENV',os.R_OK):
    globalCompenv=ReadFileWithComments('./COMPENV')
else:
    globalCompenv=list()

#
# Global EXECOPTS/PERFEXECOPTS
#
#
#   Prefer PERFEXECOPTS if doing performance testing; otherwise, use
#   EXECOPTS.  Note that EXECOPTS is used for performance testing
#   currently in the absence of a PERFEXECOPTS file.  Not sure whether
#   or not this is a good idea, but preserving it for now for backwards
#   compatibility.
#
if (perftest and os.access(PerfDirFile('EXECOPTS'),os.R_OK)): # ./PERFEXECOPTS
    tgeo=ReadFileWithComments(PerfDirFile('EXECOPTS'))
    globalExecopts= shlex.split(tgeo[0])
elif os.access('./EXECOPTS',os.R_OK):
    tgeo=ReadFileWithComments('./EXECOPTS')
    globalExecopts= shlex.split(tgeo[0])
else:
    globalExecopts=list()
envExecopts = os.getenv('EXECOPTS')
# sys.stdout.write('globalExecopts=%s\n'%(globalExecopts))

#
# Global PRECOMP, PREDIFF & PREEXEC
#
if os.access('./PRECOMP', os.R_OK|os.X_OK):
    globalPrecomp='./PRECOMP'
else:
    globalPrecomp=None
#
if os.access('./PREDIFF',os.R_OK|os.X_OK):
    globalPrediff='./PREDIFF'
else:
    globalPrediff=None
# sys.stdout.write('globalPrediff=%s\n'%(globalPrediff))
if os.access('./PREEXEC',os.R_OK|os.X_OK):
    globalPreexec='./PREEXEC'
else:
    globalPreexec=None
#
# Start running tests
#
sys.stdout.write('[Starting subtest - %s]\n'%(time.strftime('%a %b %d %H:%M:%S %Z %Y', time.localtime())))
#sys.stdout.write('[compiler: \'%s\']\n'%(compiler))
if systemPreexecs:
    sys.stdout.write('[system-wide preexec(s): \'%s\']\n'%(', '.join(systemPreexecs)))
if systemPrediffs:
    sys.stdout.write('[system-wide prediff(s): \'%s\']\n'%(', '.join(systemPrediffs)))

# consistently look only at the files in the current directory
dirlist=os.listdir(".")
dirlist.sort()

onetestsrc = os.getenv('CHPL_ONETEST')
if onetestsrc==None:
    testsrc=filter(hasTestableExtension, dirlist)
else:
    testsrc=list()
    testsrc.append(onetestsrc)

original_compiler = compiler

for testname in testsrc:
    sys.stdout.flush()

    compiler = original_compiler

    # print testname
    sys.stdout.write('[test: %s/%s]\n'%(localdir,testname))
    test_filename = re.match(r'^(.*)\.(?:chpl|test\.c|test\.cpp|ml-test\.c|ml-test\.cpp)$', testname).group(1)
    execname = test_filename
    if uniquifyTests:
        execname += '.{0}'.format(os.getpid())
    # print test_filename

    is_c_test = testname.endswith(".test.c");
    is_ml_c_test = testname.endswith(".ml-test.c");
    is_cpp_test = testname.endswith(".test.cpp");
    is_ml_cpp_test = testname.endswith(".ml-test.cpp");
    is_c_or_cpp_test = (is_c_test or is_cpp_test)
    is_ml_c_or_cpp_test = (is_ml_c_test or is_ml_cpp_test)
    c_or_cpp = None
    if is_c_test or is_ml_c_test:
        c_or_cpp = "c"
    if is_cpp_test or is_ml_cpp_test:
        c_or_cpp = "c++"

    # If the test name ends with .doc.chpl or the compiler was set to chpldoc
    # (i.e. is_chpldoc=True), run this test with chpldoc options.
    if testname.endswith('.doc.chpl') or is_chpldoc:
        test_is_chpldoc = True
    else:
        test_is_chpldoc = False

    # Test specific settings
    catfiles = globalCatfiles
    numlocales = globalNumlocales
    lastcompopts = list()
    if globalLastcompopts:
        lastcompopts += globalLastcompopts
    # sys.stdout.write("lastcompopts=%s\n"%(lastcompopts))
    lastexecopts = list()
    if globalLastexecopts:
        lastexecopts += globalLastexecopts
    # sys.stdout.write("lastexecopts=%s\n"%(lastexecopts))

    # Get the list of files starting with 'test_filename.'
    test_filename_files = fnmatch.filter(dirlist, test_filename+'.*')
    # print test_filename_files, dirlist

    if (perftest and (test_filename_files.count(PerfTFile(test_filename,'keys'))==0) and
        (test_filename_files.count(PerfTFile(test_filename,'execopts'))==0) and
        (test_filename_files.count(PerfTFile(test_filename,'compopts'))==0)):
        sys.stdout.write('[Skipping noperf test: %s/%s]\n'%(localdir,test_filename))
        continue # on to next test

    timeout = directoryTimeout
    killtimeout = globalKillTimeout
    numTrials = globalNumTrials
    if (perftest):
        timer = globalTimer
    else:
        timer = None
    futuretest=''

    if test_is_chpldoc:
        executebin = False
    else:
        executebin = execute

    testfuturesfile=False
    testskipiffile=False
    noexecfile=False
    execoptsfile=False
    precomp=None
    prediff=None
    preexec=None

    if os.getenv('CHPL_NO_STDIN_REDIRECT') == None:
        redirectin = '/dev/null'
    else:
        redirectin = None

    # If there is a .skipif file, put it at front of list.
    skipif_i = -1
    for i, test_filename_file in enumerate(test_filename_files):
        if test_filename_file.endswith('.skipif'):
            skipif_i = i
            break
    if skipif_i > 0:
        test_filename_files.insert(0, test_filename_files.pop(skipif_i))

    # Deal with these files
    do_not_test=False
    for f in test_filename_files:
        (root, suffix) = os.path.splitext(f)
        # sys.stdout.write("**** %s ****\n"%(f))

        # 'f' is of the form test_filename.SOMETHING.suffix,
        # not pertinent at the moment
        if root != test_filename:
            continue

        # Deal with these later
        if (suffix == '.good' or
            suffix=='.compopts' or suffix=='.perfcompopts' or
            suffix=='.chpldocopts' or
            suffix=='.execenv' or suffix=='.perfexecenv' or
            suffix=='.compenv' or suffix=='.perfcompenv' or
            suffix=='.execopts' or suffix=='.perfexecopts'):
            continue # on to next file

        elif (suffix=='.notest' and (os.access(f, os.R_OK) and
                                     testnotests=='0')):
            sys.stdout.write('[Skipping notest test: %s/%s]\n'%(localdir,test_filename))
            do_not_test=True
            break

        elif (suffix=='.skipif' and (os.access(f, os.R_OK) and
               (os.getenv('CHPL_TEST_SINGLES')=='0'))):
            testskipiffile=True
            try:
                skipme=False
                skiptest=runSkipIf(f)
                if skiptest.strip() != "False":
                    skipme = skiptest.strip() == "True" or int(skiptest) == 1
                if skipme:
                    sys.stdout.write('[Skipping test based on .skipif environment settings: %s]\n'
                        % os.path.join(localdir, test_filename))
                    do_not_test=True
            except (ValueError, RuntimeError) as e:
                sys.stdout.write(str(e)+'\n')
                sys.stdout.write('[Error processing .skipif file for %s]\n'
                    % os.path.join(localdir, test_filename))
                printEndOfTestMsg(os.path.join(localdir, test_filename), 0.0)
                do_not_test=True
            if do_not_test:
                break

        elif (suffix=='.suppressif' and (os.access(f, os.R_OK))):
            try:
                suppressme=False
                suppresstest=runSkipIf(f)
                if suppresstest.strip() != "False":
                    suppressme = suppresstest.strip() == "True" or int(suppresstest) == 1
                if suppressme:
                    suppressline = ""
                    with open('./'+test_filename+'.suppressif', 'r') as suppressfile:
                        for line in suppressfile:
                            line = line.strip()
                            if (line.startswith("#") and
                                not line.startswith("#!")):
                                suppressline = line.replace('#','').strip()
                                break
                    futuretest='Suppress (' + suppressline + ') '
            except (ValueError, RuntimeError) as e:
                sys.stdout.write(str(e)+'\n')
                sys.stdout.write('[Error processing .suppressif file for %s]\n'
                    % os.path.join(localdir, test_filename))
                printEndOfTestMsg(os.path.join(localdir, test_filename), 0.0)
                do_not_test=True
                break

        elif (suffix==timeoutsuffix and os.access(f, os.R_OK)):
            fileTimeout = ReadIntegerValue(f, localdir)
            if fileTimeout < defaultTimeout or fileTimeout > globalTimeout:
                timeout = fileTimeout
                sys.stdout.write('[Overriding default timeout with %d]\n'%(timeout))
        elif (perftest and suffix==PerfSfx('timeexec') and os.access(f, os.R_OK)): #e.g. .perftimeexec
            timer = GetTimer(f)

        elif (suffix==PerfSfx('numtrials') and os.access(f, os.R_OK)): #e.g. .perfnumtrials
            numTrials = ReadIntegerValue(f, localdir)

        elif (suffix=='.killtimeout' and os.access(f, os.R_OK)):
            killtimeout=ReadIntegerValue(f, localdir)

        elif (suffix=='.catfiles' and os.access(f, os.R_OK)):
            execcatfiles=py3_compat.Popen(['cat', f], stdout=subprocess.PIPE).communicate()[0].strip()
            if catfiles:
                catfiles+=execcatfiles
            else:
                catfiles=execcatfiles

        elif (suffix=='.lastcompopts' and os.access(f, os.R_OK)):
            lastcompopts+=py3_compat.Popen(['cat', f], stdout=subprocess.PIPE).communicate()[0].strip().split()
            # sys.stdout.write("lastcompopts=%s\n"%(lastcompopts))

        elif (suffix=='.lastexecopts' and os.access(f, os.R_OK)):
            lastexecopts+=py3_compat.Popen(['cat', f], stdout=subprocess.PIPE).communicate()[0].strip().split()
            # sys.stdout.write("lastexecopts=%s\n"%(lastexecopts))

        elif (suffix==PerfSfx('numlocales') and os.access(f, os.R_OK)):
            numlocales=ReadIntegerValue(f, localdir)

        elif suffix==futureSuffix and os.access(f, os.R_OK):
            with open('./'+test_filename+futureSuffix, 'r') as futurefile:
                futuretest='Future ('+futurefile.readline().strip()+') '

        elif (suffix=='.noexec' and os.access(f, os.R_OK)):
            noexecfile=True
            executebin=False

        elif (suffix=='.precomp' and os.access(f, os.R_OK|os.X_OK)):
            precomp=f

        elif (suffix=='.prediff' and os.access(f, os.R_OK|os.X_OK)):
            prediff=f

        elif (suffix=='.preexec' and os.access(f, os.R_OK|os.X_OK)):
            preexec=f

        elif (suffix=='.stdin' and os.access(f, os.R_OK)):
            if redirectin == None:
                sys.stdout.write('[Skipping test with .stdin input since -nostdinredirect is given: %s/%s]\n'%(localdir,test_filename))
                do_not_test=True
                break
            else:
                redirectin = f

        if suffix==futureSuffix:
            testfuturesfile=True

    del test_filename_files

    # Skip to the next test
    if do_not_test:
        continue # on to next test

    # 0: test no futures
    if testfutures == 0 and testfuturesfile == True:
        sys.stdout.write('[Skipping future test: %s/%s]\n'%(localdir,test_filename))
        continue # on to next test
    # 1: test all futures
    elif testfutures == 1:
        pass
    # 2: test only futures
    elif testfutures == 2 and testfuturesfile == False:
        sys.stdout.write('[Skipping non-future test: %s/%s]\n'%(localdir,test_filename))
        continue # on to next test
    # 3: test futures that have a .skipif file
    elif testfutures == 3 and testfuturesfile == True and testskipiffile == False:
        sys.stdout.write('[Skipping future test without a skipif: %s/%s]\n'%(localdir,test_filename))
        continue # on to next test

    # c tests don't have a way to launch themselves
    if is_c_or_cpp_test and chpllauncher != 'none':
        sys.stdout.write('[Skipping %s test: %s/%s]\n'%(c_or_cpp,localdir,test_filename))
        continue

    # .ml-test.c tests should only run when we are in a multilocale setting
    if is_ml_c_or_cpp_test and chplcomm == 'none':
        sys.stdout.write('[Skipping multilocale-only %s test: %s/%s]\n'%(c_or_cpp,localdir,test_filename))
        continue

    # Set numlocales
    if (numlocales == 0) or (chplcomm=='none') or is_c_or_cpp_test:
        numlocexecopts = None
    else:
        if maxLocalesAvailable is not None:
            if numlocales > maxLocalesAvailable:
                sys.stdout.write('[Skipping test {0} because it requires '
                                 '{1} locales but only {2} are available]\n'
                                 .format(os.path.join(localdir, test_filename),
                                         numlocales, maxLocalesAvailable))
                continue
        if os.getenv('CHPL_TEST_MULTILOCALE_ONLY') and (numlocales <= 1) and not is_ml_c_or_cpp_test:
            sys.stdout.write('[Skipping {0} because it does not '
                             'use more than one locale]\n'
                             .format(os.path.join(localdir, test_filename)))
            continue
        numlocexecopts = ' -nl '+str(numlocales)

    # if any performance test has a timeout longer than the default we only
    # want to run it once
    if (timeout > globalTimeout):
        if numTrials != 1:
            sys.stdout.write('[Lowering number of trials for {0} to 1]\n'.format(test_filename))
            numTrials = 1

    # Get list of test specific compiler options
    # Default to [' ']
    compoptslist = list(' ')

    chpldoc_opts_filename = test_filename + chpldocsuffix
    if test_is_chpldoc and os.access(chpldoc_opts_filename, os.R_OK):
        compoptslist = ReadFileWithComments(chpldoc_opts_filename, False)
        if os.stat(chpldoc_opts_filename).st_size == 0:
            sys.stdout.write('[Warning: ignoring an empty chpldocopts file %s]\n' %
                             (test_filename+compoptssuffix))
    elif os.access(test_filename+compoptssuffix, os.R_OK):
        compoptslist = ReadFileWithComments(test_filename+compoptssuffix, False)
        if os.stat(test_filename+compoptssuffix).st_size == 0:
            # cf. for execoptslist no warning is issued
            sys.stdout.write('[Warning: ignoring an empty compopts file %s]\n'%(test_filename+compoptssuffix))

    compoptslist = compoptslist or list(' ')
    directoryCompopts = directoryCompopts or list(' ')

    # Merge global compopts list with local compopts.
    # Use the "product" of the two if they are both provided.
    usecompoptslist = [ ]
    # Note -- this could use itertools.product
    for dir_compopts in directoryCompopts:
        for file_compopts in compoptslist:
            useopt = [dir_compopts, file_compopts]
            usearg = ' '.join(useopt)
            # But change all-spaces into single space.
            if usearg.strip() == '':
              usearg = ' '
            usecompoptslist += [usearg]
    compoptslist = usecompoptslist

    # The test environment is that of this process, augmented as specified
    if os.access(test_filename+execenvsuffix, os.R_OK):
        execenv = ReadFileWithComments(test_filename+execenvsuffix)
    else:
        execenv = list()

    if os.access(test_filename+compenvsuffix, os.R_OK):
        compenv = ReadFileWithComments(test_filename+compenvsuffix)
    else:
        compenv = list()

    testenv = {}
    for var, val in [env.split('=', 1) for env in globalExecenv]:
        testenv[var.strip()] = val.strip()
    for var, val in [env.split('=', 1) for env in execenv]:
        testenv[var.strip()] = val.strip()

    testcompenv = {}
    for var, val in [env.split('=', 1) for env in globalCompenv]:
        testcompenv[var.strip()] = val.strip()
    for var, val in [env.split('=', 1) for env in compenv]:
        testcompenv[var.strip()] = val.strip()


    # Get list of test specific exec options
    if os.access(test_filename+execoptssuffix, os.R_OK):
        execoptsfile=True
        execoptslist = ReadFileWithComments(test_filename+execoptssuffix, False)
    else:
        execoptslist = list()
    # Handle empty execopts list
    if len(execoptslist) == 0:
        # cf. for compoptslist, a warning is issued in this case
        execoptslist.append(' ')

    if (os.getenv('CHPL_TEST_INTERP')=='on' and
        (noexecfile or testfuturesfile or execoptsfile)):
        sys.stdout.write('[Skipping interpretation of: %s/%s]\n'%(localdir,test_filename))
        continue # on to next test

    clist = list()
    curFileTestStart = time.time()

    # For all compopts + execopts combos..
    compoptsnum = 0
    for compopts in compoptslist:
        sys.stdout.flush()
        del clist
        # use the remaining portion as a .good file for executing tests
        #  clist will be *added* to execopts if it is empty, or just used
        #  as the default .good file if not empty
        clist = compopts.split('#')
        if len(clist) >= 2:
            compopts = clist.pop(0)
            cstr = ' #' + '#'.join(clist)
            del clist[:]
            clist.append(cstr)
        else:
            del clist[:]

        if compopts == ' ':
            complog=execname+'.comp.out.tmp'
        else:
            compoptsnum += 1
            complog = execname+'.'+str(compoptsnum)+'.comp.out.tmp'

        #
        # Run the precompile script
        #
        if globalPrecomp:
            sys.stdout.write('[Executing ./PRECOMP %s %s %s]\n'%(execname, complog, compiler))
            sys.stdout.flush()
            p = py3_compat.Popen(['./PRECOMP', execname, complog, compiler],
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            sys.stdout.write(p.communicate()[0])
            sys.stdout.flush()

        if precomp:
            sys.stdout.write('[Executing precomp %s.precomp]\n'%(test_filename))
            sys.stdout.flush()
            test_precomp = './{0}.precomp'.format(test_filename)
            p = py3_compat.Popen([test_precomp, execname, complog, compiler],
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            sys.stdout.write(p.communicate()[0])
            sys.stdout.flush()


        #
        # Build the test program
        #
        args = []
        if test_is_chpldoc:
            args += globalChpldocOpts + shlex.split(compopts)
        elif 'CHPL_TEST_NO_USE_O' not in os.environ or \
             os.environ.get('CHPL_TEST_NO_USE_O') != "true":
            args += ['-o', execname] + envCompopts + shlex.split(compopts)
        else:
            args += envCompopts + shlex.split(compopts)
        args += [testname]

        if is_c_or_cpp_test or is_ml_c_or_cpp_test:
            # we need to drop envCompopts for C tests as those are options
            # for `chpl` so don't include them here
            args = ['-o', test_filename]+shlex.split(compopts)+[testname]
            cmd = None
            if is_c_test:
                cmd = c_compiler
            elif is_ml_c_test:
                cmd_pieces = [host_c_compiler, runtime_includes_and_defines]
                cmd = ' '.join(cmd_pieces)
            elif is_cpp_test:
                cmd = cpp_compiler
            elif is_ml_cpp_test:
                cmd_pieces = [host_cpp_compiler, runtime_includes_and_defines]
                cmd = ' '.join(cmd_pieces)
        else:
            if test_is_chpldoc and not compiler.endswith('chpldoc'):
                # For tests with .doc.chpl suffix, use chpldoc compiler. Update
                # the compopts accordingly. Add 'doc' prefix to existing compiler.
                compiler += 'doc'
                cmd = compiler

                if which(cmd) is None:
                    sys.stdout.write(
                        '[Warning: Could not find chpldoc, skipping test '
                        '{0}/{1}]\n'.format(localdir, test_filename))
                    break

            if valgrindcomp:
                cmd = valgrindcomp
                args = valgrindcompopts+[compiler]+args
            # TODO: temporary way to plugin chpldoc next for testing
            elif 'CHPL_CHPLDOC_NEXT' in os.environ:
                cmd = os.environ['CHPL_CHPLDOC_NEXT']
            else:
                cmd = compiler

        if lastcompopts:
            args += lastcompopts

        compStart = time.time()
        #
        # Compile (with timeout)
        #
        # compilation timeout defaults to 4 * execution timeout.
        # This is to quiet compilation timeouts in some oversubscribed test
        # configurations (since they are generating a lot of testing noise, but
        # don't represent a real issue.)
        #
        # TODO (Elliot 02/27/15): Ideally what we want is separate options for
        # compiler and testing timeout, but that's more work to thread through
        # sub_test right now and this is causing a lot of noise in nightly
        # testing. Hopefully this is just a temporary work around and I'll
        # remember to add the cleaner solution soon.
        #
        comptimeout = 4*timeout
        cmd=ShellEscapeCommand(cmd);
        sys.stdout.write('[Executing compiler %s'%(cmd))
        if args:
            sys.stdout.write(' %s'%(' '.join(args)))
        sys.stdout.write(' < %s]\n'%(compstdin))
        sys.stdout.flush()
        if useTimedExec:
            wholecmd = cmd+' '+' '.join(map(ShellEscape, args))
            p = py3_compat.Popen([timedexec, str(comptimeout), wholecmd],
                                 env=dict(list(os.environ.items()) + list(testcompenv.items())),
                                 stdin=open(compstdin, 'r'),
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            output = p.communicate()[0]
            status = p.returncode

            if status == 222:
                sys.stdout.write('%s[Error: Timed out compilation for %s/%s'%
                                 (futuretest, localdir, test_filename))
                printTestVariation(compoptsnum, compoptslist);
                sys.stdout.write(']\n')
                sys.stdout.write('[Compilation output was as follows:]\n')
                sys.stdout.write(trim_output(output))
                cleanup(execname)
                cleanup(printpassesfile)
                continue # on to next compopts

        else:
            p = py3_compat.Popen([cmd]+args,
                                 env=dict(list(os.environ.items()) + list(testcompenv.items())),
                                 stdin=open(compstdin, 'r'),
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            try:
                output = py3_compat.bytes_to_str(SuckOutputWithTimeout(p.stdout, comptimeout))
            except ReadTimeoutException:
                sys.stdout.write('%s[Error: Timed out compilation for %s/%s'%
                                 (futuretest, localdir, test_filename))
                printTestVariation(compoptsnum, compoptslist);
                sys.stdout.write(']\n')
                KillProc(p, killtimeout)
                cleanup(execname)
                cleanup(printpassesfile)
                continue # on to next compopts

            p.poll()
            status = p.returncode

        elapsedCompTime = time.time() - compStart
        test_name = os.path.join(localdir, test_filename)
        if compoptsnum != 0:
            test_name += ' (compopts: {0})'.format(compoptsnum)

        print('[Elapsed compilation time for "{0}" - {1:.3f} '
            'seconds]'.format(test_name, elapsedCompTime))

        output = remove_clock_skew_warning(output)

        # remove some_file: output from C compilers
        if is_c_or_cpp_test:
          for arg in args:
            if arg.endswith(".c") or arg.endswith(".cpp"):
              # remove lines like
              # somefile.c:
              # that some C compilers emit when compiling multiple files
              output = output.replace(arg + ":\n", "");

        if (status!=0 or not executebin):
            # Save original output
            origoutput = output;
            dealWithBinary = False

            # Compare compiler output with expected program output
            if catfiles:
                sys.stdout.write('[Concatenating extra files: %s]\n'%
                                 (test_filename+'.catfiles'))
                sys.stdout.flush()
                WaitForFiles(catfiles.split())
                p = py3_compat.Popen(['cat']+catfiles.split(),
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                catoutput = p.communicate()[0]
                # If the python 3 compatible popen command to concatenate files
                # has given us a bytes back but the output up to this point was
                # all str, then we may not be able to translate the cat output
                # into a str and may have to go the other way
                if (isinstance(catoutput, bytes) and
                    not isinstance(output, bytes)):
                    try:
                        decodedcat = catoutput.decode('utf_8')
                        output += decodedcat
                    except UnicodeDecodeError:
                        dealWithBinary = True
                        output = output.encode('utf_8')
                        output += catoutput
                else:
                    output += catoutput

            # Sadly these scripts require an actual file
            with open(complog, 'w') as complogfile:
                complogfile.write('%s'%(output))

            if systemPrediffs:
                for sprediff in systemPrediffs:
                    sys.stdout.write('[Executing system-wide prediff %s]\n'%(sprediff))
                    sys.stdout.flush()
                    p = py3_compat.Popen([sprediff, execname, complog, compiler,
                                          ' '.join(envCompopts)+' '+compopts, ' '.join(args)],
                                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                    sys.stdout.write(p.communicate()[0])
                    sys.stdout.flush()

            if globalPrediff:
                sys.stdout.write('[Executing ./PREDIFF]\n')
                sys.stdout.flush()
                p = py3_compat.Popen(['./PREDIFF', execname, complog, compiler,
                                     ' '.join(envCompopts)+' '+compopts, ' '.join(args)],
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                sys.stdout.write(p.communicate()[0])
                sys.stdout.flush()

            if prediff:
                sys.stdout.write('[Executing prediff %s.prediff]\n'%(test_filename))
                sys.stdout.flush()
                test_prediff = './{0}.prediff'.format(test_filename)
                p = py3_compat.Popen([test_prediff, execname, complog, compiler,
                                     ' '.join(envCompopts)+' '+compopts, ' '.join(args)],
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                sys.stdout.write(p.communicate()[0])
                sys.stdout.flush()


            # find the compiler .good file to compare against. The compiler
            # .good file can be of the form testname.<configuration>.good or
            # explicitname.<configuration>.good. It's not currently setup to
            # handle testname.<configuration>.<compoptsnum>.good, but that
            # would be easy to add. 
            basename = test_filename 
            if len(clist) != 0:
                explicitcompgoodfile = clist[0].split('#')[1].strip()
                basename = explicitcompgoodfile.replace('.good', '')

            goodfile = FindGoodFile(basename)
            # sys.stdout.write('default goodfile=%s\n'%(goodfile))
            error_msg = filter_compiler_errors(origoutput)
            if error_msg != '':
                sys.stdout.write('[Error %s for %s]\n'%(error_msg, test_name))
                sys.stdout.write('[Compiler output was as follows:]\n')
                sys.stdout.write(origoutput)
                cleanup(execname)
                cleanup(printpassesfile)
                continue

            if not os.path.isfile(goodfile) or not os.access(goodfile, os.R_OK):
                if perftest or executebin:
                    sys.stdout.write('[Error compilation failed for %s]\n'%(test_name))
                else:
                    sys.stdout.write('[Error cannot locate compiler output comparison file %s/%s]\n'%(localdir, goodfile))
                sys.stdout.write('[Compiler output was as follows:]\n')
                sys.stdout.write(origoutput)
                cleanup(execname)
                cleanup(printpassesfile)
                continue # on to next compopts

            if (dealWithBinary):
                result = DiffBinaryFiles(goodfile, complog)
            else:
                result = DiffFiles(goodfile, complog)
            if result==0:
                os.unlink(complog)
                sys.stdout.write('%s[Success '%(futuretest))
            else:
                sys.stdout.write('%s[Error '%(futuretest))
            sys.stdout.write('matching compiler output for %s/%s'%
                                 (localdir, test_filename))
            printTestVariation(compoptsnum, compoptslist);
            sys.stdout.write(']\n')

            # If there was a prediff it may have hidden the compilation
            # failure, so print out the full log. Note that we don't check
            # systemPrediffs since they shouldn't be filtering all output
            # anyways, and it could create noise for configs that use them.
            if result != 0:
                if prediff or globalPrediff:
                    sys.stdout.write('[Compiler output before prediff was as follows:]\n')
                    sys.stdout.write(origoutput)

            if (result != 0 and futuretest != ''):
                badfile=test_filename+'.bad'
                if os.access(badfile, os.R_OK):
                    badresult = DiffBadFiles(badfile, complog)
                    if badresult==0:
                        os.unlink(complog);
                        sys.stdout.write('[Clean match against .bad file ')
                    else:
                        # bad file doesn't match, which is bad
                        sys.stdout.write('[Error matching .bad file ')
                    sys.stdout.write('for %s/%s'%(localdir, test_filename))
                    printTestVariation(compoptsnum, compoptslist);
                    sys.stdout.write(']\n');

            cleanup(execname)
            cleanup(printpassesfile)
            continue # on to next compopts
        else:
            compoutput = output # save for diff

            exec_log_names = []

            # Exactly one execution output file.
            if len(compoptslist) == 1 and len(execoptslist) == 1:
                exec_log_names.append(get_exec_log_name(execname))

            # One execution output file for the current compiler opt.
            elif len(compoptslist) > 1 and len(execoptslist) == 1:
                exec_opts_num_tmp = 1
                if execoptslist[0] == ' ':
                    exec_opts_num_tmp = 0
                exec_log_names.append(get_exec_log_name(execname, compoptsnum, exec_opts_num_tmp))

            # One execution output file for each of the execution opts.
            elif len(compoptslist) == 1 and len(execoptslist) > 1:
                for i in range(1, len(execoptslist) + 1):
                    exec_log_names.append(get_exec_log_name(execname, compoptsnum, i))

            # This enumerates the cross product of all compiler and execution
            # opts. It's not clear whether this is actually supported elsewhere
            # (like start_test), but it's here.
            else:
                for i in range(1, len(execoptslist) + 1):
                    exec_log_names.append(get_exec_log_name(execname, compoptsnum, i))

            # Write the log(s), so it/they can be modified by preexec(s).
            for exec_log_name in exec_log_names:
                with open(exec_log_name, 'w') as execlogfile:
                    execlogfile.write(compoutput)

        #
        # Compile successful
        #
        sys.stdout.write('[Success compiling %s/%s]\n'%(localdir, test_filename))

        # Note that compiler performance only times successful compilations. 
        # Tests that are designed to fail before compilation is complete will 
        # not get timed, so the total time compiling might be off slightly.   
        if compperftest and not is_c_or_cpp_test:
            # make the compiler performance directories if they don't exist 
            timePasses = True
            if not os.path.isdir(compperfdir) and not os.path.isfile(compperfdir):
                py3_compat.makedirs(compperfdir, exist_ok=True)
            if not os.access(compperfdir, os.R_OK|os.X_OK):
                sys.stdout.write('[Error creating compiler performance test directory %s]\n'%(compperfdir))
                timePasses = False

            if not os.path.isdir(tempDatFilesDir) and not os.path.isfile(tempDatFilesDir):
                py3_compat.makedirs(tempDatFilesDir, exist_ok=True)
            if not os.access(compperfdir, os.R_OK|os.X_OK):
                sys.stdout.write('[Error creating compiler performance temp dat file test directory %s]\n'%(tempDatFilesDir))
                timePasses = False

            # so long as we have to the directories 
            if timePasses: 
                # We need to name the files differently for each compiler
                # option. 0 is the default compoptsnum if there are no options
                # listed so we don't need to clutter the names with that
                compoptsstring = str(compoptsnum)
                if compoptsstring == '0':
                    compoptsstring = ''

                # make the datFileName the full path with / replaced with ~~ so
                # we can keep the full path for later but not create a bunch of
                # new directories.
                datFileName = localdir.replace('/', '~~') + '~~' + test_filename + compoptsstring

                # computePerfStats for the current test
                sys.stdout.write('[Executing computePerfStats %s %s %s %s %s]\n'%(datFileName, tempDatFilesDir, keyfile, printpassesfile, 'False'))
                sys.stdout.flush()
                p = py3_compat.Popen([utildir+'/test/computePerfStats', datFileName, tempDatFilesDir, keyfile, printpassesfile, 'False'], stdout=subprocess.PIPE)
                compkeysOutput = p.communicate()[0]
                datFiles = [tempDatFilesDir+'/'+datFileName+'.dat',  tempDatFilesDir+'/'+datFileName+'.error']
                status = p.returncode

                if status == 0:
                    sys.stdout.write('[Success finding compiler performance keys for %s/%s]\n'% (localdir, test_filename))
                else:
                    sys.stdout.write('[Error finding compiler performance keys for %s/%s.]\n'% (localdir, test_filename))
                    printTestVariation(compoptsnum, compoptslist);
                    sys.stdout.write('computePerfStats output was:\n%s\n'%(compkeysOutput))
                    sys.stdout.flush()
                    sys.stdout.write('Deleting .dat files for %s/%s because of failure to find all keys\n'%(localdir, test_filename))
                    for datFile in datFiles:
                        if os.path.isfile(datFile):
                            os.unlink(datFile)

            #delete the timing file     
            cleanup(printpassesfile)


        if os.getenv('CHPL_COMPONLY'):
            sys.stdout.write('[Note: Not executing or comparing the output due to -noexec flags]\n')
            cleanup(execname)
            continue # on to next compopts
        explicitcompgoodfile = None
        # Execute the test for all requested execopts
        execoptsnum = 0
        if len(clist)!=0:
            if len(clist[0].split('#')) > 1:
                explicitcompgoodfile = clist[0].split('#')[1].strip()
        redirectin_set_in_loop = False
        redirectin_original_value = redirectin
        for texecopts in execoptslist:
            sys.stdout.flush()

            # Reset redirectin, in case execopts has multiple lines with
            # different stdin files.
            if redirectin_set_in_loop:
                redirectin = redirectin_original_value
                redirectin_set_in_loop = False
            if (len(compoptslist)==1) and (len(execoptslist)==1):
                onlyone = True
                execlog = get_exec_log_name(execname)
            else:
                onlyone = False
                if texecopts != ' ':
                    execoptsnum += 1
                execlog = get_exec_log_name(execname, compoptsnum, execoptsnum)

            tlist = texecopts.split('#')
            execopts = tlist[0].strip()

            if numlocexecopts != None:
                execopts += numlocexecopts;
            if len(tlist) > 1:
                # Ignore everything after the first token
                explicitexecgoodfile = tlist[1].strip().split()[0]
            else:
                explicitexecgoodfile = explicitcompgoodfile
            del tlist

            if systemPreexecs:
                for spreexec in systemPreexecs:
                    sys.stdout.write('[Executing system-wide preexec %s]\n'%(spreexec))
                    sys.stdout.flush()
                    p = py3_compat.Popen([spreexec, execname, execlog, compiler],
                                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                    sys.stdout.write(p.communicate()[0])
                    sys.stdout.flush()

            if globalPreexec:
                sys.stdout.write('[Executing ./PREEXEC]\n')
                sys.stdout.flush()
                p = py3_compat.Popen(['./PREEXEC', execname, execlog, compiler],
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                sys.stdout.write(p.communicate()[0])
                sys.stdout.flush()

            if preexec:
                sys.stdout.write('[Executing preexec %s.preexec]\n'%(test_filename))
                sys.stdout.flush()
                test_preexec = './{0}.preexec'.format(test_filename)
                p = py3_compat.Popen([test_preexec, execname, execlog, compiler],
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                sys.stdout.write(p.communicate()[0])
                sys.stdout.flush()

            pre_exec_output = ''
            if os.path.exists(execlog):
                with open(execlog, 'r') as exec_log_file:
                    pre_exec_output = exec_log_file.read()

            if not os.access(execname, os.R_OK|os.X_OK):
                sys.stdout.write('%s[Error could not locate executable %s for %s/%s'%
                                 (futuretest, execname, localdir, test_filename))
                printTestVariation(compoptsnum, compoptslist,
                                   execoptsnum, execoptslist)
                sys.stdout.write(']\n')
                break; # on to next compopts

            # When doing whole program execution, we want to time the _real
            # binary for launchers that use a queue so we don't include the
            # time to get the reservation. These are the launchers known to
            # support timing the _real using CHPL_LAUNCHER_REAL_WRAPPER.
            timereal = (chpllauncher in ['pbs-aprun', 'aprun', 'slurm-srun'] or
                        'slurm-gasnetrun' in chpllauncher)

            args=list()
            if timer and timereal:
                cmd='./'+execname
                testenv['CHPL_LAUNCHER_REAL_WRAPPER'] = timer
            elif timer:
                cmd=timer
                args+=['./'+execname]
            elif valgrindbin:
                cmd=valgrindbin
                args+=valgrindbinopts+['./'+execname]
            else:
                cmd='./'+execname

            # if we're using a launchcmd, build up the command to call
            # launchcmd, and have it run the cmd and args built above
            if launchcmd:
                # have chpl_launchcmd time execution and place results in a
                # file since sub_test time will include time to get reservation
                launchcmd_exec_time_file = execname + '_launchcmd_exec_time.txt'
                os.environ['CHPL_LAUNCHCMD_EXEC_TIME_FILE'] = launchcmd_exec_time_file

                # save old cmd and args and add them after launchcmd args.
                oldcmd = cmd
                oldargs = list(args)
                launch_cmd_list = shlex.split(launchcmd)
                cmd = launch_cmd_list[0]
                args = launch_cmd_list[1:]
                args += [oldcmd]
                args += oldargs

            args+=globalExecopts
            args+=shlex.split(execopts)
            # envExecopts are meant for chpl programs, dont add them to C tests
            if not is_c_or_cpp_test and envExecopts != None:
                args+=shlex.split(envExecopts)
            # lastexecopts really must be last, so add any launcher timeout now
            if useLauncherTimeout:
                args += LauncherTimeoutArgs(timeout)
            if lastexecopts:
                args += lastexecopts
            # sys.stdout.write("args=%s\n"%(args))

            if len(args) >= 2 and '<' in args:
              redirIdx = args.index('<')
              execOptRedirect = args[redirIdx + 1]
              args.pop(redirIdx+1)
              args.pop(redirIdx)
              if redirectin == None:
                  # It is a little unfortunate that we compile the test only to skip it here.
                  # In order to prevent this, the logic for combining all the places execpopts
                  # come from and checking for '<' would have to be factored out or duplicated
                  print('[Skipping test with stdin redirection ("<") in execopts since '
                        '-nostdinredirect is given {0}/{1}]'.format(localdir, test_filename))
                  break;
              elif redirectin == "/dev/null":
                if os.access(execOptRedirect, os.R_OK):
                  redirectin = execOptRedirect
                  redirectin_set_in_loop = True
                else:
                  sys.stdout.write('[Error: redirection file %s does not exist]\n'%(execOptRedirect))
                  break
              else:
                sys.stdout.write('[Error: a redirection file already exists: %s]\n'%(redirectin))
                break

            #
            # Run program (with timeout)
            #
            skip_remaining_trials = False
            for count in range(numTrials):
                if skip_remaining_trials:
                    break

                exec_limiter = execution_limiter.NoLock()
                if os.getenv("CHPL_TEST_LIMIT_RUNNING_EXECUTABLES") is not None:
                    exec_name = os.path.join(localdir, test_filename)
                    exec_limiter = execution_limiter.FileLock()

                with exec_limiter:
                    exectimeout = False  # 'exectimeout' is specific to one trial of one execopt setting
                    launcher_error = ''  # used to suppress output/timeout errors whose root cause is a launcher error
                    sys.stdout.write('[Executing program %s %s'%(cmd, ' '.join(args)))
                    if redirectin:
                        sys.stdout.write(' < %s'%(redirectin))
                    sys.stdout.write(']\n')
                    sys.stdout.flush()

                    execStart = time.time()
                    if useLauncherTimeout:
                        if redirectin == None:
                            my_stdin = None
                        else:
                            my_stdin=open(redirectin, 'r')
                        test_command = [cmd] + args
                        p = py3_compat.Popen(test_command,
                                             env=dict(list(os.environ.items()) + list(testenv.items())),
                                             stdin=my_stdin, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                        (stdout, stderr) = p.communicate()
                        output = py3_compat.concat_streams(stdout, stderr)
                        status = p.returncode

                        # Check for well-known failure modes
                        launcher_error, thistimeout = translateOutput(output)
                        exectimeout = exectimeout or thistimeout

                        if launcher_error:
                            sys.stdout.write('%s[Error: %s %s/%s'%
                                            (futuretest, launcher_error, localdir, test_filename))
                            printTestVariation(compoptsnum, compoptslist,
                                               execoptsnum, execoptslist);
                            sys.stdout.write(']\n')
                            sys.stdout.write('[Execution output was as follows:]\n')
                            sys.stdout.write(trim_output(output))

                    elif useTimedExec:
                        wholecmd = cmd+' '+' '.join(map(ShellEscape, args))

                        if redirectin == None:
                            my_stdin = sys.stdin
                        else:
                            my_stdin = open(redirectin, 'r')
                        p = py3_compat.Popen([timedexec, str(timeout), wholecmd],
                                             env=dict(list(os.environ.items()) + list(testenv.items())),
                                             stdin=my_stdin, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                        (stdout, stderr) = p.communicate()
                        output = py3_compat.concat_streams(stdout, stderr)
                        status = p.returncode

                        if status == 222:
                            exectimeout = True
                            sys.stdout.write('%s[Error: Timed out executing program %s/%s'%
                                            (futuretest, localdir, test_filename))
                            printTestVariation(compoptsnum, compoptslist,
                                               execoptsnum, execoptslist);
                            sys.stdout.write(']\n')
                            sys.stdout.write('[Execution output was as follows:]\n')
                            sys.stdout.write(trim_output(output))
                        else:
                            # for perf runs print out the 5 processes with the
                            # highest cpu usage. This should help identify if other
                            # processes might have interfered with a test.
                            if perftest:
                                print('[Reporting processes with top 5 highest cpu usages]')
                                sys.stdout.flush()
                                psCom = 'ps ax -o user,pid,pcpu,command '
                                subprocess.call(psCom + '| head -n 1', shell=True)
                                subprocess.call(psCom + '| tail -n +2 | sort -r -k 3 | head -n 5', shell=True)


                    else:
                        if redirectin == None:
                            my_stdin = None
                        else:
                            my_stdin=open(redirectin, 'r')
                        p = py3_compat.Popen([cmd]+args,
                                             env=dict(list(os.environ.items()) + list(testenv.items())),
                                             stdin=my_stdin, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                        try:
                            stdoutOutput = SuckOutputWithTimeout(p.stdout, timeout)
                        except ReadTimeoutException:
                            exectimeout = True
                            sys.stdout.write('%s[Error: Timed out executing program %s/%s'%
                                            (futuretest, localdir, test_filename))
                            printTestVariation(compoptsnum, compoptslist,
                                               execoptsnum, execoptslist);
                            sys.stdout.write(']\n')
                            KillProc(p, killtimeout)

                        stderrOutput = p.stderr.read()
                        output = py3_compat.concat_streams(stdoutOutput, stderrOutput)
                        p.poll()
                        status = p.returncode

                # executable is done running

                elapsedExecTime = time.time() - execStart
                test_name = os.path.join(localdir, test_filename)
                compExecStr = ''
                if compoptsnum != 0:
                    compExecStr += 'compopts: {0} '.format(compoptsnum)
                if execoptsnum != 0:
                    compExecStr += 'execopts: {0}'.format(execoptsnum)
                if compExecStr:
                    test_name += ' ({0})'.format(compExecStr.strip())

                if launchcmd and os.path.exists(launchcmd_exec_time_file):
                    with open(launchcmd_exec_time_file, 'r') as fp:
                        try:
                            launchcmd_exec_time = float(fp.read())
                            print('[launchcmd reports elapsed execution time '
                                'for "{0}" - {1:.3f} seconds]'
                                .format(test_name, launchcmd_exec_time))
                        except ValueError:
                            print('Could not parse launchcmd time file '
                                '{0}'.format(launchcmd_exec_time_file))
                    os.unlink(launchcmd_exec_time_file)

                print('[Elapsed execution time for "{0}" - {1:.3f} '
                    'seconds]'.format(test_name, elapsedExecTime))


                if execTimeWarnLimit and elapsedExecTime > execTimeWarnLimit:
                    sys.stdout.write('[Warning: %s/%s took over %.0f seconds to '
                        'execute]\n' %(localdir, test_filename, execTimeWarnLimit))

                if execTimeSkipTrials and elapsedExecTime > execTimeSkipTrials:
                    skip_remaining_trials = True

                if catfiles:
                    sys.stdout.write('[Concatenating extra files: %s]\n'%
                                    (test_filename+'.catfiles'))
                    sys.stdout.flush()
                    WaitForFiles(catfiles.split())
                    p = py3_compat.Popen(['cat']+catfiles.split(),
                                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                    cat_output = p.communicate()[0]
                    if sys.version_info[0] >= 3 and isinstance(cat_output, bytes):
                        output = py3_compat.str_to_bytes(output)
                    output += cat_output

                # Sadly the scripts used below require an actual file
                open_mode = 'w' if isinstance(output, str) else 'wb'
                with open(execlog, open_mode) as execlogfile:
                    if open_mode == 'wb':
                        pre_exec_output_content = py3_compat.str_to_bytes(pre_exec_output)
                    else:
                        pre_exec_output_content = pre_exec_output
                    execlogfile.write(pre_exec_output_content)

                    if open_mode == 'wb':
                        output_content = py3_compat.str_to_bytes(output)
                    else:
                        output_content = output
                    execlogfile.write(output_content)

                if not exectimeout and not launcher_error:
                    if systemPrediffs:
                        for sprediff in systemPrediffs:
                            sys.stdout.write('[Executing system-wide prediff %s]\n'%(sprediff))
                            sys.stdout.flush()
                            p = py3_compat.Popen([sprediff, execname, execlog, compiler,
                                                  ' '.join(envCompopts)+' '+compopts, ' '.join(args)],
                                                 env=dict(list(os.environ.items()) + list(testenv.items())),
                                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                            sys.stdout.write(p.communicate()[0])

                    if globalPrediff:
                        sys.stdout.write('[Executing ./PREDIFF]\n')
                        sys.stdout.flush()
                        p = py3_compat.Popen(['./PREDIFF', execname, execlog, compiler,
                                             ' '.join(envCompopts)+ ' '+compopts, ' '.join(args)],
                                             env=dict(list(os.environ.items()) + list(testenv.items())),
                                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                        sys.stdout.write(p.communicate()[0])

                    if prediff:
                        sys.stdout.write('[Executing prediff ./%s]\n'%(prediff))
                        sys.stdout.flush()
                        p = py3_compat.Popen(['./'+prediff, execname, execlog, compiler,
                                             ' '.join(envCompopts)+' '+compopts, ' '.join(args)],
                                             env=dict(list(os.environ.items()) + list(testenv.items())),
                                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                        sys.stdout.write(p.communicate()[0])

                    if not perftest:
                        # find the good file 

                        basename = test_filename
                        commExecNum = ['']

                        # if there were multiple compopts/execopts find the
                        # .good file that corresponds to that run 
                        if not onlyone:
                            commExecNum.insert(0,'.'+str(compoptsnum)+'-'+str(execoptsnum))

                        # if the .good file was explicitly specified, look for
                        # that version instead of the multiple
                        # compopts/execopts or just the base .good file 
                        if explicitexecgoodfile != None:
                            basename  = explicitexecgoodfile.replace('.good', '')
                            commExecNum = ['']

                        execgoodfile = FindGoodFile(basename, commExecNum)

                        if not os.path.isfile(execgoodfile) or not os.access(execgoodfile, os.R_OK):
                            sys.stdout.write('[Error cannot locate program output comparison file %s/%s]\n'%(localdir, execgoodfile))
                            sys.stdout.write('[Execution output was as follows:]\n')
                            p = py3_compat.Popen(['cat', execlog], stdout=subprocess.PIPE)
                            exec_output = p.communicate()[0]
                            sys.stdout.write(trim_output(exec_output))

                            continue # on to next execopts

                        result = DiffFiles(execgoodfile, execlog)
                        if result==0:
                            os.unlink(execlog)
                            sys.stdout.write('%s[Success '%(futuretest))
                        else:
                            extra_msg = filter_errors(output, pre_exec_output, execgoodfile, execlog)
                            sys.stdout.write('%s[Error %s'%(futuretest, extra_msg))

                        sys.stdout.write('matching program output for %s/%s'%
                                        (localdir, test_filename))
                        if result!=0:
                            printTestVariation(compoptsnum, compoptslist,
                                               execoptsnum, execoptslist);
                        sys.stdout.write(']\n')

                        if (result != 0 and futuretest != ''):
                            badfile=test_filename+'.bad'
                            if os.access(badfile, os.R_OK):
                                badresult = DiffFiles(badfile, execlog)
                                if badresult==0:
                                    os.unlink(execlog);
                                    sys.stdout.write('[Clean match against .bad file ')
                                else:
                                    # bad file doesn't match, which is bad
                                    sys.stdout.write('[Error matching .bad file ')
                                sys.stdout.write('for %s/%s'%(localdir, test_filename))
                                printTestVariation(compoptsnum, compoptslist);
                                sys.stdout.write(']\n');


                if perftest:
                    if not os.path.isdir(perfdir) and not os.path.isfile(perfdir):
                        py3_compat.makedirs(perfdir, exist_ok=True)
                    if not os.access(perfdir, os.R_OK|os.X_OK):
                        sys.stdout.write('[Error creating performance test directory %s]\n'%(perfdir))
                        break # on to next compopts

                    if explicitexecgoodfile==None:
                        perfexecname = test_filename
                        keyfile = PerfTFile(test_filename,'keys') #e.g. .perfkeys
                    else:
                        perfexecname = re.sub(r'\{0}$'.format(PerfSfx('keys')), '', explicitexecgoodfile)
                        if (os.path.isfile(explicitexecgoodfile) and
                            test_filename != explicitexecgoodfile):
                            keyfile = explicitexecgoodfile
                        else:
                            keyfile = PerfTFile(test_filename,'keys')

                    perfdate = os.getenv('CHPL_TEST_PERF_DATE')
                    if perfdate == None:
                        perfdate = datetime.date.today().strftime("%m/%d/%y")

                    sys.stdout.write('[Executing %s/test/computePerfStats %s %s %s %s %s %s]\n'%(utildir, perfexecname, perfdir, keyfile, execlog, str(exectimeout), perfdate))
                    sys.stdout.flush()
                    p = py3_compat.Popen([utildir+'/test/computePerfStats',
                                         perfexecname, perfdir, keyfile, execlog, str(exectimeout), perfdate],
                                         stdout=subprocess.PIPE)
                    sys.stdout.write('%s'%(p.communicate()[0]))
                    sys.stdout.flush()

                    status = p.returncode
                    if not exectimeout and not launcher_error:
                        if status == 0:
                            os.unlink(execlog)
                            sys.stdout.write('%s[Success '%(futuretest))
                        else:
                            sys.stdout.write('%s[Error '%(futuretest))
                        sys.stdout.write('matching performance keys for %s/%s'%
                                        (localdir, test_filename))
                        if status!=0:
                            printTestVariation(compoptsnum, compoptslist,
                                               execoptsnum, execoptslist);
                        sys.stdout.write(']\n')

                    if exectimeout or status != 0:
                        break

        cleanup(execname, len(compoptslist) > 1)

    del execoptslist
    del compoptslist

    elapsedCurFileTestTime = time.time() - curFileTestStart
    test_name = os.path.join(localdir, test_filename)
    printEndOfTestMsg(test_name, elapsedCurFileTestTime)


sys.exit(0)
