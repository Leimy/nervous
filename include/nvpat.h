typedef struct Expr Expr;
typedef struct NvPattern NvPattern;
typedef struct NvBinding NvBinding;
typedef struct NvBindings NvBindings;
typedef struct NvPatClause NvPatClause;
typedef struct NvBinseg NvBinseg;

enum {
	Pwild,
	Pvar,
	Pint,
	Patom,
	Ptuple,
	Pbin,
};

/*
 * D076: one binary pattern segment, in source order. A Bsegint decodes
 * `width` bytes (1, 2, 4 or 8) at the current position into an integer; a
 * Bsegsized extracts `size` bytes (sizeval, or the variable sizename) into
 * a fresh binary; a Bsegrest extracts all remaining bytes into a fresh
 * binary and must be the final segment. `name` is the bound variable (nil
 * for a discarded value); for a Bsegint, the Bsegtest flag plus ival
 * instead tests that the decoded bytes equal ival.
 */
enum {
	Bsegint,
	Bsegsized,
	Bsegrest,
	Bsegsigned = 1,
	Bseglittle = 2,
	Bsegtest = 4,
};

struct NvBinseg {
	int kind;
	int width;	/* Bsegint: byte count 1, 2, 4 or 8; 0 otherwise */
	int flags;	/* Bsegint: Bsegsigned, Bseglittle, Bsegtest */
	char *name;	/* bound variable, or nil for a wildcard */
	char *sizename;	/* Bsegsized: variable holding the byte count, or nil */
	vlong sizeval;	/* Bsegsized: literal count when sizename is nil */
	vlong ival;	/* Bsegint + Bsegtest: the literal the bytes must encode */
};

struct NvPattern {
	int kind;
	char *name;
	vlong ival;
	int n;
	NvPattern *elem;
	NvBinseg *seg;	/* Pbin: the segment list; nil otherwise */
	int nseg;		/* Pbin: number of segments */
};

/*
 * D061: a binding holds a term word, not storage. It is valid as long
 * as the term it was matched against is (the fragment or heap the
 * subject lives in), which is the caller's to guarantee.
 */
struct NvBinding {
	char *name;
	NvTerm value;
};

struct NvBindings {
	int n;
	NvBinding *bind;
};

struct NvPatClause {
	NvPattern *pattern;
};

/*
 * D076: the matcher takes the heap it allocates binary segment bindings
 * into (a sized or remainder segment binds a fresh, self-contained Bbin,
 * so matching one genuinely allocates, as nvtuple does). The subject
 * term itself is only read, never copied by the matcher; its storage
 * (a heap object or a mailbox fragment, D064) is the caller's to keep
 * alive for the duration of the match. h may be nil only if the pattern
 * has no sized or remainder segment that binds (integer and wildcard
 * segments allocate nothing).
 */
int nvpatternmatch(NvHeap *, NvPattern *, NvTerm, NvBindings *, char *, int);
int nvclauseselect(NvHeap *, NvPatClause *, int, NvTerm, NvBindings *, int *, char *, int);
NvPattern *nvpatternfromexpr(Expr *, char *, int);
void nvpatternfree(NvPattern *);
NvTerm *nvbinding(NvBindings *, char *);
void nvbindingsfree(NvBindings *);
