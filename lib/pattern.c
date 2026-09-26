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

/*
 * D077: match a binary pattern against a binary term, left to right,
 * no backtracking. Each segment either advances the byte position (an
 * integer segment by its fixed width, a binary segment by its size) or
 * fails the whole pattern. A sized or remainder segment that binds
 * allocates a fresh, self-contained Bbin (h); a later segment's failure
 * leaves that Bbin as garbage, and the caller's transactional rollback
 * (nvpatternmatch) discards the binding. The pattern must consume the
 * complete value: a non-remainder final segment leaves pos < len, which
 * is a trailing-byte mismatch.
 */
static int
matchbin(NvHeap *h, NvPattern *p, NvTerm v, NvBindings *b)
{
	uchar *buf;
	uvlong len, pos;
	int i, bv;
	NvBinseg *s;
	NvTerm bt, *sv;
	vlong val, size;

	if(nvtermkind(v) != Vbin)
		return 0;
	buf = nvbinbytes(v);
	len = nvbinlen(v);
	pos = 0;
	for(i = 0; i < p->nseg; i++){
		s = &p->seg[i];
		switch(s->kind){
		case Bsegint:
			if((uvlong)s->width > len-pos)
				return 0;
			nvbindec(buf+pos, s->width, s->flags, &val);
			if(s->name == nil && (s->flags & Bsegtest)){
				if(val != s->ival)
					return 0;
			}else if(s->name != nil){
				bt = nvint(h, val);
				if(bt == NvNil)
					return nvheapexhausted(h) ? -2 : -1;
				bv = bindvalue(b, s->name, bt);
				if(bv != 1)
					return bv;
			}
			pos += s->width;
			break;
		case Bsegsized:
			if(s->sizename != nil){
				sv = nvbinding(b, s->sizename);
				if(sv == nil || nvtermkind(*sv) != Vint)
					return 0;
				size = nvtermint(*sv);
			}else
				size = s->sizeval;
			if(size < 0 || (uvlong)size > len-pos)
				return 0;
			bt = nvbin(h, buf+pos, (uvlong)size);
			if(bt == NvNil)
				return nvheapexhausted(h) ? -2 : -1;
			if(s->name != nil){
				bv = bindvalue(b, s->name, bt);
				if(bv != 1)
					return bv;
			}
			pos += (uvlong)size;
			break;
		case Bsegrest:
			bt = nvbin(h, buf+pos, len-pos);
			if(bt == NvNil)
				return nvheapexhausted(h) ? -2 : -1;
			if(s->name != nil){
				bv = bindvalue(b, s->name, bt);
				if(bv != 1)
					return bv;
			}
			pos = len;
			break;
		}
	}
	return pos == len ? 1 : 0;
}

static int
match(NvHeap *h, NvPattern *p, NvTerm v, NvBindings *b)
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
			r = match(h, &p->elem[i], nvtupleelem(v, i), b);
			if(r <= 0)
				return r;
		}
		return 1;
	case Pbin:
		return matchbin(h, p, v, b);
	}
	return 0;
}

int
nvclauseselect(NvHeap *h, NvPatClause *clause, int nclause, NvTerm v, NvBindings *b, int *which, char *err, int nerr)
{
	int i, r;

	if(which != nil)
		*which = -1;
	for(i = 0; i < nclause; i++){
		r = nvpatternmatch(h, clause[i].pattern, v, b, err, nerr);
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
nvpatternmatch(NvHeap *h, NvPattern *p, NvTerm v, NvBindings *b, char *err, int nerr)
{
	NvBindings tmp, old;
	int r;

	if(bindingscopy(&tmp, b) < 0){
		snprint(err, nerr, "out_of_memory");
		return -1;
	}
	r = match(h, p, v, &tmp);
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
