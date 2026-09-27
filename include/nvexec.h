typedef struct NvExec NvExec;
typedef struct NvExecHost NvExecHost;

enum {
	NvYield,
	NvDone,
	NvFault,
	NvExit,
	NvCollect,	/* request only; exec state remains NvYield */
};

/*
 * D043/D065: the host callback table. Every callback receives the
 * executing NvExec; the host finds its own state through e->host->aux
 * and, when it must allocate a term for the process (makeref), uses
 * e->heap. Terms passed in (pid, value, arg, duration) are words in the
 * process's registers and are valid only for the duration of the call;
 * a host that keeps one must copy it (nvfragcopy), which is exactly what
 * send and spawn do.
 *
 * recvbegin/recvnext (D064) place a term pointing into the candidate
 * mailbox fragment in *value; no copy is made. recvtake returns the
 * unlinked fragment through *taken and the interpreter adopts it into
 * e->heap; a host with no heap to adopt into frees it.
 */
struct NvExecHost {
	void *aux;
	int (*self)(NvExec *, NvTerm *, char *, int);
	int (*makeref)(NvExec *, NvTerm *, char *, int);
	int (*send)(NvExec *, NvTerm pid, NvTerm value, char *, int);
	int (*spawn)(NvExec *, char *entry, NvTerm arg, NvTerm *pid, char *, int);
	int (*recvbegin)(NvExec *, NvTerm *, char *, int);
	int (*recvnext)(NvExec *, NvTerm *, char *, int);
	/* Validate/size the candidate without consuming it; paired with recvtake. */
	int (*recvneed)(NvExec *, uvlong *, char *, int);
	int (*recvtake)(NvExec *, NvFrag **taken, char *, int);
	int (*recvwait)(NvExec *, char *, int);
	/*
	 * D051 timeout boundary. recvdeadline arms one absolute deadline from
	 * a duration term and returns 0, or -1 with a reason for a duration
	 * outside the D049 domain; an `'infinity` duration arms nothing and
	 * still returns 0. recvwaitdeadline returns 0 when the process blocked
	 * and must resume at the retry target, 1 when the armed deadline had
	 * already expired and execution must fall through to the timeout body,
	 * and -1 with a reason on an illegal scan phase.
	 */
	int (*recvdeadline)(NvExec *, NvTerm duration, char *, int);
	int (*recvwaitdeadline)(NvExec *, char *, int);
	/*
	 * D053-D057 I/O boundary. print/eprint write one value to host
	 * stdout/stderr respectively and return 0 on success or -1 with a
	 * reason (io_error on a host write failure, system_limit if the value
	 * is deeper than NvMaxtermdepth or exceeds the D080 work ceiling; a
	 * host that prints through nvtermprintw with e->work also charges
	 * the visits to the process). The callback does not produce the
	 * destination value: nvexecrun writes the fixed atom 'ok into the
	 * destination register itself on success. A nil slot faults
	 * bad_process_context exactly like every other process instruction.
	 */
	int (*print)(NvExec *, NvTerm, char *, int);
	int (*eprint)(NvExec *, NvTerm, char *, int);
};

/*
 * D065: the frame stack. One contiguous array of words. A frame is
 * NvFramehdr header words followed by the function's nreg register
 * words, all initialized to NvNil at push. The header holds the
 * function's index in module->func, the pc, the caller frame's offset
 * (NvNoframe for the root frame), and the caller's destination register.
 * `fp` is the offset of the executing frame; `sp` the words in use.
 * The stack may move on a push (realloc), so register pointers are
 * recomputed from fp after every call, tail call, and return.
 */
enum {
	NvFramefunc = 0,
	NvFramepc = 1,
	NvFramecaller = 2,
	NvFramedst = 3,
	NvFramehdr = 4,
};

#define NvNoframe (~0UL)

struct NvExec {
	NvModule *module;
	NvHeap heap;		/* D063: the process heap; freed whole by nvexecfree */
	NvTerm *stack;		/* D065 */
	ulong fp;
	ulong sp;
	ulong nstack;		/* capacity in words */
	ulong nframe;
	ulong maxframe;
	NvFrag *result;		/* D064: the root return value, as a fragment; nil until NvDone */
	NvFrag *exitreason;	/* D064: the `exit` reason, as a fragment; nil until NvExit */
	uvlong reductions;
	Biobuf *trace;
	NvExecHost *host;	/* D065: shared table owned by the host; nil = no host */
	int traceon;
	int state;
	/*
	 * D060: guard mode. -1 normally; while a guard executes, the pc a
	 * fault transfers to instead of faulting the process. Not a stack:
	 * guards cannot call, so they cannot nest.
	 */
	int guardfail;
	/*
	 * D076: binary pattern-match state. binsubj is the register index of
	 * the subject binary (set by bintestbinary, -1 otherwise); binpos is
	 * the current byte position into it. Both are plain integers (no heap
	 * pointer), so they are safe across a collection: the subject term is
	 * read fresh from regs[binsubj] on every use, and the collector
	 * updates that register; binpos is a byte offset, unchanged by a move.
	 * A quantum boundary in the middle of a match preserves both (as D060
	 * preserves guardfail), and a failed match leaves them stale but
	 * unused until the next bintestbinary.
	 */
	int binsubj;
	uvlong binpos;
	int gcstress;
	int gcpending;
	int gcretry;		/* 0 fresh, 1 collected, 2 limit, 3 allocation failure */
	uvlong gcneed;		/* additional charge; also conservative space allowance */
	uvlong collections;
	uvlong livewords;	/* live heap words at last successful collection */
	/*
	 * D074: set by the scheduler proc when it launches an off-process
	 * collector for this exec's heap, before the rfork; cleared by the
	 * scheduler proc's completion fold, the first time it observes
	 * heap.owner idle again with this bit still set. The collector
	 * child itself never reads or writes this field -- it is scheduler
	 * bookkeeping, not something the collector's own restricted
	 * argument list (NvExec*, the completion semaphore) can reach.
	 */
	int offlaunched;
	/*
	 * D074 amendment (R3-F05): the outcome of the last off-process
	 * collection, written by the collector child before it publishes
	 * idle and read by the scheduler's completion fold under the same
	 * lock: 1 collected, 2 limit, 3 allocation failure. Separate from
	 * gcretry because gcretry is the *interpreter's* retry state for a
	 * pending demand reservation: a collector launched by the idle
	 * sweep has no pending reservation, must not touch gcretry (the
	 * next prepare() would misread it as a serviced retry), and yet
	 * must still report its outcome to the fold.
	 */
	int gcresult;
	/*
	 * D080: traversal work. Every counted traversal an instruction
	 * performs (testeq's equality, print/eprint's print, the root
	 * return's and exit's fragment copy, spawn's argument copy) adds
	 * its node visits to work.count; the interpreter drains whole
	 * NvWorkunit multiples into `used` and `reductions` after the
	 * instruction, so an instruction that walks a large graph
	 * consumes its quantum instead of stalling every other process
	 * for free. work.max is the per-traversal ceiling installed by
	 * nvexecsetworklimit (0 = none); exceeding it faults system_limit.
	 * Both are plain integers, never heap pointers.
	 */
	NvWork work;
	char fault[128];
};

/*
 * D080: node visits per charged reduction. A ring hop is ~24 reductions
 * at ~12 ns each and an equality visit is ~3.5 ns, so 8 is a little
 * generous to the traversal; what matters is that a quantum of 1000
 * reductions now ends after about 8000 visits of traversal work
 * wherever it happens, rather than never.
 */
enum {
	NvWorkunit = 8,
};

/*
 * nvexecinit copies arg into the fresh process heap (system_limit if it
 * is deeper than NvMaxtermdepth) and pushes the entry frame. maxheap of
 * 0 is unlimited; startup and retained stack capacity are charged.
 * nvexecsethost
 * installs the host table by pointer; the table must outlive the exec.
 * The caller owns e->result / e->exitreason after NvDone / NvExit and
 * may take them (set the field nil) before nvexecfree, which otherwise
 * frees them.
 */
int nvexecinit(NvExec *, NvModule *, char *, NvTerm, uvlong maxheap, Biobuf *, int, char *, int);
/*
 * D080: nvexecinitw is nvexecinit with the argument copy's node visits
 * charged to (and bounded by) the given NvWork -- the spawning
 * process's, when the scheduler spawns on its behalf. nil is nvexecinit.
 * A copy refused on work reports system_limit like one refused on depth.
 */
int nvexecinitw(NvExec *, NvModule *, char *, NvTerm, uvlong maxheap, Biobuf *, int, NvWork *, char *, int);
void nvexecsethost(NvExec *, NvExecHost *);
int nvexecsetframelimit(NvExec *, ulong);
/* D080: per-traversal visit ceiling for this exec's counted traversals; 0 = none. */
void nvexecsetworklimit(NvExec *, uvlong);
/* NvCollect leaves pc, registers and reductions unchanged at the pending
 * instruction. Stop the owner and call nvexecgc before retrying. */
int nvexecrun(NvExec *, uvlong);
/* Explicit collection only while suspended at NvYield; no state/pc/reduction
 * change. Charges stack capacity (nstack), scans only active registers.
 * Returns nvheapcollect's status; malformed frame metadata is NvTermerror.
 */
int nvexeccollect(NvExec *, uvlong need);
/* Service one NvCollect request, recording success/failure for the retry.
 * Never executes an instruction or faults a guard outside the interpreter. */
void nvexecgc(NvExec *);
/* Standalone convenience: service requests inline within the same quantum. */
int nvexecruninline(NvExec *, uvlong);
void nvexecfree(NvExec *);
