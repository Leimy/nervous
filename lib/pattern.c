#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../include/nvbc.h"
#include "../include/nvvm.h"
#include "../include/nvpat.h"

/*
 * D061: a binding holds an NvTerm word, not owned storage. Freeing a
 * bindings set only frees the name strings; the terms belong to whatever
 * fragment or heap the matched subject lives in, which is the caller's to
 * keep alive for as long as the bindings are used.
 */
void
nvbindingsfree(NvBindings *b)
{
	int i;

	if(b == nil)
		return;
	for(i = 0; i < b->n; i++)
		free(b->bind[i].name);
	free(b->bind);
	memset(b, 0, sizeof *b);
}

NvTerm *
nvbinding(NvBindings *b, char *name)
{
	int i;

	for(i = 0; i < b->n; i++)
		if(strcmp(b->bind[i].name, name) == 0)
			return &b->bind[i].value;
	return nil;
}

static int
bindingscopy(NvBindings *d, NvBindings *s)
{
	int i;

	memset(d, 0, sizeof *d);
	d->bind = mallocz(s->n*sizeof *d->bind, 1);
	if(s->n != 0 && d->bind == nil)
		return -1;
	for(i = 0; i < s->n; i++){
		d->n++;
		d->bind[i].name = strdup(s->bind[i].name);
		if(d->bind[i].name == nil){
			nvbindingsfree(d);
			return -1;
		}
		d->bind[i].value = s->bind[i].value;
	}
	return 0;
}

/*
 * Binds name to v, or, for a repeated variable, tests the new occurrence
 * against the one already bound (D002). Returns 1 (bound or equal), 0
 * (mismatch), -1 (allocation failure -- out_of_memory), or -2 (the
 * equality test itself exceeded NvMaxtermdepth -- system_limit). -1 and -2
 * are both "less than or equal to 0" so match()'s tuple recursion, which
 * propagates any r<=0 unchanged, treats them uniformly; nvpatternmatch
 * distinguishes them only when reporting the error string.
 */
static int
bindvalue(NvBindings *b, char *name, NvTerm v)
{
	NvBinding *p;
	NvTerm *old;
	int eq;

	old = nvbinding(b, name);
	if(old != nil){
		eq = nvtermequal(*old, v);
		if(eq == NvTermlimit)
			return -2;
		return eq ? 1 : 0;
	}
	p = realloc(b->bind, (b->n+1)*sizeof *p);
	if(p == nil)
		return -1;
	b->bind = p;
	b->bind[b->n].name = strdup(name);
	if(b->bind[b->n].name == nil)
		return -1;
	b->bind[b->n].value = v;
	b->n++;
	return 1;
}

static int
match(NvPattern *p, NvTerm v, NvBindings *b)
{
	int i, r;

	if(p == nil || v == NvNil)
		return 0;
	switch(p->kind){
	case Pwild:
		return 1;
	case Pvar:
		return bindvalue(b, p->name, v);
	case Pint:
		return nvtermkind(v) == Vint && nvtermint(v) == p->ival;
	case Patom:
		return nvtermkind(v) == Vatom && strcmp(nvtermatom(v), p->name) == 0;
	case Ptuple:
		if(nvtermkind(v) != Vtuple || nvtuplelen(v) != p->n)
			return 0;
		for(i = 0; i < p->n; i++){
			r = match(&p->elem[i], nvtupleelem(v, i), b);
			if(r <= 0)
				return r;
		}
		return 1;
	}
	return 0;
}

int
nvclauseselect(NvPatClause *clause, int nclause, NvTerm v, NvBindings *b, int *which, char *err, int nerr)
{
	int i, r;

	if(which != nil)
		*which = -1;
	for(i = 0; i < nclause; i++){
		r = nvpatternmatch(clause[i].pattern, v, b, err, nerr);
		if(r < 0)
			return -1;
		if(r > 0){
			if(which != nil)
				*which = i;
			return 1;
		}
	}
	return 0;
}

int
nvpatternmatch(NvPattern *p, NvTerm v, NvBindings *b, char *err, int nerr)
{
	NvBindings tmp, old;
	int r;

	if(bindingscopy(&tmp, b) < 0){
		snprint(err, nerr, "out_of_memory");
		return -1;
	}
	r = match(p, v, &tmp);
	if(r < 0){
		nvbindingsfree(&tmp);
		snprint(err, nerr, r == -2 ? "system_limit" : "out_of_memory");
		return -1;
	}
	if(r == 0){
		nvbindingsfree(&tmp);
		return 0;
	}
	old = *b;
	*b = tmp;
	nvbindingsfree(&old);
	return 1;
}
