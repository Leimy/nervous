#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../../include/nervous.h"
#include "../../include/nvbc.h"
#include "../../include/nvvm.h"
#include "../../include/nvcompile.h"
#include "../../include/nvalloc.h"

static void
fail(char *s)
{
	fprint(2, "FAIL: %s\n", s);
	exits("test");
}

static int
run(char *src, char *entry, NvValue *arg, NvValue *result, char *err, int nerr)
{
	Parser parser;
	Program *p;
	NvModule *m;
	int r;

	p = parseprogram(&parser, "compile-pattern-test", src, strlen(src));
	if(p == nil){
		snprint(err, nerr, "%s", parser.err);
		return -2;
	}
	m = nvcompile(p, err, nerr);
	programfree(p);
	if(m == nil)
		return -2;
	if(nvverify(m, err, nerr) < 0){
		nvmodulefree(m);
		return -2;
	}
	r = nvexecute(nil, m, entry, arg, 0, 100000, result, err, nerr);
	nvmodulefree(m);
	return r;
}

static NvModule *
compiles(char *src, char *err, int nerr)
{
	Parser parser;
	Program *p;
	NvModule *m;

	p = parseprogram(&parser, "compile-process-test", src, strlen(src));
	if(p == nil){ snprint(err,nerr,"%s",parser.err); return nil; }
	m = nvcompile(p,err,nerr);
	programfree(p);
	return m;
}

static void
compilefails(char *src, char *expect)
{
	Parser parser;
	Program *p;
	NvModule *m;
	char err[256];

	p = parseprogram(&parser, "compile-negative-test", src, strlen(src));
	if(p == nil)
		fail(parser.err);
	m = nvcompile(p, err, sizeof err);
	programfree(p);
	if(m != nil){ nvmodulefree(m); fail("expected compile failure"); }
	if(strstr(err, expect) == nil)
		fail(err);
}

static void
parsefails(char *src, char *expect)
{
	Parser parser;
	Program *p;

	p = parseprogram(&parser, "parse-negative-test", src, strlen(src));
	if(p != nil){ programfree(p); fail("expected parse failure"); }
	if(strstr(parser.err, expect) == nil)
		fail(parser.err);
}

static void
allocationfailures(void)
{
	char *src;
	Parser parser;
	Program *p;
	NvModule *m;
	char err[256];
	int i, succeeded;

	src = "fn helper('ok, x) { x }\nfn helper(_) { 0 }\n"
	      "fn main() { y = ${'ok, 7}; match y { ${'ok, z} => helper(z); _ => 0; } }\n"
	      /* Exercises the Orecvdeadline/Orecvwaitdeadline emit() paths under the same fault-injection sweep. */
	      "fn timedworker(p) { receive { 'go => p; after 5 => 'late; } }\n"
	      /* And the if lowering, including the else-less 'ok path. */
	      "fn cond(x) { if x > 1 { 'big } else if x > 0 { 'one } else { 'none } }\nfn cond2(x) { if x { 1 } }\n"
	      /* And the unary/logical lowerings (compileunary, compilelogical, boolcopy). */
	      "fn logic(a, b) { ${not a and b or a, -b, +b, -1} }\n";
	p = parseprogram(&parser, "allocation-failure-test", src, strlen(src));
	if(p == nil)
		fail(parser.err);
	succeeded = 0;
	for(i = 0; i < 512; i++){
		nvallocfail(i);
		err[0] = 0;
		m = nvcompile(p, err, sizeof err);
		if(m != nil){
			nvmodulefree(m);
			succeeded = 1;
			break;
		}
		if(err[0] == 0 || strstr(err, "out_of_memory") == nil){
			nvallocfail(-1);
			programfree(p);
			fail(err[0] == 0 ? "allocation failure without diagnostic" : err);
		}
	}
	nvallocfail(-1);
	programfree(p);
	if(!succeeded || i == 0)
		fail("allocation failure sweep did not reach success");
}

static void
registerpressure(void)
{
	char *src, *p;
	int i, n;

	n = 1024;
	src = malloc(n);
	if(src == nil)
		fail("register-pressure source allocation");
	p = seprint(src, src+n, "fn main() { 0; ${");
	for(i = 0; i < 129; i++)
		p = seprint(p, src+n, "%s1", i == 0 ? "" : ",");
	seprint(p, src+n, "} }\n");
	compilefails(src, "register limit exceeded");
	free(src);
}

static void
emptytuple(NvValue *v)
{
	if(nvvaluetuple(v, nil, 0) < 0)
		fail("argument allocation");
}

void
main(void)
{
	NvValue arg, result;
	NvModule *m;
	NvFunc *f;
	NvInsn *in;
	char err[256];
	int r, i, j, seen[Nopcode];

	emptytuple(&arg);
	r = run("fn main() { 0; ${'ok, x} = ${'ok, 7}; x }\n", "main", &arg, &result, err, sizeof err);
	if(r != 0 || result.kind != Vint || result.i != 7)
		fail(r == -2 ? err : "asserted binding result");
	nvvaluefree(&result);
	print("ok - asserted binding success\n");

	r = run("fn main() { 0; ${'ok, x} = ${'error, 7}; x }\n", "main", &arg, &result, err, sizeof err);
	if(r >= 0 || strcmp(err, "match_fail") != 0)
		fail("asserted binding failure reason");
	print("ok - asserted binding failure\n");

	r = run("fn main() { x = 7; ${x} = ${7}; x }\n", "main", &arg, &result, err, sizeof err);
	if(r != 0 || result.kind != Vint || result.i != 7)
		fail(r == -2 ? err : "existing binding pattern result");
	nvvaluefree(&result);
	print("ok - existing binding pattern tests equality\n");

	r = run("fn main() { match ${${'ok, 5}, 4} { ${${'ok, z}, 3} => z; ${_, y} => y; } }\n", "main", &arg, &result, err, sizeof err);
	if(r != 0 || result.kind != Vint || result.i != 4)
		fail(r == -2 ? err : "match fallback result");
	nvvaluefree(&result);
	print("ok - match rollback and fallback\n");

	r = run("fn f('a, x) { ${'ok, y} = x; y }\nfn f(_, _) { 'fallback }\n"
	        "fn main() { f('a, ${'bad, 1}) }\n", "main", &arg, &result, err, sizeof err);
	if(r >= 0 || strcmp(err, "match_fail") != 0)
		fail(r == -2 ? err : "clause body failure escaped into next function clause");
	print("ok - function clause patching excludes clause body\n");

	/* D058: if is sugar over 'true/'false; else-if chains nest; no else yields 'ok; non-booleans fault. */
	r = run("fn main() { x = 3; if x > 5 { 'big } else if x > 2 { 'mid } else { 'small } }\n", "main", &arg, &result, err, sizeof err);
	if(r != 0 || result.kind != Vatom || strcmp(result.atom, "mid") != 0)
		fail(r == -2 ? err : "else-if chain result");
	nvvaluefree(&result);
	r = run("fn main() { if 1 > 2 { 'yes } }\n", "main", &arg, &result, err, sizeof err);
	if(r != 0 || result.kind != Vatom || strcmp(result.atom, "ok") != 0)
		fail(r == -2 ? err : "else-less if result");
	nvvaluefree(&result);
	r = run("fn main() { if 1 > 2 { 'yes } 7 }\n", "main", &arg, &result, err, sizeof err);
	if(r != 0 || result.kind != Vint || result.i != 7)
		fail(r == -2 ? err : "block-like expression needs no semicolon");
	nvvaluefree(&result);
	r = run("fn main() { if 7 { 1 } else { 2 } }\n", "main", &arg, &result, err, sizeof err);
	if(r >= 0 || strcmp(err, "match_fail") != 0)
		fail(r == -2 ? err : "non-boolean if condition must fault match_fail");
	r = run("fn main() { if 'true { y = 1; y } else { 2 }; y }\n", "main", &arg, &result, err, sizeof err);
	if(r != -2 || strstr(err, "unbound variable") == nil)
		fail("if branch bindings must not escape the branch");
	print("ok - if lowering, else-less 'ok, and branch-local bindings\n");

	/* D028 structural equality through the == and != operators (lowered onto testeq). */
	r = run("fn main() { ${1 == 1, 'a != 'b, ${1, 'x} == ${1, 'x}, 1 == 'a, 2 != 2} }\n", "main", &arg, &result, err, sizeof err);
	if(r != 0 || result.kind != Vtuple || result.tuple->n != 5 ||
	   strcmp(result.tuple->elem[0].atom, "true") != 0 || strcmp(result.tuple->elem[1].atom, "true") != 0 ||
	   strcmp(result.tuple->elem[2].atom, "true") != 0 || strcmp(result.tuple->elem[3].atom, "false") != 0 ||
	   strcmp(result.tuple->elem[4].atom, "false") != 0)
		fail(r == -2 ? err : "equality operator results");
	nvvaluefree(&result);
	print("ok - equality operators\n");

	/*
	 * Tail calls: a call in tail position (here, in an if branch that is
	 * the whole clause body) replaces the frame instead of pushing one,
	 * so 3000 iterations succeed under nvexecinit's default maxframe of
	 * 1024. The same recursion with the call as an operand is not in tail
	 * position and must still fault system_limit at the frame ceiling.
	 */
	r = run("fn loop(n) { if n == 0 { 'done } else { loop(n - 1) } }\nfn main() { loop(3000) }\n", "main", &arg, &result, err, sizeof err);
	if(r != 0 || result.kind != Vatom || strcmp(result.atom, "done") != 0)
		fail(r == -2 ? err : r < 0 ? err : "tail-recursive loop result");
	nvvaluefree(&result);
	r = run("fn deep(n) { if n == 0 { 0 } else { 1 + deep(n - 1) } }\nfn main() { deep(3000) }\n", "main", &arg, &result, err, sizeof err);
	if(r >= 0 || strcmp(err, "system_limit") != 0)
		fail(r == -2 ? err : "non-tail recursion must still hit the frame limit");
	/* Tail position also reaches through match clause bodies and nested blocks. */
	r = run("fn count(n, acc) { match n { 0 => acc; _ => { x = acc + 1; count(n - 1, x) } } }\nfn main() { count(3000, 0) }\n", "main", &arg, &result, err, sizeof err);
	if(r != 0 || result.kind != Vint || result.i != 3000)
		fail(r == -2 ? err : r < 0 ? err : "tail call through match clause and block");
	nvvaluefree(&result);
	m = compiles("fn loop(n) { if n == 0 { 'done } else { loop(n - 1) } }\n", err, sizeof err);
	if(m == nil)
		fail(err);
	memset(seen,0,sizeof seen);
	for(i = 0; i < m->nfunc; i++){
		f = &m->func[i];
		for(j = 0; j < f->ninsn; j++){
			in = &f->insn[j];
			if(in->op >= 0 && in->op < Nopcode)
				seen[in->op] = 1;
		}
	}
	if(!seen[Otailcall] || seen[Ocall])
		fail("tail-position call did not lower to tailcall");
	nvmodulefree(m);
	print("ok - tail calls run in constant frame depth; non-tail calls still bounded\n");
	nvvaluefree(&arg);

	if(nvvaluetuple(&arg, nil, 0) < 0)
		fail("argument allocation");
	r = run("fn choose() { 1 }\nfn choose(_) { 2 }\n", "choose", &arg, &result, err, sizeof err);
	if(r != 0 || result.kind != Vint || result.i != 1)
		fail(r == -2 ? err : "different arity result");
	nvvaluefree(&result);
	nvvaluefree(&arg);
	print("ok - function clause arity dispatch\n");

	if(nvvalueint(&result, "9") < 0 || nvvaluetuple(&arg, &result, 1) < 0)
		fail("argument allocation");
	nvvaluefree(&result);
	r = run("fn choose(x) { x }\nfn choose(_) { 0 }\n", "choose", &arg, &result, err, sizeof err);
	if(r != 0 || result.kind != Vint || result.i != 9)
		fail(r == -2 ? err : "overlapping clause order");
	nvvaluefree(&result);
	print("ok - function clauses preserve source order\n");

	r = run("fn choose('ok) { 1 }\n", "choose", &arg, &result, err, sizeof err);
	if(r >= 0 || strcmp(err, "function_clause") != 0)
		fail(r == -2 ? err : "function clause failure reason");
	nvvaluefree(&arg);
	print("ok - function clause failure\n");

	/* Only adjacent declarations merge; a separated redeclaration is still a duplicate. */
	compilefails("fn same() { 1 } fn other() { 2 } fn same() { 3 }\n", "duplicate function same");
	print("ok - duplicate function rejected\n");
	compilefails("fn main() { missing() }\n", "undefined function missing");
	print("ok - unresolved call rejected\n");
	/*
	 * D016/D014: unary -, +, not; and/or short-circuit; all boolean forms
	 * require 'true/'false. The shared argument tuple was freed by the
	 * clause-failure test above; every run() here needs a live empty tuple.
	 */
	emptytuple(&arg);
	r = run("fn main() { -1 }\n", "main", &arg, &result, err, sizeof err);
	if(r != 0 || result.kind != Vint || result.i != -1)
		fail(r < 0 ? err : "negative literal folds to a constant");
	nvvaluefree(&result);
	r = run("fn main() { x = 5; ${-x, +x, - -x, 3 - -x} }\n", "main", &arg, &result, err, sizeof err);
	if(r != 0 || result.kind != Vtuple || result.tuple->n != 4 ||
	   result.tuple->elem[0].i != -5 || result.tuple->elem[1].i != 5 ||
	   result.tuple->elem[2].i != 5 || result.tuple->elem[3].i != 8)
		fail(r < 0 ? err : "unary minus and plus on variables");
	nvvaluefree(&result);
	r = run("fn main() { -'a }\n", "main", &arg, &result, err, sizeof err);
	if(r >= 0 || strcmp(err, "badarith") != 0)
		fail(r == -2 ? err : "unary minus on a non-integer must fault badarith");
	r = run("fn main() { ${'true and 'false, 'true and 'true, 'false or 'false, 'false or 'true, not 'true, not 'false} }\n",
		"main", &arg, &result, err, sizeof err);
	if(r != 0 || result.kind != Vtuple || result.tuple->n != 6 ||
	   strcmp(result.tuple->elem[0].atom, "false") != 0 || strcmp(result.tuple->elem[1].atom, "true") != 0 ||
	   strcmp(result.tuple->elem[2].atom, "false") != 0 || strcmp(result.tuple->elem[3].atom, "true") != 0 ||
	   strcmp(result.tuple->elem[4].atom, "false") != 0 || strcmp(result.tuple->elem[5].atom, "true") != 0)
		fail(r < 0 ? err : "and/or/not truth table");
	nvvaluefree(&result);
	/* The right operand is not evaluated when the left decides: boom() would fault divide_by_zero. */
	r = run("fn boom() { 1 / 0 }\nfn main() { ${'false and boom() == 1, 'true or boom() == 1} }\n",
		"main", &arg, &result, err, sizeof err);
	if(r != 0 || result.kind != Vtuple || result.tuple->n != 2 ||
	   strcmp(result.tuple->elem[0].atom, "false") != 0 || strcmp(result.tuple->elem[1].atom, "true") != 0)
		fail(r < 0 ? err : "and/or short-circuit");
	nvvaluefree(&result);
	r = run("fn main() { 1 < 2 and 2 < 3 or 'false }\n", "main", &arg, &result, err, sizeof err);
	if(r != 0 || result.kind != Vatom || strcmp(result.atom, "true") != 0)
		fail(r < 0 ? err : "comparison binds tighter than and, and tighter than or");
	nvvaluefree(&result);
	r = run("fn main() { 1 and 'true }\n", "main", &arg, &result, err, sizeof err);
	if(r >= 0 || strcmp(err, "match_fail") != 0)
		fail(r == -2 ? err : "non-boolean left operand of and must fault match_fail");
	r = run("fn main() { 'true and 1 }\n", "main", &arg, &result, err, sizeof err);
	if(r >= 0 || strcmp(err, "match_fail") != 0)
		fail(r == -2 ? err : "non-boolean right operand of and must fault match_fail");
	r = run("fn main() { 'false or 'maybe }\n", "main", &arg, &result, err, sizeof err);
	if(r >= 0 || strcmp(err, "match_fail") != 0)
		fail(r == -2 ? err : "non-boolean right operand of or must fault match_fail");
	r = run("fn main() { not 0 }\n", "main", &arg, &result, err, sizeof err);
	if(r >= 0 || strcmp(err, "match_fail") != 0)
		fail(r == -2 ? err : "not on a non-boolean must fault match_fail");
	r = run("fn main() { 'true and { y = 'true; y }; y }\n", "main", &arg, &result, err, sizeof err);
	if(r != -2 || strstr(err, "unbound variable") == nil)
		fail("a binding inside a short-circuit operand must not escape it");
	nvvaluefree(&arg);
	print("ok - unary operators, short-circuit and/or, and boolean strictness\n");

	/* D060: guards on function and match clauses; fault or non-boolean means the clause fails. */
	emptytuple(&arg);
	r = run("fn sign(x) when x > 0 { 1 }\nfn sign(x) when x < 0 { -1 }\nfn sign(_) { 0 }\n"
	        "fn main() { ${sign(5), sign(-3), sign(0)} }\n", "main", &arg, &result, err, sizeof err);
	if(r != 0 || result.kind != Vtuple || result.tuple->n != 3 ||
	   result.tuple->elem[0].i != 1 || result.tuple->elem[1].i != -1 || result.tuple->elem[2].i != 0)
		fail(r < 0 ? err : "function clause guards select in source order");
	nvvaluefree(&result);
	r = run("fn f(x) when x > 0 { 'pos }\nfn f(_) { 'other }\nfn main() { f('a) }\n", "main", &arg, &result, err, sizeof err);
	if(r != 0 || result.kind != Vatom || strcmp(result.atom, "other") != 0)
		fail(r < 0 ? err : "a guard that faults badarith is false, not a process fault");
	nvvaluefree(&result);
	r = run("fn d(x) when 10 / x > 1 { 'big }\nfn d(_) { 'small }\nfn main() { d(0) }\n", "main", &arg, &result, err, sizeof err);
	if(r != 0 || result.kind != Vatom || strcmp(result.atom, "small") != 0)
		fail(r < 0 ? err : "a guard that faults divide_by_zero is false");
	nvvaluefree(&result);
	r = run("fn g(x) when x { 'yes }\nfn g(_) { 'no }\nfn main() { ${g('true), g(5), g('false)} }\n", "main", &arg, &result, err, sizeof err);
	if(r != 0 || result.kind != Vtuple || result.tuple->n != 3 ||
	   strcmp(result.tuple->elem[0].atom, "yes") != 0 || strcmp(result.tuple->elem[1].atom, "no") != 0 ||
	   strcmp(result.tuple->elem[2].atom, "no") != 0)
		fail(r < 0 ? err : "a non-boolean guard value is false");
	nvvaluefree(&result);
	r = run("fn h(x) when is_int(x) and x > 0 { 'pos }\nfn h(_) { 'neg }\nfn main() { ${h(3), h('a), h(-1)} }\n",
		"main", &arg, &result, err, sizeof err);
	if(r != 0 || result.kind != Vtuple || result.tuple->n != 3 ||
	   strcmp(result.tuple->elem[0].atom, "pos") != 0 || strcmp(result.tuple->elem[1].atom, "neg") != 0 ||
	   strcmp(result.tuple->elem[2].atom, "neg") != 0)
		fail(r < 0 ? err : "type test guarding a comparison");
	nvvaluefree(&result);
	r = run("fn kind(x) when is_int(x) { 'int }\nfn kind(x) when is_atom(x) { 'atom }\nfn kind(x) when is_tuple(x) { 'tuple }\n"
	        "fn main() { ${kind(1), kind('a), kind(${}), is_pid(1), is_ref('a), is_tuple(${1})} }\n",
		"main", &arg, &result, err, sizeof err);
	if(r != 0 || result.kind != Vtuple || result.tuple->n != 6 ||
	   strcmp(result.tuple->elem[0].atom, "int") != 0 || strcmp(result.tuple->elem[1].atom, "atom") != 0 ||
	   strcmp(result.tuple->elem[2].atom, "tuple") != 0 || strcmp(result.tuple->elem[3].atom, "false") != 0 ||
	   strcmp(result.tuple->elem[4].atom, "false") != 0 || strcmp(result.tuple->elem[5].atom, "true") != 0)
		fail(r < 0 ? err : "type tests as guards and as ordinary expressions");
	nvvaluefree(&result);
	r = run("fn main() { match 7 { n when n % 2 == 0 => 'even; n when n % 2 == 1 => 'odd; } }\n", "main", &arg, &result, err, sizeof err);
	if(r != 0 || result.kind != Vatom || strcmp(result.atom, "odd") != 0)
		fail(r < 0 ? err : "match clause guards");
	nvvaluefree(&result);
	r = run("fn e(x) when x > 100 { 'big }\nfn main() { e(1) }\n", "main", &arg, &result, err, sizeof err);
	if(r >= 0 || strcmp(err, "function_clause") != 0)
		fail(r == -2 ? err : "guards failing every clause is function_clause");
	r = run("fn main() { match 1 { n when n > 5 => 'big; } }\n", "main", &arg, &result, err, sizeof err);
	if(r >= 0 || strcmp(err, "match_fail") != 0)
		fail(r == -2 ? err : "guards failing every match clause is match_fail");
	/* Guard mode must be off again after a failed guard: a fault in the next clause's body is a real fault. */
	r = run("fn f(x) when x > 0 { 'pos }\nfn f(x) { 1 / x }\nfn main() { f(0) }\n", "main", &arg, &result, err, sizeof err);
	if(r >= 0 || strcmp(err, "divide_by_zero") != 0)
		fail(r == -2 ? err : "a fault after a failed guard is an ordinary process fault");
	r = run("fn f(x) when x > 0 { 'pos }\nfn f(_) { 1 / 0 }\nfn main() { f('a) }\n", "main", &arg, &result, err, sizeof err);
	if(r >= 0 || strcmp(err, "divide_by_zero") != 0)
		fail(r == -2 ? err : "a fault after a guard that itself faulted is an ordinary process fault");
	r = run("fn f(x) when x > 0 { 1 / x }\nfn main() { f(0) }\n", "main", &arg, &result, err, sizeof err);
	if(r >= 0 || strcmp(err, "function_clause") != 0)
		fail(r == -2 ? err : "guard failure on 0 > 0 reaches function_clause, not the body");
	r = run("fn f(x) when x == 0 { 1 / x }\nfn main() { f(0) }\n", "main", &arg, &result, err, sizeof err);
	if(r >= 0 || strcmp(err, "divide_by_zero") != 0)
		fail(r == -2 ? err : "a fault in the body of a clause whose guard passed is a real fault");
	/* Negative literal patterns. */
	r = run("fn n(-1) { 'minus }\nfn n(_) { 'other }\nfn main() { ${n(-1), n(1), match -5 { -5 => 'five; _ => 'no; }} }\n",
		"main", &arg, &result, err, sizeof err);
	if(r != 0 || result.kind != Vtuple || result.tuple->n != 3 ||
	   strcmp(result.tuple->elem[0].atom, "minus") != 0 || strcmp(result.tuple->elem[1].atom, "other") != 0 ||
	   strcmp(result.tuple->elem[2].atom, "five") != 0)
		fail(r < 0 ? err : "negative integer literal patterns");
	nvvaluefree(&result);
	nvvaluefree(&arg);
	/* Guard expression restrictions and reserved type-test names. */
	compilefails("fn g(_) { 'true }\nfn f(x) when g(x) { 1 }\n", "only type tests may be called in a guard");
	compilefails("fn f(x) when { x } { 1 }\n", "expression not allowed in a guard");
	compilefails("fn f(x) when y = x { 1 }\n", "expression not allowed in a guard");
	compilefails("fn f(x) when self == x { 1 }\n", "expression not allowed in a guard");
	compilefails("fn f(x) when print(x) { 1 }\n", "only type tests may be called in a guard");
	compilefails("fn f(x) when is_int(x, x) { 1 }\n", "type test expects one value");
	compilefails("fn is_int(_) { 1 }\n", "reserved intrinsic is_int");
	print("ok - guards: selection, fault-means-false, type tests, restrictions, negative literal patterns\n");
	registerpressure();
	print("ok - register pressure rejected\n");

	m = compiles("fn worker(parent) { receive { ${'ping, x} => parent ! x; _ => exit 'bad; } }\n"
	             "fn main() { me = self; ref = mkref; pid = spawn worker(me, ref); pid ! ${'ping, ref} }\n", err, sizeof err);
	if(m == nil)
		fail(err);
	memset(seen,0,sizeof seen);
	for(i = 0; i < m->nfunc; i++){
		f = &m->func[i];
		for(j = 0; j < f->ninsn; j++){
			in = &f->insn[j];
			if(in->op >= 0 && in->op < Nopcode)
				seen[in->op] = 1;
		}
	}
	if(!seen[Oself] || !seen[Omakeref] || !seen[Ospawn] || !seen[Osend] ||
	   !seen[Orecvbegin] || !seen[Orecvnext] || !seen[Orecvtake] || !seen[Orecvwait] || !seen[Oexit])
		fail("source process forms did not lower to complete process bytecode");
	nvmodulefree(m);
	print("ok - source process forms lower to process bytecode\n");
	/* D058: the spawn target is a grammatical function name, and the process forms are keywords. */
	parsefails("fn main() { spawn 'worker() }\n", "spawn requires a function name");
	parsefails("fn main() { spawn worker }\n", "expected (");
	parsefails("fn self() { 0 }\n", "function name expected");
	parsefails("fn main() { exit = 1 }\n", "expected expression");
	compilefails("fn print() { 0 }\n", "reserved intrinsic print");
	print("ok - malformed and reserved process forms rejected\n");

	m = compiles("fn worker(parent) { receive { 'ping => parent ! 'pong; after 1000000 => exit 'timeout; } }\n"
	             "fn main() { pid = spawn worker(self); pid ! 'ping }\n", err, sizeof err);
	if(m == nil)
		fail(err);
	memset(seen,0,sizeof seen);
	for(i = 0; i < m->nfunc; i++){
		f = &m->func[i];
		for(j = 0; j < f->ninsn; j++){
			in = &f->insn[j];
			if(in->op >= 0 && in->op < Nopcode)
				seen[in->op] = 1;
		}
	}
	if(!seen[Orecvdeadline] || !seen[Orecvwaitdeadline])
		fail("after clause did not lower to deadline bytecode");
	if(seen[Orecvwait])
		fail("receive with an after clause should not also emit plain recvwait");
	nvmodulefree(m);
	print("ok - after clause lowers to recvdeadline/recvwaitdeadline\n");

	compilefails("fn main() { receive { 'x => 1; after 1000000000000000001 => 2; } }\n", "bad_timeout");
	compilefails("fn main() { receive { 'x => 1; after 'soon => 2; } }\n", "bad_timeout");
	print("ok - out-of-range literal receive timeouts are compile errors\n");

	allocationfailures();
	print("ok - compiler allocation failure sweep\n");

	print("all compiled pattern tests passed\n");
	exits(nil);
}
