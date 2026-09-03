#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../include/nervous.h"
#include "../include/nvbc.h"
#include "../include/nvvm.h"
#include "../include/nvpat.h"
#include "../include/nvpatbc.h"
#include "../include/nvcompile.h"
#include "../include/nvalloc.h"

#define malloc nvmalloc
#define mallocz nvmallocz
#define realloc nvrealloc
#define strdup nvstrdup

typedef struct Binding Binding;
typedef struct Compiler Compiler;
typedef struct Fcomp Fcomp;

struct Binding {
	char *name;
	int reg;
};

struct Compiler {
	NvModule *module;
	char *err;
	int nerr;
};

struct Fcomp {
	Compiler *compiler;
	NvFunc *func;
	int nextreg;
	int nbind;
	Binding *bind;
};

/*
 * compileexpr returns the register holding the expression's value, or -1
 * on error. When asked to compile in tail position (tail != 0), a call
 * becomes Otailcall, which replaces the current frame and never yields a
 * value to this function; such an expression returns Rnone instead of a
 * register so the enclosing if/match/receive/function can omit the
 * move/jump/return that nothing could ever reach. Rnone only ever comes
 * back from a tail-position compile, so callers passing tail == 0 need
 * not check for it.
 */
enum {
	Rnone = -2,
};

static int compileexpr(Fcomp *, Expr *, int);

static int
seterr(Fcomp *f, Expr *e, char *s)
{
	if(f->compiler->err[0] == 0)
		snprint(f->compiler->err, f->compiler->nerr, "%d:%d: %s", e->span.line, e->span.col, s);
	return -1;
}

static int
newreg(Fcomp *f, Expr *e)
{
	if(f->nextreg >= NvMaxreg)
		return seterr(f, e, "register limit exceeded");
	return f->nextreg++;
}

static int
emit(Fcomp *f, int op, int a, int b, int c)
{
	NvInsn *p;
	int pc;

	if(f->func->ninsn == NvMaxitem){
		snprint(f->compiler->err, f->compiler->nerr, "instruction limit exceeded");
		return -1;
	}
	p = realloc(f->func->insn, (f->func->ninsn+1)*sizeof *p);
	if(p == nil){
		snprint(f->compiler->err, f->compiler->nerr, "out_of_memory");
		return -1;
	}
	f->func->insn = p;
	pc = f->func->ninsn++;
	memset(&p[pc], 0, sizeof p[pc]);
	p[pc].op = op;
	p[pc].a = a;
	p[pc].b = b;
	p[pc].c = c;
	return pc;
}

static int
constant(Compiler *c, int kind, vlong n, char *s)
{
	NvConst *k, *p;
	int i;

	for(i = 0; i < c->module->nconst; i++){
		k = &c->module->konst[i];
		if(k->kind == kind && (kind == Kint ? k->ival == n : strcmp(k->text, s) == 0))
			return i;
	}
	if(c->module->nconst == NvMaxitem){
		snprint(c->err, c->nerr, "constant limit exceeded");
		return -1;
	}
	p = realloc(c->module->konst, (c->module->nconst+1)*sizeof *p);
	if(p == nil){
		snprint(c->err, c->nerr, "out_of_memory");
		return -1;
	}
	c->module->konst = p;
	k = &p[c->module->nconst];
	memset(k, 0, sizeof *k);
	k->kind = kind;
	k->ival = n;
	if(kind != Kint){
		k->text = strdup(s);
		if(k->text == nil){
			snprint(c->err, c->nerr, "out_of_memory");
			return -1;
		}
	}
	return c->module->nconst++;
}

static int
findbind(Fcomp *f, char *name)
{
	int i;

	for(i = f->nbind-1; i >= 0; i--)
		if(strcmp(f->bind[i].name, name) == 0)
			return f->bind[i].reg;
	return -1;
}

static int
addbind(Fcomp *f, char *name, int reg)
{
	Binding *p;

	if(findbind(f, name) >= 0)
		return -1;
	p = realloc(f->bind, (f->nbind+1)*sizeof *p);
	if(p == nil)
		return -1;
	f->bind = p;
	p[f->nbind].name = strdup(name);
	if(p[f->nbind].name == nil)
		return -1;
	p[f->nbind].reg = reg;
	f->nbind++;
	return 0;
}

static void
trimenv(Fcomp *f, int n)
{
	while(f->nbind > n)
		free(f->bind[--f->nbind].name);
}

static int
loadliteral(Fcomp *f, Expr *e, int kind, vlong n, char *s)
{
	int r, k;

	r = newreg(f, e);
	k = constant(f->compiler, kind, n, s);
	if(r < 0 || k < 0 || emit(f, Oloadk, r, k, 0) < 0)
		return -1;
	return r;
}

static int
compilelist(Fcomp *f, Exprs *x, int *first, int *count, Expr *owner)
{
	Exprs *q;
	int *reg;
	int i, n, r, dst;

	for(n = 0, q = x; q != nil; q = q->next)
		n++;
	reg = malloc(n*sizeof *reg);
	if(n != 0 && reg == nil)
		return seterr(f, owner, "out_of_memory");
	for(i = 0; x != nil; i++, x = x->next){
		r = compileexpr(f, x->expr, 0);
		if(r < 0){ free(reg); return -1; }
		reg[i] = r;
	}
	*first = f->nextreg;
	for(i = 0; i < n; i++){
		dst = newreg(f, owner);
		if(dst < 0 || emit(f, Omove, dst, reg[i], 0) < 0){ free(reg); return -1; }
	}
	free(reg);
	*count = n;
	return 0;
}

static void
patchtests(NvFunc *f, int start, int end, int target)
{
	NvInsn *i;
	int pc;

	for(pc = start; pc < end; pc++){
		i = &f->insn[pc];
		if(i->op == Otestatom || i->op == Otestint || i->op == Otesteq || i->op == Otestarity)
			i->c = target;
	}
}

static int
compilepattern(Fcomp *f, Expr *pattern, int src, int failpc)
{
	NvPattern *p;
	NvPatCode code;
	NvInsn *insn;
	NvConst *konst;
	NvPatReg *known;
	int i, oldn, base, oldbind, oldreg;
	char err[128];

	p = nvpatternfromexpr(pattern, err, sizeof err);
	if(p == nil)
		return seterr(f, pattern, err);
	base = f->compiler->module->nconst;
	oldbind = f->nbind;
	oldreg = f->nextreg;
	known = mallocz(f->nbind*sizeof *known, 1);
	if(f->nbind != 0 && known == nil){ nvpatternfree(p); return seterr(f, pattern, "out_of_memory"); }
	for(i = 0; i < f->nbind; i++){
		known[i].name = f->bind[i].name;
		known[i].reg = f->bind[i].reg;
	}
	if(nvpatterncode(p, known, f->nbind, src, f->nextreg, base, failpc, &code, err, sizeof err) < 0){
		free(known);
		nvpatternfree(p);
		return seterr(f, pattern, err);
	}
	free(known);
	nvpatternfree(p);
	oldn = f->func->ninsn;
	insn = realloc(f->func->insn, (oldn+code.ninsn)*sizeof *insn);
	if(insn == nil && code.ninsn != 0){ nvpatterncodefree(&code); return seterr(f, pattern, "out_of_memory"); }
	f->func->insn = insn;
	for(i = 0; i < code.ninsn; i++)
		f->func->insn[oldn+i] = code.insn[i];
	f->func->ninsn += code.ninsn;
	if(code.nconst != 0){
		konst = realloc(f->compiler->module->konst, (base+code.nconst)*sizeof *konst);
		if(konst == nil){
			f->func->ninsn = oldn;
			nvpatterncodefree(&code);
			return seterr(f, pattern, "out_of_memory");
		}
		f->compiler->module->konst = konst;
		for(i = 0; i < code.nconst; i++){
			konst[base+i] = code.konst[i];
			code.konst[i].text = nil;
		}
		f->compiler->module->nconst += code.nconst;
	}
	for(i = code.nknown; i < code.nbind; i++)
		if(addbind(f, code.bind[i].name, code.bind[i].reg) < 0){
			while(f->compiler->module->nconst > base)
				free(f->compiler->module->konst[--f->compiler->module->nconst].text);
			f->func->ninsn = oldn;
			trimenv(f, oldbind);
			f->nextreg = oldreg;
			nvpatterncodefree(&code);
			return seterr(f, pattern, "duplicate binding or out_of_memory");
		}
	f->nextreg = code.nextreg;
	nvpatterncodefree(&code);
	return 0;
}

/* Only the block's final expression inherits the block's tail position. */
static int
compileblock(Fcomp *f, Expr *e, int tail)
{
	Exprs *x;
	int r;

	if(e->list == nil)
		return seterr(f, e, "empty block has no value");
	r = -1;
	for(x = e->list; x != nil; x = x->next){
		r = compileexpr(f, x->expr, x->next == nil ? tail : 0);
		if(r < 0 && r != Rnone)
			return -1;
	}
	return r;
}

/*
 * `==` and `!=` are D028 structural equality, which the pattern test
 * Otesteq already implements; lower them to that test selecting one of
 * the two boolean atoms rather than adding a value-producing opcode.
 */
static int
compileequality(Fcomp *f, Expr *e, int l, int r, int d)
{
	int ktrue, kfalse, test, jump, eq;

	eq = strcmp(e->text, "==") == 0;
	ktrue = constant(f->compiler, Katom, 0, "true");
	kfalse = constant(f->compiler, Katom, 0, "false");
	if(ktrue < 0 || kfalse < 0)
		return -1;
	test = emit(f, Otesteq, l, r, 0);
	if(test < 0 || emit(f, Oloadk, d, eq ? ktrue : kfalse, 0) < 0)
		return -1;
	jump = emit(f, Ojump, 0, 0, 0);
	if(jump < 0)
		return -1;
	f->func->insn[test].c = f->func->ninsn;
	if(emit(f, Oloadk, d, eq ? kfalse : ktrue, 0) < 0)
		return -1;
	f->func->insn[jump].a = f->func->ninsn;
	return d;
}

static int
compilebinary(Fcomp *f, Expr *e)
{
	int l, r, d, op;

	l = compileexpr(f, e->left, 0);
	r = compileexpr(f, e->right, 0);
	d = newreg(f, e);
	if(l < 0 || r < 0 || d < 0)
		return -1;
	if(strcmp(e->text, "==") == 0 || strcmp(e->text, "!=") == 0)
		return compileequality(f, e, l, r, d);
	if(strcmp(e->text, "+") == 0) op = Oadd;
	else if(strcmp(e->text, "-") == 0) op = Osub;
	else if(strcmp(e->text, "*") == 0) op = Omul;
	else if(strcmp(e->text, "/") == 0) op = Odiv;
	else if(strcmp(e->text, "%") == 0) op = Orem;
	else if(strcmp(e->text, "<") == 0) op = Olt;
	else if(strcmp(e->text, "<=") == 0) op = Ole;
	else if(strcmp(e->text, ">") == 0) op = Ogt;
	else if(strcmp(e->text, ">=") == 0) op = Oge;
	else return seterr(f, e, "operator not yet supported by compiler");
	if(emit(f, op, d, l, r) < 0)
		return -1;
	return d;
}

static int
compilebind(Fcomp *f, Expr *e)
{
	int src, start, successjump, failpc, reason, end;

	src = compileexpr(f, e->right, 0);
	if(src < 0)
		return -1;
	start = f->func->ninsn;
	if(compilepattern(f, e->left, src, 0) < 0)
		return -1;
	successjump = emit(f, Ojump, 0, 0, 0);
	if(successjump < 0)
		return -1;
	failpc = f->func->ninsn;
	reason = constant(f->compiler, Katom, 0, "match_fail");
	if(reason < 0 || emit(f, Ofail, reason, 0, 0) < 0)
		return -1;
	end = f->func->ninsn;
	patchtests(f->func, start, successjump, failpc);
	f->func->insn[successjump].a = end;
	return src;
}

/*
 * Clause bodies inherit the match's tail position. A body that tail-calls
 * (Rnone) emits no move/jump, since control has left the frame; if every
 * clause does so, nothing can reach the join point and the whole match is
 * itself Rnone, so the caller omits its return as well.
 */
static int
compilematch(Fcomp *f, Expr *e, int tail)
{
	Clause *cl;
	NvInsn *i;
	int src, dst, env, start, patend, body, next, move, jump, reason, nclause;
	int *done, ndone, *p, j;

	src = compileexpr(f, e->left, 0);
	dst = newreg(f, e);
	if(src < 0 || dst < 0)
		return -1;
	done = nil;
	ndone = 0;
	nclause = 0;
	for(cl = e->clauses; cl != nil; cl = cl->next){
		nclause++;
		env = f->nbind;
		start = f->func->ninsn;
		if(cl->patterns == nil || cl->patterns->next != nil){
			free(done);
			return seterr(f, e, "match clause must have one pattern");
		}
		if(compilepattern(f, cl->patterns->expr, src, 0) < 0){ free(done); return -1; }
		patend = f->func->ninsn;
		body = compileexpr(f, cl->body, tail);
		if(body < 0 && body != Rnone){ free(done); return -1; }
		if(body != Rnone){
			move = emit(f, Omove, dst, body, 0);
			jump = emit(f, Ojump, 0, 0, 0);
			if(move < 0 || jump < 0){ free(done); return -1; }
			p = realloc(done, (ndone+1)*sizeof *p);
			if(p == nil){ free(done); return seterr(f, e, "out_of_memory"); }
			done = p;
			done[ndone++] = jump;
		}
		next = f->func->ninsn;
		patchtests(f->func, start, patend, next);
		trimenv(f, env);
	}
	reason = constant(f->compiler, Katom, 0, "match_fail");
	if(reason < 0 || emit(f, Ofail, reason, 0, 0) < 0){ free(done); return -1; }
	next = f->func->ninsn;
	for(j = 0; j < ndone; j++){
		i = &f->func->insn[done[j]];
		i->a = next;
	}
	free(done);
	return tail && nclause > 0 && ndone == 0 ? Rnone : dst;
}

static int
exprcount(Exprs *x)
{
	int n;

	for(n = 0; x != nil; x = x->next)
		n++;
	return n;
}

/*
 * D053/D054: print and eprint are the only call-shaped intrinsics left
 * after D058 gave the process forms their own syntax. They stay calls
 * because they belong in a future io module, not in the language core.
 */
static int
compileintrinsic(Fcomp *f, Expr *e)
{
	int n, a, d;

	if(strcmp(e->text, "print") != 0 && strcmp(e->text, "eprint") != 0)
		return -2;
	n = exprcount(e->list);
	if(n != 1) return seterr(f,e,"print/eprint expects one value");
	a = compileexpr(f,e->list->expr,0);
	d = newreg(f,e);
	if(a < 0 || d < 0 || emit(f, strcmp(e->text,"print") == 0 ? Oprint : Oeprint, d, a, 0) < 0) return -1;
	return d;
}

static int
compilespawn(Fcomp *f, Expr *e)
{
	int a, d, first, count, k;

	if(compilelist(f,e->list,&first,&count,e) < 0) return -1;
	a = newreg(f,e);
	if(a < 0 || emit(f,Otuple,a,first,count) < 0) return -1;
	d = newreg(f,e);
	k = constant(f->compiler,Kfunc,0,e->text);
	if(d < 0 || k < 0 || emit(f,Ospawn,d,k,a) < 0) return -1;
	return d;
}

static int
compilesend(Fcomp *f, Expr *e)
{
	int a, b, d;

	a = compileexpr(f,e->left,0);
	b = compileexpr(f,e->right,0);
	d = newreg(f,e);
	if(a < 0 || b < 0 || d < 0 || emit(f,Osend,d,a,b) < 0) return -1;
	return d;
}

/*
 * D058: `if` is sugar over the boolean atoms. The condition must be
 * exactly 'true or 'false; anything else faults match_fail, so no
 * truthiness rule enters the language. A missing else yields 'ok.
 * Branch bindings are local to the branch, as match clause bindings
 * are (D013).
 */
static int
compileif(Fcomp *f, Expr *e, int tail)
{
	int src, dst, ktrue, kfalse, kok, reason, env, test, body, jthen, jelse;

	src = compileexpr(f, e->left, 0);
	dst = newreg(f, e);
	ktrue = constant(f->compiler, Katom, 0, "true");
	kfalse = constant(f->compiler, Katom, 0, "false");
	if(src < 0 || dst < 0 || ktrue < 0 || kfalse < 0)
		return -1;
	env = f->nbind;
	test = emit(f, Otestatom, src, ktrue, 0);
	if(test < 0)
		return -1;
	/*
	 * Both branches inherit the if's tail position. A branch that
	 * tail-calls (Rnone) has already left the frame, so it gets no move
	 * into dst and no jump to the join; jthen/jelse stay -1 for it.
	 */
	jthen = -1;
	body = compileexpr(f, e->right, tail);
	if(body < 0 && body != Rnone)
		return -1;
	if(body != Rnone){
		if(emit(f, Omove, dst, body, 0) < 0)
			return -1;
		jthen = emit(f, Ojump, 0, 0, 0);
		if(jthen < 0)
			return -1;
	}
	trimenv(f, env);
	f->func->insn[test].c = f->func->ninsn;
	test = emit(f, Otestatom, src, kfalse, 0);
	if(test < 0)
		return -1;
	jelse = -1;
	if(e->list != nil){
		body = compileexpr(f, e->list->expr, tail);
		if(body < 0 && body != Rnone)
			return -1;
		if(body != Rnone && emit(f, Omove, dst, body, 0) < 0)
			return -1;
	}else{
		kok = constant(f->compiler, Katom, 0, "ok");
		if(kok < 0 || emit(f, Oloadk, dst, kok, 0) < 0)
			return -1;
		body = dst;
	}
	if(body != Rnone){
		jelse = emit(f, Ojump, 0, 0, 0);
		if(jelse < 0)
			return -1;
	}
	trimenv(f, env);
	f->func->insn[test].c = f->func->ninsn;
	reason = constant(f->compiler, Katom, 0, "match_fail");
	if(reason < 0 || emit(f, Ofail, reason, 0, 0) < 0)
		return -1;
	if(jthen >= 0)
		f->func->insn[jthen].a = f->func->ninsn;
	if(jelse >= 0)
		f->func->insn[jelse].a = f->func->ninsn;
	/* No branch produced a value here: nothing reaches the join point. */
	return jthen < 0 && jelse < 0 ? Rnone : dst;
}

/*
 * D049: a literal duration out of the accepted domain is a compile-time
 * error with source position. A non-literal duration (variable, call,
 * ...) defers to the runtime bad_timeout fault in nvprocarmdeadline.
 */
static int
checkdurationliteral(Fcomp *f, Expr *e)
{
	if(e->kind == Eint){
		if(e->ival < 0 || e->ival > NvMaxduration)
			return seterr(f, e, "bad_timeout: duration literal out of range");
		return 0;
	}
	if(e->kind == Eatom){
		if(strcmp(e->text, "infinity") != 0)
			return seterr(f, e, "bad_timeout: duration atom must be 'infinity");
		return 0;
	}
	return 0;
}

/*
 * Clause bodies and the `after` timeout body inherit the receive's tail
 * position, exactly as match clause bodies do; a body that tail-calls
 * (Rnone) has consumed its message via recvtake and left the frame, so it
 * emits no move/jump to the join. This is what lets an Erlang-style
 * message loop (`fn loop(s) { receive { m => loop(s2) } }`) run in one
 * frame instead of growing by one frame per message received.
 */
static int
compilereceive(Fcomp *f, Expr *e, int tail)
{
	Clause *cl;
	NvInsn *i;
	int candidate, found, dst, truth, begin, check, env, start, take, body, move, jump, next, advance, wait, end;
	int *done, ndone, *p, j, nvalue;
	int dur, timeout;

	if(e->left != nil && checkdurationliteral(f, e->left) < 0)
		return -1;
	candidate = newreg(f,e);
	found = newreg(f,e);
	dst = newreg(f,e);
	truth = constant(f->compiler,Katom,0,"true");
	if(candidate < 0 || found < 0 || dst < 0 || truth < 0)
		return -1;
	if(e->left != nil){
		dur = compileexpr(f, e->left, 0);
		if(dur < 0 || emit(f,Orecvdeadline,dur,0,0) < 0)
			return -1;
	}
	begin = emit(f,Orecvbegin,candidate,found,0);
	check = emit(f,Otestatom,found,truth,0);
	if(begin < 0 || check < 0)
		return -1;
	done = nil;
	ndone = 0;
	nvalue = 0;
	for(cl = e->clauses; cl != nil; cl = cl->next){
		env = f->nbind;
		start = f->func->ninsn;
		if(cl->patterns == nil || cl->patterns->next != nil){ free(done); return seterr(f,e,"receive clause must have one pattern"); }
		if(compilepattern(f,cl->patterns->expr,candidate,0) < 0){ free(done); return -1; }
		take = emit(f,Orecvtake,0,0,0);
		if(take < 0){ free(done); return -1; }
		body = compileexpr(f,cl->body,tail);
		if(body < 0 && body != Rnone){ free(done); return -1; }
		if(body != Rnone){
			nvalue++;
			move = emit(f,Omove,dst,body,0);
			jump = emit(f,Ojump,0,0,0);
			if(move < 0 || jump < 0){ free(done); return -1; }
			p = realloc(done,(ndone+1)*sizeof *p);
			if(p == nil){ free(done); return seterr(f,e,"out_of_memory"); }
			done = p;
			done[ndone++] = jump;
		}
		next = f->func->ninsn;
		patchtests(f->func,start,take,next);
		trimenv(f,env);
	}
	advance = emit(f,Orecvnext,candidate,found,0);
	if(advance < 0 || emit(f,Ojump,check,0,0) < 0){ free(done); return -1; }
	if(e->left != nil){
		/*
		 * D051: recvwaitdeadline has two successors. Blocking (or an
		 * exhausted-scan race rescan) resumes at begin; an expired
		 * deadline falls through into the timeout body compiled right
		 * after it, which cannot read any receive-clause binding
		 * because every clause's tentative bindings were already
		 * trimmed back to the pre-receive environment above.
		 */
		wait = emit(f,Orecvwaitdeadline,begin,0,0);
		if(wait < 0){ free(done); return -1; }
		f->func->insn[check].c = wait;
		timeout = compileexpr(f, e->right, tail);
		if(timeout < 0 && timeout != Rnone){ free(done); return -1; }
		if(timeout != Rnone){
			nvalue++;
			if(emit(f,Omove,dst,timeout,0) < 0){ free(done); return -1; }
		}
	}else{
		wait = emit(f,Orecvwait,begin,0,0);
		if(wait < 0){ free(done); return -1; }
		f->func->insn[check].c = wait;
	}
	end = f->func->ninsn;
	for(j = 0; j < ndone; j++){
		i = &f->func->insn[done[j]];
		i->a = end;
	}
	free(done);
	/*
	 * Without an after clause, recvwait always loops back to begin, so
	 * if every clause body tail-called nothing can fall through to end
	 * and the receive as a whole is Rnone. With an after clause the
	 * timeout body is the extra path that must also have tail-called.
	 */
	return tail && e->clauses != nil && nvalue == 0 ? Rnone : dst;
}

static int
compileexpr(Fcomp *f, Expr *e, int tail)
{
	int r, d, first, count, k;

	switch(e->kind){
	case Eint: return loadliteral(f, e, Kint, e->ival, nil);
	case Eatom: return loadliteral(f, e, Katom, 0, e->text);
	case Evar:
		r = findbind(f, e->text);
		return r >= 0 ? r : seterr(f, e, "unbound variable");
	case Etuple:
		if(compilelist(f, e->list, &first, &count, e) < 0) return -1;
		d = newreg(f, e);
		if(d < 0 || emit(f, Otuple, d, first, count) < 0) return -1;
		return d;
	case Eblock: return compileblock(f, e, tail);
	case Ebinary: return compilebinary(f, e);
	case Ecall:
		r = compileintrinsic(f,e);
		if(r != -2) return r;
		if(compilelist(f, e->list, &first, &count, e) < 0) return -1;
		r = newreg(f, e);
		if(r < 0 || emit(f, Otuple, r, first, count) < 0) return -1;
		k = constant(f->compiler, Kfunc, 0, e->text);
		if(k < 0) return -1;
		/*
		 * A call whose value would be returned unchanged is a tail call:
		 * the callee replaces this frame (exec.c Otailcall), so recursion
		 * in tail position runs in constant frame depth rather than
		 * counting against maxframe once per iteration. There is no
		 * result register because this frame no longer exists.
		 */
		if(tail){
			if(emit(f, Otailcall, k, r, 0) < 0) return -1;
			return Rnone;
		}
		d = newreg(f, e);
		if(d < 0 || emit(f, Ocall, d, k, r) < 0) return -1;
		return d;
	case Ebind:
		return compilebind(f, e);
	case Ematch:
		return compilematch(f, e, tail);
	case Ereceive:
		return compilereceive(f, e, tail);
	case Eif:
		return compileif(f, e, tail);
	case Eself:
		d = newreg(f, e);
		if(d < 0 || emit(f, Oself, d, 0, 0) < 0) return -1;
		return d;
	case Emkref:
		d = newreg(f, e);
		if(d < 0 || emit(f, Omakeref, d, 0, 0) < 0) return -1;
		return d;
	case Espawn:
		return compilespawn(f, e);
	case Esend:
		return compilesend(f, e);
	case Eexit:
		r = compileexpr(f, e->left, 0);
		if(r < 0 || emit(f, Oexit, r, 0, 0) < 0) return -1;
		return r;
	case Eunary:
		return seterr(f, e, "unary operator not yet supported by compiler");
	case Ewild:
		return seterr(f, e, "wildcard is not an expression");
	}
	return seterr(f, e, "unsupported expression");
}

static Expr *
clausetuple(Clause *c)
{
	Expr *e;
	Span s;

	memset(&s, 0, sizeof s);
	e = mallocz(sizeof *e, 1);
	if(e == nil)
		return nil;
	e->kind = Etuple;
	e->span = s;
	e->list = c->patterns;
	return e;
}

static void
fcompfree(Fcomp *f)
{
	trimenv(f, 0);
	free(f->bind);
}

static int
compilefunction(Compiler *c, Fn *source, NvFunc *out)
{
	Fcomp f;
	Clause *cl;
	Expr *p;
	int r, env, start, patend, end, reason;

	memset(&f, 0, sizeof f);
	f.compiler = c;
	f.func = out;
	f.nextreg = 1;
	out->name = strdup(source->name);
	if(out->name == nil){ snprint(c->err,c->nerr,"out_of_memory"); return -1; }
	for(cl = source->clauses; cl != nil; cl = cl->next){
		env = f.nbind;
		start = f.func->ninsn;
		p = clausetuple(cl);
		if(p == nil){ snprint(c->err,c->nerr,"out_of_memory"); fcompfree(&f); return -1; }
		if(compilepattern(&f, p, 0, 0) < 0){ p->list = nil; exprfree(p); fcompfree(&f); return -1; }
		p->list = nil;
		exprfree(p);
		patend = f.func->ninsn;
		/* A clause body is in tail position; Rnone means it ended in a tail call and needs no return. */
		r = compileexpr(&f, cl->body, 1);
		if(r < 0 && r != Rnone){ fcompfree(&f); return -1; }
		if(r != Rnone && emit(&f, Oreturn, r, 0, 0) < 0){ fcompfree(&f); return -1; }
		end = f.func->ninsn;
		patchtests(f.func, start, patend, end);
		trimenv(&f, env);
	}
	reason = constant(c, Katom, 0, "function_clause");
	if(reason < 0 || emit(&f, Ofail, reason, 0, 0) < 0){ fcompfree(&f); return -1; }
	out->nreg = f.nextreg;
	fcompfree(&f);
	return 0;
}

/*
 * D058: self, mkref, spawn, send, and exit are keywords now, so the
 * lexer keeps them out of function names. Only the call-shaped I/O
 * intrinsics still need a compiler-level reservation.
 */
static int
reserved(char *s)
{
	return strcmp(s,"print") == 0 || strcmp(s,"eprint") == 0;
}

static int
validatemodule(NvModule *m, char *err, int nerr)
{
	int i, j, found;

	for(i = 0; i < m->nfunc; i++)
		if(reserved(m->func[i].name)){
			snprint(err,nerr,"reserved intrinsic %s",m->func[i].name);
			return -1;
		}
	for(i = 0; i < m->nfunc; i++)
		for(j = i+1; j < m->nfunc; j++)
			if(strcmp(m->func[i].name, m->func[j].name) == 0){
				snprint(err, nerr, "duplicate function %s", m->func[i].name);
				return -1;
			}
	for(i = 0; i < m->nconst; i++)
		if(m->konst[i].kind == Kfunc){
			found = 0;
			for(j = 0; j < m->nfunc; j++)
				if(strcmp(m->konst[i].text, m->func[j].name) == 0)
					found++;
			if(found != 1){
				snprint(err, nerr, "undefined function %s", m->konst[i].text);
				return -1;
			}
		}
	if(nvverify(m, err, nerr) < 0)
		return -1;
	return 0;
}

NvModule *
nvcompile(Program *program, char *err, int nerr)
{
	Compiler c;
	NvModule *m;
	NvFunc *p;
	Fn *f;
	int i;

	err[0] = 0;
	m = mallocz(sizeof *m, 1);
	if(m == nil){ snprint(err,nerr,"out_of_memory"); return nil; }
	c.module = m;
	c.err = err;
	c.nerr = nerr;
	for(f = program->fns; f != nil; f = f->next){
		if(m->nfunc == NvMaxitem){ snprint(err,nerr,"function limit exceeded"); nvmodulefree(m); return nil; }
		p = realloc(m->func, (m->nfunc+1)*sizeof *p);
		if(p == nil){ snprint(err,nerr,"out_of_memory"); nvmodulefree(m); return nil; }
		m->func = p;
		memset(&m->func[m->nfunc], 0, sizeof m->func[m->nfunc]);
		m->nfunc++;
		if(compilefunction(&c, f, &m->func[m->nfunc-1]) < 0){ nvmodulefree(m); return nil; }
	}
	for(i = 0; i < m->nfunc; i++)
		if(m->func[i].ninsn == 0){ snprint(err,nerr,"empty compiled function"); nvmodulefree(m); return nil; }
	if(validatemodule(m, err, nerr) < 0){
		nvmodulefree(m);
		return nil;
	}
	return m;
}
