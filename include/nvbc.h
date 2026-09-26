typedef struct NvConst NvConst;
typedef struct NvInsn NvInsn;
typedef struct NvFunc NvFunc;
typedef struct NvModule NvModule;

enum {
	NvMaxreg = 256,
	NvMaxitem = 65535,
};

enum {
	Kint,
	Katom,
	Kfunc,
};

struct NvConst {
	int kind;
	vlong ival;
	char *text;
	/*
	 * D062: for a Katom constant, the interned atom term for text, filled
	 * in once before execution (nvexecinit) instead of interning on
	 * every loadk/testatom dispatch. 0 (NvNil) until interned. Declared
	 * as uvlong rather than NvTerm because nvbc.h does not include
	 * nvvm.h; they are the same type.
	 */
	uvlong atom;
};

enum {
	Oloadk,
	Omove,
	Otuple,
	Ojump,
	Ocall,
	Otailcall,
	Oreturn,
	Otestatom,
	Otestint,
	Otesteq,
	Otestarity,
	Ogetelem,
	Oadd,
	Osub,
	Omul,
	Odiv,
	Orem,
	Olt,
	Ole,
	Ogt,
	Oge,
	Ofail,
	Onop,
	Oself,
	Omakeref,
	Osend,
	Ospawn,
	Orecvbegin,
	Orecvnext,
	Orecvtake,
	Orecvwait,
	Oexit,
	Orecvdeadline,
	Orecvwaitdeadline,
	Oprint,
	Oeprint,
	Oguard,
	Oguardend,
	Oistype,
	/* D076-D079: binary construction and matching. Construction appends
	 * (each opcode reads the running binary in src, reads the segment, and
	 * allocates a fresh self-contained Bbin holding both). Matching walks
	 * the subject binary (e->binsubj) at e->binpos; a fail-target is a
	 * mismatch (select the next clause), never a fault -- allocation
	 * failures fault the process instead. */
	Obinalloc,		/* a=dst: dst = the empty binary */
	Obinappint,		/* a=dst b=src c=value-reg d=(width<<2)|flags */
	Obinappbin,		/* a=dst b=src c=bin-reg d=size-reg */
	Obinappend,		/* a=dst b=src c=bin-reg: append all of bin's bytes */
	Obintestbinary,	/* a=src b=fail-target: set subject/pos or mismatch */
	Obinintget,		/* a=dst b=fail-target c=(width<<2)|flags */
	Obinbinget,		/* a=dst b=size-reg c=fail-target */
	Obinremget,		/* a=dst: extract remaining bytes (always succeeds) */
	Obinend,		/* a=fail-target: require complete consumption */
	Nopcode,
};

/* Operands have opcode-specific meanings documented in docs/bytecode.md. */
struct NvInsn {
	int op;
	int a;
	int b;
	int c;
	int d;
};

struct NvFunc {
	char *name;
	int nreg;
	int ninsn;
	NvInsn *insn;
};

struct NvModule {
	int nconst;
	NvConst *konst;
	int nfunc;
	NvFunc *func;
};

char *nvopname(int);
void nvmodulefree(NvModule *);
int nvverify(NvModule *, char *, int);
void nvdisasm(Biobuf *, NvModule *);
NvModule *nvread(Biobuf *, char *, char *, int);
