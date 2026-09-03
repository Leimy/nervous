typedef struct NvPatReg NvPatReg;
typedef struct NvPatCode NvPatCode;

struct NvPatReg {
	char *name;
	int reg;
};

/* Bind registers are tentative and may be read only on the success edge. */
struct NvPatCode {
	int ninsn;
	NvInsn *insn;
	int nconst;
	NvConst *konst;
	int nbind;
	int nknown;
	NvPatReg *bind;
	int nextreg;
};

int nvpatterncode(NvPattern *pattern, NvPatReg *known, int nknown, int srcreg, int firstreg, int constbase, int failpc, NvPatCode *, char *, int);
void nvpatterncodefree(NvPatCode *);
