typedef struct NvPatClause NvPatClause;
typedef struct NvBindings NvBindings;
typedef struct NvLimits NvLimits;
typedef struct NvProcess NvProcess;
typedef struct NvRuntime NvRuntime;
typedef struct NvExec NvExec;

/*
 * D066: maxmailbox and maxmessage are word counts of fragments including
 * their root word (nvfragwords), checked all-or-nothing during the D064
 * copy. maxheap is the per-process word budget: heap in use plus adopted
 * fragments plus retained frame-stack capacity. 0 means unlimited.
 * Initialize gcstress to 0 or 1; the scheduler copies it to each exec.
 */
struct NvLimits {
	ulong maxprocess;	/* may not exceed NvMaxslot+1 (D061) */
	uvlong maxmailbox;
	uvlong maxmessage;
	uvlong maxheap;
	int gcstress;		/* force one collection at every allocating instruction */
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
	/*
	 * D062: the interned atom table's maximum size, installed into the
	 * process-wide table by nvruntimeinit via nvatomlimit. Validated
	 * non-zero like the other limits above.
	 */
	ulong maxatom;
};

enum {
	Prfree,
	Prrunnable,
	Prrunning,
	Prwaiting,
	Prexited,
	Prretired,
};

/* Run-queue link value meaning "no slot" (D059). */
#define NvNoslot (~0UL)

/*
 * D064: the mailbox is a chain of fragments; the fragment is the message.
 * scan/scanprev point at fragments, which never move, so a receive scan
 * may span quanta.
 */
struct NvProcess {
	ulong generation;
	int state;
	NvFrag *head;
	NvFrag *tail;
	NvFrag *scanprev;
	NvFrag *scan;
	int scanning;
	uvlong mailboxwords;
	/*
	 * D050: a waiting process owns its own deadline. There is no timer
	 * object, queue, or token, so a deadline cannot outlive its slot,
	 * fire twice, or wake a reused generation. An `'infinity` timeout
	 * leaves hasdeadline zero.
	 */
	uvlong deadline;
	int hasdeadline;
	/*
	 * D059: intrusive FIFO run-queue links, as slot indices so they
	 * survive process-table realloc. Meaningful only while state is
	 * Prrunnable; a slot is in the queue exactly when it is Prrunnable.
	 */
	ulong runnext;
	ulong runprev;
	NvExec *exec;
};

struct NvRuntime {
	NvLimits limits;
	NvProcess *process;
	ulong nslot;		/* initialized slots, including exited/retired */
	ulong nalloc;		/* allocated capacity; tail is not initialized */
	ulong freehint;		/* no reusable slot below this index */
	ulong nlive;
	uvlong slotprobes;	/* slots examined while searching for reuse */
	uvlong tablegrows;	/* successful allocation/growth calls */
	uvlong tablemoves;	/* growth calls that changed the base address */
	uvlong tablemovebytes;	/* old requested capacity bytes on moved growths */
	/* D059: FIFO of Prrunnable slots; NvNoslot when empty. */
	ulong runhead;
	ulong runtail;
	ulong nrunnable;
	/* Lifetime counters for `nervous -s`; never read by the runtime itself. */
	uvlong nspawned;
	ulong maxlive;
	uvlong nsent;
	uvlong ndropped;
	uvlong incarnation;
	uvlong nextref;
};

int nvruntimeinit(NvRuntime *, NvLimits *, uvlong, char *, int);
void nvruntimefree(NvRuntime *);
int nvprocspawn(NvRuntime *, NvTerm *pid, char *, int);
int nvprocdispatch(NvRuntime *, NvTerm pid, char *, int);
int nvprocyield(NvRuntime *, NvTerm pid, char *, int);
int nvprocwait(NvRuntime *, NvTerm pid, char *, int);
int nvprocexit(NvRuntime *, NvTerm pid);
/*
 * D059: run-queue access for the scheduler. nvprocrunhead reports the
 * slot that has been runnable longest without removing it (dispatch
 * removes it); nvprocwake moves one Prwaiting slot to Prrunnable at the
 * queue tail, the only way a non-message event (a deadline) may make a
 * process runnable. Message arrival wakes through nvprocsend.
 */
int nvprocrunhead(NvRuntime *, ulong *);
int nvprocwake(NvRuntime *, ulong);
int nvprocalive(NvRuntime *, NvTerm pid);
/* nvprocref allocates the new ref in the given heap (the calling process's). */
int nvprocref(NvRuntime *, NvHeap *, NvTerm *ref, char *, int);
/*
 * D064 message boundary. nvprocsend copies value once into a fragment
 * owned by the destination mailbox: 1 enqueued, 0 dropped (dead PID,
 * nothing copied), -1 with mailbox_full (word or depth limits, or an
 * NvNil value) or system_limit (allocation failure). nvprocpop and
 * nvprocreceive hand the removed fragment to the caller, who owns it.
 * nvprocrecvbegin/nvprocrecvnext set *value to a term pointing into the
 * candidate fragment, which stays valid while the fragment is queued or
 * adopted. nvprocrecvtake unlinks the selected fragment and returns it
 * through *taken for the caller to adopt (nvheapadopt) or free.
 */
int nvprocsend(NvRuntime *, NvTerm pid, NvTerm value, char *, int);
int nvprocpop(NvRuntime *, NvTerm pid, NvFrag **msg, char *, int);
int nvprocrecvbegin(NvRuntime *, NvTerm pid, NvTerm *value, char *, int);
int nvprocrecvnext(NvRuntime *, NvTerm pid, NvTerm *value, char *, int);
int nvprocrecvneed(NvRuntime *, NvTerm pid, uvlong *, char *, int);
int nvprocrecvtake(NvRuntime *, NvTerm pid, NvFrag **taken, char *, int);
int nvprocrecvwait(NvRuntime *, NvTerm pid, char *, int);
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
int nvprocarmdeadline(NvRuntime *, NvTerm pid, NvTerm duration, uvlong, char *, int);
int nvprocrecvwaitdeadline(NvRuntime *, NvTerm pid, uvlong, char *, int);
int nvprocreceive(NvRuntime *, NvTerm pid, NvPatClause *, int, NvBindings *, int *, NvFrag **msg, char *, int);
