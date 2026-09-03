#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../../include/nvbc.h"
#include "../../include/nvvm.h"
#include "../../include/nvpat.h"

static void
fail(char *s)
{
	fprint(2, "FAIL: %s\n", s);
	exits("test");
}

static NvValue
integer(vlong n)
{
	NvValue v;

	memset(&v, 0, sizeof v);
	v.valid = 1;
	v.kind = Vint;
	v.i = n;
	return v;
}

static NvValue
atom(char *s)
{
	NvValue v;

	if(nvvalueatom(&v, s) < 0)
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
	NvValue v, ve[2], ie[2], *x;
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
	check(nvvaluetuple(&v, ve, 2) == 0, "clause tuple allocation");
	check(nvclauseselect(c, 3, &v, &b, &which, err, sizeof err) == 1 && which == 0, "source-order clause selection");
	x = nvbinding(&b, "first");
	check(x != nil && x->i == 9, "selected clause binding");
	nvvaluefree(&v);
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
	check(nvvaluetuple(&ve[0], ie, 2) == 0, "nested clause allocation");
	ve[1] = integer(4);
	check(nvvaluetuple(&v, ve, 2) == 0, "clause outer allocation");
	nvvaluefree(&ve[0]);
	check(nvclauseselect(c, 2, &v, &b, &which, err, sizeof err) == 1 && which == 1, "later clause selection");
	check(nvbinding(&b, "leaked") == nil, "failed clause leaked binding");
	x = nvbinding(&b, "later");
	check(x != nil && x->i == 4, "later clause binding");
	nvvaluefree(&v);
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
	check(nvvaluetuple(&v, ve, 2) == 0, "arity clause allocation");
	check(nvclauseselect(c, 2, &v, &b, &which, err, sizeof err) == 1 && which == 1, "independent arity dispatch");
	nvvaluefree(&v);
	print("ok - different arities dispatch independently\n");

	v = integer(99);
	check(nvclauseselect(c, 2, &v, &b, &which, err, sizeof err) == 0 && which == -1, "no clause result");
	check(b.n == 0, "no clause changed bindings");
	print("ok - no clause preserves bindings\n");
	nvbindingsfree(&b);
}

void
main(void)
{
	NvBindings b;
	NvPattern p, pe[2], nested[2], inner[2];
	NvValue v, ve[2], ne[2], ie[2], *x;
	char err[128];
	int r;

	memset(&b, 0, sizeof b);
	pe[0] = var("x");
	pe[1] = var("x");
	p = tuple(pe, 2);
	ve[0] = integer(7);
	ve[1] = integer(7);
	check(nvvaluetuple(&v, ve, 2) == 0, "tuple allocation");
	check(nvpatternmatch(&p, &v, &b, err, sizeof err) == 1, "repeated variable success");
	x = nvbinding(&b, "x");
	check(x != nil && x->kind == Vint && x->i == 7, "repeated variable binding");
	nvvaluefree(&v);
	print("ok - repeated variable equality\n");

	ve[0] = integer(7);
	ve[1] = integer(8);
	check(nvvaluetuple(&v, ve, 2) == 0, "tuple allocation");
	check(nvpatternmatch(&p, &v, &b, err, sizeof err) == 0, "repeated variable mismatch");
	x = nvbinding(&b, "x");
	check(x != nil && x->i == 7, "failed attempt changed existing binding");
	nvvaluefree(&v);
	print("ok - failed repeat rolls back\n");

	pe[0] = var("y");
	p = tuple(pe, 1);
	ve[0] = integer(1);
	ve[1] = integer(2);
	check(nvvaluetuple(&v, ve, 2) == 0, "tuple allocation");
	check(nvpatternmatch(&p, &v, &b, err, sizeof err) == 0, "exact arity mismatch");
	check(nvbinding(&b, "y") == nil, "arity mismatch leaked binding");
	nvvaluefree(&v);
	print("ok - exact tuple arity\n");

	inner[0] = var("z");
	inner[1] = pint(9);
	nested[0] = tuple(inner, 2);
	nested[1] = pint(3);
	p = tuple(nested, 2);
	ie[0] = integer(5);
	ie[1] = integer(8);
	check(nvvaluetuple(&ne[0], ie, 2) == 0, "inner tuple allocation");
	ne[1] = integer(3);
	check(nvvaluetuple(&v, ne, 2) == 0, "outer tuple allocation");
	nvvaluefree(&ne[0]);
	r = nvpatternmatch(&p, &v, &b, err, sizeof err);
	check(r == 0, "late nested mismatch");
	check(nvbinding(&b, "z") == nil, "late nested mismatch leaked binding");
	nvvaluefree(&v);
	print("ok - late nested failure rolls back\n");

	p.kind = Patom;
	p.name = "ok";
	v = atom("ok");
	check(nvpatternmatch(&p, &v, &b, err, sizeof err) == 1, "atom literal");
	nvvaluefree(&v);
	print("ok - atom literal\n");

	nvbindingsfree(&b);
	clauses();
	print("all pattern tests passed\n");
	exits(nil);
}
