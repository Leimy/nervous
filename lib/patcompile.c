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
segclear(NvBinseg *s)
{
	free(s->name);
	free(s->sizename);
}

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
	/* D076: a binary pattern's segment list is a flat array of NvBinseg;
	 * each holds up to two strings. Freed here, not in nvpatternfree, so
	 * every recursive clear (and the Ptuple recursion above) covers it. */
	for(i = 0; i < p->nseg; i++)
		segclear(&p->seg[i]);
	free(p->seg);
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

/*
 * D076/D077: an Ebinagg pattern to Pbin. The parser (patternok) has
 * already restricted segment values and sizes to the pattern forms and
 * placed any remainder last. The one check left here is the size
 * direction: a size variable may name an enclosing binding or a
 * variable bound by an EARLIER segment, never one bound by the same or
 * a later segment of this binary (the milestone's "invalid forward
 * size reference rejected by the frontend").
 */
static int
convertbin(NvPattern *p, Expr *e, char *err, int nerr)
{
	Exprs *x, *y;
	Expr *s, *v;
	NvBinseg *g;
	int i, n;

	p->kind = Pbin;
	n = count(e->list);
	if(n == 0)
		return 0;
	p->seg = mallocz(n*sizeof *p->seg, 1);
	if(p->seg == nil)
		goto nomem;
	p->nseg = n;
	for(i = 0, x = e->list; x != nil; i++, x = x->next){
		s = x->expr;
		v = s->left;
		g = &p->seg[i];
		switch(s->ival >> 8){
		case Binsegint:
			g->kind = Bsegint;
			g->width = (s->ival >> 2) & 0x3f;
			if(s->ival & 2)
				g->flags |= Bsegsigned;
			if(s->ival & 1)
				g->flags |= Bseglittle;
			if(v->kind == Eint){
				g->flags |= Bsegtest;
				g->ival = v->ival;
			}else if(v->kind == Eunary){
				g->flags |= Bsegtest;
				g->ival = -v->left->ival;
			}
			break;
		case Binsegsized:
			g->kind = Bsegsized;
			if(s->right->kind == Eint)
				g->sizeval = s->right->ival;
			else{
				for(y = x; y != nil; y = y->next)
					if(y->expr->left->kind == Evar &&
					   strcmp(y->expr->left->text, s->right->text) == 0){
						snprint(err, nerr, "binary size %s at %d:%d is bound by this or a later segment",
							s->right->text, s->right->span.line, s->right->span.col);
						patternclear(p);
						return -1;
					}
				g->sizename = strdup(s->right->text);
				if(g->sizename == nil)
					goto nomem;
			}
			break;
		default:
			g->kind = Bsegrest;
			break;
		}
		if(v->kind == Evar){
			g->name = strdup(v->text);
			if(g->name == nil)
				goto nomem;
		}
	}
	return 0;
nomem:
	patternclear(p);
	snprint(err, nerr, "out_of_memory");
	return -1;
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
	case Eunary:
		/* D060: a negative integer literal pattern is the constant it denotes. */
		if(strcmp(e->text, "-") == 0 && e->left != nil && e->left->kind == Eint){
			p->kind = Pint;
			p->ival = -e->left->ival;
			return 0;
		}
		snprint(err, nerr, "expression at %d:%d is not a supported pattern", e->span.line, e->span.col);
		return -1;
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
	case Ebinagg:
		return convertbin(p, e, err, nerr);
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
