#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../include/nvbc.h"
#include "../include/nvvm.h"

/*
 * D061-D066: NvTerm, NvHeap, NvFrag. This file replaces the old NvValue
 * implementation (a struct that owned its storage and was deep-copied on
 * every move) with the tagged-word representation nvvm.h declares. There
 * is no free function for a term any more: storage belongs to a heap
 * (freed whole by nvheapfree) or a fragment (freed whole by nvfragfree).
 *
 * Host construction and startup copying use non-moving chunks. Managed
 * execution heaps refuse chunk growth: D067 reservation returns NvCollect
 * before an instruction, and the host calls lib/gc.c while the owner is
 * stopped. Constructors never collect or invalidate C locals themselves.
 */

/*
 * D062: one runtime-wide, append-only atom table. Open hashing with
 * chaining keeps intern O(1) average, same as the old NvValue atom
 * table. What's new here is atomtext[]: a plain index -> text array,
 * grown by realloc, so an atom term's payload (the table index) can be
 * turned back into text in O(1) without walking the hash chain.
 */
enum {
	NvAtomHashsize = 4096,
	NvAtomDefaultlimit = 65536,
	NvAtomTextinitial = 64,
};

/*
 * Private heap chunk sizing (D063 "the only sizing policy in this
 * milestone" applies to the stage-3 collector's to-space; stage 2's
 * chunk list uses this simpler policy: start small, double, cap, but
 * never refuse a single object bigger than the cap).
 */
enum {
	NvHeapminchunk = 64,
	NvHeapmaxchunk = 65536,
};

typedef struct NvAtomEntry NvAtomEntry;
struct NvAtomEntry {
	char *text;
	ulong index;
	NvAtomEntry *next;
};

static NvAtomEntry *atomtab[NvAtomHashsize];
static char **atomtext;
static ulong atomtextcap;
static ulong atomcount;
static ulong atommax = NvAtomDefaultlimit;

static ulong
atomhash(char *s)
{
	ulong h;
	uchar *p;

	h = 0;
	for(p = (uchar*)s; *p != 0; p++)
		h = h*33 + *p;
	return h;
}

static NvAtomEntry *
atomfind(char *s, ulong h)
{
	NvAtomEntry *e;

	for(e = atomtab[h]; e != nil; e = e->next)
		if(strcmp(e->text, s) == 0)
			return e;
	return nil;
}

/* D062: nvatomlookup never interns; NvNil if the atom has not been interned. */
NvTerm
nvatomlookup(char *s)
{
	ulong h;
	NvAtomEntry *e;

	if(s == nil)
		return NvNil;
	h = atomhash(s) % NvAtomHashsize;
	e = atomfind(s, h);
	if(e == nil)
		return NvNil;
	return ((NvTerm)e->index << 2) | NvTagatom;
}

/* D062: nvatom interns, returning NvNil if the table is at its limit or out of memory. */
NvTerm
nvatom(char *s)
{
	ulong h;
	NvAtomEntry *e;
	char *dup;
	char **newtext;
	ulong newcap;

	if(s == nil)
		return NvNil;
	h = atomhash(s) % NvAtomHashsize;
	e = atomfind(s, h);
	if(e != nil)
		return ((NvTerm)e->index << 2) | NvTagatom;
	if(atomcount >= atommax)
		return NvNil;
	if(atomcount >= atomtextcap){
		newcap = atomtextcap != 0 ? atomtextcap*2 : NvAtomTextinitial;
		newtext = realloc(atomtext, newcap*sizeof(char*));
		if(newtext == nil)
			return NvNil;
		atomtext = newtext;
		atomtextcap = newcap;
	}
	e = malloc(sizeof *e);
	if(e == nil)
		return NvNil;
	dup = strdup(s);
	if(dup == nil){
		free(e);
		return NvNil;
	}
	e->text = dup;
	e->index = atomcount;
	e->next = atomtab[h];
	atomtab[h] = e;
	atomtext[atomcount] = dup;
	atomcount++;
	return ((NvTerm)e->index << 2) | NvTagatom;
}

int
nvatomlimit(ulong max)
{
	if(max < atomcount)
		return -1;
	atommax = max;
	return 0;
}

ulong
nvatomcount(void)
{
	return atomcount;
}

/* D061: pack slot/generation into the PID payload; mask defensively at both ends. */
NvTerm
nvpid(ulong slot, ulong generation)
{
	return ((NvTerm)generation << (2+NvPidslotbits)) | (((NvTerm)slot & NvMaxslot) << 2) | NvTagpid;
}

ulong
nvpidslot(NvTerm t)
{
	return (ulong)((t >> 2) & NvMaxslot);
}

ulong
nvpidgeneration(NvTerm t)
{
	return (ulong)((t >> (2+NvPidslotbits)) & NvMaxgeneration);
}

/*
 * D061: a small int is value<<2|1, sign-extended by an arithmetic shift
 * of the term reinterpreted as a signed vlong (nvtermint below). A value
 * outside [NvMinsmall, NvMaxsmall] boxes as Bint (1 body word); NvNil if
 * there is no heap to box into or the allocation/budget fails.
 */
NvTerm
nvint(NvHeap *h, vlong v)
{
	NvTerm *w;

	if(v >= NvMinsmall && v <= NvMaxsmall)
		return ((NvTerm)v << 2) | NvTagint;
	if(h == nil)
		return NvNil;
	w = nvheapalloc(h, 2);
	if(w == nil)
		return NvNil;
	w[0] = NvHdr(Bint, 1);
	w[1] = (NvTerm)v;
	return (NvTerm)(uintptr)w;
}

NvTerm
nvref(NvHeap *h, uvlong incarnation, uvlong counter)
{
	NvTerm *w;

	if(h == nil)
		return NvNil;
	w = nvheapalloc(h, 3);
	if(w == nil)
		return NvNil;
	w[0] = NvHdr(Bref, 2);
	w[1] = incarnation;
	w[2] = counter;
	return (NvTerm)(uintptr)w;
}

/* D061: construction never traverses; the n element words are copied, not walked. */
NvTerm
nvtuple(NvHeap *h, NvTerm *elem, int n)
{
	NvTerm *w;

	if(h == nil || n < 0)
		return NvNil;
	w = nvheapalloc(h, 1+n);
	if(w == nil)
		return NvNil;
	w[0] = NvHdr(Btuple, n);
	if(n > 0)
		memmove(w+1, elem, n*sizeof(NvTerm));
	return (NvTerm)(uintptr)w;
}

int
nvtermkind(NvTerm t)
{
	if(t == NvNil)
		return Vnil;
	switch(t & NvTagmask){
	case NvTagint:
		return Vint;
	case NvTagatom:
		return Vatom;
	case NvTagpid:
		return Vpid;
	}
	switch(NvHdrkind(NvBoxptr(t)[0])){
	case Btuple:
		return Vtuple;
	case Bref:
		return Vref;
	case Bint:
		return Vint;
	case Bbin:
		return Vbin;
	}
	return Vnil;	/* unreachable for a verified program: unknown header kind */
}

vlong
nvtermint(NvTerm t)
{
	if((t & NvTagmask) == NvTagint)
		return (vlong)t >> 2;
	return (vlong)NvBoxptr(t)[1];
}

char *
nvtermatom(NvTerm t)
{
	return atomtext[t>>2];
}

uvlong
nvrefincarnation(NvTerm t)
{
	return NvBoxptr(t)[1];
}

uvlong
nvrefcounter(NvTerm t)
{
	return NvBoxptr(t)[2];
}

int
nvtuplelen(NvTerm t)
{
	return (int)NvHdrlen(NvBoxptr(t)[0]);
}

NvTerm *
nvtupleelems(NvTerm t)
{
	return NvBoxptr(t)+1;
}

NvTerm
nvtupleelem(NvTerm t, int i)
{
	return NvBoxptr(t)[1+i];
}

/* D076: a binary is a self-contained boxed kind; the body is a byte count
 * (word 0) followed by the bytes, zero-padded to a whole word. */
uvlong
nvbinlen(NvTerm t)
{
	return (uvlong)NvBoxptr(t)[1];
}

void *
nvbinbytes(NvTerm t)
{
	return (void*)(NvBoxptr(t)+2);
}

/*
 * D076: a binary is 1 (header) + 1 (count) + ceil(nbytes/8) words. Returns
 * 0 if nbytes is too large to be represented as a heap object, so the
 * caller (a constructor or the D079 sizing reservation) reports
 * system_limit/out_of_memory rather than overflowing the word arithmetic.
 */
uvlong
nvbinwords(uvlong nbytes)
{
	uvlong w;

	/* nvheapalloc takes a ulong word count (32 bits on Plan 9). */
	if(nbytes > ~0ULL-7)
		return 0;
	w = (nbytes+7)/8;
	if(w > (~0UL)/sizeof(NvTerm)-2)
		return 0;
	return w+2;
}

NvTerm
nvbin(NvHeap *h, void *bytes, uvlong nbytes)
{
	NvTerm *w;
	uvlong n;

	n = nvbinwords(nbytes);
	if(n == 0)
		return NvNil;
	w = nvheapalloc(h, (ulong)n);
	if(w == nil)
		return NvNil;
	w[n-1] = 0;
	w[0] = NvHdr(Bbin, n-1);
	w[1] = (NvTerm)nbytes;
	if(nbytes != 0 && bytes != nil)
		memmove(w+2, bytes, nbytes);
	return (NvTerm)(uintptr)w;
}

/*
 * D077: allocate a fresh self-contained binary holding cur's bytes followed
 * by addlen bytes from addbytes. One GC-safe append unit: the running
 * binary is read before the allocation and the result written to the
 * destination register after, so no C pointer into a heap survives the
 * allocation (D063). Returns NvNil on overflow or allocation failure.
 */
NvTerm
nvbinapp(NvHeap *h, NvTerm cur, void *add, uvlong addlen)
{
	uvlong clen, total;
	NvTerm *w;
	uvlong n;

	clen = nvbinlen(cur);
	if(clen > ~0ULL-addlen)
		return NvNil;
	total = clen+addlen;
	n = nvbinwords(total);
	if(n == 0)
		return NvNil;
	w = nvheapalloc(h, (ulong)n);
	if(w == nil)
		return NvNil;
	w[n-1] = 0;
	w[0] = NvHdr(Bbin, n-1);
	w[1] = (NvTerm)total;
	if(clen != 0)
		memmove(w+2, nvbinbytes(cur), clen);
	if(addlen != 0 && add != nil)
		memmove((uchar*)(w+2)+clen, add, addlen);
	return (NvTerm)(uintptr)w;
}

/*
 * D078: encode val into width bytes (width in 1, 2, 4, 8) at out, per
 * flags (bit 0 signed, bit 1 little-endian; the Bsegsigned/Bseglittle
 * values in nvpat.h). The caller range-checks val with nvbinfits first.
 */
void
nvbinenc(vlong val, int width, int flags, uchar *out)
{
	uchar b[8];
	int i;

	for(i = 0; i < width; i++){
		if(flags & 2)
			b[i] = (uchar)((val>>(i*8))&0xff);
		else
			b[width-1-i] = (uchar)((val>>(i*8))&0xff);
	}
	memmove(out, b, width);
}

/* D078: decode width bytes at src (bit 0 signed, bit 1 little) into out. */
void
nvbindec(uchar *src, int width, int flags, vlong *out)
{
	uchar b[8];
	vlong v, mask;
	int i;

	for(i = 0; i < width; i++){
		if(flags & 2)
			b[width-1-i] = src[i];
		else
			b[i] = src[i];
	}
	v = 0;
	for(i = 0; i < width; i++)
		v = (v<<8) | b[i];
	if((flags & 1) && width < 8){
		mask = 1LL << (width*8-1);
		if(v & mask)
			v |= ~((1LL << (width*8))-1);
	}
	*out = v;
}

/* D078: 1 if val fits in width bytes signed (flags bit 0) or unsigned. */
int
nvbinfits(vlong val, int width, int flags)
{
	vlong hi, lo;

	if(width == 8)
		return (flags & 1) || val >= 0;
	if(flags & 1){
		lo = -(1LL<<(width*8-1));
		hi = (1LL<<(width*8-1))-1;
	}else{
		lo = 0;
		hi = (vlong)((1ULL<<(width*8))-1);
	}
	return val >= lo && val <= hi;
}

/*
 * D028/D061 equality. Identical words are the fast path (also covers two
 * NvNil words, which never happens in a verified program). Atoms and
 * PIDs are immediates, so once the fast path fails and the kinds still
 * match, the words differ and the terms are unequal -- no separate
 * comparison is needed for those cases below.
 *
 * The depth check comes before the fast path so that equality and the
 * copies (fragcount, heapcopy) agree exactly on where the ceiling is:
 * every traversal refuses a term at depth NvMaxtermdepth+1 whatever its
 * kind. Otherwise two independently built chains whose innermost
 * elements happen to be the same immediate word would compare "equal"
 * one level below the depth at which a copy of either is refused.
 */
/*
 * D080: one node visit against an absolute ceiling. `limit` is the
 * count at which this traversal must stop, computed once at entry by
 * worklimit() from the caller's cumulative count plus its per-call
 * max, so the accumulated count of earlier traversals does not eat
 * into this one's allowance. nil work is free and unlimited. A macro,
 * not a function: 7c does not inline, and a call per node on the
 * uncounted path measured as a visible slowdown of host-side equality
 * (bench/README.md, "D080 build").
 */
#define VISIT(w, limit)	((w) != nil && ++(w)->count > (limit))

static uvlong
worklimit(NvWork *w)
{
	if(w == nil || w->max == 0 || w->count > ~0ULL-w->max)
		return ~0ULL;
	return w->count + w->max;
}

static int
termequal(NvTerm a, NvTerm b, ulong depth, NvWork *w, uvlong limit)
{
	int ka, kb, n, i, rc;

	if(depth > NvMaxtermdepth)
		return NvTermlimit;
	if(VISIT(w, limit))
		return NvTermlimit;
	if(a == b)
		return 1;
	ka = nvtermkind(a);
	kb = nvtermkind(b);
	if(ka != kb)
		return 0;
	switch(ka){
	case Vint:
		return nvtermint(a) == nvtermint(b);
	case Vref:
		return nvrefincarnation(a) == nvrefincarnation(b) && nvrefcounter(a) == nvrefcounter(b);
	case Vbin:
		/* D076/D028: byte-exact, no term depth (the bytes are not terms). */
		return nvbinlen(a) == nvbinlen(b) &&
			memcmp(nvbinbytes(a), nvbinbytes(b), nvbinlen(a)) == 0;
	case Vtuple:
		n = nvtuplelen(a);
		if(n != nvtuplelen(b))
			return 0;
		for(i = 0; i < n; i++){
			rc = termequal(nvtupleelem(a, i), nvtupleelem(b, i), depth+1, w, limit);
			if(rc != 1)
				return rc;
		}
		return 1;
	default:
		/* Vatom, Vpid, Vnil: distinct words already means unequal. */
		return 0;
	}
}

int
nvtermequal(NvTerm a, NvTerm b)
{
	return termequal(a, b, 1, nil, ~0ULL);
}

int
nvtermequalw(NvTerm a, NvTerm b, NvWork *w)
{
	return termequal(a, b, 1, w, worklimit(w));
}

static int
termprint(Biobuf *b, NvTerm t, ulong depth, NvWork *w, uvlong limit)
{
	int i, n, rc, hit;

	if(t == NvNil){
		Bprint(b, "<uninitialized>");
		return 0;
	}
	if(depth > NvMaxtermdepth){
		Bprint(b, "<deep>");
		return NvTermlimit;
	}
	if(VISIT(w, limit)){
		Bprint(b, "<limit>");
		return NvTermlimit;
	}
	switch(nvtermkind(t)){
	case Vint:
		Bprint(b, "%lld", nvtermint(t));
		return 0;
	case Vatom:
		Bprint(b, "'%s", nvtermatom(t));
		return 0;
	case Vpid:
		Bprint(b, "<pid:%lud:%lud>", nvpidslot(t), nvpidgeneration(t));
		return 0;
	case Vref:
		Bprint(b, "<ref:%llux:%llud>", nvrefincarnation(t), nvrefcounter(t));
		return 0;
	case Vbin: {
		uvlong n, i;
		uchar *p;

		/* D076: <<b0, b1, ...>> with each byte a decimal 0-255. */
		Bprint(b, "<<");
		n = nvbinlen(t);
		p = nvbinbytes(t);
		for(i = 0; i < n; i++){
			if(i != 0)
				Bprint(b, ", ");
			Bprint(b, "%ud", p[i]);
		}
		Bprint(b, ">>");
		return 0;
	}
	case Vtuple:
		n = nvtuplelen(t);
		Bprint(b, "${");
		hit = 0;
		for(i = 0; i < n; i++){
			if(i != 0)
				Bprint(b, ", ");
			rc = termprint(b, nvtupleelem(t, i), depth+1, w, limit);
			if(rc == NvTermlimit){
				hit = 1;
				/*
				 * D080: a depth refusal prints its marker and lets the
				 * siblings print; a work refusal must stop the whole
				 * traversal, or the remaining siblings would keep
				 * spending the visits the ceiling just denied. Close
				 * this tuple so the output stays well-formed.
				 */
				if(w != nil && w->count > limit){
					Bprint(b, "}");
					return NvTermlimit;
				}
			}
		}
		Bprint(b, "}");
		return hit ? NvTermlimit : 0;
	default:
		Bprint(b, "<bad-value>");
		return 0;
	}
}

int
nvtermprint(Biobuf *b, NvTerm t)
{
	return termprint(b, t, 1, nil, ~0ULL);
}

int
nvtermprintw(Biobuf *b, NvTerm t, NvWork *w)
{
	return termprint(b, t, 1, w, worklimit(w));
}

/* D063: a zeroed NvHeap is a valid empty heap; init only records the word budget. */
void
nvheapinit(NvHeap *h, uvlong maxwords)
{
	if(h == nil)
		return;
	memset(h, 0, sizeof *h);
	h->maxwords = maxwords;
}

void
nvheapfree(NvHeap *h)
{
	NvChunk *c, *cn;
	NvFrag *f, *fn;

	if(h == nil)
		return;
	if(h->cur != nil){
		free(h->cur->word);
		free(h->cur);
	}
	for(c = h->full; c != nil; c = cn){
		cn = c->next;
		free(c->word);
		free(c);
	}
	for(f = h->adopted; f != nil; f = fn){
		fn = f->next;
		free(f);
	}
	memset(h, 0, sizeof *h);
}

/*
 * Bump allocation over a chunk list. A new chunk is sized to the larger
 * of the requested object, twice the current chunk (growth), and a
 * small minimum, capped at a maximum -- except a single object bigger
 * than the cap still gets a chunk large enough to hold it. The old
 * current chunk moves to `full`, kept only so nvheapfree can free it.
 */
NvTerm *
nvheapalloc(NvHeap *h, ulong nword)
{
	NvChunk *c;
	ulong cap;
	NvTerm *w;

	if(h == nil)
		return nil;
	h->exhausted = 0;
	if(h->words > ~0ULL-h->stackwords ||
	   nword > ~0ULL-h->stackwords-h->words ||
	   (h->maxwords != 0 && (h->stackwords > h->maxwords ||
	    h->words > h->maxwords-h->stackwords ||
	    nword > h->maxwords-h->stackwords-h->words))){
		h->exhausted = 1;
		return nil;
	}
	if(h->cur != nil && nword <= h->cur->cap-h->cur->top){
		w = h->cur->word + h->cur->top;
		h->cur->top += nword;
		h->words += nword;
		return w;
	}
	/* Managed heaps grow only at a stopped-owner collection boundary. */
	if(h->managed)
		return nil;
	if(nword > (~0UL)/sizeof(NvTerm)){
		h->exhausted = 1;
		return nil;
	}
	cap = nword;
	if(h->cur != nil && 2*h->cur->cap > cap)
		cap = 2*h->cur->cap;
	if(cap < NvHeapminchunk)
		cap = NvHeapminchunk;
	if(cap > NvHeapmaxchunk && nword <= NvHeapmaxchunk)
		cap = NvHeapmaxchunk;
	if(cap > (~0UL)/sizeof(NvTerm)){
		h->exhausted = 1;
		return nil;
	}
	c = malloc(sizeof *c);
	if(c == nil){
		h->exhausted = 0;
		return nil;
	}
	c->word = malloc(cap*sizeof(NvTerm));
	if(c->word == nil){
		free(c);
		h->exhausted = 0;
		return nil;
	}
	c->cap = cap;
	c->top = nword;
	c->next = nil;
	if(h->cur != nil){
		h->cur->next = h->full;
		h->full = h->cur;
	}
	h->cur = c;
	h->words += nword;
	return c->word;
}

/* D063/D066: why the last refusal happened -- 1 the word budget (system_limit), 0 the host allocator (out_of_memory). */
int
nvheapexhausted(NvHeap *h)
{
	return h != nil && h->exhausted;
}

/* D064: adopt a taken fragment, charging its word count to the same budget as the heap. */
int
nvheapadopt(NvHeap *h, NvFrag *f)
{
	uvlong fw;

	if(h == nil || f == nil)
		return -1;
	fw = nvfragwords(f);
	h->exhausted = 0;
	if(h->words > ~0ULL-h->stackwords || fw > ~0ULL-h->stackwords-h->words ||
	   (h->maxwords != 0 && (h->stackwords > h->maxwords ||
	    h->words > h->maxwords-h->stackwords || fw > h->maxwords-h->stackwords-h->words))){
		h->exhausted = 1;
		return -1;
	}
	f->next = h->adopted;
	h->adopted = f;
	h->words += fw;
	return 0;
}

/*
 * Deep copy into a heap, allocating each boxed object with nvheapalloc.
 * Tuple children are copied into a scratch array first and handed to
 * nvtuple() in one call, rather than filling a held heap pointer while
 * recursing -- the D063 discipline of never holding a heap pointer
 * across a call that might allocate, applied at construction time.
 */
static int
heapcopy(NvHeap *h, NvTerm src, NvTerm *out, ulong depth, NvWork *w, uvlong limit)
{
	NvTerm *tmp, t;
	int i, n, rc;

	if(src == NvNil)
		return NvTermerror;
	if(depth > NvMaxtermdepth)
		return NvTermlimit;
	if(VISIT(w, limit))
		return NvTermlimit;
	switch(src & NvTagmask){
	case NvTagint:
	case NvTagatom:
	case NvTagpid:
		*out = src;
		return 0;
	}
	switch(NvHdrkind(NvBoxptr(src)[0])){
	case Bint:
		t = nvint(h, nvtermint(src));
		if(t == NvNil)
			return -1;
		*out = t;
		return 0;
	case Bref:
		t = nvref(h, nvrefincarnation(src), nvrefcounter(src));
		if(t == NvNil)
			return -1;
		*out = t;
		return 0;
	case Bbin:
		t = nvbin(h, nvbinbytes(src), nvbinlen(src));
		if(t == NvNil)
			return -1;
		*out = t;
		return 0;
	case Btuple:
		n = nvtuplelen(src);
		if(n == 0){
			t = nvtuple(h, nil, 0);
			if(t == NvNil)
				return -1;
			*out = t;
			return 0;
		}
		tmp = malloc(n*sizeof(NvTerm));
		if(tmp == nil)
			return -1;
		for(i = 0; i < n; i++){
			rc = heapcopy(h, nvtupleelem(src, i), &tmp[i], depth+1, w, limit);
			if(rc != 0){
				free(tmp);
				return rc;
			}
		}
		t = nvtuple(h, tmp, n);
		free(tmp);
		if(t == NvNil)
			return -1;
		*out = t;
		return 0;
	}
	return NvTermerror;	/* unreachable: unknown header kind */
}

int
nvheapcopy(NvHeap *heap, NvTerm src, NvTerm *out)
{
	return heapcopy(heap, src, out, 1, nil, ~0ULL);
}

/*
 * D080: a heap copy refused on work leaves the objects it already
 * allocated in the destination heap as garbage (unrooted, reclaimed by
 * that heap's next collection or its free). Its one caller, the spawn
 * argument copy into a fresh child heap, frees the whole heap on that
 * failure, so nothing leaks.
 */
int
nvheapcopyw(NvHeap *heap, NvTerm src, NvWork *w, NvTerm *out)
{
	return heapcopy(heap, src, out, 1, w, worklimit(w));
}

/*
 * D064 fragment copy, two passes so a refused copy allocates nothing.
 * Pass 1 (fragcount) walks src counting the words the copy's boxed
 * objects would need (immediates cost 0), tracking depth, and stops as
 * soon as depth or the word budget (count+1, the +1 for the root word)
 * is exceeded -- bounding the cost of a refusal by maxwords rather than
 * by the size of src. Pass 2 (fragbuild) walks src again, this time
 * writing boxed objects into the fragment's word array with absolute
 * pointers; a tuple's children are written after being computed, using
 * a bumped index rather than a saved pointer, though nothing in a
 * fragment ever moves once allocated.
 */
static int
fragcount(NvTerm t, ulong depth, uvlong *count, uvlong maxwords, NvWork *w, uvlong limit)
{
	int i, n, rc;

	if(t == NvNil)
		return NvTermerror;
	if(depth > NvMaxtermdepth)
		return NvTermlimit;
	if(VISIT(w, limit))
		return NvTermlimit;
	switch(t & NvTagmask){
	case NvTagint:
	case NvTagatom:
	case NvTagpid:
		return 0;
	}
	switch(NvHdrkind(NvBoxptr(t)[0])){
	case Bint:
		*count += 2;
		if(*count+1 > maxwords)
			return NvTermlimit;
		return 0;
	case Bref:
		*count += 3;
		if(*count+1 > maxwords)
			return NvTermlimit;
		return 0;
	case Bbin:
		/* D076: the bytes are not terms, so no depth is charged; the cost
		 * is the object's whole word count, which may be large, so the
		 * budget test is overflow-safe. */
		if(nvbinlen(t) > ~0ULL-9)
			return NvTermlimit;
		*count += 2+(uvlong)(nvbinlen(t)+7)/8;
		if(*count+1 > maxwords)
			return NvTermlimit;
		return 0;
	case Btuple:
		n = nvtuplelen(t);
		*count += 1+n;
		if(*count+1 > maxwords)
			return NvTermlimit;
		for(i = 0; i < n; i++){
			rc = fragcount(nvtupleelem(t, i), depth+1, count, maxwords, w, limit);
			if(rc != 0)
				return rc;
		}
		return 0;
	}
	return NvTermerror;	/* unreachable: unknown header kind */
}

static int
fragbuild(NvTerm t, NvTerm *word, uvlong *top, NvTerm *out)
{
	uvlong pos;
	int i, n, rc;
	NvTerm child;

	switch(t & NvTagmask){
	case NvTagint:
	case NvTagatom:
	case NvTagpid:
		*out = t;
		return 0;
	}
	switch(NvHdrkind(NvBoxptr(t)[0])){
	case Bint:
		pos = *top;
		*top += 2;
		word[pos] = NvHdr(Bint, 1);
		word[pos+1] = (NvTerm)nvtermint(t);
		*out = (NvTerm)(uintptr)&word[pos];
		return 0;
	case Bref:
		pos = *top;
		*top += 3;
		word[pos] = NvHdr(Bref, 2);
		word[pos+1] = nvrefincarnation(t);
		word[pos+2] = nvrefcounter(t);
		*out = (NvTerm)(uintptr)&word[pos];
		return 0;
	case Bbin:
		/* D076: the count word and the zero-padded byte words. The object
		 * was already validated and budgeted by pass 1 (fragcount). */
		pos = *top;
		*top += 2+(nvbinlen(t)+7)/8;
		word[*top-1] = 0;	/* padding; before the count word for an empty binary */
		word[pos] = NvHdr(Bbin, (ulong)(*top-pos)-1);
		word[pos+1] = (NvTerm)nvbinlen(t);
		if(nvbinlen(t) != 0)
			memmove(word+pos+2, nvbinbytes(t), nvbinlen(t));
		*out = (NvTerm)(uintptr)&word[pos];
		return 0;
	case Btuple:
		n = nvtuplelen(t);
		pos = *top;
		*top += 1+n;
		word[pos] = NvHdr(Btuple, n);
		for(i = 0; i < n; i++){
			rc = fragbuild(nvtupleelem(t, i), word, top, &child);
			if(rc != 0)
				return rc;
			word[pos+1+i] = child;
		}
		*out = (NvTerm)(uintptr)&word[pos];
		return 0;
	}
	return NvTermerror;	/* unreachable: unknown header kind, already validated by pass 1 */
}

/*
 * D080: the sizing pass is the one that can refuse, so it is the one
 * that is counted and bounded; the build pass walks the same nodes
 * again but has already been paid for by the words it was allowed.
 */
int
nvfragcopyw(NvTerm src, uvlong maxwords, NvWork *w, NvFrag **out)
{
	uvlong count, top;
	int rc;
	NvFrag *f;
	NvTerm root;

	if(src == NvNil)
		return NvTermerror;
	count = 0;
	rc = fragcount(src, 1, &count, maxwords, w, worklimit(w));
	if(rc != 0)
		return rc;
	if(count+1 > maxwords)
		return NvTermlimit;
	/*
	 * R3-F02: malloc takes ulong bytes and NvFrag.nword is ulong (both
	 * 32 bits on Plan 9), but count is a 64-bit word total that a Bbin
	 * -- the one kind whose fragment cost NvMaxtermdepth does not bound
	 * -- can push past 2^29 words under an unlimited heap budget.
	 * Refuse, as nvbinwords and gc.c's space sizing do, rather than let
	 * the byte count wrap and fragbuild write past a short allocation.
	 */
	if(count > ((~0UL)-sizeof(NvFrag))/sizeof(NvTerm))
		return NvTermlimit;
	f = malloc(sizeof(NvFrag) + count*sizeof(NvTerm));
	if(f == nil)
		return -1;
	f->nword = count;
	f->next = nil;
	f->word = (NvTerm*)(f+1);
	top = 0;
	rc = fragbuild(src, f->word, &top, &root);
	if(rc != 0){
		free(f);
		return rc;
	}
	f->root = root;
	*out = f;
	return 0;
}

int
nvfragcopy(NvTerm src, uvlong maxwords, NvFrag **out)
{
	return nvfragcopyw(src, maxwords, nil, out);
}

uvlong
nvfragwords(NvFrag *f)
{
	if(f == nil)
		return 0;
	return f->nword + 1;
}

void
nvfragfree(NvFrag *f)
{
	free(f);
}
