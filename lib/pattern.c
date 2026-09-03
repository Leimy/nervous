#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../include/nvbc.h"
#include "../include/nvvm.h"
#include "../include/nvpat.h"

void
nvbindingsfree(NvBindings *b)
{
	int i;

	if(b == nil)
		return;
	for(i = 0; i < b->n; i++){
		free(b->bind[i].name);
		nvvaluefree(&b->bind[i].value);
	}
	free(b->bind);
	memset(b, 0, sizeof *b);
}

NvValue *
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
		if(d->bind[i].name == nil || nvvaluecopy(&d->bind[i].value, &s->bind[i].value) < 0){
			nvbindingsfree(d);
			return -1;
		}
	}
	return 0;
}

static int
bindvalue(NvBindings *b, char *name, NvValue *v)
{
	NvBinding *p;
	NvValue *old;

	old = nvbinding(b, name);
	if(old != nil)
		return nvvalueequal(old, v) ? 1 : 0;
	p = realloc(b->bind, (b->n+1)*sizeof *p);
	if(p == nil)
		return -1;
	b->bind = p;
	memset(&b->bind[b->n], 0, sizeof b->bind[b->n]);
	b->bind[b->n].name = strdup(name);
	if(b->bind[b->n].name == nil || nvvaluecopy(&b->bind[b->n].value, v) < 0){
		free(b->bind[b->n].name);
		memset(&b->bind[b->n], 0, sizeof b->bind[b->n]);
		return -1;
	}
	b->n++;
	return 1;
}

static int
match(NvPattern *p, NvValue *v, NvBindings *b)
{
	int i, r;

	if(p == nil || v == nil || !v->valid)
		return 0;
	switch(p->kind){
	case Pwild:
		return 1;
	case Pvar:
		return bindvalue(b, p->name, v);
	case Pint:
		return v->kind == Vint && v->i == p->ival;
	case Patom:
		return v->kind == Vatom && strcmp(v->atom, p->name) == 0;
	case Ptuple:
		if(v->kind != Vtuple || v->tuple->n != p->n)
			return 0;
		for(i = 0; i < p->n; i++){
			r = match(&p->elem[i], &v->tuple->elem[i], b);
			if(r <= 0)
				return r;
		}
		return 1;
	}
	return 0;
}

int
nvclauseselect(NvPatClause *clause, int nclause, NvValue *v, NvBindings *b, int *which, char *err, int nerr)
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
nvpatternmatch(NvPattern *p, NvValue *v, NvBindings *b, char *err, int nerr)
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
		snprint(err, nerr, "out_of_memory");
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
