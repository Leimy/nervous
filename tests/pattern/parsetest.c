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
	if(nvclauseselect(pc, 2, args, &b, &which, err, sizeof err) != 1 || which != 0)
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
	if(nvclauseselect(pc, 2, args, &b, &which, err, sizeof err) != 1 || which != 1)
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
	print("all parsed pattern tests passed\n");
	nvheapfree(&hostheap);
	exits(nil);
}
