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
	check(nvclauseselect(c, 3, v, &b, &which, err, sizeof err) == 1 && which == 0, "source-order clause selection");
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
	check(nvclauseselect(c, 2, v, &b, &which, err, sizeof err) == 1 && which == 1, "later clause selection");
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
	check(nvclauseselect(c, 2, v, &b, &which, err, sizeof err) == 1 && which == 1, "independent arity dispatch");
	print("ok - different arities dispatch independently\n");

	v = integer(99);
	check(nvclauseselect(c, 2, v, &b, &which, err, sizeof err) == 0 && which == -1, "no clause result");
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
	check(nvpatternmatch(&p, v, &b, err, sizeof err) == 1, "repeated variable success");
	x = nvbinding(&b, "x");
	check(x != nil && nvtermkind(*x) == Vint && nvtermint(*x) == 7, "repeated variable binding");
	print("ok - repeated variable equality\n");

	ve[0] = integer(7);
	ve[1] = integer(8);
	v = nvtuple(&hostheap, ve, 2);
	check(v != NvNil, "tuple allocation");
	check(nvpatternmatch(&p, v, &b, err, sizeof err) == 0, "repeated variable mismatch");
	x = nvbinding(&b, "x");
	check(x != nil && nvtermint(*x) == 7, "failed attempt changed existing binding");
	print("ok - failed repeat rolls back\n");

	pe[0] = var("y");
	p = tuple(pe, 1);
	ve[0] = integer(1);
	ve[1] = integer(2);
	v = nvtuple(&hostheap, ve, 2);
	check(v != NvNil, "tuple allocation");
	check(nvpatternmatch(&p, v, &b, err, sizeof err) == 0, "exact arity mismatch");
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
	r = nvpatternmatch(&p, v, &b, err, sizeof err);
	check(r == 0, "late nested mismatch");
	check(nvbinding(&b, "z") == nil, "late nested mismatch leaked binding");
	print("ok - late nested failure rolls back\n");

	p.kind = Patom;
	p.name = "ok";
	v = atom("ok");
	check(nvpatternmatch(&p, v, &b, err, sizeof err) == 1, "atom literal");
	print("ok - atom literal\n");

	nvbindingsfree(&b);
	clauses();
	print("all pattern tests passed\n");
	nvheapfree(&hostheap);
	exits(nil);
}
