typedef struct NvScheduler NvScheduler;
typedef struct NvClock NvClock;
typedef struct NvIO NvIO;

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
 * they do with no host at all. nvschedspawn snapshots the currently
 * installed NvIO into each newly spawned process's host callbacks, so
 * nvschedsetio must be called before spawning any process that needs
 * output; spawning first and calling nvschedsetio afterward does not
 * retroactively rewire already-spawned processes.
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

struct NvScheduler {
	NvRuntime runtime;
	NvModule *module;
	NvClock clock;
	NvIO io;
	uvlong quantum;
	ulong cursor;
	ulong currentslot;
	ulong currentgeneration;
	int currentvalid;
	uvlong dispatches;
	uvlong completed;
	uvlong faulted;
	uvlong exited;
	NvValue lastexit;
	char lastfault[128];
	ulong rootslot;
	ulong rootgeneration;
	int rootvalid;
	int rootstate;
	NvValue rootvalue;
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
 * default here. Call before spawning any process that needs print/eprint
 * to succeed; see the NvIO comment above nvsched.h's struct definition.
 */
void nvschedsetio(NvScheduler *, NvIO *);
void nvschedfree(NvScheduler *);
int nvschedspawn(NvScheduler *, char *, NvValue *, NvValue *, char *, int);
int nvschedspawnroot(NvScheduler *, char *, NvValue *, NvValue *, char *, int);
int nvschedstep(NvScheduler *, char *, int);
