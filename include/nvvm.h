/*
 * D061-D066: term representation, heaps, and fragments.
 *
 * A term is one 64-bit word (NvTerm). The low two bits are the tag:
 *
 *   ..00  boxed: a nonzero 8-byte-aligned pointer to a header word
 *         followed by the body. The word 0 (NvNil) is "no term": an
 *         uninitialized register, an absent value. It is never a valid
 *         term and never reaches a verified program.
 *   ..01  small integer: value<<2 | 1, 62-bit two's complement. Integers
 *         outside [NvMinsmall, NvMaxsmall] are boxed (Bint) so D007's
 *         checked 64-bit arithmetic is unchanged.
 *   ..10  atom: D062 table index<<2 | 2. Equality is word equality.
 *   ..11  PID: generation<<(2+NvPidslotbits) | slot<<2 | 3. The split is
 *         an implementation constant (D061); NvLimits.maxprocess may not
 *         exceed NvMaxslot+1, and D041's slot retirement keys off
 *         NvMaxgeneration, which equals ~0UL exactly because generation
 *         is a 32-bit ulong.
 *
 * A boxed header is NvHdr(kind, n): kind in the low 3 bits, one reserved
 * bit (the stage-3 collector's forwarding mark), body length in words
 * from bit 8. Body layouts: Btuple n element terms; Bref 2 words
 * (incarnation, counter); Bint 1 word (the vlong).
 *
 * Terms are immutable. Within one heap a copy of a term is a copy of its
 * word; structure is shared. Deep copies happen only at a process
 * boundary and always produce a fragment (D064) or a heap copy
 * (nvheapcopy). Construction never traverses, so D047's depth ceiling
 * NvMaxtermdepth is enforced by the traversals -- nvfragcopy,
 * nvheapcopy, nvtermequal, nvtermprint -- each of which reports
 * NvTermlimit rather than recursing past it.
 *
 * Load-bearing rule (D063): no C variable holds a pointer into a heap
 * across anything that may collect that heap. D067 reserves before an
 * instruction, then yields to its host to collect; constructors never
 * collect internally. The frame stack is stable during collection.
 * Queued fragments stay put; adopted objects move into the heap.
 */

typedef uvlong NvTerm;
typedef struct NvHeap NvHeap;
typedef struct NvChunk NvChunk;
typedef struct NvFrag NvFrag;
typedef struct NvRoot NvRoot;

enum {
	NvMaxtermdepth = 256,
	NvTermerror = -1,	/* malformed input: NvNil where a term is required, nil pointers */
	NvTermlimit = -2,	/* depth above NvMaxtermdepth or a word budget exceeded */
};

/*
 * D049: the accepted receive-timeout duration domain, in nanoseconds.
 * Chosen so that any monotonic clock reading plus any accepted duration
 * stays inside the signed 64-bit nanosecond domain, which is what makes
 * deadline arithmetic total. It is not a policy about useful timeouts.
 */
#define NvMaxduration 1000000000000000000LL

/*
 * Term kinds as reported by nvtermkind. The order Vint..Vref is part of
 * the bytecode contract: Oistype's immediate operand is one of these
 * values (D060). Vnil is reported for NvNil only.
 */
enum {
	Vint,
	Vatom,
	Vtuple,
	Vpid,
	Vref,
	Vnil = -1,
};

/* Tags and PID payload split. */
enum {
	NvTagmask = 3,
	NvTagbox = 0,
	NvTagint = 1,
	NvTagatom = 2,
	NvTagpid = 3,
	NvPidslotbits = 30,
	NvPidgenbits = 32,
};

#define NvNil ((NvTerm)0)
#define NvMinsmall (-(1LL<<61))
#define NvMaxsmall ((1LL<<61)-1)
#define NvMaxslot ((1UL<<NvPidslotbits)-1)
#define NvMaxgeneration (~0UL)

/* Boxed header kinds. 0 is deliberately unused so a zero word is never a header. */
enum {
	Btuple = 1,
	Bref = 2,
	Bint = 3,
};

/*
 * D074: heap owner state, orthogonal to the D041 process lifecycle.
 * NvHeapIdle is 0 so a zeroed NvHeap (nvheapinit) starts idle. Dispatch
 * only ever runs a process whose heap is NvHeapIdle; NvHeapRunning is set
 * only for the duration of nvexecrun on the currently dispatched process
 * (defensive today under one scheduler proc, load-bearing once milestone
 * 10 has more than one). NvHeapCollecting marks a heap a launched
 * off-process collector owns; the owning proc, the scheduler, and a
 * collector are never more than one at a time (D068).
 */
enum {
	NvHeapIdle,
	NvHeapRunning,
	NvHeapCollecting,
};

#define NvHdr(kind, n) ((NvTerm)(kind) | (NvTerm)(n)<<8)
#define NvHdrkind(h) ((int)((h)&7))
#define NvHdrlen(h) ((ulong)((h)>>8))
#define NvBoxed(t) (((t)&NvTagmask) == NvTagbox && (t) != NvNil)
#define NvBoxptr(t) ((NvTerm*)(uintptr)(t))

/*
 * A heap: execution uses one contiguous bump chunk plus adopted
 * fragments. managed heaps refuse unreserved growth; a stopped-owner
 * collection is the only growth path. Host construction and startup
 * copying retain non-moving chunk growth until roots are registered.
 * words counts used objects plus adopted fragment words; stackwords is
 * retained frame capacity. maxwords bounds their sum (0 = unlimited).
 * nvheapinit is lazy; nvexecinit compacts and enables managed allocation.
 */
struct NvChunk {
	NvChunk *next;
	ulong cap;
	ulong top;
	NvTerm *word;
};

struct NvHeap {
	NvChunk *cur;		/* chunk allocation currently bumps in; nil until first alloc */
	NvChunk *full;		/* earlier chunks, kept until nvheapfree */
	NvFrag *adopted;	/* D064: taken messages, freed with the heap */
	uvlong words;
	uvlong maxwords;
	uvlong stackwords;	/* retained frame capacity charged with heap words */
	int managed;		/* execution heap: no unreserved chunk growth */
	int exhausted;		/* why the last refusal happened: 1 budget, 0 host allocator; read by nvheapexhausted */
	/*
	 * D074: owner state and the spinlock guarding transitions into and
	 * out of NvHeapCollecting. A read that only decides dispatch order
	 * (skip a collecting heap vs. dispatch it) needs no lock -- a stale
	 * NvHeapCollecting costs only a wasted requeue. A read-modify-write
	 * transition, and any read that then trusts fields the collector
	 * wrote (gcretry/livewords on NvExec), must hold this lock: unlock
	 * only guarantees release ordering, not acquire ordering, for a
	 * reader that never takes the lock, which matters on an
	 * architecture without strong store ordering (this project builds
	 * with 7c, i.e. arm64). A spinlock, not QLock, because the critical
	 * section is one word (D070: "locks in shared memory").
	 */
	int owner;
	Lock lock;
};

/*
 * A fragment: one self-contained allocation holding a term and every
 * boxed object it reaches, with absolute internal pointers into word[].
 * `root` is the term itself (an immediate, or a pointer into word[]).
 * Fragments never move; they are freed whole. `next` threads a mailbox
 * or a heap's adopted list. Size for accounting is nvfragwords(): nword
 * plus one for the root.
 */
struct NvFrag {
	NvTerm root;
	ulong nword;
	NvFrag *next;
	NvTerm *word;		/* nword words, in the same allocation as the struct */
};

/* D062: atoms. nvatom interns (NvNil if the table is at its limit or out of memory); nvatomlookup never interns. */
NvTerm nvatom(char *);
NvTerm nvatomlookup(char *);
int nvatomlimit(ulong);
ulong nvatomcount(void);

/*
 * Construction. Immediates never allocate. nvint boxes only when the
 * value is outside the small range; with a nil heap it then returns
 * NvNil. nvtuple copies the n element words without traversing them.
 * Every allocating constructor returns NvNil on allocation failure or
 * budget exhaustion; the caller decides between out_of_memory and
 * system_limit from the heap's state (nvheapexhausted).
 */
NvTerm nvpid(ulong slot, ulong generation);
NvTerm nvint(NvHeap *, vlong);
NvTerm nvref(NvHeap *, uvlong incarnation, uvlong counter);
NvTerm nvtuple(NvHeap *, NvTerm *elem, int n);

/* Inspection. Valid on any term wherever its storage lives. Accessors assume the kind has been checked. */
int nvtermkind(NvTerm);
vlong nvtermint(NvTerm);
char *nvtermatom(NvTerm);
ulong nvpidslot(NvTerm);
ulong nvpidgeneration(NvTerm);
uvlong nvrefincarnation(NvTerm);
uvlong nvrefcounter(NvTerm);
int nvtuplelen(NvTerm);
NvTerm nvtupleelem(NvTerm, int);
NvTerm *nvtupleelems(NvTerm);

/*
 * nvtermequal: 1 equal, 0 unequal (D028, type-sensitive, identical words
 * are a fast path), NvTermlimit if the comparison would exceed
 * NvMaxtermdepth. nvtermprint: 0, or NvTermlimit after printing a
 * "<deep>" marker in place of the subterm it refused to descend into.
 * Both treat NvNil as "<uninitialized>" / unequal to everything.
 */
int nvtermequal(NvTerm, NvTerm);
int nvtermprint(Biobuf *, NvTerm);

/*
 * Heaps. nvheapinit zeroes the heap and records the budget (0 =
 * unlimited). nvheapalloc returns the header slot of a fresh nword-word
 * object (uninitialized), or nil. nvheapexhausted reports whether the
 * last failed allocation was refused by the budget (1, system_limit) or
 * by the host allocator (0, out_of_memory). nvheapadopt takes ownership
 * of a fragment (D064), charging nvfragwords() to the budget; -1 if
 * that would exceed it, in which case the fragment is untouched.
 * nvheapcopy deep-copies src into the heap: 0 and *out set, NvTermlimit,
 * or -1. Note that NvTermerror (an NvNil src) and allocation failure are
 * both -1; a caller that must tell them apart checks src != NvNil before
 * calling. The same holds for nvfragcopy below. This is deliberate: a
 * verified program never presents NvNil, so only host code can, and it
 * knows what it passed.
 */
void nvheapinit(NvHeap *, uvlong maxwords);
void nvheapfree(NvHeap *);
NvTerm *nvheapalloc(NvHeap *, ulong nword);
int nvheapexhausted(NvHeap *);
int nvheapadopt(NvHeap *, NvFrag *);
int nvheapcopy(NvHeap *, NvTerm src, NvTerm *out);

/*
 * Explicit stopped-owner collection (M08-T04a). Each range describes
 * only term slots; the caller skips frame headers. Ranges and slots
 * must live outside the heap/adopted storage and remain stable for the
 * call. External boxed terms must be self-contained, stable fragments;
 * they are neither traversed nor moved. No concurrent heap/roots access.
 *
 * stackwords is the charged frame-stack size; need is additional word
 * budget required after collection (not allocated by this call). Returns
 * 0, NvTermlimit (live + stackwords + need cannot fit, or host allocation
 * size cannot be represented), or NvTermerror (allocation failure or
 * malformed owned object). exhausted distinguishes a resource limit
 * from other failures. On failure roots, objects and ownership remain
 * unchanged; only exhausted changes. On success all chunks/adopted
 * fragments are replaced by one contiguous chunk, words = live words.
 *
 * The collector accepts non-moving host/startup chunks too. Managed
 * execution heaps remain contiguous; their hosts service NvCollect
 * requests only at instruction boundaries.
 */
struct NvRoot {
	NvTerm *word;
	ulong nword;
	NvRoot *next;
};
int nvheapcollect(NvHeap *, NvRoot *, uvlong stackwords, uvlong need);

/*
 * Fragments (D064). nvfragcopy copies src into a new fragment: 0 and
 * *out set; NvTermlimit if the copy would exceed NvMaxtermdepth or
 * nvfragwords() would exceed maxwords (nothing is allocated in that
 * case); -1 for NvNil (NvTermerror) or on allocation failure -- see the
 * note on nvheapcopy. The cost of a refused copy is bounded by maxwords.
 * nvfragwords is nword+1.
 */
int nvfragcopy(NvTerm src, uvlong maxwords, NvFrag **out);
uvlong nvfragwords(NvFrag *);
void nvfragfree(NvFrag *);

/*
 * Standalone one-process execution (the -x/-t CLI path and the VM
 * fixtures). The argument may live in any storage; the result leaves as
 * a fragment the caller frees.
 */
int nvexecute(Biobuf *, NvModule *, char *, NvTerm, int, vlong, NvFrag **, char *, int);
int nvexecutelimits(Biobuf *, NvModule *, char *, NvTerm, int, vlong, uvlong maxheap, int gcstress, NvFrag **, char *, int);
