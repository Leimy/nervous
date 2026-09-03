#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../include/nvbc.h"

/* This order is the symbolic opcode contract. */
static char *opnames[] = {
	"loadk", "move", "tuple", "jump", "call", "tailcall", "return",
	"testatom", "testint", "testeq", "testarity", "getelem",
	"add", "sub", "mul", "div", "rem", "lt", "le", "gt", "ge", "fail", "nop",
	"self", "makeref", "send", "spawn",
	"recvbegin", "recvnext", "recvtake", "recvwait", "exit",
	"recvdeadline", "recvwaitdeadline",
	"print", "eprint",
};

char *
nvopname(int op)
{
	if(op < 0 || op >= nelem(opnames))
		return "badop";
	return opnames[op];
}

void
nvmodulefree(NvModule *m)
{
	int i;

	if(m == nil)
		return;
	for(i = 0; i < m->nconst; i++)
		free(m->konst[i].text);
	for(i = 0; i < m->nfunc; i++){
		free(m->func[i].name);
		free(m->func[i].insn);
	}
	free(m->konst);
	free(m->func);
	free(m);
}

static void
putinsn(Biobuf *b, int pc, NvInsn *i)
{
	Bprint(b, "%d: %s", pc, nvopname(i->op));
	switch(i->op){
	case Oloadk: case Omove: case Ojump: case Oreturn: case Ofail:
	case Oself: case Omakeref: case Orecvwait: case Oexit:
	case Orecvdeadline: case Orecvwaitdeadline:
		Bprint(b, " %d", i->a);
		if(i->op == Oloadk || i->op == Omove)
			Bprint(b, " %d", i->b);
		break;
	case Otuple: case Ocall: case Otestatom: case Otestint:
	case Osend: case Ospawn:
		Bprint(b, " %d %d %d", i->a, i->b, i->c);
		break;
	case Otailcall: case Orecvbegin: case Orecvnext: case Oprint: case Oeprint:
		Bprint(b, " %d %d", i->a, i->b);
		break;
	case Otesteq: case Otestarity: case Ogetelem:
	case Oadd: case Osub: case Omul: case Odiv: case Orem:
	case Olt: case Ole: case Ogt: case Oge:
		Bprint(b, " %d %d %d", i->a, i->b, i->c);
		break;
	case Onop: case Orecvtake:
		break;
	}
	Bputc(b, '\n');
}

void
nvdisasm(Biobuf *b, NvModule *m)
{
	NvConst *k;
	NvFunc *f;
	int i, j;

	Bprint(b, "module\n");
	for(i = 0; i < m->nconst; i++){
		k = &m->konst[i];
		switch(k->kind){
		case Kint: Bprint(b, "const int %lld\n", k->ival); break;
		case Katom: Bprint(b, "const atom %s\n", k->text); break;
		case Kfunc: Bprint(b, "const func %s\n", k->text); break;
		}
	}
	for(i = 0; i < m->nfunc; i++){
		f = &m->func[i];
		Bprint(b, "func %s %d\n", f->name, f->nreg);
		for(j = 0; j < f->ninsn; j++)
			putinsn(b, j, &f->insn[j]);
		Bprint(b, "end\n");
	}
}
