typedef struct NvPatClause NvPatClause;
typedef struct NvBindings NvBindings;
typedef struct NvLimits NvLimits;
typedef struct NvProcess NvProcess;
typedef struct NvRunq NvRunq;
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
	/*
	 * D074: word threshold (same units as e->heap.words, used plus
	 * adopted) at or above which a collection is launched off-process
	 * instead of run inline. Polarity is the opposite of gcstress and
	 * easy to get backwards: 0 means NEVER off-process, matching this
	 * codebase's convention that a zero-valued limit is the safe/inert
	 * default (maxheap 0 is unlimited, gcstress 0 is off) -- this is the
	 * corrected polarity from D068's original text, which said 0 forced
	 * every collection off-process; see D074's amendment for why. Every
	 * NvLimits construction site in this tree assigns fields
	 * individually without zeroing the struct first, so this field must
	 * be explicitly set to 0 at each one (D074 lists them) rather than
	 * relying on this being a safe default by accident. A threshold of 1
	 * forces every real collection off-process (every execution heap
	 * has at least one live word, its argument tuple, so 1 is never
	 * accidentally unreachable) -- this is the distinct "always
	 * off-process" test/stress setting D068 originally assigned to 0.
	 */
	uvlong gcoffload;
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
	/*
	 * D083: the index of the scheduler that owns this process -- the
	 * queue it is enqueued on when runnable, and the scheduler that
	 * owns its deadline while waiting. Always defined for a live slot:
	 * set by nvprocspawnon, changed only by a steal (M10-T01), 0 at
	 * N=1. Read today by the run-queue functions only.
	 */
	int owner;
	/*
	 * M10-T01 (D081): oncpu is set by nvprocdispatch and cleared by
	 * nvprocoffcpu, both under the runtime lock, and is true exactly
	 * while some scheduler proc is inside nvexecrun for this process.
	 * A receive that finds nothing marks the process Prwaiting from
	 * inside that quantum (nvprocrecvwait), a few instructions before
	 * the interpreter yields; at N>1 a sender on another scheduler can
	 * see Prwaiting in that window. It must not enqueue the process --
	 * it is still running -- so nvprocwakefrom records pendingwake
	 * instead and nvprocoffcpu, called by the owner at quantum end,
	 * performs the enqueue. BEAM's RUNNING/ACTIVE split, in two ints.
	 */
	int oncpu;
	int pendingwake;
	NvExec *exec;
};

/*
 * D059/D083/D087: one FIFO of Prrunnable slots per scheduler, threaded
 * through the slots by index (runnext/runprev). head/tail are NvNoslot
 * when empty. A slot is on exactly one queue, its owner's, exactly when
 * it is Prrunnable. The queues are malloc'd and owned by NvRuntime
 * (RFMEM shares heap, not stacks) and indexed by NvProcess.owner; each
 * NvSched also points at its own (NvSched.runq) so the dispatch path
 * never indexes. nvruntimeinit creates one; nvruntimesetnrunq grows
 * the set before any process is spawned on a higher index.
 */
struct NvRunq {
	ulong head;
	ulong tail;
	ulong nrunnable;
	uvlong enqueues;	/* D088: arrivals on this queue (spawn, yield, wake), lifetime */
};

/* D087: upper bound on the -p scheduler proc count, and so on run queues. */
enum {
	NvMaxsched = 64,
};

/*
 * D082: the process table is segmented. Slots live in fixed-size chunks
 * reached through an array of chunk pointers; growth appends a chunk and
 * never moves an existing slot, so an NvProcess* is stable for as long
 * as the slot exists -- the property a second scheduler proc needs and
 * the old realloc'd table (whose "re-fetch by slot after anything that
 * may spawn" discipline no other proc can honor) could not give. Slot
 * indices, PIDs and D059's index-threaded queue links are unchanged;
 * nvprocat costs one extra indirection. Only the chunk pointer array
 * itself is ever realloc'd, and it holds no slot.
 *
 * nvprocat is a macro (Plan 9 C has no inline) and evaluates `slot`
 * twice: the argument must be side-effect free. `nvprocat(r, n++)` is
 * wrong and was the first D082 bug (the root spawned into one slot and
 * was queued under another).
 */
enum {
	NvProcshift = 10,
	NvProcchunk = 1<<NvProcshift,	/* slots per chunk */
};
#define nvprocat(r, slot)	((r)->chunk[(slot)>>NvProcshift] + ((slot)&(NvProcchunk-1)))

struct NvRuntime {
	NvLimits limits;
	NvProcess **chunk;	/* D082: nchunk chunk pointers, each NvProcchunk slots */
	ulong nchunk;
	ulong nslot;		/* initialized slots, including exited/retired */
	ulong freehint;		/* no reusable slot below this index */
	ulong nlive;
	uvlong slotprobes;	/* slots examined while searching for reuse */
	uvlong tablegrows;	/* successful chunk appends (D082) */
	uvlong tablemoves;	/* retired by D082: always 0; kept so `-s` output shape is unchanged */
	uvlong tablemovebytes;	/* retired by D082: always 0 */
	/* D083/D087: per-scheduler run queues, indexed by NvProcess.owner. */
	NvRunq **runq;
	int nrunq;
	/*
	 * D085 (M10-T01): called by nvprocwakefrom, after the woken slot is
	 * on its owner's queue, with the waker's scheduler index (-1 for a
	 * host caller) and the owner's. The scheduler installs it to
	 * semrelease an idle owner; nil means no one to tell (bare-runtime
	 * fixtures). Called with whatever lock the caller holds; must not
	 * block or re-enter the runtime.
	 */
	void (*wakehook)(void *aux, int from, int owner);
	void *wakeaux;
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
/*
 * D083/D087: grow the run-queue set to n (1..NvMaxsched); never shrinks,
 * and must be called before any process is spawned with an owner >= the
 * old count. nvruntimeinit leaves exactly one queue, so N=1 callers
 * never call this.
 */
int nvruntimesetnrunq(NvRuntime *, int n, char *, int);
/*
 * D083: nvprocspawnon creates the process owned by (and queued on)
 * scheduler `owner`; nvprocspawn is the same with owner 0, which at
 * N=1 is the only scheduler there is.
 */
int nvprocspawnon(NvRuntime *, int owner, NvTerm *pid, char *, int);
int nvprocspawn(NvRuntime *, NvTerm *pid, char *, int);
int nvprocdispatch(NvRuntime *, NvTerm pid, char *, int);
/*
 * M10-T01: the scheduler's quantum-end bookend to nvprocdispatch, called
 * under the runtime lock once nvexecrun has returned and before the
 * yield/wait/exit transition is examined. Clears oncpu and, if a wake
 * was deferred while the process ran (pendingwake) and it is Prwaiting,
 * makes it Prrunnable on its owner's queue. Returns 1 if it did that
 * (the caller then sees Prrunnable where it would otherwise expect
 * Prwaiting), 0 otherwise, -1 for a bad pid.
 */
int nvprocoffcpu(NvRuntime *, NvTerm pid);
int nvprocyield(NvRuntime *, NvTerm pid, char *, int);
int nvprocwait(NvRuntime *, NvTerm pid, char *, int);
int nvprocexit(NvRuntime *, NvTerm pid);
/*
 * D059: run-queue access for the scheduler. nvprocrunhead reports the
 * slot that has been runnable longest on scheduler `sched`'s queue
 * without removing it (dispatch removes it); nvprocwake moves one
 * Prwaiting slot to Prrunnable at the tail of its owner's queue, the
 * only way a non-message event (a deadline) may make a process
 * runnable. Message arrival wakes through nvprocsend.
 */
int nvprocrunhead(NvRuntime *, int sched, ulong *);
int nvprocwake(NvRuntime *, ulong);
/*
 * D083/D085 (M10-T01): the same wake, naming the scheduler doing it so
 * the runtime's wakehook can tell a remote enqueue from a local one.
 * nvprocwake is nvprocwakefrom with from == -1 (a host caller).
 */
int nvprocwakefrom(NvRuntime *, int from, ulong);
/*
 * D083 (M10-T01): move a Prrunnable slot from its owner's queue to the
 * tail of scheduler newowner's, changing nothing but owner and queue
 * membership (PID, heap, stack, mailbox, deadline untouched). -1 if the
 * slot is not Prrunnable, already owned by newowner, or under
 * off-process collection (offlaunched: D074's bookkeeping belongs to
 * the launching scheduler, so the process is unmovable until folded).
 */
int nvprocsteal(NvRuntime *, ulong slot, int newowner);
/*
 * D074: move one Prrunnable slot to the run-queue tail, wherever it
 * currently sits (in practice always called on the current head, right
 * after nvprocrunhead, to skip a slot whose heap is under off-process
 * collection). Preserves the D059 invariant that the queue holds exactly
 * the Prrunnable slots; -1 if the slot is not Prrunnable.
 */
int nvprocrequeue(NvRuntime *, ulong);
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
/* M10-T01: nvprocsend naming the sending scheduler for the wake (see nvprocwakefrom). */
int nvprocsendfrom(NvRuntime *, int from, NvTerm pid, NvTerm value, char *, int);
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
int nvprocreceive(NvRuntime *, NvHeap *, NvTerm pid, NvPatClause *, int, NvBindings *, int *, NvFrag **msg, char *, int);
