#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../../include/nvbc.h"
#include "../../include/nvvm.h"
#include "../../include/nvpat.h"
#include "../../include/nvpatbc.h"

static void
fail(char *s)
{
	fprint(2, "FAIL: %s\n", s);
	exits("test");
}

static void
check(int ok, char *s)
{
	if(!ok)
		fail(s);
}

void
main(void)
{
	NvPattern p, e[3];
	NvPatCode code;
	NvModule m;
	NvFunc f;
	NvConst konst[2];
	NvInsn *insn;
	char err[128];
	int i, n;

	memset(&p, 0, sizeof p);
	memset(e, 0, sizeof e);
	e[0].kind = Patom;
	e[0].name = "ok";
	e[1].kind = Pvar;
	e[1].name = "x";
	e[2].kind = Pvar;
	e[2].name = "x";
	p.kind = Ptuple;
	p.n = 3;
	p.elem = e;
	check(nvpatterncode(&p, nil, 0, 0, 1, 0, 8, &code, err, sizeof err) == 0, err);
	check(code.ninsn == 7, "unexpected lowering length");
	check(code.nconst == 1 && code.konst[0].kind == Katom && strcmp(code.konst[0].text, "ok") == 0, "atom constant lowering");
	check(code.nbind == 1 && strcmp(code.bind[0].name, "x") == 0 && code.bind[0].reg == 3, "variable binding metadata");
	check(code.insn[0].op == Otestarity && code.insn[0].a == 0 && code.insn[0].b == 3 && code.insn[0].c == 8, "arity lowering");
	check(code.insn[1].op == Ogetelem && code.insn[2].op == Otestatom, "atom lowering");
	check(code.insn[3].op == Ogetelem && code.insn[4].op == Omove, "first variable lowering");
	check(code.insn[5].op == Ogetelem && code.insn[6].op == Otesteq, "repeated variable lowering");
	print("ok - primitive pattern lowering\n");

	n = code.ninsn+2;
	insn = mallocz(n*sizeof *insn, 1);
	if(insn == nil)
		fail("instruction allocation");
	for(i = 0; i < code.ninsn; i++)
		insn[i] = code.insn[i];
	insn[7].op = Oreturn;
	insn[7].a = code.bind[0].reg;
	insn[8].op = Ofail;
	insn[8].a = 1;
	memset(konst, 0, sizeof konst);
	konst[0] = code.konst[0];
	konst[1].kind = Katom;
	konst[1].text = "match_fail";
	memset(&f, 0, sizeof f);
	f.name = "main";
	f.nreg = code.nextreg;
	f.ninsn = n;
	f.insn = insn;
	memset(&m, 0, sizeof m);
	m.nconst = 2;
	m.konst = konst;
	m.nfunc = 1;
	m.func = &f;
	check(nvverify(&m, err, sizeof err) == 0, err);
	print("ok - lowered pattern verifies\n");

	free(insn);
	nvpatterncodefree(&code);
	print("all pattern bytecode tests passed\n");
	exits(nil);
}
