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

struct NvBinding {
	char *name;
	NvValue value;
};

struct NvBindings {
	int n;
	NvBinding *bind;
};

struct NvPatClause {
	NvPattern *pattern;
};

int nvpatternmatch(NvPattern *, NvValue *, NvBindings *, char *, int);
int nvclauseselect(NvPatClause *, int, NvValue *, NvBindings *, int *, char *, int);
NvPattern *nvpatternfromexpr(Expr *, char *, int);
void nvpatternfree(NvPattern *);
NvValue *nvbinding(NvBindings *, char *);
void nvbindingsfree(NvBindings *);
