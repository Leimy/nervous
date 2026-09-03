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
