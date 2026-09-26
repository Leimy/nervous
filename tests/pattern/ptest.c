#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../../include/nvbc.h"
#include "../../include/nvvm.h"
#include "../../include/nvpat.h"

/* D061/D064: one host heap backs every term this program builds. */
static NvHeap hostheap;

static void
fail(char *s)
{
	fprint(2, "FAIL: %s\n", s);
	exits("test");
}

static NvTerm
integer(vlong n)
{
	return nvint(nil, n);
}

static NvTerm
atom(char *s)
{
	NvTerm v;

	v = nvatom(s);
	if(v == NvNil)
		fail("atom allocation");
	return v;
}

static NvPattern
var(char *s)
{
	NvPattern p;

	memset(&p, 0, sizeof p);
	p.kind = Pvar;
	p.name = s;
	return p;
}

static NvPattern
pint(vlong n)
{
	NvPattern p;

	memset(&p, 0, sizeof p);
	p.kind = Pint;
	p.ival = n;
	return p;
}

static NvPattern
tuple(NvPattern *e, int n)
{
	NvPattern p;

	memset(&p, 0, sizeof p);
	p.kind = Ptuple;
	p.elem = e;
	p.n = n;
	return p;
}

static void
check(int ok, char *s)
{
	if(!ok)
		fail(s);
}

/* D076: build a Bbin term in the host heap from a byte buffer. */
static NvTerm
mkbin(uchar *b, uvlong n)
{
	NvTerm v;

	v = nvbin(&hostheap, b, n);
	if(v == NvNil)
		fail("binary allocation");
	return v;
}

/* D076: a single-segment binary pattern. */
static NvPattern
bintest(int width, int flags, char *name)
{
	NvPattern p;
	/* static: the returned pattern points at it; each call reuses it. */
	static NvBinseg s;

	memset(&p, 0, sizeof p);
	memset(&s, 0, sizeof s);
	s.kind = Bsegint;
	s.width = width;
	s.flags = flags;
	s.name = name;
	p.kind = Pbin;
	p.seg = &s;
	p.nseg = 1;
	return p;
}

/*
 * D076/D077/D078: binary matching. Exercises the runtime matcher directly:
 * integer decode (big/little, signed/unsigned), a length-prefixed sized
 * segment, a remainder, trailing-byte rejection, and late-failure rollback
 * of earlier segment bindings.
 */
static void
binarytests(void)
{
	NvBindings b;
	NvPattern p;
	NvBinseg s[3];
	NvTerm v, *x;
	uchar bytes[8];
	char err[128];

	/* Big-endian unsigned 16-bit decode. */
	bytes[0] = 0x12;
	bytes[1] = 0x34;
	v = mkbin(bytes, 2);
	memset(&b, 0, sizeof b);
	p = bintest(2, 0, "x");
	check(nvpatternmatch(&hostheap, &p, v, &b, err, sizeof err) == 1, "big-endian 16-bit match");
	x = nvbinding(&b, "x");
	check(x != nil && nvtermint(*x) == 0x1234, "big-endian decode");
	nvbindingsfree(&b);

	/* Little-endian: low byte first. */
	bytes[0] = 0x34;
	bytes[1] = 0x12;
	v = mkbin(bytes, 2);
	memset(&b, 0, sizeof b);
	p = bintest(2, Bseglittle, "x");
	check(nvpatternmatch(&hostheap, &p, v, &b, err, sizeof err) == 1, "little-endian match");
	x = nvbinding(&b, "x");
	check(x != nil && nvtermint(*x) == 0x1234, "little-endian decode");
	nvbindingsfree(&b);

	/* Signed 8-bit: 0xFF is -1. */
	bytes[0] = 0xFF;
	v = mkbin(bytes, 1);
	memset(&b, 0, sizeof b);
	p = bintest(1, Bsegsigned, "x");
	check(nvpatternmatch(&hostheap, &p, v, &b, err, sizeof err) == 1, "signed 8-bit match");
	x = nvbinding(&b, "x");
	check(x != nil && nvtermint(*x) == -1, "signed decode");
	nvbindingsfree(&b);

	/* Length-prefixed payload: len : 8, payload : len / binary. */
	bytes[0] = 3;
	bytes[1] = 'a';
	bytes[2] = 'b';
	bytes[3] = 'c';
	v = mkbin(bytes, 4);
	memset(&b, 0, sizeof b);
	memset(s, 0, sizeof s);
	s[0].kind = Bsegint;
	s[0].width = 1;
	s[0].name = "len";
	s[1].kind = Bsegsized;
	s[1].sizename = "len";
	s[1].name = "payload";
	p.kind = Pbin;
	p.seg = s;
	p.nseg = 2;
	memset(&b, 0, sizeof b);
	check(nvpatternmatch(&hostheap, &p, v, &b, err, sizeof err) == 1, "length-prefixed match");
	x = nvbinding(&b, "len");
	check(x != nil && nvtermint(*x) == 3, "length binding");
	x = nvbinding(&b, "payload");
	check(x != nil && nvtermkind(*x) == Vbin && nvbinlen(*x) == 3, "payload is a 3-byte binary");
	check(memcmp(nvbinbytes(*x), "abc", 3) == 0, "payload bytes");
	nvbindingsfree(&b);

	/* A payload longer than the remaining bytes is a mismatch, not a fault. */
	bytes[0] = 9;
	bytes[1] = 'a';
	v = mkbin(bytes, 2);
	memset(&b, 0, sizeof b);
	check(nvpatternmatch(&hostheap, &p, v, &b, err, sizeof err) == 0, "oversized payload mismatch");
	check(nvbinding(&b, "len") == nil && nvbinding(&b, "payload") == nil, "oversized payload leaked no binding");
	nvbindingsfree(&b);

	/* Trailing-byte rejection: a non-remainder final segment must consume all. */
	bytes[0] = 1;
	bytes[1] = 2;
	v = mkbin(bytes, 2);
	memset(&b, 0, sizeof b);
	p = bintest(1, 0, "x");	/* consumes 1 byte, 1 left over */
	check(nvpatternmatch(&hostheap, &p, v, &b, err, sizeof err) == 0, "trailing byte rejected");
	nvbindingsfree(&b);

	/* A remainder binds all remaining bytes. */
	memset(&b, 0, sizeof b);
	memset(s, 0, sizeof s);
	s[0].kind = Bsegint;
	s[0].width = 1;
	s[0].name = "x";
	s[1].kind = Bsegrest;
	s[1].name = "rest";
	p.kind = Pbin;
	p.seg = s;
	p.nseg = 2;
	check(nvpatternmatch(&hostheap, &p, v, &b, err, sizeof err) == 1, "remainder match");
	x = nvbinding(&b, "rest");
	check(x != nil && nvtermkind(*x) == Vbin && nvbinlen(*x) == 1, "remainder is the leftover byte");
	nvbindingsfree(&b);

	/* Late failure rolls back an earlier segment's binding. */
	bytes[0] = 7;
	bytes[1] = 9;	/* second byte is 9, the literal test wants 5 */
	v = mkbin(bytes, 2);
	memset(&b, 0, sizeof b);
	memset(s, 0, sizeof s);
	s[0].kind = Bsegint;
	s[0].width = 1;
	s[0].name = "a";
	s[1].kind = Bsegint;
	s[1].width = 1;
	s[1].flags = Bsegtest;
	s[1].ival = 5;
	p.kind = Pbin;
	p.seg = s;
	p.nseg = 2;
	check(nvpatternmatch(&hostheap, &p, v, &b, err, sizeof err) == 0, "late literal mismatch");
	check(nvbinding(&b, "a") == nil, "late failure rolled back earlier binding");
	nvbindingsfree(&b);

	/* The same literal test succeeds when the byte matches. */
	bytes[1] = 5;
	v = mkbin(bytes, 2);
	memset(&b, 0, sizeof b);
	check(nvpatternmatch(&hostheap, &p, v, &b, err, sizeof err) == 1, "literal segment match");
	x = nvbinding(&b, "a");
	check(x != nil && nvtermint(*x) == 7, "binding before a passing literal");
	nvbindingsfree(&b);

	/* A non-binary subject is a mismatch. */
	v = integer(5);
	memset(&b, 0, sizeof b);
	p = bintest(1, 0, "x");
	check(nvpatternmatch(&hostheap, &p, v, &b, err, sizeof err) == 0, "non-binary subject mismatch");
	nvbindingsfree(&b);

	print("ok - binary construction decode, length-prefix, remainder, and rollback\n");
}

/*
 * D076-D078: binary construction and integer encoding. Exercises the
 * builders and the encode/decode/range primitives directly -- nothing in
 * binarytests() builds a binary or checks an encoded byte sequence:
 * staged appends that cross an 8-byte word boundary, exact enc bytes,
 * decode round trips (sign extension at 32 bits, the unsigned 32-bit
 * all-ones case), nvbinfits range limits, and equality plus a fragment
 * copy of a binary.
 */
static void
constructiontests(void)
{
	NvTerm v, a, b, other, ibin;
	NvFrag *f;
	uchar seven[7], five[5], exp12[12], enc[8];
	uchar e16be[2], e16le[2], em2be[4], e64be[8], e64le[8];
	vlong got, rvals[4];
	int rws[4];
	int i, e;
	static char msg[64];

	/* Staged appends: 7, then 5, then 0 bytes -- crosses the 8-byte word. */
	seven[0]=1; seven[1]=2; seven[2]=3; seven[3]=4; seven[4]=5; seven[5]=6; seven[6]=7;
	five[0]=8; five[1]=9; five[2]=0x0A; five[3]=0x0B; five[4]=0x0C;
	for(i = 0; i < 12; i++)
		exp12[i] = (i < 7) ? seven[i] : five[i-7];

	v = nvbin(&hostheap, nil, 0);
	check(v != NvNil, "empty binary allocation");
	check(nvbinlen(v) == 0, "empty binary length");
	v = nvbinapp(&hostheap, v, seven, 7);
	check(v != NvNil, "append 7 bytes");
	check(nvbinlen(v) == 7, "length after appending 7");
	check(memcmp(nvbinbytes(v), exp12, 7) == 0, "bytes after appending 7");
	v = nvbinapp(&hostheap, v, five, 5);
	check(v != NvNil, "append 5 bytes");
	check(nvbinlen(v) == 12, "length after appending 5");
	check(memcmp(nvbinbytes(v), exp12, 12) == 0, "bytes after appending 5");
	v = nvbinapp(&hostheap, v, nil, 0);
	check(v != NvNil, "append 0 bytes");
	check(nvbinlen(v) == 12, "length after zero-length append");
	check(memcmp(nvbinbytes(v), exp12, 12) == 0, "bytes after zero-length append");

	/* Exact enc bytes. */
	e16be[0]=0x12; e16be[1]=0x34;
	nvbinenc(0x1234, 2, 0, enc);
	check(memcmp(enc, e16be, 2) == 0, "enc 0x1234 w2 big-endian");
	e16le[0]=0x34; e16le[1]=0x12;
	nvbinenc(0x1234, 2, Bseglittle, enc);
	check(memcmp(enc, e16le, 2) == 0, "enc 0x1234 w2 little-endian");
	em2be[0]=0xFF; em2be[1]=0xFF; em2be[2]=0xFF; em2be[3]=0xFE;
	nvbinenc(-2, 4, Bsegsigned, enc);
	check(memcmp(enc, em2be, 4) == 0, "enc -2 w4 signed big-endian");
	for(i = 0; i < 8; i++)
		e64be[i] = (uchar)(0x01+i);
	nvbinenc(0x0102030405060708LL, 8, 0, enc);
	check(memcmp(enc, e64be, 8) == 0, "enc 8-byte big-endian");
	for(i = 0; i < 8; i++)
		e64le[i] = (uchar)(0x08-i);
	nvbinenc(0x0102030405060708LL, 8, Bseglittle, enc);
	check(memcmp(enc, e64le, 8) == 0, "enc 8-byte little-endian");

	/* Decode round trips: widths 1,2,4,8 x big/little, unsigned. */
	rws[0]=1; rvals[0]=0x5A;
	rws[1]=2; rvals[1]=0x1234;
	rws[2]=4; rvals[2]=0x01020304;
	rws[3]=8; rvals[3]=0x0102030405060708LL;
	for(i = 0; i < 4; i++){
		for(e = 0; e < 2; e++){
			nvbinenc(rvals[i], rws[i], e?Bseglittle:0, enc);
			nvbindec(enc, rws[i], e?Bseglittle:0, &got);
			snprint(msg, sizeof msg, "unsigned round trip w%d %s", rws[i], e?"little":"big");
			check(got == rvals[i], msg);
		}
	}
	/* Signed negatives, including sign extension at 32 bits. */
	nvbinenc(-1, 1, Bsegsigned, enc);
	nvbindec(enc, 1, Bsegsigned, &got);
	check(got == -1, "signed round trip -1 w1");
	nvbinenc(-128, 1, Bsegsigned, enc);
	nvbindec(enc, 1, Bsegsigned, &got);
	check(got == -128, "signed round trip -128 w1");
	nvbinenc(-2, 4, Bsegsigned, enc);
	nvbindec(enc, 4, Bsegsigned, &got);
	check(got == -2, "signed round trip -2 w4");
	nvbinenc(-2147483648LL, 4, Bsegsigned, enc);
	nvbindec(enc, 4, Bsegsigned, &got);
	check(got == -2147483648LL, "signed round trip int32min w4");
	/* Unsigned 32-bit all-ones must decode to 4294967295, not -1. */
	nvbinenc(0xFFFFFFFFLL, 4, 0, enc);
	nvbindec(enc, 4, 0, &got);
	check(got == 4294967295LL, "unsigned round trip 0xFFFFFFFF w4");
	nvbinenc(0xFFFFFFFFLL, 4, Bseglittle, enc);
	nvbindec(enc, 4, Bseglittle, &got);
	check(got == 4294967295LL, "unsigned round trip 0xFFFFFFFF w4 little");

	/* nvbinfits range limits. */
	check(nvbinfits(127, 1, Bsegsigned) == 1, "fits signed w1 127");
	check(nvbinfits(-128, 1, Bsegsigned) == 1, "fits signed w1 -128");
	check(nvbinfits(128, 1, Bsegsigned) == 0, "reject signed w1 128");
	check(nvbinfits(-129, 1, Bsegsigned) == 0, "reject signed w1 -129");
	check(nvbinfits(255, 1, 0) == 1, "fits unsigned w1 255");
	check(nvbinfits(0, 1, 0) == 1, "fits unsigned w1 0");
	check(nvbinfits(256, 1, 0) == 0, "reject unsigned w1 256");
	check(nvbinfits(-1, 1, 0) == 0, "reject unsigned w1 -1");
	check(nvbinfits(-1, 8, 0) == 0, "reject unsigned w8 -1");
	check(nvbinfits(-1, 8, Bsegsigned) == 1, "fits signed w8 -1");

	/* Equality and copy. */
	a = nvbin(&hostheap, exp12, 12);
	b = nvbin(&hostheap, exp12, 12);
	check(a != NvNil && b != NvNil, "equality binary allocation");
	check(nvtermequal(a, b) == 1, "identical binaries compare 1");
	ibin = nvbin(&hostheap, exp12, 4);
	check(ibin != NvNil, "short binary allocation");
	check(nvtermequal(a, ibin) == 0, "different-length compare 0");
	exp12[11] ^= 0xFF;
	other = nvbin(&hostheap, exp12, 12);
	check(other != NvNil, "differing-bytes allocation");
	check(nvtermequal(a, other) == 0, "differing-bytes compare 0");
	exp12[11] ^= 0xFF;
	check(nvtermequal(a, integer(12)) == 0, "binary vs integer compare 0");
	check(nvfragcopy(a, 1000, &f) == 0, "fragment copy of binary");
	check(f != nil, "fragment non-nil");
	check(nvtermequal(f->root, a) == 1, "fragment root equals original");
	nvfragfree(f);

	print("ok - binary construction, encoding, range checks, equality and copy\n");
}

static void
clauses(void)
{
	NvBindings b;
	NvPatClause c[3];
	NvPattern p[3], e0[2], e1[2], inner[2];
	NvTerm v, ve[2], ie[2], *x;
	char err[128];
	int which;

	memset(&b, 0, sizeof b);
	memset(c, 0, sizeof c);
	memset(p, 0, sizeof p);
	memset(e0, 0, sizeof e0);
	memset(e1, 0, sizeof e1);
	memset(inner, 0, sizeof inner);
	e0[0] = pint(1);
	e0[1] = var("first");
	p[0] = tuple(e0, 2);
	e1[0] = pint(1);
	e1[1].kind = Pwild;
	e1[1].name = nil;
	p[1] = tuple(e1, 2);
	p[2].kind = Pwild;
	p[2].name = nil;
	c[0].pattern = &p[0];
	c[1].pattern = &p[1];
	c[2].pattern = &p[2];
	ve[0] = integer(1);
	ve[1] = integer(9);
	v = nvtuple(&hostheap, ve, 2);
	check(v != NvNil, "clause tuple allocation");
	check(nvclauseselect(&hostheap, c, 3, v, &b, &which, err, sizeof err) == 1 && which == 0, "source-order clause selection");
	x = nvbinding(&b, "first");
	check(x != nil && nvtermint(*x) == 9, "selected clause binding");
	print("ok - overlapping clauses preserve source order\n");

	nvbindingsfree(&b);
	memset(&b, 0, sizeof b);
	inner[0] = var("leaked");
	inner[1] = pint(7);
	e0[0] = tuple(inner, 2);
	e0[1] = pint(3);
	p[0] = tuple(e0, 2);
	e1[0].kind = Pwild;
	e1[1] = var("later");
	p[1] = tuple(e1, 2);
	c[0].pattern = &p[0];
	c[1].pattern = &p[1];
	ie[0] = integer(5);
	ie[1] = integer(8);
	ve[0] = nvtuple(&hostheap, ie, 2);
	check(ve[0] != NvNil, "nested clause allocation");
	ve[1] = integer(4);
	v = nvtuple(&hostheap, ve, 2);
	check(v != NvNil, "clause outer allocation");
	check(nvclauseselect(&hostheap, c, 2, v, &b, &which, err, sizeof err) == 1 && which == 1, "later clause selection");
	check(nvbinding(&b, "leaked") == nil, "failed clause leaked binding");
	x = nvbinding(&b, "later");
	check(x != nil && nvtermint(*x) == 4, "later clause binding");
	print("ok - failed clause rolls back before later clause\n");

	nvbindingsfree(&b);
	memset(&b, 0, sizeof b);
	e0[0] = pint(1);
	p[0] = tuple(e0, 1);
	e1[0] = pint(1);
	e1[1] = pint(2);
	p[1] = tuple(e1, 2);
	c[0].pattern = &p[0];
	c[1].pattern = &p[1];
	ve[0] = integer(1);
	ve[1] = integer(2);
	v = nvtuple(&hostheap, ve, 2);
	check(v != NvNil, "arity clause allocation");
	check(nvclauseselect(&hostheap, c, 2, v, &b, &which, err, sizeof err) == 1 && which == 1, "independent arity dispatch");
	print("ok - different arities dispatch independently\n");

	v = integer(99);
	check(nvclauseselect(&hostheap, c, 2, v, &b, &which, err, sizeof err) == 0 && which == -1, "no clause result");
	check(b.n == 0, "no clause changed bindings");
	print("ok - no clause preserves bindings\n");
	nvbindingsfree(&b);
}

void
main(void)
{
	NvBindings b;
	NvPattern p, pe[2], nested[2], inner[2];
	NvTerm v, ve[2], ne[2], ie[2], *x;
	char err[128];
	int r;

	nvheapinit(&hostheap, 0);

	memset(&b, 0, sizeof b);
	pe[0] = var("x");
	pe[1] = var("x");
	p = tuple(pe, 2);
	ve[0] = integer(7);
	ve[1] = integer(7);
	v = nvtuple(&hostheap, ve, 2);
	check(v != NvNil, "tuple allocation");
	check(nvpatternmatch(&hostheap, &p, v, &b, err, sizeof err) == 1, "repeated variable success");
	x = nvbinding(&b, "x");
	check(x != nil && nvtermkind(*x) == Vint && nvtermint(*x) == 7, "repeated variable binding");
	print("ok - repeated variable equality\n");

	ve[0] = integer(7);
	ve[1] = integer(8);
	v = nvtuple(&hostheap, ve, 2);
	check(v != NvNil, "tuple allocation");
	check(nvpatternmatch(&hostheap, &p, v, &b, err, sizeof err) == 0, "repeated variable mismatch");
	x = nvbinding(&b, "x");
	check(x != nil && nvtermint(*x) == 7, "failed attempt changed existing binding");
	print("ok - failed repeat rolls back\n");

	pe[0] = var("y");
	p = tuple(pe, 1);
	ve[0] = integer(1);
	ve[1] = integer(2);
	v = nvtuple(&hostheap, ve, 2);
	check(v != NvNil, "tuple allocation");
	check(nvpatternmatch(&hostheap, &p, v, &b, err, sizeof err) == 0, "exact arity mismatch");
	check(nvbinding(&b, "y") == nil, "arity mismatch leaked binding");
	print("ok - exact tuple arity\n");

	inner[0] = var("z");
	inner[1] = pint(9);
	nested[0] = tuple(inner, 2);
	nested[1] = pint(3);
	p = tuple(nested, 2);
	ie[0] = integer(5);
	ie[1] = integer(8);
	ne[0] = nvtuple(&hostheap, ie, 2);
	check(ne[0] != NvNil, "inner tuple allocation");
	ne[1] = integer(3);
	v = nvtuple(&hostheap, ne, 2);
	check(v != NvNil, "outer tuple allocation");
	r = nvpatternmatch(&hostheap, &p, v, &b, err, sizeof err);
	check(r == 0, "late nested mismatch");
	check(nvbinding(&b, "z") == nil, "late nested mismatch leaked binding");
	print("ok - late nested failure rolls back\n");

	p.kind = Patom;
	p.name = "ok";
	v = atom("ok");
	check(nvpatternmatch(&hostheap, &p, v, &b, err, sizeof err) == 1, "atom literal");
	print("ok - atom literal\n");

	nvbindingsfree(&b);
	clauses();
	binarytests();
	constructiontests();
	print("all pattern tests passed\n");
	nvheapfree(&hostheap);
	exits(nil);
}
