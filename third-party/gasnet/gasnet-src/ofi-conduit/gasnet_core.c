/*   $Source: bitbucket.org:berkeleylab/gasnet.git/ofi-conduit/gasnet_core.c $
 * Description: GASNet libfabric (OFI) conduit Implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Copyright 2015, Intel Corporation
 * Portions copyright 2018-2020, The Regents of the University of California.
 * Terms of use are as specified in license.txt
 */

#include <gasnet_internal.h>
#include <gasnet_core_internal.h>
#include <gasnet_ofi.h>

#include <errno.h>
#include <unistd.h>
#include <signal.h>


GASNETI_IDENT(gasnetc_IdentString_Version, "$GASNetCoreLibraryVersion: " GASNET_CORE_VERSION_STR " $");
GASNETI_IDENT(gasnetc_IdentString_Name,    "$GASNetCoreLibraryName: " GASNET_CORE_NAME_STR " $");

gex_AM_Entry_t *gasnetc_handler; // TODO-EX: will be replaced with per-EP tables

/* Exit coordination timeouts */
#define GASNETC_DEFAULT_EXITTIMEOUT_MAX         480.0   // 8 min - extrapolated from Summit data in bug 4360
#define GASNETC_DEFAULT_EXITTIMEOUT_MIN          10.0   // 10 sec
#define GASNETC_DEFAULT_EXITTIMEOUT_FACTOR      0.25    // 1/4 second per process
static double gasnetc_exittimeout = GASNETC_DEFAULT_EXITTIMEOUT_MAX;

static int gasnetc_exit_init(void);

#if GASNET_PAR
struct gasnetc_ofi_locks_ gasnetc_ofi_locks;
#endif

size_t gasnetc_sizeof_segment_t(void) {
  gasnetc_Segment_t segment;
  return sizeof(*segment);
}

/* ------------------------------------------------------------------------------------ */
/*
  Initialization
  ==============
*/
/* called at startup to check configuration sanity */
static void gasnetc_check_config(void) {
  gasneti_check_config_preinit();
}

static int gasnetc_init(int *argc, char ***argv, gex_Flags_t flags) {
  /*  check system sanity */
  gasnetc_check_config();

  if (gasneti_init_done) 
    GASNETI_RETURN_ERRR(NOT_INIT, "GASNet already initialized");

  gasneti_init_done = 1; /* enable early to allow tracing */

  gasneti_freezeForDebugger();

  #if GASNET_DEBUG_VERBOSE
    /* note - can't call trace macros during gasnet_init because trace system not yet initialized */
    fprintf(stderr,"gasnetc_init(): about to spawn...\n"); fflush(stderr);
  #endif

  gasneti_spawner = gasneti_spawnerInit(argc, argv, NULL, &gasneti_nodes, &gasneti_mynode);
  if (!gasneti_spawner) GASNETI_RETURN_ERRR(NOT_INIT, "GASNet job spawn failed");

  /* Must init timers after global env, and preferably before tracing */
  GASNETI_TICKS_INIT();

  /* Now enable tracing of all the following steps */
  gasneti_trace_init(argc, argv);

  /* bootstrap the nodes for ofi conduit */
  int ret = gasnetc_ofi_init();
  if (GASNET_OK != ret)
	 return ret;

  #if GASNET_DEBUG_VERBOSE
    fprintf(stderr,"gasnetc_init(): spawn successful - node %i/%i starting...\n", 
      gasneti_mynode, gasneti_nodes); fflush(stderr);
  #endif

  gasneti_assert_zeroret(gasnetc_exit_init());

  gasneti_nodemapInit(gasneti_spawner->Exchange, NULL, 0, 0);

  #if GASNET_PSHM
  gasneti_pshm_init(gasneti_bootstrapSNodeBroadcast, 0);
  #endif

  uintptr_t mmap_limit;
  #if HAVE_MMAP
    // Bound per-host (sharedLimit) argument to gasneti_segmentLimit()
    // while properly reserving space for aux segments.
    uint64_t sharedLimit = gasneti_sharedLimit();
    uint64_t hostAuxSegs = gasneti_myhost.node_count * gasneti_auxseg_preinit();
    if (sharedLimit <= hostAuxSegs) {
      gasneti_fatalerror("per-host segment limit %"PRIu64" is too small to accommodate %i aux segments, "
                         "total size %"PRIu64". You may need to adjust OS shared memory limits.",
                         sharedLimit, gasneti_myhost.node_count, hostAuxSegs);
    }
    sharedLimit -= hostAuxSegs;

    mmap_limit = gasneti_segmentLimit((uintptr_t)-1, sharedLimit,
                                gasneti_spawner->Exchange,
                                gasneti_spawner->Barrier);
  #else
    // TODO-EX: we can at least look at rlimits but such logic belongs in conduit-indep code
    mmap_limit = (intptr_t)-1;
  #endif

  /* allocate and attach an aux segment */
  gasnet_seginfo_t auxseg = gasneti_auxsegAttach((uintptr_t)-1, gasneti_spawner->Exchange);
  gasnetc_auxseg_register(auxseg);

  /* determine Max{Local,GLobal}SegmentSize */
  gasneti_segmentInit(mmap_limit, gasneti_spawner->Exchange, flags);

  gasneti_init_done = 1;  

  return GASNET_OK;
}

/* ------------------------------------------------------------------------------------ */
extern int gasnetc_attach_primary(void) {
  /* ------------------------------------------------------------------------------------ */
  /*  register fatal signal handlers */

  /* catch fatal signals and convert to SIGQUIT */
  gasneti_registerSignalHandlers(gasneti_defaultSignalHandler);

  // register process exit-time hook
  gasneti_registerExitHandler(gasnetc_exit);

  /* ------------------------------------------------------------------------------------ */
  /*  primary attach complete */
  gasneti_attach_done = 1;
  gasneti_spawner->Barrier();

  GASNETI_TRACE_PRINTF(C,("gasnetc_attach_primary(): primary attach complete"));

  gasnete_init(); /* init the extended API */

  gasneti_nodemapFini();

  if (! gasneti_mynode) {
    fflush(NULL);
    fprintf(stderr,
      " WARNING: ofi-conduit is experimental and should not be used for\n"
      "          performance measurements.\n"
      "          Please see `ofi-conduit/README` for more details.\n");
    fflush(NULL);
  }

  /* ensure extended API is initialized across nodes */
  gasneti_spawner->Barrier();

  /* (###) Optionally (but recommended) free spawner's idle resources.
   * Safe even if spawner collectives are used after attach
   */
  gasneti_spawner->Cleanup();

#if GASNET_SEGMENT_EVERYTHING
  GASNETI_SAFE_PROPAGATE( gasnetc_segment_register(NULL) );
#endif

  return GASNET_OK;
}
/* ------------------------------------------------------------------------------------ */
int gasnetc_segment_create_hook(gex_Segment_t e_segment)
{
  // Until we have key management for more than aux + client:
  static int count = 0;
  if (count > 1) { // Note that the aux seg is not counted here
    GASNETI_RETURN_ERRR(RESOURCE,"ofi-conduit does not support multiple user segments");
  }
  ++count;

  // Register the segment
  gasnetc_Segment_t segment = (gasnetc_Segment_t) gasneti_import_segment(e_segment);
  // TODO: non-fatal error handling:
  // When gasnetc_segment_register() returns non-zero, either it or this hook
  // must cleanup the conduit-specific state prior to returning any value other
  // than GASNET_OK.
  // Currently there is a leak of the registration created by `fi_mr_reg()`, which
  // is inconsequential in practice until multi-EP support is added.
  return gasnetc_segment_register(segment);
}

void gasnetc_segment_destroy_hook(gasneti_Segment_t i_segment)
{
  gasneti_assert_zeroret( gasnetc_segment_deregister((gasnetc_Segment_t) i_segment) );
}

int gasnetc_segment_attach_hook(gex_Segment_t e_segment, gex_TM_t e_tm)
{
#if GASNET_SEGMENT_FAST || GASNET_SEGMENT_LARGE
  // Register the segment
  int rc = gasnetc_segment_create_hook(e_segment);
  if (rc) return rc;

  // Exchange memory keys
  gex_EP_t e_ep = gex_TM_QueryEP(e_tm);
  gasnetc_segment_exchange(e_tm, &e_ep, 1);
#else
  // Everything was completed in gasnetc_attach_primary()
#endif

  return GASNET_OK;
}
/* ------------------------------------------------------------------------------------ */
// TODO-EX: this is a candidate for factorization (once we understand the per-conduit variations)
extern int gasnetc_Client_Init(
                               gex_Client_t            *client_p,
                               gex_EP_t                *ep_p,
                               gex_TM_t                *tm_p,
                               const char              *clientName,
                               int                     *argc,
                               char                    ***argv,
                               gex_Flags_t             flags)
{
  gasneti_assert(client_p);
  gasneti_assert(ep_p);
  gasneti_assert(tm_p);
  gasneti_assert(clientName);
#if !GASNET_NULL_ARGV_OK
  gasneti_assert(argc);
  gasneti_assert(argv);
#endif

  //  main init
  // TODO-EX: must split off per-client and per-endpoint portions
  if (!gasneti_init_done) {
    int retval = gasnetc_init(argc, argv, flags);
    if (retval != GASNET_OK) GASNETI_RETURN(retval);
  #if 0
    /* called within gasnetc_init to allow init tracing */
    gasneti_trace_init(argc, argv);
  #endif
  }

  // Do NOT move this prior to the gasneti_trace_init() call
  GASNETI_TRACE_PRINTF(O,("gex_Client_Init: name='%s' argc_p=%p argv_p=%p flags=%d",
                          clientName, (void *)argc, (void *)argv, flags));

  //  allocate the client object
  gasneti_Client_t client = gasneti_alloc_client(clientName, flags);
  *client_p = gasneti_export_client(client);

  //  create the initial endpoint with internal handlers
  if (gex_EP_Create(ep_p, *client_p, GEX_EP_CAPABILITY_ALL, flags))
    GASNETI_RETURN_ERRR(RESOURCE,"Error creating initial endpoint");
  gasneti_EP_t ep = gasneti_import_ep(*ep_p);
  gasnetc_handler = ep->_amtbl; // TODO-EX: this global variable to be removed

  // TODO-EX: create team
  gasneti_TM_t tm = gasneti_alloc_tm(ep, gasneti_mynode, gasneti_nodes, flags);
  *tm_p = gasneti_export_tm(tm);

  if (0 == (flags & GASNETI_FLAG_INIT_LEGACY)) {
    /*  primary attach  */
    if (GASNET_OK != gasnetc_attach_primary())
      GASNETI_RETURN_ERRR(RESOURCE,"Error in primary attach");

    /* ensure everything is initialized across all nodes */
    gasnet_barrier(0, GASNET_BARRIERFLAG_UNNAMED);
  }

  return GASNET_OK;
}

extern int gasnetc_ep_publishboundsegment_hook(
                gex_TM_t               tm,
                gex_EP_t               *eps,
                size_t                 num_eps,
                gex_Flags_t            flags)
{
  // Conduit-dependent parts
  // TODO: merge comms into gasneti_EP_PublishBoundSegment().
  gasnetc_segment_exchange(tm, eps, num_eps);

  // Avoid race in which AMRequestLong triggers AMRepyLong before exchange completes remotely
  // TODO: barrier for multi-tm per-process
  gex_Event_Wait(gex_Coll_BarrierNB(tm, 0));

  return GASNET_OK;
}
/* ------------------------------------------------------------------------------------ */
int gasnetc_exit_in_progress = 0;

static gasneti_atomic_t gasnetc_exit_code = gasneti_atomic_init(0);     /* value to _exit() with */

static const char * volatile gasnetc_exit_state = "UNKNOWN STATE";

// NOTE: Please keep GASNETC_EXIT_STATE_MAXLEN fairly "tight" to bound the
// volume of garbage that might get printed in the event of memory corruption.
#define GASNETC_EXIT_STATE_MAXLEN 40

#if GASNET_DEBUG_VERBOSE
  #define GASNETC_TRACE_EXIT_STATE() do {                 \
        fprintf(stderr, "%d> EXIT STATE %s\n",            \
                (int)gasneti_mynode, gasnetc_exit_state); \
        fflush(NULL);                                     \
  } while (0)
#else
  #define GASNETC_TRACE_EXIT_STATE() ((void)0)
#endif

#define GASNETC_EXIT_STATE(st) do {                                      \
        gasneti_static_assert(sizeof(st) <= GASNETC_EXIT_STATE_MAXLEN+1);\
        gasnetc_exit_state = st;                                         \
        GASNETC_TRACE_EXIT_STATE();                                      \
  } while (0)

// TODO-EX: is this really necessary?
extern void gasnetc_exit_cautious(int exitcode) {
  if (!gasnetc_exit_in_progress) gasnetc_exit(exitcode);
}

static int gasnetc_exit_init(void) {
  gasnetc_exittimeout = gasneti_get_exittimeout(GASNETC_DEFAULT_EXITTIMEOUT_MAX,
                                                GASNETC_DEFAULT_EXITTIMEOUT_MIN,
                                                GASNETC_DEFAULT_EXITTIMEOUT_FACTOR,
                                                GASNETC_DEFAULT_EXITTIMEOUT_MIN);
  // register process exit-time hook
  gasneti_registerExitHandler(gasnetc_exit_cautious);

  return GASNET_OK;
}

#if HAVE_ON_EXIT
static void gasnetc_on_exit(int exitcode, void *arg) {
  if (!gasnetc_exit_in_progress)
    gasnetc_exit(exitcode);
}
#else
static void gasnetc_atexit(void) {
  if (!gasnetc_exit_in_progress)
    gasnetc_exit(0);
}
#endif

/* This signal handler is for a last-ditch exit when a signal arrives while
 * attempting the graceful exit.  That includes SIGALRM if we get wedged.
 * DOES NOT RETURN
 */
static void gasnetc_exit_sighandler(int sig) {
  int exitcode = (int)gasneti_atomic_read(&gasnetc_exit_code, 0);
  static gasneti_atomic_t once = gasneti_atomic_init(1);

#if GASNET_DEBUG
  // protect until we reach reentrance check
  gasneti_reghandler(SIGALRM, _exit);
  gasneti_unblocksig(SIGALRM);
  alarm(30);
#endif

  const char * state = gasnetc_exit_state;
  size_t state_len = gasneti_strnlen(state, GASNETC_EXIT_STATE_MAXLEN);

  /* note - can't call trace macros here, or even sprintf */
  if (sig == SIGALRM) {
    static const char msg[] = "gasnet_exit(): WARNING: timeout during exit... goodbye.  [";
    (void) write(STDERR_FILENO, msg, sizeof(msg) - 1);
    (void) write(STDERR_FILENO, state, state_len);
    (void) write(STDERR_FILENO, "]\n", 2);
  } else {
    static const char msg1[] = "gasnet_exit(): ERROR: signal ";
    static const char msg2[] = " received during exit... goodbye.  [";
    char digit;

    (void) write(STDERR_FILENO, msg1, sizeof(msg1) - 1);

    /* assume sig < 100 */
    if (sig > 9) {
      digit = '0' + ((sig / 10) % 10);
      (void) write(STDERR_FILENO, &digit, 1);
    }
    digit = '0' + (sig % 10);
    (void) write(STDERR_FILENO, &digit, 1);

    (void) write(STDERR_FILENO, msg2, sizeof(msg2) - 1);
    (void) write(STDERR_FILENO, state, state_len);
    (void) write(STDERR_FILENO, "]\n", 2);
  }
  (void) fsync(STDERR_FILENO);

  if (gasneti_atomic_decrement_and_test(&once, 0)) {
    /* We ask the bootstrap support to kill us, but only once */
    GASNETC_EXIT_STATE("in suicide timer");
    gasneti_reghandler(SIGALRM, gasnetc_exit_sighandler);
    gasneti_unblocksig(SIGALRM);
    alarm(MAX(5,gasnetc_exittimeout));
    gasneti_bootstrapAbort(exitcode);
  } else {
    gasneti_killmyprocess(exitcode);
    gasneti_reghandler(SIGABRT, SIG_DFL);
    gasneti_fatalerror("gasnetc_exit aborting...");
  }

  /* NOT REACHED */
}

/* AM Handlers for exit handling */
static void gasnetc_noop(void) { return; }
static gasneti_atomic_t gasnetc_exit_dist = gasneti_atomic_init(0);     /* OR of reduce distances */
static void gasnetc_exit_reqh(gex_Token_t token,
                              gex_AM_Arg_t arg0,
                              gex_AM_Arg_t arg1) {
  gasneti_atomic_val_t distance = arg1;
#if defined(GASNETI_HAVE_ATOMIC_ADD_SUB)
  /* atomic OR via ADD since no bit will be set more than once */
  gasneti_assert(GASNETI_POWEROFTWO(distance));
  gasneti_atomic_add(&gasnetc_exit_dist, distance, GASNETI_ATOMIC_REL);
#elif defined(GASNETI_HAVE_ATOMIC_CAS)
  /* atomic OR via C-A-S */
  uint32_t old_val;
  do {
    old_val = gasneti_atomic_read(&gasnetc_exit_dist, 0);
  } while (!gasneti_atomic_compare_and_swap(&gasnetc_exit_dist,
                                            old_val, old_val|distance,
                                            GASNETI_ATOMIC_REL));
#else
  #error "required atomic compare-and-swap is not yet implemented for your CPU/OS/compiler"
#endif

  if (!gasnetc_exit_in_progress)
    gasnetc_exit((int)arg0);
}

/* Coordinate a global exit, returning non-zero on success */
static int gasnetc_exit_coordinate(int exitcode) {
  GASNET_BEGIN_FUNCTION(); // OK - not a critical-path

  /* Disable processing of user's AMs, to avoid reentrance if user's handler exits */
  for (int i = GASNETE_HANDLER_BASE; i < GASNETC_MAX_NUMHANDLERS; ++i) {
    gasnetc_handler[i].gex_fnptr = (gex_AM_Fn_t)&gasnetc_noop;
  }

  // prevent possible GASNETI_CHECK_INJECT() failures when we communicate
  GASNETI_CHECK_INJECT_RESET();

  /* Coordinate using dissemination-pattern, with timeout.
   * lg(N) rounds each of which sends and recvs 1 AM
   */
  const uint64_t timeout_ns = gasnetc_exittimeout * 1000000000L;
  const gasneti_tick_t t_start = gasneti_ticks_now();
  for (int distance = 1; distance < gasneti_nodes; distance *= 2) {
    gex_Rank_t jobrank = (gasneti_mynode + distance) % gasneti_nodes;
    int ret = gasnetc_AMRequestShortM(gasneti_THUNK_TM, jobrank, gasneti_handleridx(gasnetc_exit_reqh),
                                      0 GASNETI_THREAD_PASS, 2, exitcode, distance);
    if (ret != GASNET_OK) return 0;
    do { /* wait for completion of the proper receive, which might arrive out of order */
      if (timeout_ns < gasneti_ticks_to_ns(gasneti_ticks_now() - t_start)) return 0;
      gasnetc_AMPoll(GASNETI_THREAD_PASS_ALONE);
    } while (!(distance & gasneti_atomic_read(&gasnetc_exit_dist, 0)));
  }
  return 1;
}

extern void gasnetc_exit(int exitcode) {
  gasnetc_exit_in_progress = 1;
  gasneti_atomic_set(&gasnetc_exit_code, exitcode, GASNETI_ATOMIC_REL);

  /* once we start a shutdown, ignore all future SIGQUIT signals or we risk reentrancy */
  gasneti_reghandler(SIGQUIT, SIG_IGN);

  {  /* ensure only one thread ever continues past this point */
    static gasneti_mutex_t exit_lock = GASNETI_MUTEX_INITIALIZER;
    gasneti_mutex_lock(&exit_lock);
  }

  GASNETI_TRACE_PRINTF(C,("gasnet_exit(%i)\n", exitcode));

  /* Establish a last-ditch signal handler in case of failure. */
  gasneti_reghandler(SIGALRM, gasnetc_exit_sighandler);
  #if GASNET_DEBUG
    gasneti_reghandler(SIGABRT, SIG_DFL);
  #else
    gasneti_reghandler(SIGABRT, gasnetc_exit_sighandler);
  #endif
  gasneti_reghandler(SIGILL,  gasnetc_exit_sighandler);
  gasneti_reghandler(SIGSEGV, gasnetc_exit_sighandler);
  gasneti_reghandler(SIGFPE,  gasnetc_exit_sighandler);
  gasneti_reghandler(SIGBUS,  gasnetc_exit_sighandler);

  /* Prior to attach we cannot send AMs to coordinate the exit */
  if (! gasneti_attach_done) {
    GASNETC_EXIT_STATE("in pre-attach gasneti_bootstrapAbort()");
    fprintf(stderr, "WARNING: GASNet ofi-conduit may not shutdown cleanly when gasnet_exit() is called before gasnet_attach()\n");
    gasneti_bootstrapAbort(exitcode);
    gasneti_killmyprocess(exitcode);
  }

  const unsigned int timeout = (unsigned int)gasnetc_exittimeout;

  // One alarm timer for the exit coordination
  // +2 is margin of safety around the timed coordination
  GASNETC_EXIT_STATE("coordinating shutdown");
  alarm(2 + timeout);
  int graceful = gasnetc_exit_coordinate(exitcode);

  // A second alarm timer for most of the remaining exit steps
  // TODO: 120 is arbitrary and hard-coded
  alarm(MAX(120, timeout));
  if (graceful) {
    GASNETC_EXIT_STATE("draining network");
    { GASNET_BEGIN_FUNCTION(); // OK - not a critical-path
      gex_Event_t *events = NULL;
      size_t count;
      gasneti_finalize_all_nbi_ff(&events, &count GASNETI_THREAD_PASS);
      if (count) { // bounded polling (upto 25% of the total timeout) to drain any nbi_ff operations
        const uint64_t timeout_ns = (timeout * 1000000000L) / 4;
        const gasneti_tick_t t_start = gasneti_ticks_now();
        gasneti_polluntil((gasneti_ticks_to_ns(gasneti_ticks_now() - t_start) > timeout_ns) ||
                          (GASNET_ERR_NOT_READY != gasnete_test_all(events, count GASNETI_THREAD_PASS)));
      }
      gasneti_free(events);
    }

    GASNETC_EXIT_STATE("in gasnetc_ofi_exit()");
    gasnetc_ofi_exit();
  }
  GASNETC_EXIT_STATE("flushing output");
  gasneti_flush_streams();
  gasneti_trace_finish();
  gasneti_sched_yield();

  // One last alarm to cover the Fini or Abort
  // This has been observed to be the slowest step in some cases (see bug 4360)
  // TODO: 30 is arbitrary and hard-coded
  alarm(MAX(30, timeout));
  if (graceful) {
    GASNETC_EXIT_STATE("in gasneti_bootstrapFini()");
    gasneti_bootstrapFini();
  } else {
    GASNETC_EXIT_STATE("in gasneti_bootstrapAbort()");
    gasneti_bootstrapAbort(exitcode);
  }
  alarm(0);

  gasneti_killmyprocess(exitcode);
  gasneti_fatalerror("gasnetc_exit failed!");
}

/* ------------------------------------------------------------------------------------ */
/*
  Misc. Active Message Functions
  ==============================
*/
#if GASNET_PSHM
/* (###) GASNETC_GET_HANDLER
 *   If your conduit will support PSHM, then there needs to be a way
 *   for PSHM to see your handler table.  If you use the recommended
 *   implementation then you don't need to do anything special.
 *   Othwerwise, #define GASNETC_GET_HANDLER in gasnet_core_fwd.h and
 *   implement gasnetc_get_handler() as a macro in
 *   gasnet_core_internal.h
 *
 * (###) Tokens and "nbrhd" (loopback and PSHM):
 *   To permit conduit-specific tokens to co-exist with ones used by the
 *   conduit-independent implementation of AMs within the neighborhood,
 *   the nbrhd implementation produces tokens with the least-significant
 *   bit set (assuming the conduit never will).  This restricts the
 *   conduit's implemention of tokens, but allows the common choice in
 *   which tokens are pointers to a type with alignment greater than 1.
 */
#endif

extern gex_TI_t gasnetc_Token_Info(
                gex_Token_t         token,
                gex_Token_Info_t    *info,
                gex_TI_t            mask)
{
  gasneti_assert(token);
  gasneti_assert(info);

  if (gasnetc_token_in_nbrhd(token)) {
    return gasnetc_nbrhd_Token_Info(token, info, mask);
  }

  gex_TI_t result = 0;

  info->gex_srcrank = ((gasnetc_ofi_am_send_buf_t*)token)->sourceid;
  result |= GEX_TI_SRCRANK;

  info->gex_ep = gasneti_THUNK_EP;
  result |= GEX_TI_EP;

#if 0 // TODO-EX: implement these
  /* (###) add code here to write the address of the handle entry into info->gex_entry (optional) */
  info->gex_entry = ###;
  result |= GEX_TI_ENTRY;

  /* (###) add code here to set boolean "is a request" field info->gex_is_req (optional) */
  info->gex_is_req = real_token->u.generic.is_req;
  result |= GEX_TI_IS_REQ;

  /* (###) add code here to set boolean "is a long" field info->gex_is_long (optional) */
  info->gex_is_long = real_token->is_long;
  result |= GEX_TI_IS_LONG;
#endif

  return GASNETI_TOKEN_INFO_RETURN(result, info, mask);
}

extern int gasnetc_AMPoll(GASNETI_THREAD_FARG_ALONE) {
  int retval;
  GASNETI_CHECKATTACH();

#if GASNET_PSHM
  /* If your conduit will support PSHM, let it make progress here. */
  gasneti_AMPSHMPoll(0 GASNETI_THREAD_PASS);
#endif

  /* add code here to run your AM progress engine */
  /* should be a generic polling */
  gasnetc_ofi_poll();

  return GASNET_OK;
}

/* ------------------------------------------------------------------------------------ */
/*
  Active Message Request Functions
  ================================
*/

GASNETI_INLINE(gasnetc_AMRequestShort)
int gasnetc_AMRequestShort( gex_TM_t tm, gex_Rank_t rank, gex_AM_Index_t handler,
                            gex_Flags_t flags,
                            int numargs, va_list argptr GASNETI_THREAD_FARG)
{
  int retval;
  gex_Rank_t jobrank = gasneti_e_tm_rank_to_jobrank(tm, rank);
  if (GASNETI_NBRHD_JOBRANK_IS_LOCAL(jobrank)) {
    retval = gasnetc_nbrhd_RequestGeneric( gasneti_Short, jobrank, handler,
                                           0, 0, 0,
                                           flags, numargs, argptr GASNETI_THREAD_PASS);
  } else {
    retval = gasnetc_ofi_am_send_short(jobrank, handler, numargs, argptr, 1 GASNETI_THREAD_PASS);
    gasneti_assert(! retval); // TODO-EX: IMM AM support
  }
  return retval;
}

extern int gasnetc_AMRequestShortM(
                            gex_TM_t tm,/* local context */
                            gex_Rank_t rank,       /* with tm, defines remote context */
                            gex_AM_Index_t handler, /* index into destination endpoint's handler table */
                            gex_Flags_t flags
                            GASNETI_THREAD_FARG,
                            int numargs, ...) {
  GASNETI_COMMON_AMREQUESTSHORT(tm,rank,handler,flags,numargs);
  GASNETC_IMMEDIATE_MAYBE_POLL(flags); /* (###) poll at least once, to assure forward progress */

  va_list argptr;
  va_start(argptr, numargs); /*  pass in last argument */
  int retval = gasnetc_AMRequestShort(tm,rank,handler,flags,numargs,argptr GASNETI_THREAD_PASS);
  va_end(argptr);
  return retval;
}

GASNETI_INLINE(gasnetc_AMRequestMedium)
int gasnetc_AMRequestMedium(gex_TM_t tm, gex_Rank_t rank, gex_AM_Index_t handler,
                            void *source_addr, size_t nbytes,
                            gex_Event_t *lc_opt, gex_Flags_t flags,
                            int numargs, va_list argptr GASNETI_THREAD_FARG)
{
  int retval;
  gex_Rank_t jobrank = gasneti_e_tm_rank_to_jobrank(tm, rank);
  if (GASNETI_NBRHD_JOBRANK_IS_LOCAL(jobrank)) {
    gasneti_leaf_finish(lc_opt); // synchronous LC
    retval = gasnetc_nbrhd_RequestGeneric( gasneti_Medium, jobrank, handler,
                                           source_addr, nbytes, 0,
                                           flags, numargs, argptr GASNETI_THREAD_PASS);
  } else {
    gasneti_leaf_finish(lc_opt); // TODO-EX: async LC
    retval = gasnetc_ofi_am_send_medium(jobrank, handler, source_addr, nbytes, numargs, argptr, 1 GASNETI_THREAD_PASS);
    gasneti_assert(! retval); // TODO-EX: IMM AM support
  }
  return retval;
}

extern int gasnetc_AMRequestMediumV(
                            gex_TM_t tm, gex_Rank_t rank, gex_AM_Index_t handler,
                            void *source_addr, size_t nbytes,
                            gex_Event_t *lc_opt, gex_Flags_t flags,
                            int numargs, va_list argptr GASNETI_THREAD_FARG)
{
  return gasnetc_AMRequestMedium(tm,rank,handler,source_addr,nbytes,lc_opt,flags,numargs,argptr GASNETI_THREAD_PASS);
}

extern int gasnetc_AMRequestMediumM(
                            gex_TM_t tm,/* local context */
                            gex_Rank_t rank,       /* with tm, defines remote context */
                            gex_AM_Index_t handler, /* index into destination endpoint's handler table */
                            void *source_addr, size_t nbytes,   /* data payload */
                            gex_Event_t *lc_opt,       /* local completion of payload */
                            gex_Flags_t flags
                            GASNETI_THREAD_FARG,
                            int numargs, ...) {
  GASNETI_COMMON_AMREQUESTMEDIUM(tm,rank,handler,source_addr,nbytes,lc_opt,flags,numargs);
  GASNETC_IMMEDIATE_MAYBE_POLL(flags); /* (###) poll at least once, to assure forward progress */

  va_list argptr;
  va_start(argptr, numargs); /*  pass in last argument */
  int retval = gasnetc_AMRequestMedium(tm,rank,handler,source_addr,nbytes,lc_opt,flags,numargs,argptr GASNETI_THREAD_PASS);
  va_end(argptr);
  return retval;
}

GASNETI_INLINE(gasnetc_AMRequestLong)
int gasnetc_AMRequestLong(  gex_TM_t tm, gex_Rank_t rank, gex_AM_Index_t handler,
                            void *source_addr, size_t nbytes, void *dest_addr,
                            gex_Event_t *lc_opt, gex_Flags_t flags,
                            int numargs, va_list argptr GASNETI_THREAD_FARG)
{
  int retval;
  gex_Rank_t jobrank = gasneti_e_tm_rank_to_jobrank(tm, rank);
  if (GASNETI_NBRHD_JOBRANK_IS_LOCAL(jobrank)) {
    gasneti_leaf_finish(lc_opt); // synchronous LC
    retval = gasnetc_nbrhd_RequestGeneric( gasneti_Long, jobrank, handler,
                                           source_addr, nbytes, dest_addr,
                                           flags, numargs, argptr GASNETI_THREAD_PASS);
  } else {
    gasneti_leaf_finish(lc_opt); // TODO-EX: async LC
    retval = gasnetc_ofi_am_send_long(jobrank, handler, source_addr, nbytes, dest_addr, numargs, argptr, 1, 0 GASNETI_THREAD_PASS);
    gasneti_assert(! retval); // TODO-EX: IMM AM support
  }
  return retval;
}

extern int gasnetc_AMRequestLongV(
                            gex_TM_t tm, gex_Rank_t rank, gex_AM_Index_t handler,
                            void *source_addr, size_t nbytes, void *dest_addr,
                            gex_Event_t *lc_opt, gex_Flags_t flags,
                            int numargs, va_list argptr GASNETI_THREAD_FARG)
{
  return gasnetc_AMRequestLong(tm,rank,handler,source_addr,nbytes,dest_addr,lc_opt,flags,numargs,argptr GASNETI_THREAD_PASS);
}

extern int gasnetc_AMRequestLongM(
                            gex_TM_t tm,/* local context */
                            gex_Rank_t rank,       /* with tm, defines remote context */
                            gex_AM_Index_t handler, /* index into destination endpoint's handler table */
                            void *source_addr, size_t nbytes,   /* data payload */
                            void *dest_addr,                    /* data destination on destination node */
                            gex_Event_t *lc_opt,       /* local completion of payload */
                            gex_Flags_t flags
                            GASNETI_THREAD_FARG,
                            int numargs, ...) {
  GASNETI_COMMON_AMREQUESTLONG(tm,rank,handler,source_addr,nbytes,dest_addr,lc_opt,flags,numargs);
  GASNETC_IMMEDIATE_MAYBE_POLL(flags); /* (###) poll at least once, to assure forward progress */

  va_list argptr;
  va_start(argptr, numargs); /*  pass in last argument */
  int retval = gasnetc_AMRequestLong(tm,rank,handler,source_addr,nbytes,dest_addr,lc_opt,flags,numargs,argptr GASNETI_THREAD_PASS);
  va_end(argptr);
  return retval;
}

GASNETI_INLINE(gasnetc_AMReplyShort)
int gasnetc_AMReplyShort(   gex_Token_t token, gex_AM_Index_t handler,
                            gex_Flags_t flags,
                            int numargs, va_list argptr)
{
  int retval;
  if_pt (gasnetc_token_in_nbrhd(token)) {
    retval = gasnetc_nbrhd_ReplyGeneric( gasneti_Short, token, handler,
                                         0, 0, 0,
                                         flags, numargs, argptr);
  } else {
    GASNET_BEGIN_FUNCTION(); // TODO-EX: stash threadinfo in token
    retval = gasnetc_ofi_am_send_short(((gasnetc_ofi_am_send_buf_t*)token)->sourceid, handler, numargs, argptr, 0 GASNETI_THREAD_PASS);
    gasneti_assert(! retval); // TODO-EX: IMM AM support
  }
  return retval;
}

extern int gasnetc_AMReplyShortM(
                            gex_Token_t token,     /* token provided on handler entry */
                            gex_AM_Index_t handler, /* index into destination endpoint's handler table */
                            gex_Flags_t flags,
                            int numargs, ...) {
  GASNETI_COMMON_AMREPLYSHORT(token,handler,flags,numargs);

  va_list argptr;
  va_start(argptr, numargs); /*  pass in last argument */
  int retval = gasnetc_AMReplyShort(token,handler,flags,numargs,argptr);
  va_end(argptr);
  return retval;
}

GASNETI_INLINE(gasnetc_AMReplyMedium)
int gasnetc_AMReplyMedium(  gex_Token_t token, gex_AM_Index_t handler,
                            void *source_addr, size_t nbytes,
                            gex_Event_t *lc_opt, gex_Flags_t flags,
                            int numargs, va_list argptr)
{
  int retval;
  if_pt (gasnetc_token_in_nbrhd(token)) {
    gasneti_leaf_finish(lc_opt); // synchronous LC
    retval = gasnetc_nbrhd_ReplyGeneric( gasneti_Medium, token, handler,
                                         source_addr, nbytes, 0,
                                         flags, numargs, argptr);
  } else {
    gasneti_leaf_finish(lc_opt); // TODO-EX: async LC
    GASNET_BEGIN_FUNCTION(); // TODO-EX: stash threadinfo in token
    retval = gasnetc_ofi_am_send_medium(((gasnetc_ofi_am_send_buf_t*)token)->sourceid, handler, source_addr, nbytes, numargs, argptr, 0 GASNETI_THREAD_PASS);
    gasneti_assert(! retval); // TODO-EX: IMM AM support
  }
  return retval;
}

extern int gasnetc_AMReplyMediumV(
                            gex_Token_t token, gex_AM_Index_t handler,
                            void *source_addr, size_t nbytes,
                            gex_Event_t *lc_opt, gex_Flags_t flags,
                            int numargs, va_list argptr)
{
  return gasnetc_AMReplyMedium(token,handler,source_addr,nbytes,lc_opt,flags,numargs,argptr);
}

extern int gasnetc_AMReplyMediumM(
                            gex_Token_t token,     /* token provided on handler entry */
                            gex_AM_Index_t handler, /* index into destination endpoint's handler table */
                            void *source_addr, size_t nbytes,   /* data payload */
                            gex_Event_t *lc_opt,       /* local completion of payload */
                            gex_Flags_t flags,
                            int numargs, ...) {
  GASNETI_COMMON_AMREPLYMEDIUM(token,handler,source_addr,nbytes,lc_opt,flags,numargs);

  va_list argptr;
  va_start(argptr, numargs); /*  pass in last argument */
  int retval = gasnetc_AMReplyMedium(token,handler,source_addr,nbytes,lc_opt,flags,numargs,argptr);
  va_end(argptr);
  return retval;
}

GASNETI_INLINE(gasnetc_AMReplyLong)
int gasnetc_AMReplyLong(    gex_Token_t token, gex_AM_Index_t handler,
                            void *source_addr, size_t nbytes, void *dest_addr,
                            gex_Event_t *lc_opt, gex_Flags_t flags,
                            int numargs, va_list argptr)
{
  int retval;
  if_pt (gasnetc_token_in_nbrhd(token)) {
    gasneti_leaf_finish(lc_opt); // synchronous LC
    retval = gasnetc_nbrhd_ReplyGeneric( gasneti_Long, token, handler,
                                         source_addr, nbytes, dest_addr,
                                         flags, numargs, argptr);
  } else {
    gasneti_leaf_finish(lc_opt); // TODO-EX: async LC
    GASNET_BEGIN_FUNCTION(); // TODO-EX: stash threadinfo in token
    retval = gasnetc_ofi_am_send_long(((gasnetc_ofi_am_send_buf_t*)token)->sourceid, handler, source_addr, nbytes, dest_addr, numargs, argptr, 0, 0 GASNETI_THREAD_PASS);
    gasneti_assert(! retval); // TODO-EX: IMM AM support
  }
  return retval;
}

extern int gasnetc_AMReplyLongV(
                            gex_Token_t token, gex_AM_Index_t handler,
                            void *source_addr, size_t nbytes, void *dest_addr,
                            gex_Event_t *lc_opt, gex_Flags_t flags,
                            int numargs, va_list argptr)
{
  return gasnetc_AMReplyLong(token,handler,source_addr,nbytes,dest_addr,lc_opt,flags,numargs,argptr);
}

extern int gasnetc_AMReplyLongM(
                            gex_Token_t token,     /* token provided on handler entry */
                            gex_AM_Index_t handler, /* index into destination endpoint's handler table */
                            void *source_addr, size_t nbytes,   /* data payload */
                            void *dest_addr,                    /* data destination on destination node */
                            gex_Event_t *lc_opt,       /* local completion of payload */
                            gex_Flags_t flags,
                            int numargs, ...) {
  GASNETI_COMMON_AMREPLYLONG(token,handler,source_addr,nbytes,dest_addr,lc_opt,flags,numargs);

  va_list argptr;
  va_start(argptr, numargs); /*  pass in last argument */
  int retval = gasnetc_AMReplyLong(token,handler,source_addr,nbytes,dest_addr,lc_opt,flags,numargs,argptr);
  va_end(argptr);
  return retval;
}

/* ------------------------------------------------------------------------------------ */
/*
  Handler-safe locks
  ==================
*/
#if !GASNETC_NULL_HSL
extern void gasnetc_hsl_init   (gex_HSL_t *hsl) {
  GASNETI_CHECKATTACH();
  gasneti_mutex_init(&(hsl->lock));
}

extern void gasnetc_hsl_destroy(gex_HSL_t *hsl) {
  GASNETI_CHECKATTACH();
  gasneti_mutex_destroy(&(hsl->lock));
}

extern void gasnetc_hsl_lock   (gex_HSL_t *hsl) {
  GASNETI_CHECKATTACH();

  {
    #if GASNETI_STATS_OR_TRACE
      gasneti_tick_t startlock = GASNETI_TICKS_NOW_IFENABLED(L);
    #endif
    #if GASNETC_HSL_SPINLOCK
      if_pf (gasneti_mutex_trylock(&(hsl->lock)) == EBUSY) {
        if (gasneti_wait_mode == GASNET_WAIT_SPIN) {
          while (gasneti_mutex_trylock(&(hsl->lock)) == EBUSY) {
            gasneti_spinloop_hint();
          }
        } else {
          gasneti_mutex_lock(&(hsl->lock));
        }
      }
    #else
      gasneti_mutex_lock(&(hsl->lock));
    #endif
    #if GASNETI_STATS_OR_TRACE
      hsl->acquiretime = GASNETI_TICKS_NOW_IFENABLED(L);
      GASNETI_TRACE_EVENT_TIME(L, HSL_LOCK, hsl->acquiretime-startlock);
    #endif
  }
}

extern void gasnetc_hsl_unlock (gex_HSL_t *hsl) {
  GASNETI_CHECKATTACH();

  GASNETI_TRACE_EVENT_TIME(L, HSL_UNLOCK, GASNETI_TICKS_NOW_IFENABLED(L)-hsl->acquiretime);

  gasneti_mutex_unlock(&(hsl->lock));
}

extern int  gasnetc_hsl_trylock(gex_HSL_t *hsl) {
  GASNETI_CHECKATTACH();

  {
    int locked = (gasneti_mutex_trylock(&(hsl->lock)) == 0);

    GASNETI_TRACE_EVENT_VAL(L, HSL_TRYLOCK, locked);
    if (locked) {
      #if GASNETI_STATS_OR_TRACE
        hsl->acquiretime = GASNETI_TICKS_NOW_IFENABLED(L);
      #endif
    }

    return locked ? GASNET_OK : GASNET_ERR_NOT_READY;
  }
}
#endif
/* ------------------------------------------------------------------------------------ */
/*
  Private Handlers:
  ================
  see mpi-conduit and extended-ref for examples on how to declare AM handlers here
  (for internal conduit use in bootstrapping, job management, etc.)
*/
static gex_AM_Entry_t const gasnetc_handlers[] = {
  GASNETC_COMMON_HANDLERS(),

  /* ptr-width independent handlers */
  gasneti_handler_tableentry_no_bits(gasnetc_exit_reqh,2,REQUEST,SHORT,0),

  /* ptr-width dependent handlers */
  // none

  GASNETI_HANDLER_EOT
};

gex_AM_Entry_t const *gasnetc_get_handlertable(void) {
  return gasnetc_handlers;
}

/* ------------------------------------------------------------------------------------ */
