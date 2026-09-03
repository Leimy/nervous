#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../include/nervous.h"
#include "../include/nvbc.h"
#include "../include/nvvm.h"
#include "../include/nvpat.h"
#include "../include/nvalloc.h"

#define malloc nvmalloc
#define mallocz nvmallocz
#define realloc nvrealloc
#define strdup nvstrdup

static void
patternclear(NvPattern *p)
{
	int i;

	if(p == nil)
		return;
	free(p->name);
	for(i = 0; i < p->n; i++)
		patternclear(&p->elem[i]);
	free(p->elem);
	memset(p, 0, sizeof *p);
}

void
nvpatternfree(NvPattern *p)
{
	if(p == nil)
		return;
	patternclear(p);
	free(p);
}

static int
count(Exprs *x)
{
	int n;

	for(n = 0; x != nil; x = x->next)
		n++;
	return n;
}

static int
convert(NvPattern *p, Expr *e, char *err, int nerr)
{
	Exprs *x;
	int i, n;

	memset(p, 0, sizeof *p);
	if(e == nil){
		snprint(err, nerr, "nil pattern");
		return -1;
	}
	switch(e->kind){
	case Ewild:
		p->kind = Pwild;
		return 0;
	case Evar:
		p->kind = Pvar;
		p->name = strdup(e->text);
		break;
	case Eint:
		p->kind = Pint;
		p->ival = e->ival;
		return 0;
	case Eatom:
		p->kind = Patom;
		p->name = strdup(e->text);
		break;
	case Etuple:
		p->kind = Ptuple;
		n = count(e->list);
		p->elem = mallocz(n*sizeof *p->elem, 1);
		if(n != 0 && p->elem == nil)
			break;
		p->n = n;
		for(i = 0, x = e->list; x != nil; i++, x = x->next)
			if(convert(&p->elem[i], x->expr, err, nerr) < 0){
				patternclear(p);
				return -1;
			}
		return 0;
	default:
		snprint(err, nerr, "expression at %d:%d is not a supported pattern", e->span.line, e->span.col);
		return -1;
	}
	if(p->name != nil)
		return 0;
	patternclear(p);
	snprint(err, nerr, "out_of_memory");
	return -1;
}

NvPattern *
nvpatternfromexpr(Expr *e, char *err, int nerr)
{
	NvPattern *p;

	p = mallocz(sizeof *p, 1);
	if(p == nil){
		snprint(err, nerr, "out_of_memory");
		return nil;
	}
	if(convert(p, e, err, nerr) < 0){
		free(p);
		return nil;
	}
	return p;
}
