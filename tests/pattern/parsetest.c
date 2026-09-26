#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../../include/nervous.h"
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

static int
count(Exprs *x)
{
	int n;

	for(n = 0; x != nil; x = x->next)
		n++;
	return n;
}

static NvPattern *
clausepattern(Clause *c, char *err, int nerr)
{
	NvPattern *p, *q;
	Exprs *x;
	int i;

	p = mallocz(sizeof *p, 1);
	if(p == nil)
		return nil;
	p->kind = Ptuple;
	p->n = count(c->patterns);
	p->elem = mallocz(p->n*sizeof *p->elem, 1);
	if(p->n != 0 && p->elem == nil){
		free(p);
		return nil;
	}
	for(i = 0, x = c->patterns; x != nil; i++, x = x->next){
		q = nvpatternfromexpr(x->expr, err, nerr);
		if(q == nil){
			nvpatternfree(p);
			return nil;
		}
		p->elem[i] = *q;
		free(q);
	}
	return p;
}

static NvTerm
integer(vlong n)
{
	return nvint(nil, n);
}

/* D076: binagg patterns, end to end: conversion shape and matching. */
static void
binpatterns(void)
{
	char *src;
	Parser parser;
	Program *program;
	Fn *f;
	NvPattern *p;
	NvBinseg *s;
	NvBindings b;
	NvTerm t;
	NvTerm elem[2];
	NvTerm *x, *y;
	uchar by[8];
	char err[128];

	/* a: the converted segment list mirrors the source. */
	src = "fn b(<<1:8, x:16/signed/little, b:x/binary, r/binary>>) { 1 }\n";
	program = parseprogram(&parser, "bin-shape", src, strlen(src));
	if(program == nil)
		fail(parser.err);
	f = program->fns;
	if(f == nil || f->clauses == nil || f->clauses->patterns == nil)
		fail("bin shape: clause missing");
	p = nvpatternfromexpr(f->clauses->patterns->expr, err, sizeof err);
	programfree(program);
	if(p == nil)
		fail(err[0] ? err : "bin shape: conversion");
	if(p->kind != Pbin || p->nseg != 4 || p->seg == nil)
		fail("bin shape: expected a 4-segment Pbin");
	s = p->seg;
	if(s[0].kind != Bsegint || s[0].width != 1 || s[0].flags != Bsegtest || s[0].ival != 1 || s[0].name != nil)
		fail("bin shape: segment 0 is not a literal test");
	if(s[1].kind != Bsegint || s[1].width != 2 || s[1].flags != (Bsegsigned|Bseglittle) ||
	   s[1].name == nil || strcmp(s[1].name, "x") != 0)
		fail("bin shape: segment 1 is not signed little-endian x");
	if(s[2].kind != Bsegsized || s[2].sizename == nil || strcmp(s[2].sizename, "x") != 0 ||
	   s[2].name == nil || strcmp(s[2].name, "b") != 0)
		fail("bin shape: segment 2 is not x-sized b");
	if(s[3].kind != Bsegrest || s[3].name == nil || strcmp(s[3].name, "r") != 0)
		fail("bin shape: segment 3 is not the rest");
	nvpatternfree(p);
	print("ok - binary pattern conversion shape\n");

	/* b: an exact two-byte pattern matches only its own bytes. */
	src = "fn b(<<a:8, b:8>>) { 1 }\n";
	program = parseprogram(&parser, "bin-exact", src, strlen(src));
	if(program == nil)
		fail(parser.err);
	f = program->fns;
	if(f == nil || f->clauses == nil || f->clauses->patterns == nil)
		fail("bin exact: clause missing");
	p = nvpatternfromexpr(f->clauses->patterns->expr, err, sizeof err);
	programfree(program);
	if(p == nil)
		fail(err[0] ? err : "bin exact: conversion");
	by[0] = 1;
	by[1] = 2;
	t = nvbin(&hostheap, by, 2);
	if(t == NvNil)
		fail("value allocation");
	memset(&b, 0, sizeof b);
	if(nvpatternmatch(&hostheap, p, t, &b, err, sizeof err) != 1)
		fail("bin exact: expected a match");
	x = nvbinding(&b, "a");
	y = nvbinding(&b, "b");
	if(x == nil || nvtermint(*x) != 1 || y == nil || nvtermint(*y) != 2)
		fail("bin exact: bindings wrong");
	nvbindingsfree(&b);
	print("ok - binary pattern exact match\n");

	/* c: leftover bytes defeat an exact pattern. */
	by[2] = 3;
	t = nvbin(&hostheap, by, 3);
	if(t == NvNil)
		fail("value allocation");
	memset(&b, 0, sizeof b);
	if(nvpatternmatch(&hostheap, p, t, &b, err, sizeof err) != 0)
		fail("bin exact: trailing bytes must not match");
	nvbindingsfree(&b);
	nvpatternfree(p);
	print("ok - binary pattern rejects trailing bytes\n");

	/* d: a rest segment takes the whole remainder, empty or not. */
	src = "fn b(<<a:8, r/binary>>) { 1 }\n";
	program = parseprogram(&parser, "bin-rest", src, strlen(src));
	if(program == nil)
		fail(parser.err);
	f = program->fns;
	if(f == nil || f->clauses == nil || f->clauses->patterns == nil)
		fail("bin rest: clause missing");
	p = nvpatternfromexpr(f->clauses->patterns->expr, err, sizeof err);
	programfree(program);
	if(p == nil)
		fail(err[0] ? err : "bin rest: conversion");
	by[0] = 5;
	t = nvbin(&hostheap, by, 1);
	if(t == NvNil)
		fail("value allocation");
	memset(&b, 0, sizeof b);
	if(nvpatternmatch(&hostheap, p, t, &b, err, sizeof err) != 1)
		fail("bin rest: expected an empty remainder to match");
	x = nvbinding(&b, "a");
	y = nvbinding(&b, "r");
	if(x == nil || nvtermint(*x) != 5 || y == nil ||
	   nvtermkind(*y) != Vbin || nvbinlen(*y) != 0)
		fail("bin rest: empty remainder wrong");
	nvbindingsfree(&b);
	print("ok - binary rest segment takes an empty remainder\n");
	by[1] = 6;
	by[2] = 7;
	t = nvbin(&hostheap, by, 3);
	if(t == NvNil)
		fail("value allocation");
	memset(&b, 0, sizeof b);
	if(nvpatternmatch(&hostheap, p, t, &b, err, sizeof err) != 1)
		fail("bin rest: expected a nonempty remainder to match");
	y = nvbinding(&b, "r");
	if(y == nil || nvtermkind(*y) != Vbin || nvbinlen(*y) != 2 ||
	   memcmp(nvbinbytes(*y), by+1, 2) != 0)
		fail("bin rest: remainder bytes wrong");
	nvbindingsfree(&b);
	nvpatternfree(p);
	print("ok - binary rest segment takes a nonempty remainder\n");

	/* e: a sized segment reads a count from an earlier variable. */
	src = "fn b(<<n:8, p:n/binary, r/binary>>) { 1 }\n";
	program = parseprogram(&parser, "bin-sized", src, strlen(src));
	if(program == nil)
		fail(parser.err);
	f = program->fns;
	if(f == nil || f->clauses == nil || f->clauses->patterns == nil)
		fail("bin sized: clause missing");
	p = nvpatternfromexpr(f->clauses->patterns->expr, err, sizeof err);
	programfree(program);
	if(p == nil)
		fail(err[0] ? err : "bin sized: conversion");
	by[0] = 2;
	by[1] = 10;
	by[2] = 11;
	by[3] = 12;
	t = nvbin(&hostheap, by, 4);
	if(t == NvNil)
		fail("value allocation");
	memset(&b, 0, sizeof b);
	if(nvpatternmatch(&hostheap, p, t, &b, err, sizeof err) != 1)
		fail("bin sized: expected a match");
	x = nvbinding(&b, "n");
	y = nvbinding(&b, "p");
	if(x == nil || nvtermint(*x) != 2 || y == nil ||
	   nvtermkind(*y) != Vbin || nvbinlen(*y) != 2 ||
	   memcmp(nvbinbytes(*y), by+1, 2) != 0)
		fail("bin sized: payload wrong");
	y = nvbinding(&b, "r");
	if(y == nil || nvtermkind(*y) != Vbin || nvbinlen(*y) != 1 ||
	   memcmp(nvbinbytes(*y), by+3, 1) != 0)
		fail("bin sized: rest wrong");
	nvbindingsfree(&b);
	nvpatternfree(p);
	print("ok - binary sized segment reads its count from a variable\n");

	/* f: width, endianness, and signedness follow the source modifiers. */
	src = "fn b(<<a:16, b:16/little, c:8/signed, d:16/signed/little>>) { 1 }\n";
	program = parseprogram(&parser, "bin-endian", src, strlen(src));
	if(program == nil)
		fail(parser.err);
	f = program->fns;
	if(f == nil || f->clauses == nil || f->clauses->patterns == nil)
		fail("bin endian: clause missing");
	p = nvpatternfromexpr(f->clauses->patterns->expr, err, sizeof err);
	programfree(program);
	if(p == nil)
		fail(err[0] ? err : "bin endian: conversion");
	by[0] = 0x01;
	by[1] = 0x02;
	by[2] = 0x01;
	by[3] = 0x02;
	by[4] = 0xFF;
	by[5] = 0xFE;
	by[6] = 0xFF;
	t = nvbin(&hostheap, by, 7);
	if(t == NvNil)
		fail("value allocation");
	memset(&b, 0, sizeof b);
	if(nvpatternmatch(&hostheap, p, t, &b, err, sizeof err) != 1)
		fail("bin endian: expected a match");
	x = nvbinding(&b, "a");
	y = nvbinding(&b, "b");
	if(x == nil || nvtermint(*x) != 258 || y == nil || nvtermint(*y) != 513)
		fail("bin endian: 16-bit values wrong");
	y = nvbinding(&b, "c");
	if(y == nil || nvtermint(*y) != -1)
		fail("bin endian: signed byte wrong");
	y = nvbinding(&b, "d");
	if(y == nil || nvtermint(*y) != -2)
		fail("bin endian: signed little value wrong");
	nvbindingsfree(&b);
	nvpatternfree(p);
	print("ok - binary width, endian, and signed decoding\n");

	/* g: a bare integer segment is a test, not a binding. */
	src = "fn b(<<7:8, x:8>>) { 1 }\n";
	program = parseprogram(&parser, "bin-test", src, strlen(src));
	if(program == nil)
		fail(parser.err);
	f = program->fns;
	if(f == nil || f->clauses == nil || f->clauses->patterns == nil)
		fail("bin test: clause missing");
	p = nvpatternfromexpr(f->clauses->patterns->expr, err, sizeof err);
	programfree(program);
	if(p == nil)
		fail(err[0] ? err : "bin test: conversion");
	by[0] = 7;
	by[1] = 9;
	t = nvbin(&hostheap, by, 2);
	if(t == NvNil)
		fail("value allocation");
	memset(&b, 0, sizeof b);
	if(nvpatternmatch(&hostheap, p, t, &b, err, sizeof err) != 1)
		fail("bin test: literal must match");
	x = nvbinding(&b, "x");
	if(x == nil || nvtermint(*x) != 9)
		fail("bin test: x wrong");
	nvbindingsfree(&b);
	by[0] = 8;
	t = nvbin(&hostheap, by, 2);
	if(t == NvNil)
		fail("value allocation");
	memset(&b, 0, sizeof b);
	if(nvpatternmatch(&hostheap, p, t, &b, err, sizeof err) != 0)
		fail("bin test: a different literal must not match");
	nvbindingsfree(&b);
	nvpatternfree(p);
	print("ok - binary integer segment tests a literal\n");

	/* h: a sized segment may only name an earlier segment. */
	src = "fn b(<<p:n/binary, n:8>>) { 1 }\n";
	program = parseprogram(&parser, "bin-forward", src, strlen(src));
	if(program == nil)
		fail(parser.err);
	f = program->fns;
	if(f == nil || f->clauses == nil || f->clauses->patterns == nil)
		fail("bin forward: clause missing");
	p = nvpatternfromexpr(f->clauses->patterns->expr, err, sizeof err);
	programfree(program);
	if(p != nil || strstr(err, "later segment") == nil){
		nvpatternfree(p);
		fail("bin forward: forward size reference must fail");
	}
	print("ok - binary forward size reference is rejected\n");
	src = "fn b(<<n:8, p:n/binary>>) { 1 }\n";
	program = parseprogram(&parser, "bin-backward", src, strlen(src));
	if(program == nil)
		fail(parser.err);
	f = program->fns;
	if(f == nil || f->clauses == nil || f->clauses->patterns == nil)
		fail("bin backward: clause missing");
	p = nvpatternfromexpr(f->clauses->patterns->expr, err, sizeof err);
	programfree(program);
	if(p == nil)
		fail(err[0] ? err : "bin backward: conversion");
	nvpatternfree(p);
	print("ok - binary backward size reference is accepted\n");

	/* i: a failed clause match rolls back every binding, even a matched prefix. */
	src = "fn b(${<<a:8, b:8>>, 'x}) { 1 }\n";
	program = parseprogram(&parser, "bin-rollback", src, strlen(src));
	if(program == nil)
		fail(parser.err);
	f = program->fns;
	if(f == nil || f->clauses == nil || f->clauses->patterns == nil)
		fail("bin rollback: clause missing");
	p = clausepattern(f->clauses, err, sizeof err);
	programfree(program);
	if(p == nil)
		fail(err[0] ? err : "bin rollback: conversion");
	by[0] = 1;
	by[1] = 2;
	elem[0] = nvbin(&hostheap, by, 2);
	elem[1] = nvatom("y");
	if(elem[0] == NvNil || elem[1] == NvNil)
		fail("value allocation");
	t = nvtuple(&hostheap, elem, 2);
	if(t == NvNil)
		fail("value allocation");
	memset(&b, 0, sizeof b);
	if(nvpatternmatch(&hostheap, p, t, &b, err, sizeof err) != 0)
		fail("bin rollback: expected the tuple pattern not to match");
	if(nvbinding(&b, "a") != nil || nvbinding(&b, "b") != nil)
		fail("bin rollback: failed match left bindings behind");
	nvbindingsfree(&b);
	nvpatternfree(p);
	print("ok - failed match rolls back binary segment bindings\n");
}

void
main(void)
{
	char *src, *deep, *q, *end;
	Parser parser;
	Program *program;
	Fn *f;
	Clause *c;
	NvPatClause pc[2];
	NvPattern *p[2];
	NvBindings b;
	NvTerm args, elem[2], inner, ie[2], *x;
	char err[128];
	int which, nchain;

	nvheapinit(&hostheap, 0);

	/* Two adjacent declarations of one name are one function's clauses (D058). */
	src = "fn choose(${'ok, x}, x) { x }\nfn choose(_, y) { y }\n";
	program = parseprogram(&parser, "parse-pattern-test", src, strlen(src));
	if(program == nil)
		fail(parser.err);
	f = program->fns;
	if(f == nil || f->clauses == nil || f->clauses->next == nil)
		fail("parsed clauses missing");
	for(c = f->clauses, which = 0; c != nil && which < 2; c = c->next, which++){
		p[which] = clausepattern(c, err, sizeof err);
		if(p[which] == nil)
			fail(err[0] ? err : "pattern conversion");
		pc[which].pattern = p[which];
	}

	/*
	 * D062: atoms are interned, so ie[0] must be built through nvatom
	 * rather than a private strdup. D061: nvtuple copies the term word,
	 * not the atom text, so there is no dangling-pointer hazard here any
	 * more -- the atom stays interned in the runtime-wide table.
	 */
	ie[0] = nvatom("ok");
	if(ie[0] == NvNil)
		fail("value allocation");
	ie[1] = integer(7);
	inner = nvtuple(&hostheap, ie, 2);
	if(inner == NvNil)
		fail("value allocation");
	elem[0] = inner;
	elem[1] = integer(7);
	args = nvtuple(&hostheap, elem, 2);
	if(args == NvNil)
		fail("argument allocation");
	memset(&b, 0, sizeof b);
	if(nvclauseselect(&hostheap, pc, 2, args, &b, &which, err, sizeof err) != 1 || which != 0)
		fail("parsed first clause not selected");
	x = nvbinding(&b, "x");
	if(x == nil || nvtermkind(*x) != Vint || nvtermint(*x) != 7)
		fail("parsed repeated binding missing");
	print("ok - parsed repeated tuple pattern\n");
	nvbindingsfree(&b);

	ie[0] = nvatom("other");
	if(ie[0] == NvNil)
		fail("value allocation");
	ie[1] = integer(4);
	inner = nvtuple(&hostheap, ie, 2);
	if(inner == NvNil)
		fail("value allocation");
	elem[0] = inner;
	elem[1] = integer(9);
	args = nvtuple(&hostheap, elem, 2);
	if(args == NvNil)
		fail("argument allocation");
	memset(&b, 0, sizeof b);
	if(nvclauseselect(&hostheap, pc, 2, args, &b, &which, err, sizeof err) != 1 || which != 1)
		fail("parsed fallback clause not selected");
	x = nvbinding(&b, "y");
	if(x == nil || nvtermkind(*x) != Vint || nvtermint(*x) != 9)
		fail("parsed fallback binding missing");
	print("ok - parsed clauses preserve source order\n");

	nvbindingsfree(&b);
	nvpatternfree(p[0]);
	nvpatternfree(p[1]);
	programfree(program);

	/* D058: `!` associates to the right and binds tighter than `=`. */
	src = "fn main() { x = a ! b ! c }\n";
	program = parseprogram(&parser, "send-precedence", src, strlen(src));
	if(program == nil)
		fail(parser.err);
	f = program->fns;
	if(f == nil || f->clauses == nil || f->clauses->body == nil || f->clauses->body->list == nil)
		fail("send precedence: body missing");
	{
		Expr *bind, *outer, *inner;

		bind = f->clauses->body->list->expr;
		if(bind->kind != Ebind || bind->right->kind != Esend)
			fail("send precedence: `=` must bind looser than `!`");
		outer = bind->right;
		if(outer->left->kind != Evar || strcmp(outer->left->text, "a") != 0 || outer->right->kind != Esend)
			fail("send precedence: `!` must associate to the right");
		inner = outer->right;
		if(inner->left->kind != Evar || strcmp(inner->left->text, "b") != 0 ||
		   inner->right->kind != Evar || strcmp(inner->right->text, "c") != 0)
			fail("send precedence: inner send operands");
	}
	programfree(program);
	print("ok - send operator precedence and associativity\n");

	deep = malloc(NvMaxsourcedepth+32);
	if(deep == nil)
		fail("deep source allocation");
	strcpy(deep, "fn deep() { ");
	for(which = 0; which <= NvMaxsourcedepth; which++)
		strcat(deep, "+");
	strcat(deep, "1 }\n");
	program = parseprogram(&parser, "deep-source", deep, strlen(deep));
	free(deep);
	if(program != nil || strstr(parser.err, "nesting too deep") == nil){
		programfree(program);
		fail("deep source nesting not rejected");
	}
	print("ok - source nesting depth is controlled\n");

	/*
	 * A left-associative operator chain is built iteratively, so it adds
	 * AST depth without recursing through parseexpr. Every later walk
	 * (exprfree, format, AST print, compile) descends that left spine.
	 */
	nchain = 4*NvMaxsourcedepth+32;
	deep = malloc(nchain);
	if(deep == nil)
		fail("operator chain allocation");
	end = deep+nchain;
	q = seprint(deep, end, "fn chain() { 1");
	for(which = 0; which < 64; which++)
		q = seprint(q, end, "+1");
	seprint(q, end, " }\n");
	program = parseprogram(&parser, "short-chain", deep, strlen(deep));
	if(program == nil){
		free(deep);
		fail(parser.err[0] ? parser.err : "ordinary operator chain rejected");
	}
	programfree(program);
	q = seprint(deep, end, "fn chain() { 1");
	for(which = 0; which < NvMaxsourcedepth; which++)
		q = seprint(q, end, "+1");
	seprint(q, end, " }\n");
	program = parseprogram(&parser, "deep-chain", deep, strlen(deep));
	free(deep);
	if(program != nil || strstr(parser.err, "nesting too deep") == nil){
		programfree(program);
		fail("deep operator chain not rejected");
	}
	print("ok - operator chain depth is controlled\n");
	binpatterns();
	print("all parsed pattern tests passed\n");
	nvheapfree(&hostheap);
	exits(nil);
}
