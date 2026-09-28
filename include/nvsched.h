typedef struct NvScheduler NvScheduler;
typedef struct NvSched NvSched;
typedef struct NvClock NvClock;
typedef struct NvIO NvIO;
typedef struct NvMemstats NvMemstats;

/* Requested storage at an explicit quiescent snapshot, NOT allocator or
 * OS resident bytes. Used counts are diagnostic subsets, not extra storage. */
struct NvMemstats {
	uvlong tablebytes;
	uvlong execbytes;
	uvlong heapbytes;    /* chunk descriptors plus reserved word capacity */
	uvlong heapused;     /* used chunk words in bytes, excludes adoption */
	uvlong stackbytes;   /* retained capacity */
	uvlong stackused;
	uvlong adoptedbytes;
	uvlong mailboxbytes;
	uvlong reportbytes;  /* rootvalue/lastexit fragments */
	uvlong totalbytes;
	uvlong nexec;
	uvlong nadopted;
	uvlong nmailbox;
	/*
	 * D074: a process whose heap is under off-process collection is
	 * skipped entirely rather than read racily (its heap.cur/full/
	 * adopted may be actively rewritten by the collector proc); this
	 * counts how many were skipped, so a snapshot taken during heavy
	 * offload is honestly partial rather than silently wrong.
	 */
	uvlong ncollecting;
};

/*
 * D050: the scheduler reads time only through this interface, never a
 * native clock call reachable from bytecode. The production clock uses
 * `uptime` from time(2), monotonic nanoseconds since boot; `nsec` is
 * wrong here because it reports settable wall-clock time. `wait` blocks
 * until `deadline`, so an idle production scheduler sleeps rather than
 * polling; a deterministic test clock jumps instead and never sleeps.
 */
struct NvClock {
	void *aux;
	uvlong (*now)(void *);
	void (*wait)(void *, uvlong);
};

/*
 * D054-D057: optional host output streams. Unlike NvClock, there is no
 * default: leaving both nil (the zero value nvschedinit installs) means
 * print/eprint fault bad_process_context under this scheduler, exactly as
 * they do with no host at all. The scheduler's single shared host table
 * (D065) checks these at call time, so nvschedsetio may be called before
 * or after spawning.
 */
struct NvIO {
	Biobuf *out;
	Biobuf *err;
};

enum {
	NvSchedProgress,
	NvSchedIdle,
	NvSchedDone,
	NvSchedError,
};

enum {
	NvRootRunning,
	NvRootDone,
	NvRootFault,
	NvRootExit,
};

/* D085: why the machine stopped; NvScheduler.stopping. */
enum {
	NvStopNone,
	NvStopDone,		/* nlive reached 0 */
	NvStopIdle,		/* every scheduler idle, nothing runnable, no timer, no collector: D046 deadlock */
	NvStopError,		/* a scheduler reported NvSchedError; text in stoperr */
	NvStopInterrupt,	/* nvmachineinterrupt */
};

/*
 * D087 (M10-T00b): the per-scheduler-proc half of the old NvScheduler.
 * One NvSched per scheduler proc; at N=1 there is exactly one,
 * sched[0], and every existing entry point (nvschedstep and friends)
 * operates on it. Everything here is touched by one scheduler proc
 * only (or, for gcsem/gchold, by that proc and the collector children
 * it forks). It is malloc'd by nvschedinit, never embedded: the D074
 * lesson is that rfork(RFPROC|RFMEM) does not share a caller's stack,
 * and NvScheduler is stack-allocated by every caller. The D088
 * counters beyond those already here and the idle semaphore arrive in
 * later T00b/T01 steps. Nothing here changes behavior at N=1; every
 * -s figure keeps its meaning.
 */
struct NvSched {
	int index;		/* position in NvScheduler.sched; 0 at N=1 */
	/*
	 * D083/D087: this scheduler's FIFO of Prrunnable slots, the queue
	 * every process it owns is enqueued on. Points at runtime.runq[index]
	 * (malloc'd and owned by NvRuntime, see nvproc.h); process.c reaches
	 * the same queue through NvProcess.owner.
	 */
	NvRunq *runq;
	ulong currentslot;
	ulong currentgeneration;
	int currentvalid;
	uvlong dispatches;
	uvlong reductions;
	uvlong timerwakes;	/* D088 "deadline fires" */
	/*
	 * D088 measurement counters, per scheduler proc, aggregated by -s
	 * at exit and never incremented through a shared word. Arrivals on
	 * this scheduler's own queue are counted at the queue
	 * (runq->enqueues). The rest have no event to count until M10-T01
	 * and stay 0 here: remoteenq (enqueues this scheduler made onto
	 * another scheduler's queue), stealstaken/stealsgiven (processes
	 * this scheduler took from, or lost to, another), wakessent/
	 * wakesrecv (semrelease sent to another scheduler / tsemacquire
	 * returns that were a wake rather than a timeout), sleeps/sleepns
	 * (idle tsemacquire waits and their total duration), lockacq/
	 * lockwaitns (stage-1 global lock acquisitions and time spent
	 * waiting for it). Printed from the first T00b build so the -s
	 * row's shape is settled before any of them can move.
	 */
	uvlong remoteenq;
	uvlong stealstaken;
	uvlong stealsgiven;
	uvlong wakessent;
	uvlong wakesrecv;
	uvlong sleeps;
	uvlong sleepns;
	uvlong lockacq;
	uvlong lockwaitns;
	/*
	 * D085 (M10-T01): the idle semaphore, malloc'd like gcsem and for the
	 * same reason; a remote waker semrelease's it when this scheduler's
	 * idle bit is set. wakeat/wakeatvalid are what step() leaves behind
	 * when it returns NvSchedIdle at N>1 instead of blocking: the
	 * earliest deadline this scheduler owns, so the proc loop can bound
	 * its sleep, and so termination detection knows this idle scheduler
	 * still has a timer pending. Meaningless while the scheduler is
	 * running; rewritten on every idle return.
	 */
	long *sem;
	uvlong wakeat;
	int wakeatvalid;
	uvlong collections;
	uvlong gcfailed;
	uvlong lastlivewords;	/* heap words in the most recently collected process */
	uvlong maxlivewords;	/* largest successful per-process live sample */
	uvlong gcinputwords;	/* used/adopted words presented to all attempts */
	uvlong gcoutputwords;	/* live words from successful collections */
	uvlong gcdemand;
	uvlong gcidle;
	/*
	 * D074: off-process collection bookkeeping. gcoutstanding is the
	 * number of heaps currently NvHeapCollecting under a launched
	 * collector proc whose completion this scheduler has not yet
	 * folded; it is the only new cross-cutting state a collector's
	 * existence adds here. gcofffallback counts a failed rfork that
	 * fell back to an immediate inline collection in the parent,
	 * distinct from a deliberate policy choice to collect inline.
	 * gcsem is the malloc'd completion semaphore a launched collector
	 * child semrelease's; gchold is a malloc'd test-only hold point (see
	 * nvschedgchold below). Both must be malloc'd, not plain fields:
	 * rfork(RFPROC|RFMEM) shares only data/bss, never a caller's stack
	 * segment, and NvScheduler is stack-allocated by every caller
	 * (cmd/nervous/main.c, every test's main), so a word the collector
	 * child writes or semrelease's must live outside the NvScheduler
	 * struct itself or the child's writes land in its own private copy
	 * of the parent's stack and are never observed.
	 */
	uvlong gcoutstanding;
	uvlong gcofffallback;
	long *gcsem;
	long *gchold;
	/*
	 * D074 amendment (R3-F06): every successfully forked collector
	 * child semrelease's gcsem exactly once, as its last touch of
	 * shared memory, *after* publishing idle -- so a fold that observed
	 * idle proves the heap is free of the child but not that the child
	 * is finished with gcsem. gclaunched counts successful forks (the
	 * rfork-failure fallback is not one) and gccredits counts the
	 * semaphore credits this scheduler has consumed; nvschedfree may
	 * free gcsem only once they are equal. Scheduler-proc-only fields,
	 * so they may live in the struct.
	 */
	uvlong gclaunched;
	uvlong gccredits;
	/*
	 * D074 amendment/M08-T04d "Test determinism": a second test-only
	 * seam, nil (no-op) in production like gchold above but structurally
	 * different from it -- this one is a plain function-pointer field,
	 * not a malloc'd shared word, because it is called and only ever
	 * touched by the scheduler proc itself, never by a forked collector
	 * child (RFMEM sharing is irrelevant here). Called exactly once,
	 * inside nvschedstep, immediately after finddispatchable reports
	 * nothing currently dispatchable and before the idle sweep or
	 * gcfoldall run. It exists because the false-idle bug this amendment
	 * fixed (r->nrunnable != 0 check above gcoutstanding==0) depends on
	 * every currently-outstanding off-process collector completing
	 * between finddispatchable's per-slot scan and gcfoldall's scan --
	 * two adjacent, fast, in-process calls with no syscall or existing
	 * hold point between them, a window nvschedgchold's park-in-the-
	 * child design cannot pin (releasing it only controls when a real
	 * collector CHILD resumes, not this scheduler-side interleaving).
	 * A test installs this hook to flip every outstanding exec's heap to
	 * idle-with-success directly, under its own lock, from the
	 * scheduler proc -- exactly what a real collector child would have
	 * published, just done without needing one to actually race for it.
	 * See tests/memory/offloadtest.c's holdfalseidle, the one test that
	 * sets this field.
	 */
	void (*gcidlestep)(NvScheduler *);
	int profile;		/* opt-in real monotonic elapsed timing, default off */
	uvlong execns;		/* includes host callbacks and nested spawn time */
	uvlong gcns;		/* demand + idle collection, excludes startup */
	uvlong spawnns;		/* all spawn attempts; overlaps execns for bytecode spawn */
};

/*
 * D064: lastexit and rootvalue are fragments owned by the scheduler
 * (taken from the exiting NvExec), freed by nvschedfree. rootvalue is
 * the root's return value after NvRootDone and its exit reason after
 * NvRootExit; nil otherwise.
 *
 * D087: this is the shared "machine" half -- state every scheduler proc
 * reads (runtime, module, host table, clock, io, limits) plus the root
 * tracking and completion totals that are aggregated, not per proc.
 * Per-proc state lives in sched[i] (see NvSched above); nvschedinit
 * allocates sched[0..nsched-1] and nvschedfree releases them.
 */
struct NvScheduler {
	NvRuntime runtime;
	NvModule *module;
	NvExecHost host;	/* D065: one table shared by every process's NvExec */
	NvClock clock;
	NvIO io;
	uvlong quantum;
	NvSched **sched;	/* D087: malloc'd array (NvMaxsched entries) of malloc'd per-proc state */
	int nsched;		/* 1 from nvschedinit; nvschedsetnsched raises it */
	/*
	 * D081 stage 1 (M10-T01): the one runtime lock. Held for every
	 * mutation of the process table, a mailbox, a run queue, a deadline
	 * or the ref counter, and for every table scan step() makes;
	 * released for the whole of nvexecrun, and never held across
	 * clock.wait, tsemacquire, rfork or Bflush. It is a plain field,
	 * not a pointer: nvmachinerun requires the NvScheduler itself to be
	 * malloc'd (its runtime is shared state too), and the N=1 fixtures
	 * that keep a stack NvScheduler never fork a scheduler proc, so a
	 * stack QLock is fine there. Taken at N=1 as well, so the N=1
	 * suites exercise the discipline (a QLock is not recursive: a
	 * nested take deadlocks visibly instead of racing invisibly).
	 * iolock (D086) serializes one print/eprint line and is never held
	 * with lock.
	 */
	QLock lock;
	QLock iolock;
	/*
	 * Whether lockrt actually takes `lock`. Set by nvschedsetnsched for
	 * N>1 and by nvschedsetlocking (tests). Measured on M10-T01's first
	 * landing: taking the uncontended lock at N=1 (7.4 acquisitions per
	 * ring hop) cost 13-17% of wall time on message-heavy shapes, so
	 * N=1 skips it; a deterministic fixture that wants the nested-take
	 * check turns it on explicitly.
	 */
	int locking;
	/*
	 * D085: idle/termination state, all under lock. idlemask bit i is
	 * set while scheduler i is between "found nothing to do" and "woke
	 * from its sleep"; nidle is its population count. stopping is one of
	 * the NvStop values below once the machine has decided to end;
	 * stoperr carries a scheduler error's text. finished is the malloc'd
	 * semaphore each forked scheduler proc releases once as its last
	 * act; the main proc collects nsched-1 credits before teardown.
	 */
	uvlong idlemask;
	int nidle;
	int stopping;
	long *finished;
	char stoperr[128];
	uvlong maxtermwork;	/* D080: per-traversal visit ceiling for spawned processes; 0 = none */
	uvlong completed;
	uvlong faulted;
	uvlong exited;
	NvFrag *lastexit;
	char lastfault[128];
	ulong rootslot;
	ulong rootgeneration;
	int rootvalid;
	int rootstate;
	NvFrag *rootvalue;
	char rootfault[128];
};

int nvschedinit(NvScheduler *, NvModule *, NvLimits *, uvlong, uvlong, char *, int);
/*
 * M10-T01 (D087): configure n scheduler procs, 1..NvMaxsched. nvschedinit
 * leaves the machine at n == 1 and every existing caller stays there.
 * Must be called before the root is spawned and before any process
 * exists (ownership is fixed at spawn, D083): "scheduler count fixed
 * once a process exists" otherwise. Grows the runtime's run queues and
 * allocates sched[1..n-1]; on failure the machine keeps its previous
 * count and remains usable. Does not start any proc: that is
 * nvmachinerun's job.
 */
int nvschedsetnsched(NvScheduler *, int n, char *, int);
/* M10-T01: force the runtime lock on (or off) at N=1; see NvScheduler.locking. */
void nvschedsetlocking(NvScheduler *, int on);
/*
 * M10-T01 (D085/D087): run the machine to completion with nsched
 * scheduler procs: forks sched[1..nsched-1] (rfork RFPROC|RFMEM|RFNOWAIT),
 * runs sched[0] on the calling proc, waits for every forked proc to
 * finish, and returns NvSchedDone (nlive reached 0), NvSchedIdle (D046
 * deadlock), or NvSchedError with the first scheduler error in err.
 * The NvScheduler and everything it points at must be malloc'd: a
 * forked proc shares data and heap but not the caller's stack. Root
 * outcome is in rootstate/rootvalue as after an nvschedstep loop. At
 * nsched == 1 nothing is forked and the result is what the nvschedstep
 * loop would have produced. nvmachineinterrupt, callable from a note
 * handler, makes a running machine stop with NvSchedError "interrupted".
 */
int nvmachinerun(NvScheduler *, char *, int);
void nvmachineinterrupt(NvScheduler *);
/*
 * D050: nvschedinit installs a default production clock (uptime/sleep).
 * nvschedsetclock overrides it; pass nil to restore the default. Tests
 * install a deterministic counter clock so timing tests never sleep on
 * host time.
 */
void nvschedsetclock(NvScheduler *, NvClock *);
/*
 * D054-D057: install or clear the optional output streams. A nil argument
 * clears both to nil (no output installed), unlike nvschedsetclock's nil
 * meaning "restore the production default" -- there is no production
 * default here.
 */
void nvschedsetio(NvScheduler *, NvIO *);
/*
 * D080: install the per-traversal work ceiling (node visits one
 * equality, print, or boundary copy may make) for processes spawned
 * after the call; 0, the nvschedinit default, means no ceiling. Charging
 * of traversal work to reductions is unconditional and not affected.
 */
void nvschedsetworklimit(NvScheduler *, uvlong);
void nvschedfree(NvScheduler *);
/*
 * D074 "Test determinism": a deterministic hold point, absent (a no-op)
 * in production. Setting hold nonzero parks every off-process collector
 * -- already launched and still running, or launched later -- at the
 * point just after it finishes nvexecgc but before it publishes
 * completion (before it takes the heap lock to set the heap idle and
 * semrelease's the scheduler's completion semaphore). Setting hold back
 * to 0 releases every currently parked collector at once. This lets a
 * test park a launched collector, observe scheduler/process state with
 * the heap still NvHeapCollecting, then release it and observe the
 * lazy completion fold, all without depending on real scheduling races.
 * nil-safe (nvschedinit always allocates the underlying storage).
 */
void nvschedgchold(NvScheduler *, int hold);
/* The argument term may live in any storage; it is copied into the new process's heap. */
int nvschedspawn(NvScheduler *, char *, NvTerm arg, NvTerm *pid, char *, int);
int nvschedspawnroot(NvScheduler *, char *, NvTerm arg, NvTerm *pid, char *, int);
int nvschedstep(NvScheduler *, char *, int);
/* O(slots + fragments), explicitly called between dispatches with exclusive
 * access. Never called by the ordinary scheduler or collection path. */
void nvschedmemory(NvScheduler *, NvMemstats *);
