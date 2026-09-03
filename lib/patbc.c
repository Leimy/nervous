#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../include/nvbc.h"
#include "../include/nvvm.h"
#include "../include/nvpat.h"
#include "../include/nvpatbc.h"
#include "../include/nvalloc.h"

#define malloc nvmalloc
#define mallocz nvmallocz
#define realloc nvrealloc
#define strdup nvstrdup

typedef struct Builder Builder;
struct Builder {
	NvPatCode *code;
	int constbase;
	int failpc;
	char *err;
	int nerr;
};

void
nvpatterncodefree(NvPatCode *c)
{
	int i;

	if(c == nil)
		return;
	for(i = 0; i < c->nconst; i++)
		free(c->konst[i].text);
	for(i = 0; i < c->nbind; i++)
		free(c->bind[i].name);
	free(c->insn);
	free(c->konst);
	free(c->bind);
	memset(c, 0, sizeof *c);
}

static int
emit(Builder *b, int op, int a, int c, int d)
{
	NvInsn *p;

	if(b->code->ninsn == NvMaxitem){
		snprint(b->err, b->nerr, "pattern instruction limit exceeded");
		return -1;
	}
	p = realloc(b->code->insn, (b->code->ninsn+1)*sizeof *p);
	if(p == nil){
		snprint(b->err, b->nerr, "out_of_memory");
		return -1;
	}
	b->code->insn = p;
	memset(&p[b->code->ninsn], 0, sizeof p[b->code->ninsn]);
	p[b->code->ninsn].op = op;
	p[b->code->ninsn].a = a;
	p[b->code->ninsn].b = c;
	p[b->code->ninsn].c = d;
	b->code->ninsn++;
	return 0;
}

static int
constant(Builder *b, int kind, vlong n, char *s)
{
	NvConst *k, *p;
	int i;

	for(i = 0; i < b->code->nconst; i++){
		k = &b->code->konst[i];
		if(k->kind == kind && (kind == Kint ? k->ival == n : strcmp(k->text, s) == 0))
			return b->constbase+i;
	}
	if(b->code->nconst == NvMaxitem){
		snprint(b->err, b->nerr, "pattern constant limit exceeded");
		return -1;
	}
	p = realloc(b->code->konst, (b->code->nconst+1)*sizeof *p);
	if(p == nil){
		snprint(b->err, b->nerr, "out_of_memory");
		return -1;
	}
	b->code->konst = p;
	k = &p[b->code->nconst];
	memset(k, 0, sizeof *k);
	k->kind = kind;
	k->ival = n;
	if(kind != Kint){
		k->text = strdup(s);
		if(k->text == nil){
			snprint(b->err, b->nerr, "out_of_memory");
			return -1;
		}
	}
	return b->constbase+b->code->nconst++;
}

static NvPatReg *
findbind(NvPatCode *c, char *name)
{
	int i;

	for(i = 0; i < c->nbind; i++)
		if(strcmp(c->bind[i].name, name) == 0)
			return &c->bind[i];
	return nil;
}

static int
newreg(Builder *b)
{
	if(b->code->nextreg >= NvMaxreg){
		snprint(b->err, b->nerr, "pattern register limit exceeded");
		return -1;
	}
	return b->code->nextreg++;
}

static int
addbind(Builder *b, char *name, int reg)
{
	NvPatReg *p;

	p = realloc(b->code->bind, (b->code->nbind+1)*sizeof *p);
	if(p == nil){
		snprint(b->err, b->nerr, "out_of_memory");
		return -1;
	}
	b->code->bind = p;
	p[b->code->nbind].name = strdup(name);
	if(p[b->code->nbind].name == nil){
		snprint(b->err, b->nerr, "out_of_memory");
		return -1;
	}
	p[b->code->nbind].reg = reg;
	b->code->nbind++;
	return 0;
}

static int
compile(Builder *b, NvPattern *p, int src)
{
	NvPatReg *binding;
	int i, r, k;

	switch(p->kind){
	case Pwild:
		return 0;
	case Pvar:
		binding = findbind(b->code, p->name);
		if(binding != nil)
			return emit(b, Otesteq, binding->reg, src, b->failpc);
		r = newreg(b);
		if(r < 0 || emit(b, Omove, r, src, 0) < 0 || addbind(b, p->name, r) < 0)
			return -1;
		return 0;
	case Pint:
		k = constant(b, Kint, p->ival, nil);
		if(k < 0)
			return -1;
		return emit(b, Otestint, src, k, b->failpc);
	case Patom:
		k = constant(b, Katom, 0, p->name);
		if(k < 0)
			return -1;
		return emit(b, Otestatom, src, k, b->failpc);
	case Ptuple:
		if(emit(b, Otestarity, src, p->n, b->failpc) < 0)
			return -1;
		for(i = 0; i < p->n; i++){
			if(p->elem[i].kind == Pwild)
				continue;
			r = newreg(b);
			if(r < 0 || emit(b, Ogetelem, r, src, i) < 0 || compile(b, &p->elem[i], r) < 0)
				return -1;
		}
		return 0;
	}
	snprint(b->err, b->nerr, "unsupported pattern kind %d", p->kind);
	return -1;
}

int
nvpatterncode(NvPattern *pattern, NvPatReg *known, int nknown, int srcreg, int firstreg, int constbase, int failpc, NvPatCode *code, char *err, int nerr)
{
	Builder b;
	int i;

	memset(code, 0, sizeof *code);
	if(srcreg < 0 || srcreg >= NvMaxreg || firstreg < 0 || firstreg > NvMaxreg || constbase < 0 || failpc < 0){
		snprint(err, nerr, "bad pattern lowering base");
		return -1;
	}
	if(nknown < 0 || nknown != 0 && known == nil){
		snprint(err, nerr, "bad known pattern bindings");
		return -1;
	}
	if(nknown != 0){
		code->bind = mallocz(nknown*sizeof *code->bind, 1);
		if(code->bind == nil){ snprint(err,nerr,"out_of_memory"); return -1; }
		for(i = 0; i < nknown; i++){
			if(known[i].name == nil || known[i].reg < 0 || known[i].reg >= NvMaxreg){
				snprint(err,nerr,"bad known pattern binding");
				nvpatterncodefree(code);
				return -1;
			}
			code->bind[i].name = strdup(known[i].name);
			if(code->bind[i].name == nil){
				code->nbind = i+1;
				snprint(err,nerr,"out_of_memory");
				nvpatterncodefree(code);
				return -1;
			}
			code->bind[i].reg = known[i].reg;
			code->nbind++;
		}
		code->nknown = nknown;
	}
	code->nextreg = firstreg;
	b.code = code;
	b.constbase = constbase;
	b.failpc = failpc;
	b.err = err;
	b.nerr = nerr;
	if(compile(&b, pattern, srcreg) < 0){
		nvpatterncodefree(code);
		return -1;
	}
	return 0;
}
