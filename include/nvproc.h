typedef struct NvValue NvValue;
typedef struct NvPatClause NvPatClause;
typedef struct NvBindings NvBindings;
typedef struct NvLimits NvLimits;
typedef struct NvMessage NvMessage;
typedef struct NvProcess NvProcess;
typedef struct NvRuntime NvRuntime;
typedef struct NvExec NvExec;

struct NvLimits {
	ulong maxprocess;
	uvlong maxmailbox;
	uvlong maxmessage;
	ulong maxframe;
	ulong maxtermdepth;
	/*
	 * R2-F16 (deferred to milestone 10, see docs/questions.md): this field
	 * is currently advisory only. nvruntimeinit does not validate it and
	 * nvprocarmdeadline checks the compile-time NvMaxduration ceiling
	 * (D049) unconditionally, not this per-runtime value. Making it
	 * authoritative is a real semantic decision -- it would let a runtime
	 * accept a narrower duration domain than D049's literal -- and is
	 * left to whichever later milestone first needs a per-runtime bound
	 * tighter than the compile-time one, rather than being decided as a
	 * drive-by fix inside the R2 review.
	 */
	vlong maxduration;
};

struct NvMessage {
	NvValue value;
	uvlong bytes;
	NvMessage *next;
};

enum {
	Prfree,
	Prrunnable,
	Prrunning,
	Prwaiting,
	Prexited,
	Prretired,
};

struct NvProcess {
	ulong generation;
	int state;
	NvMessage *head;
	NvMessage *tail;
	NvMessage *scanprev;
	NvMessage *scan;
	int scanning;
	uvlong mailboxbytes;
	/*
	 * D050: a waiting process owns its own deadline. There is no timer
	 * object, queue, or token, so a deadline cannot outlive its slot,
	 * fire twice, or wake a reused generation. An `'infinity` timeout
	 * leaves hasdeadline zero.
	 */
	uvlong deadline;
	int hasdeadline;
	NvExec *exec;
};

struct NvRuntime {
	NvLimits limits;
	NvProcess *process;
	ulong nslot;
	ulong nlive;
	uvlong incarnation;
	uvlong nextref;
};

int nvruntimeinit(NvRuntime *, NvLimits *, uvlong, char *, int);
void nvruntimefree(NvRuntime *);
int nvprocspawn(NvRuntime *, NvValue *, char *, int);
int nvprocdispatch(NvRuntime *, NvValue *, char *, int);
int nvprocyield(NvRuntime *, NvValue *, char *, int);
int nvprocwait(NvRuntime *, NvValue *, char *, int);
int nvprocexit(NvRuntime *, NvValue *);
int nvprocalive(NvRuntime *, NvValue *);
int nvprocref(NvRuntime *, NvValue *, char *, int);
int nvprocsend(NvRuntime *, NvValue *, NvValue *, char *, int);
int nvprocpop(NvRuntime *, NvValue *, NvValue *, char *, int);
int nvprocrecvbegin(NvRuntime *, NvValue *, NvValue *, char *, int);
int nvprocrecvnext(NvRuntime *, NvValue *, NvValue *, char *, int);
int nvprocrecvtake(NvRuntime *, NvValue *, char *, int);
int nvprocrecvwait(NvRuntime *, NvValue *, char *, int);
/*
 * D050/D051: nvprocarmdeadline validates and arms one absolute deadline
 * from a duration term at the given clock reading, or leaves no deadline
 * armed for `'infinity`. nvprocrecvwaitdeadline resumes an exhausted scan
 * exactly like nvprocrecvwait when a queued message raced the scan or no
 * deadline has expired yet (returning 0, retry at the caller's target),
 * or reports expiry (returning 1, caller falls through to the timeout
 * body) without ever re-arming or re-firing the same deadline. Neither
 * function takes a clock pointer; "now" is supplied by the caller, which
 * owns the installed NvClock (see nvsched.h).
 */
int nvprocarmdeadline(NvRuntime *, NvValue *, NvValue *, uvlong, char *, int);
int nvprocrecvwaitdeadline(NvRuntime *, NvValue *, uvlong, char *, int);
int nvprocreceive(NvRuntime *, NvValue *, NvPatClause *, int, NvBindings *, int *, NvValue *, char *, int);
