typedef struct Expr Expr;
typedef struct NvPattern NvPattern;
typedef struct NvBinding NvBinding;
typedef struct NvBindings NvBindings;
typedef struct NvPatClause NvPatClause;

enum {
	Pwild,
	Pvar,
	Pint,
	Patom,
	Ptuple,
};

struct NvPattern {
	int kind;
	char *name;
	vlong ival;
	int n;
	NvPattern *elem;
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

int nvpatternmatch(NvPattern *, NvTerm, NvBindings *, char *, int);
int nvclauseselect(NvPatClause *, int, NvTerm, NvBindings *, int *, char *, int);
NvPattern *nvpatternfromexpr(Expr *, char *, int);
void nvpatternfree(NvPattern *);
NvTerm *nvbinding(NvBindings *, char *);
void nvbindingsfree(NvBindings *);
