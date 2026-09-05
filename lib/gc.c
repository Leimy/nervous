#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../include/nvbc.h"
#include "../include/nvvm.h"

/*
 * D063: Cheney copying, with no exec, scheduler or mailbox dependency.
 * During a trial copy the original header is replaced by (offset<<4)|8.
 * Offsets, not pointers, leave the forwarding bit unambiguous even for
 * one-word empty tuples and pointers whose bit 3 is set.
 *
 * A parallel array records the source address at each to-space object
 * start. Its original header is still in to-space. This lets us restore
 * every source header if space runs out, the budget fails, or a larger
 * destination cannot be allocated. Roots are committed only after the
 * complete copy and sizing succeed. No heap pointer escapes a failure.
 * A larger trial starts over after restoration; no realloc ever moves
 * an in-progress to-space. Scratch storage is proportional to capacity.
 *
 * From-space includes host/startup chunks plus adopted fragments.
 * The result is one contiguous chunk; managed execution heaps refuse
 * the host allocator's chunk-growth fallback between collections.
 */
enum {
	Forward = 8,
	Minspace = 64,
	Retry = 1,
};

typedef struct Copy Copy;
struct Copy {
	NvHeap *heap;
	NvTerm *word;
	NvTerm **source;
	ulong cap;
	ulong top;
	uvlong limit;
};

/* Integer address ranges: unrelated malloc pointers cannot be subtracted. */
static ulong
inrange(NvTerm t, NvTerm *base, ulong n)
{
	uvlong a, b, off;

	a = t;
	b = (uintptr)base;
	if(a < b)
		return 0;
	off = a-b;
	if(off % sizeof(NvTerm) != 0 || off/sizeof(NvTerm) >= n)
		return 0;
	return n - off/sizeof(NvTerm);
}

/* Number of owned words from t to the end of its allocation, else 0. */
static ulong
owned(NvHeap *h, NvTerm t)
{
	NvChunk *c;
	NvFrag *f;
	ulong n;

	c = h->cur;
	if(c != nil && (n = inrange(t, c->word, c->top)) != 0)
		return n;
	for(c = h->full; c != nil; c = c->next)
		if((n = inrange(t, c->word, c->top)) != 0)
			return n;
	for(f = h->adopted; f != nil; f = f->next)
		if((n = inrange(t, f->word, f->nword)) != 0)
			return n;
	return 0;
}

static int
forward(Copy *c, NvTerm t, NvTerm *out)
{
	NvTerm *p, header;
	ulong avail, n, pos;

	*out = t;
	if(!NvBoxed(t) || (avail = owned(c->heap, t)) == 0)
		return 0;
	p = NvBoxptr(t);
	header = p[0];
	if(header & Forward){
		if((header >> 4) >= c->top)
			return NvTermerror;
		pos = header >> 4;
		if(c->source[pos] != p)
			return NvTermerror;
		*out = (NvTerm)(uintptr)(c->word+pos);
		return 0;
	}
	/* Validate the owned object's shape before copying any of its body. */
	if((header & 0xf8) != 0 || (header >> 8) >= avail)
		return NvTermerror;
	n = NvHdrlen(header);
	switch(NvHdrkind(header)){
	case Btuple:
		break;
	case Bint:
		if(n != 1) return NvTermerror;
		break;
	case Bref:
		if(n != 2) return NvTermerror;
		break;
	default:
		return NvTermerror;
	}
	n++;
	if(c->top > c->limit || n > c->limit-c->top)
		return NvTermlimit;
	if(n > c->cap-c->top)
		return Retry;
	pos = c->top;
	memmove(c->word+pos, p, n*sizeof(NvTerm));
	c->source[pos] = p;
	c->top += n;
	p[0] = ((NvTerm)pos<<4) | Forward;
	*out = (NvTerm)(uintptr)(c->word+pos);
	return 0;
}

static void
restore(Copy *c)
{
	ulong pos;

	for(pos = 0; pos < c->top; pos += 1+NvHdrlen(c->word[pos]))
		c->source[pos][0] = c->word[pos];
}

static int
copyroots(Copy *c, NvRoot *roots)
{
	NvRoot *r;
	NvTerm ignored;
	ulong i, scan, n;
	int rc;

	/* Discover roots without changing any caller-owned slot yet. */
	for(r = roots; r != nil; r = r->next){
		if(r->nword != 0 && r->word == nil)
			return NvTermerror;
		for(i = 0; i < r->nword; i++){
			rc = forward(c, r->word[i], &ignored);
			if(rc != 0)
				return rc;
		}
	}
	/* Only tuples carry term pointers. Ref/int bodies are opaque bits. */
	for(scan = 0; scan < c->top; scan += 1+n){
		n = NvHdrlen(c->word[scan]);
		if(NvHdrkind(c->word[scan]) != Btuple)
			continue;
		for(i = 1; i <= n; i++){
			rc = forward(c, c->word[scan+i], &c->word[scan+i]);
			if(rc != 0)
				return rc;
		}
	}
	return 0;
}

/* malloc takes ulong bytes even on amd64. Check before multiplication. */
static int
spacecap(uvlong want, ulong floor, ulong *out)
{
	uvlong cap, max;

	max = (~0UL)/sizeof(NvTerm);
	if((~0UL)/sizeof(NvTerm*) < max)
		max = (~0UL)/sizeof(NvTerm*);
	cap = Minspace;
	while(cap < floor || cap/2 < want){
		if(cap > max/2)
			return -1;
		cap *= 2;
	}
	*out = cap;
	return 0;
}

int
nvheapcollect(NvHeap *h, NvRoot *roots, uvlong stackwords, uvlong need)
{
	Copy c;
	NvChunk *dest, *old, *next;
	NvFrag *f, *fn;
	NvRoot *r;
	NvTerm t;
	ulong cap, bigger, i;
	uvlong limit;
	int rc;

	if(h == nil)
		return NvTermerror;
	h->exhausted = 0;
	limit = ~0ULL;
	if(need > limit-stackwords)
		goto limited;
	limit -= stackwords+need;
	if(h->maxwords != 0){
		if(stackwords > h->maxwords || need > h->maxwords-stackwords)
			goto limited;
		limit = h->maxwords-stackwords-need;
	}
	/* Do not shrink an existing contiguous space. */
	cap = h->cur == nil ? 0 : h->cur->cap;
	if(spacecap(need, cap, &cap) < 0)
		goto limited;
	dest = malloc(sizeof *dest);
	if(dest == nil)
		return NvTermerror;
	memset(&c, 0, sizeof c);
	c.heap = h;
	c.limit = limit;
	for(;;){
		c.cap = cap;
		c.top = 0;
		c.word = malloc(cap*sizeof(NvTerm));
		c.source = mallocz(cap*sizeof(NvTerm*), 1);
		if(c.word == nil || c.source == nil){
			rc = NvTermerror;
			break;
		}
		rc = copyroots(&c, roots);
		bigger = cap;
		if(rc == Retry){
			/* Force another power-of-two trial without overflowing ulong. */
			if(spacecap((uvlong)cap, cap, &bigger) < 0)
				rc = NvTermlimit;
		}else if(rc == 0){
			if(need > ~0ULL-c.top || spacecap(c.top+need, cap, &bigger) < 0)
				rc = NvTermlimit;
			else if(bigger != cap)
				rc = Retry;
		}
		if(rc == 0)
			break;
		restore(&c);
		if(rc != Retry)
			break;
		free(c.word);
		free(c.source);
		cap = bigger;
	}
	if(rc != 0){
		free(c.word);
		free(c.source);
		free(dest);
		h->exhausted = rc == NvTermlimit;
		return rc;
	}
	/* Commit: every owned root was already forwarded; no fallible work. */
	for(r = roots; r != nil; r = r->next)
		for(i = 0; i < r->nword; i++){
			t = r->word[i];
			if(NvBoxed(t) && owned(h, t) != 0)
				r->word[i] = (NvTerm)(uintptr)(c.word+(NvBoxptr(t)[0]>>4));
		}
	dest->word = c.word;
	dest->cap = cap;
	dest->top = c.top;
	dest->next = nil;
	old = h->cur;
	if(old != nil){
		free(old->word);
		free(old);
	}
	for(old = h->full; old != nil; old = next){
		next = old->next;
		free(old->word);
		free(old);
	}
	for(f = h->adopted; f != nil; f = fn){
		fn = f->next;
		free(f);
	}
	h->cur = dest;
	h->full = nil;
	h->adopted = nil;
	h->words = c.top;
	free(c.source);
	return 0;
limited:
	h->exhausted = 1;
	return NvTermlimit;
}
