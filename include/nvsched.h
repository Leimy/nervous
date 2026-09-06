typedef struct NvScheduler NvScheduler;
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

/*
 * D064: lastexit and rootvalue are fragments owned by the scheduler
 * (taken from the exiting NvExec), freed by nvschedfree. rootvalue is
 * the root's return value after NvRootDone and its exit reason after
 * NvRootExit; nil otherwise.
 */
struct NvScheduler {
	NvRuntime runtime;
	NvModule *module;
	NvExecHost host;	/* D065: one table shared by every process's NvExec */
	NvClock clock;
	NvIO io;
	uvlong quantum;
	ulong currentslot;
	ulong currentgeneration;
	int currentvalid;
	uvlong dispatches;
	uvlong reductions;
	uvlong timerwakes;
	uvlong collections;
	uvlong gcfailed;
	uvlong lastlivewords;	/* heap words in the most recently collected process */
	uvlong maxlivewords;	/* largest successful per-process live sample */
	uvlong gcinputwords;	/* used/adopted words presented to all attempts */
	uvlong gcoutputwords;	/* live words from successful collections */
	uvlong gcdemand;
	uvlong gcidle;
	int profile;		/* opt-in real monotonic elapsed timing, default off */
	uvlong execns;		/* includes host callbacks and nested spawn time */
	uvlong gcns;		/* demand + idle collection, excludes startup */
	uvlong spawnns;		/* all spawn attempts; overlaps execns for bytecode spawn */
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
void nvschedfree(NvScheduler *);
/* The argument term may live in any storage; it is copied into the new process's heap. */
int nvschedspawn(NvScheduler *, char *, NvTerm arg, NvTerm *pid, char *, int);
int nvschedspawnroot(NvScheduler *, char *, NvTerm arg, NvTerm *pid, char *, int);
int nvschedstep(NvScheduler *, char *, int);
/* O(slots + fragments), explicitly called between dispatches with exclusive
 * access. Never called by the ordinary scheduler or collection path. */
void nvschedmemory(NvScheduler *, NvMemstats *);
