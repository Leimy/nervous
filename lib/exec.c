#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../include/nvbc.h"
#include "../include/nvvm.h"
#include "../include/nvexec.h"

/*
 * D061-D066: registers, constants, and results are NvTerm words now, not
 * NvValue structs. There is no nvvaluefree: storage belongs to e->heap
 * (D063) or to a fragment (D064), and both are freed whole, not per
 * register. D065: frames live in e->stack, a realloc-grown array of
 * words; a frame is NvFramehdr header words (function index, pc, caller
 * frame offset, caller destination register) followed by that
 * function's registers. e->fp is the executing frame's offset, e->sp
 * the words in use, e->nstack the capacity.
 *
 * Load-bearing discipline (D063): e->stack can move (realloc) only when
 * a frame is pushed -- Ocall and the growth Otailcall may need. curfunc/
 * curregs/curpc are therefore recomputed at the top of every dispatch
 * loop iteration rather than cached across iterations, so no stale
 * pointer into e->stack ever survives a call, tailcall, or return. Heap
 * allocations (nvint, nvtuple, nvheapcopy, host calls) do not move the
 * stack, so a register pointer remains valid across those; only e->stack
 * itself must never be assumed stable across a push.
 */

static int
findfuncidx(NvModule *m, char *name)
{
	int i;
	for(i = 0; i < m->nfunc; i++)
		if(strcmp(m->func[i].name, name) == 0)
			return i;
	return -1;
}

static NvFunc *
curfunc(NvExec *e)
{
	return &e->module->func[(int)e->stack[e->fp+NvFramefunc]];
}

static NvTerm *
curregs(NvExec *e)
{
	return e->stack + e->fp + NvFramehdr;
}

static ulong
curpc(NvExec *e)
{
	return (ulong)e->stack[e->fp+NvFramepc];
}

static void
setpc(NvExec *e, ulong pc)
{
	e->stack[e->fp+NvFramepc] = pc;
}

static int
stackcap(ulong current, uvlong need, ulong *out)
{
	ulong cap, max;

	max = (~0UL)/sizeof(NvTerm);
	cap = current ? current : 64;
	while(cap < need){
		if(cap > max/2)
			return -1;
		cap *= 2;
	}
	*out = cap;
	return 0;
}

/* Only the interpreter moves the frame stack; the collector never does. */
static int
growstack(NvExec *e, ulong need)
{
	ulong cap;
	NvTerm *p;

	e->heap.exhausted = 0;
	if(need <= e->nstack)
		return 0;
	if(stackcap(e->nstack, need, &cap) < 0 ||
	   (e->heap.maxwords != 0 && (cap > e->heap.maxwords ||
	    e->heap.words > e->heap.maxwords-cap))){
		e->heap.exhausted = 1;
		return -1;
	}
	p = realloc(e->stack, cap*sizeof(NvTerm));
	if(p == nil)
		return -1;
	e->stack = p;
	e->nstack = cap;
	e->heap.stackwords = cap;
	return 0;
}

/*
 * D065: push a new frame for funcidx on top of the stack, with reg[0]
 * set to arg and every other register NvNil. Sets e->fp/e->sp on
 * success; leaves them untouched on failure (out_of_memory).
 */
static int
pushframe(NvExec *e, int funcidx, NvTerm arg, ulong callerfp, int dst)
{
	NvFunc *f;
	ulong off, nsp;
	NvTerm *r;
	int i;

	f = &e->module->func[funcidx];
	off = e->sp;
	nsp = off + NvFramehdr + f->nreg;
	if(growstack(e, nsp) < 0)
		return -1;
	e->stack[off+NvFramefunc] = (NvTerm)funcidx;
	e->stack[off+NvFramepc] = 0;
	e->stack[off+NvFramecaller] = (NvTerm)callerfp;
	e->stack[off+NvFramedst] = (NvTerm)dst;
	r = e->stack + off + NvFramehdr;
	for(i = 0; i < f->nreg; i++)
		r[i] = NvNil;
	r[0] = arg;
	e->fp = off;
	e->sp = nsp;
	return 0;
}

/*
 * D062/D090: the runtime's own fixed atoms ('true, 'false, 'ok,
 * 'undefined) are interned once by nvexecinternmodule -- before any
 * instruction of any module runs -- and cached here as term words, so
 * every use is a load with no allocation and no table lookup. D090
 * removed the lazy first-use intern that used to live in fixedatom:
 * these statics are shared by every scheduler proc under RFMEM, and a
 * write here from an interpreter path would race. fixedatom is now a
 * pure read; NvNil means the caller skipped nvexecinternmodule, which
 * every nvexecinit path calls, so it is unreachable for a live exec.
 */
static NvTerm cachedtrue, cachedfalse, cachedok, cachedundefined;

static int
fixedatom(NvTerm *cache, NvTerm *out)
{
	if(*cache == NvNil)
		return -1;
	*out = *cache;
	return 0;
}

static int
boolatom(int truth, NvTerm *out)
{
	return fixedatom(truth ? &cachedtrue : &cachedfalse, out);
}

static int
internfixed(NvTerm *cache, char *text)
{
	if(*cache == NvNil)
		*cache = nvatom(text);
	return *cache == NvNil ? -1 : 0;
}

int
nvexecinternmodule(NvModule *m)
{
	int i;

	if(internfixed(&cachedtrue, "true") < 0 || internfixed(&cachedfalse, "false") < 0 ||
	   internfixed(&cachedok, "ok") < 0 || internfixed(&cachedundefined, "undefined") < 0)
		return -1;
	if(m == nil)
		return 0;
	/*
	 * D062: intern every Katom constant once, here, rather than on every
	 * loadk/testatom dispatch. m->konst is shared by every NvExec built
	 * from this module (D042 borrows one module for its whole lifetime),
	 * so after the first call each entry's atom is already set and this
	 * loop is a cheap no-op scan.
	 */
	for(i = 0; i < m->nconst; i++)
		if(m->konst[i].kind == Katom && m->konst[i].atom == NvNil){
			m->konst[i].atom = nvatom(m->konst[i].text);
			if(m->konst[i].atom == NvNil)
				return -1;
		}
	return 0;
}

static int
addok(vlong a, vlong b, vlong *r)
{
	if(b > 0 && a > (vlong)0x7fffffffffffffffLL-b || b < 0 && a < (vlong)0x8000000000000000LL-b) return -1;
	*r = a+b; return 0;
}

static int
subok(vlong a, vlong b, vlong *r)
{
	if(b < 0 && a > (vlong)0x7fffffffffffffffLL+b || b > 0 && a < (vlong)0x8000000000000000LL+b) return -1;
	*r = a-b; return 0;
}

static int
mulok(vlong a, vlong b, vlong *r)
{
	vlong min, max;
	min = (vlong)0x8000000000000000LL;
	max = (vlong)0x7fffffffffffffffLL;
	if(a == 0 || b == 0){ *r = 0; return 0; }
	if(a == -1){ if(b == min) return -1; *r = -b; return 0; }
	if(b == -1){ if(a == min) return -1; *r = -a; return 0; }
	if(a > 0){
		if(b > 0){ if(a > max/b) return -1; }
		else if(b < min/a) return -1;
	}else{
		if(b > 0){ if(a < min/b) return -1; }
		else if(a < max/b) return -1;
	}
	*r = a*b; return 0;
}

/*
 * D060: while a guard executes (guardfail >= 0), a fault is clause failure,
 * not process failure: control transfers to the guard's fail target and
 * guard mode ends. guardfail lives on NvExec, not per-frame, because
 * guards cannot call and so cannot nest (D060). NvGuardfault is private to
 * this file; run() returns it and nvexecrun resumes the loop instead of
 * reporting it.
 */
enum {
	NvGuardfault = 100,
};

static int
fault(NvExec *e, char *s)
{
	if(e->guardfail >= 0){
		setpc(e, e->guardfail);
		e->guardfail = -1;
		return NvGuardfault;
	}
	snprint(e->fault, sizeof e->fault, "%s", s);
	e->state = NvFault;
	return NvFault;
}

int
nvexecinit(NvExec *e, NvModule *m, char *entry, NvTerm arg, uvlong maxheap, Biobuf *trace, int traceon, char *err, int nerr)
{
	return nvexecinitw(e, m, entry, arg, maxheap, trace, traceon, nil, err, nerr);
}

int
nvexecinitw(NvExec *e, NvModule *m, char *entry, NvTerm arg, uvlong maxheap, Biobuf *trace, int traceon, NvWork *w, char *err, int nerr)
{
	int idx, rc;
	NvTerm copied;

	memset(e, 0, sizeof *e);
	/* D062/D090: every atom this module can name, and the fixed four. */
	if(nvexecinternmodule(m) < 0){ snprint(err,nerr,"out_of_memory"); return -1; }
	idx = findfuncidx(m, entry);
	if(idx < 0){ snprint(err,nerr,"bad_function"); return -1; }
	e->module = m;
	nvheapinit(&e->heap, maxheap);
	/*
	 * NvTermerror (nvheapcopy's "malformed input: NvNil") and a genuine
	 * allocation failure share the code -1, so the only way to report
	 * them as the distinct reasons "bad_argument" vs "out_of_memory" is
	 * to rule the NvNil case out ourselves before calling nvheapcopy.
	 */
	if(arg == NvNil){
		snprint(err,nerr,"bad_argument");
		nvheapfree(&e->heap);
		return -1;
	}
	/* Charge the initial retained stack before copying the argument. */
	if(pushframe(e, idx, NvNil, NvNoframe, -1) < 0){
		snprint(err,nerr, e->heap.exhausted ? "system_limit" : "out_of_memory");
		nvexecfree(e);
		return -1;
	}
	/* D080: the spawner, if any, pays for and bounds this copy. */
	rc = nvheapcopyw(&e->heap, arg, w, &copied);
	if(rc < 0){
		snprint(err,nerr, rc == NvTermlimit || e->heap.exhausted ? "system_limit" : "out_of_memory");
		nvexecfree(e);
		return -1;
	}
	curregs(e)[0] = copied;
	e->trace = trace;
	e->traceon = traceon;
	e->nframe = 1;
	e->maxframe = 1024;
	e->state = NvYield;
	e->guardfail = -1;
	e->binsubj = -1;
	/* Initial copying used non-moving chunks; compact once before execution. */
	rc = nvexeccollect(e, 0);
	if(rc < 0){
		snprint(err,nerr, rc == NvTermlimit ? "system_limit" : "out_of_memory");
		nvexecfree(e);
		return -1;
	}
	e->heap.managed = 1;
	e->collections = 0;
	return 0;
}

/* Host callbacks keep process and mailbox representation out of bytecode. */
int
nvexecsetframelimit(NvExec *e, ulong maxframe)
{
	if(e == nil || maxframe == 0 || e->nframe > maxframe)
		return -1;
	e->maxframe = maxframe;
	return 0;
}

void
nvexecsethost(NvExec *e, NvExecHost *host)
{
	e->host = host;
}

void
nvexecsetworklimit(NvExec *e, uvlong max)
{
	if(e != nil)
		e->work.max = max;
}

/* Pure arithmetic preflight: only a full-range result needs heap words. */
static char *
arithresult(NvExec *e, NvInsn *in, vlong *out)
{
	NvTerm *r;
	vlong a, b;

	r = curregs(e);
	if(nvtermkind(r[in->b]) != Vint || nvtermkind(r[in->c]) != Vint)
		return "badarith";
	a = nvtermint(r[in->b]); b = nvtermint(r[in->c]);
	switch(in->op){
	case Oadd: if(addok(a,b,out) < 0) return "overflow"; break;
	case Osub: if(subok(a,b,out) < 0) return "overflow"; break;
	case Omul: if(mulok(a,b,out) < 0) return "overflow"; break;
	case Odiv: case Orem:
		if(b == 0) return "divide_by_zero";
		if(a == (vlong)0x8000000000000000ULL && b == -1){
			if(in->op == Odiv) return "overflow";
			*out = 0;
		}else
			*out = in->op == Odiv ? a/b : a%b;
		break;
	}
	return nil;
}

/*
 * D076: the subject of the binary match in progress. binsubj is a
 * register index recorded by bintestbinary; a verified module cannot
 * prove that every bin*get follows one in the same frame, so check the
 * index and the term kind on every use rather than trusting it.
 */
static int
binsubject(NvExec *e, NvTerm *out)
{
	if(e->binsubj < 0 || e->binsubj >= curfunc(e)->nreg)
		return -1;
	*out = curregs(e)[e->binsubj];
	if(nvtermkind(*out) != Vbin || e->binpos > nvbinlen(*out))
		return -1;
	return 0;
}

/*
 * D079 sizing reservation for the append opcodes. The result size is
 * known from the operand registers at instruction start, so it is
 * reserved exactly like a fixed-size instruction (D067). Invalid
 * operands reserve nothing; run() reports their fault.
 */
static char *
binappneed(NvExec *e, NvInsn *in, uvlong *space)
{
	NvTerm *r;
	uvlong clen, add;
	vlong sz;

	r = curregs(e);
	*space = 0;
	if(nvtermkind(r[in->b]) != Vbin)
		return nil;
	clen = nvbinlen(r[in->b]);
	switch(in->op){
	case Obinappint:
		add = in->d>>2;
		break;
	case Obinappbin:
		if(nvtermkind(r[in->c]) != Vbin || nvtermkind(r[in->d]) != Vint)
			return nil;
		sz = nvtermint(r[in->d]);
		if(sz < 0 || (uvlong)sz > nvbinlen(r[in->c]))
			return nil;
		add = sz;
		break;
	default:	/* Obinappend */
		if(nvtermkind(r[in->c]) != Vbin)
			return nil;
		add = nvbinlen(r[in->c]);
		break;
	}
	if(clen > ~0ULL-add || (*space = nvbinwords(clen+add)) == 0)
		return "system_limit";
	return nil;
}

/*
 * D067: no writes, side effects or reduction charge precede reservation.
 * space is bump-space demand; charge also covers adoption/stack growth.
 * Adopted words participate in space pressure even when maxheap is zero.
 * A successful collection permits one retry, not endless stress requests.
 */
static int
prepare(NvExec *e, NvInsn *in, char *err, int nerr)
{
	NvHeap *h;
	NvConst *k;
	NvTerm t, *r;
	uvlong space, charge, newsp, limit;
	ulong cap;
	vlong v;
	char *why;
	int idx, active, retry, fits;

	h = &e->heap;
	retry = e->gcretry;
	e->gcretry = 0;
	if(retry >= 2){
		snprint(err,nerr, retry == 2 ? "system_limit" : "out_of_memory");
		return -1;
	}
	space = charge = 0;
	active = 0;
	why = nil;
	switch(in->op){
	case Otuple:
		space = 1+in->c;
		break;
	case Oloadk:
		k = &e->module->konst[in->b];
		if(k->kind == Kint && (k->ival < NvMinsmall || k->ival > NvMaxsmall))
			space = 2;
		break;
	case Oadd: case Osub: case Omul: case Odiv: case Orem:
		why = arithresult(e, in, &v);
		if(why == nil && (v < NvMinsmall || v > NvMaxsmall))
			space = 2;
		break;
	case Omakeref:
		if(e->host == nil || e->host->makeref == nil)
			why = "bad_process_context";
		else
			space = 3;
		break;
	/* D079: binary sizes come from operand registers, read here. */
	case Obinalloc:
		space = 2;
		break;
	case Obinappint: case Obinappbin: case Obinappend:
		why = binappneed(e, in, &space);
		break;
	case Obinintget:
		space = 2;	/* a decoded value may need a boxed Bint */
		break;
	case Obinbinget:
		r = curregs(e);
		if(binsubject(e, &t) == 0 && nvtermkind(r[in->b]) == Vint){
			v = nvtermint(r[in->b]);
			if(v >= 0 && (uvlong)v <= nvbinlen(t)-e->binpos &&
			   (space = nvbinwords((uvlong)v)) == 0)
				why = "system_limit";
		}
		break;
	case Obinremget:
		if(binsubject(e, &t) == 0 &&
		   (space = nvbinwords(nvbinlen(t)-e->binpos)) == 0)
			why = "system_limit";
		break;
	case Ocall: case Otailcall:
		active = 1;
		k = &e->module->konst[in->op == Ocall ? in->b : in->a];
		idx = findfuncidx(e->module, k->text);
		if(idx < 0){ why = "bad_function"; break; }
		if(in->op == Ocall && e->nframe >= e->maxframe){ why = "system_limit"; break; }
		newsp = (uvlong)(in->op == Ocall ? e->sp : e->fp) + NvFramehdr + e->module->func[idx].nreg;
		if(stackcap(e->nstack, newsp, &cap) < 0){ why = "system_limit"; break; }
		charge = cap-e->nstack;
		break;
	case Orecvtake:
		active = 1;
		if(e->host == nil || e->host->recvtake == nil || e->host->recvneed == nil){
			why = "bad_process_context";
			break;
		}
		if(e->host->recvneed(e, &charge, err, nerr) < 0){
			if(err[0] == 0) snprint(err,nerr,"bad_state");
			return -1;
		}
		break;
	}
	if(why != nil){ snprint(err,nerr,"%s",why); return -1; }
	if(space != 0){ charge = space; active = 1; }
	if(!active)
		return 0;
	limit = h->maxwords == 0 ? ~0ULL : h->maxwords;
	fits = e->nstack <= limit && h->words <= limit-e->nstack && charge <= limit-e->nstack-h->words;
	fits = fits && h->cur != nil && space <= h->cur->cap-h->cur->top &&
		h->words <= h->cur->cap && charge <= h->cur->cap-h->words;
	if(fits && (!e->gcstress || retry == 1))
		return 0;
	if(retry == 1){
		snprint(err,nerr,"system_limit");
		return -1;
	}
	e->gcneed = charge;
	e->gcpending = 1;
	return 1;
}

static int
run(NvExec *e, uvlong quantum)
{
	NvFunc *func, *target;
	NvTerm *regs, *r;
	NvInsn *insn;
	NvConst *k;
	NvTerm v, arg, bsubj;
	NvFrag *frag;
	char hosterr[128];
	char scratch[8];
	uvlong used, blen;
	vlong left, right, ir, bval, bsizev;
	uchar *bbytes;
	ulong pc, callerfp, newsp;
	int j, truth, eq, rc, found, targetidx, dstreg, bw, bflags;

	if(e->state != NvYield) return e->state;
	if(e->gcpending) return NvCollect;
	if(quantum == 0) return NvYield;
	used = 0;
	while(used < quantum){
		func = curfunc(e);
		regs = curregs(e);
		pc = curpc(e);
		insn = &func->insn[pc];
		hosterr[0] = 0;
		rc = prepare(e, insn, hosterr, sizeof hosterr);
		if(rc > 0)
			return NvCollect;
		if(e->traceon && e->trace != nil)
			Bprint(e->trace, "%llud %s:%d %s\n", e->reductions, func->name, (int)pc, nvopname(insn->op));
		used++; e->reductions++;
		if(rc < 0)
			return fault(e, hosterr);
		switch(insn->op){
		case Oloadk:
			k = &e->module->konst[insn->b];
			if(k->kind == Kint){
				v = nvint(&e->heap, k->ival);
				if(v == NvNil) return fault(e, nvheapexhausted(&e->heap) ? "system_limit" : "out_of_memory");
			}else if(k->kind == Katom){
				/* D062/D090: interned by nvexecinternmodule before any run;
				 * an interpreter path never writes the shared table. */
				v = (NvTerm)k->atom;
				if(v == NvNil) return fault(e,"bad_constant");
			}else{
				return fault(e,"bad_constant");
			}
			regs[insn->a] = v; setpc(e, pc+1); break;
		case Omove:
			regs[insn->a] = regs[insn->b]; setpc(e, pc+1); break;
		case Otuple:
			/* D061: no depth check here any more -- copy/equal/print own it. */
			v = nvtuple(&e->heap, regs+insn->b, insn->c);
			if(v == NvNil) return fault(e, nvheapexhausted(&e->heap) ? "system_limit" : "out_of_memory");
			regs[insn->a] = v; setpc(e, pc+1); break;
		case Ojump:
			setpc(e, insn->a); break;
		case Ocall:
			k = &e->module->konst[insn->b];
			targetidx = findfuncidx(e->module, k->text);
			if(targetidx < 0) return fault(e,"bad_function");
			if(e->nframe >= e->maxframe) return fault(e,"system_limit");
			arg = regs[insn->c];
			callerfp = e->fp;
			if(pushframe(e, targetidx, arg, callerfp, insn->a) < 0) return fault(e, e->heap.exhausted ? "system_limit" : "out_of_memory");
			e->stack[callerfp+NvFramepc] = pc+1;
			e->nframe++;
			break;
		case Otailcall:
			/* D065/D047: overwrite the current frame in place; nframe unchanged. */
			k = &e->module->konst[insn->a];
			targetidx = findfuncidx(e->module, k->text);
			if(targetidx < 0) return fault(e,"bad_function");
			arg = regs[insn->b];
			target = &e->module->func[targetidx];
			newsp = e->fp + NvFramehdr + target->nreg;
			if(growstack(e, newsp) < 0) return fault(e,"out_of_memory");
			e->stack[e->fp+NvFramefunc] = (NvTerm)targetidx;
			e->stack[e->fp+NvFramepc] = 0;
			e->sp = newsp;
			r = e->stack + e->fp + NvFramehdr;
			for(j = 0; j < target->nreg; j++)
				r[j] = NvNil;
			r[0] = arg;
			break;
		case Oreturn:
			callerfp = (ulong)e->stack[e->fp+NvFramecaller];
			if(callerfp == NvNoframe){
				rc = nvfragcopyw(regs[insn->a], ~0ULL, &e->work, &e->result);
				if(rc == NvTermlimit) return fault(e,"system_limit");
				if(rc < 0) return fault(e,"out_of_memory");
				e->state = NvDone;
				e->fp = NvNoframe;
				e->sp = 0;
				return NvDone;
			}
			dstreg = (int)e->stack[e->fp+NvFramedst];
			e->stack[callerfp+NvFramehdr+dstreg] = regs[insn->a];
			e->sp = e->fp;
			e->fp = callerfp;
			e->nframe--;
			break;
		case Otestatom:
			k = &e->module->konst[insn->b];
			if(k->atom == NvNil) return fault(e,"bad_constant");	/* D090: see Oloadk */
			setpc(e, regs[insn->a] == (NvTerm)k->atom ? pc+1 : insn->c); break;
		case Otestint:
			k = &e->module->konst[insn->b];
			setpc(e, nvtermkind(regs[insn->a]) == Vint && nvtermint(regs[insn->a]) == k->ival ? pc+1 : insn->c); break;
		case Otesteq:
			/* D080: visits are charged below; over the ceiling is system_limit. */
			eq = nvtermequalw(regs[insn->a], regs[insn->b], &e->work);
			if(eq < 0) return fault(e,"system_limit");
			setpc(e, eq ? pc+1 : insn->c); break;
		case Otestarity:
			setpc(e, nvtermkind(regs[insn->a]) == Vtuple && nvtuplelen(regs[insn->a]) == insn->b ? pc+1 : insn->c); break;
		case Ogetelem:
			if(nvtermkind(regs[insn->b]) != Vtuple) return fault(e,"bad_tuple");
			if(insn->c >= nvtuplelen(regs[insn->b])) return fault(e,"bad_element");
			regs[insn->a] = nvtupleelem(regs[insn->b], insn->c);
			setpc(e, pc+1); break;
		case Oadd: case Osub: case Omul: case Odiv: case Orem:
		case Olt: case Ole: case Ogt: case Oge:
			if(nvtermkind(regs[insn->b]) != Vint || nvtermkind(regs[insn->c]) != Vint) return fault(e,"badarith");
			left = nvtermint(regs[insn->b]); right = nvtermint(regs[insn->c]);
			if(insn->op == Oadd){
				if(addok(left,right,&ir) < 0) return fault(e,"overflow");
				v = nvint(&e->heap, ir);
				if(v == NvNil) return fault(e, nvheapexhausted(&e->heap) ? "system_limit" : "out_of_memory");
				regs[insn->a] = v;
			}else if(insn->op == Osub){
				if(subok(left,right,&ir) < 0) return fault(e,"overflow");
				v = nvint(&e->heap, ir);
				if(v == NvNil) return fault(e, nvheapexhausted(&e->heap) ? "system_limit" : "out_of_memory");
				regs[insn->a] = v;
			}else if(insn->op == Omul){
				if(mulok(left,right,&ir) < 0) return fault(e,"overflow");
				v = nvint(&e->heap, ir);
				if(v == NvNil) return fault(e, nvheapexhausted(&e->heap) ? "system_limit" : "out_of_memory");
				regs[insn->a] = v;
			}else if(insn->op == Odiv || insn->op == Orem){
				if(right == 0) return fault(e,"divide_by_zero");
				if(insn->op == Odiv && left == (vlong)0x8000000000000000LL && right == -1) return fault(e,"overflow");
				ir = insn->op == Odiv ? left/right : (left == (vlong)0x8000000000000000LL && right == -1) ? 0 : left%right;
				v = nvint(&e->heap, ir);
				if(v == NvNil) return fault(e, nvheapexhausted(&e->heap) ? "system_limit" : "out_of_memory");
				regs[insn->a] = v;
			}else{
				truth = insn->op == Olt ? left < right : insn->op == Ole ? left <= right : insn->op == Ogt ? left > right : left >= right;
				if(boolatom(truth, &v) < 0) return fault(e,"bad_constant");
				regs[insn->a] = v;
			}
			setpc(e, pc+1); break;
		case Ofail:
			k = &e->module->konst[insn->a];
			return fault(e, k->kind == Katom ? k->text : "explicit_fail");
		case Oself:
			if(e->host == nil || e->host->self == nil) return fault(e,"bad_process_context");
			hosterr[0] = 0;
			if(e->host->self(e, &v, hosterr, sizeof hosterr) < 0) return fault(e, hosterr[0] ? hosterr : "system_limit");
			regs[insn->a] = v; setpc(e, pc+1); break;
		case Omakeref:
			if(e->host == nil || e->host->makeref == nil) return fault(e,"bad_process_context");
			hosterr[0] = 0;
			if(e->host->makeref(e, &v, hosterr, sizeof hosterr) < 0) return fault(e, hosterr[0] ? hosterr : "system_limit");
			regs[insn->a] = v; setpc(e, pc+1); break;
		case Osend:
			/* D061: reg[a] is a word copy of reg[c]; no value is copied on this side. */
			if(e->host == nil || e->host->send == nil) return fault(e,"bad_process_context");
			hosterr[0] = 0;
			if(e->host->send(e, regs[insn->b], regs[insn->c], hosterr, sizeof hosterr) < 0) return fault(e, hosterr[0] ? hosterr : "system_limit");
			regs[insn->a] = regs[insn->c]; setpc(e, pc+1); break;
		case Ospawn:
			if(e->host == nil || e->host->spawn == nil) return fault(e,"bad_process_context");
			k = &e->module->konst[insn->b]; hosterr[0] = 0;
			if(e->host->spawn(e, k->text, regs[insn->c], &v, hosterr, sizeof hosterr) < 0) return fault(e, hosterr[0] ? hosterr : "system_limit");
			regs[insn->a] = v; setpc(e, pc+1); break;
		case Orecvbegin: case Orecvnext:
			if(e->host == nil || (insn->op == Orecvbegin ? e->host->recvbegin == nil : e->host->recvnext == nil))
				return fault(e,"bad_process_context");
			hosterr[0] = 0;
			found = insn->op == Orecvbegin ? e->host->recvbegin(e, &v, hosterr, sizeof hosterr)
			                               : e->host->recvnext(e, &v, hosterr, sizeof hosterr);
			if(found < 0) return fault(e, hosterr[0] ? hosterr : "system_limit");
			if(found == 0){
				if(fixedatom(&cachedundefined, &v) < 0) return fault(e,"bad_constant");
			}
			regs[insn->a] = v;
			if(boolatom(found != 0, &v) < 0) return fault(e,"bad_constant");
			regs[insn->b] = v;
			setpc(e, pc+1); break;
		case Orecvtake:
			if(e->host == nil || e->host->recvtake == nil) return fault(e,"bad_process_context");
			hosterr[0] = 0; frag = nil;
			if(e->host->recvtake(e, &frag, hosterr, sizeof hosterr) < 0) return fault(e, hosterr[0] ? hosterr : "bad_state");
			if(frag != nil && nvheapadopt(&e->heap, frag) < 0){
				nvfragfree(frag);
				return fault(e,"system_limit");
			}
			setpc(e, pc+1); break;
		case Orecvwait:
			if(e->host == nil || e->host->recvwait == nil) return fault(e,"bad_process_context");
			hosterr[0] = 0;
			if(e->host->recvwait(e, hosterr, sizeof hosterr) < 0) return fault(e, hosterr[0] ? hosterr : "bad_state");
			setpc(e, insn->a); return NvYield;
		case Orecvdeadline:
			if(e->host == nil || e->host->recvdeadline == nil) return fault(e,"bad_process_context");
			hosterr[0] = 0;
			if(e->host->recvdeadline(e, regs[insn->a], hosterr, sizeof hosterr) < 0) return fault(e, hosterr[0] ? hosterr : "bad_timeout");
			setpc(e, pc+1); break;
		case Orecvwaitdeadline:
			if(e->host == nil || e->host->recvwaitdeadline == nil) return fault(e,"bad_process_context");
			hosterr[0] = 0;
			found = e->host->recvwaitdeadline(e, hosterr, sizeof hosterr);
			if(found < 0) return fault(e, hosterr[0] ? hosterr : "bad_state");
			if(found == 0){ setpc(e, insn->a); return NvYield; }
			setpc(e, pc+1); break;
		case Oexit:
			rc = nvfragcopyw(regs[insn->a], ~0ULL, &e->work, &e->exitreason);
			if(rc == NvTermlimit) return fault(e,"system_limit");
			if(rc < 0) return fault(e,"out_of_memory");
			e->state = NvExit; return NvExit;
		case Oprint: case Oeprint:
			if(e->host == nil || (insn->op == Oprint ? e->host->print == nil : e->host->eprint == nil))
				return fault(e,"bad_process_context");
			hosterr[0] = 0;
			found = insn->op == Oprint ? e->host->print(e, regs[insn->b], hosterr, sizeof hosterr)
			                           : e->host->eprint(e, regs[insn->b], hosterr, sizeof hosterr);
			if(found < 0) return fault(e, hosterr[0] ? hosterr : "io_error");
			if(fixedatom(&cachedok, &v) < 0) return fault(e,"bad_constant");
			regs[insn->a] = v; setpc(e, pc+1); break;
		case Onop:
			setpc(e, pc+1); break;
		case Oguard:
			e->guardfail = insn->a; setpc(e, pc+1); break;
		case Oguardend:
			e->guardfail = -1; setpc(e, pc+1); break;
		case Oistype:
			if(boolatom(nvtermkind(regs[insn->b]) == insn->c, &v) < 0) return fault(e,"bad_constant");
			regs[insn->a] = v; setpc(e, pc+1); break;
		case Obinalloc:
			/* D077: the running binary starts empty. */
			v = nvbin(&e->heap, nil, 0);
			if(v == NvNil) return fault(e, nvheapexhausted(&e->heap) ? "system_limit" : "out_of_memory");
			regs[insn->a] = v; setpc(e, pc+1); break;
		case Obinappint:
			/* D079: append width bytes encoding regs[c]. A non-integer
			 * value is badarith; one outside the width's range is overflow. */
			bw = insn->d>>2; bflags = insn->d&3;
			if(nvtermkind(regs[insn->b]) != Vbin) return fault(e,"bad_binary");
			if(nvtermkind(regs[insn->c]) != Vint) return fault(e,"badarith");
			bval = nvtermint(regs[insn->c]);
			if(!nvbinfits(bval, bw, bflags)) return fault(e,"overflow");
			nvbinenc(bval, bw, bflags, (uchar*)scratch);
			v = nvbinapp(&e->heap, regs[insn->b], scratch, (uvlong)bw);
			if(v == NvNil) return fault(e, nvheapexhausted(&e->heap) ? "system_limit" : "out_of_memory");
			regs[insn->a] = v; setpc(e, pc+1); break;
		case Obinappbin:
			/* D079: append regs[d] bytes of regs[c]. A non-integer size is
			 * badarith; a non-binary operand, a negative size, or more bytes
			 * than regs[c] holds is bad_binary. */
			if(nvtermkind(regs[insn->b]) != Vbin || nvtermkind(regs[insn->c]) != Vbin) return fault(e,"bad_binary");
			if(nvtermkind(regs[insn->d]) != Vint) return fault(e,"badarith");
			blen = nvbinlen(regs[insn->c]);
			bsizev = nvtermint(regs[insn->d]);
			if(bsizev < 0 || (uvlong)bsizev > blen) return fault(e,"bad_binary");
			v = nvbinapp(&e->heap, regs[insn->b], nvbinbytes(regs[insn->c]), (uvlong)bsizev);
			if(v == NvNil) return fault(e, nvheapexhausted(&e->heap) ? "system_limit" : "out_of_memory");
			regs[insn->a] = v; setpc(e, pc+1); break;
		case Obinappend:
			/* D079: append all of regs[c]'s bytes. */
			if(nvtermkind(regs[insn->b]) != Vbin || nvtermkind(regs[insn->c]) != Vbin) return fault(e,"bad_binary");
			v = nvbinapp(&e->heap, regs[insn->b], nvbinbytes(regs[insn->c]), nvbinlen(regs[insn->c]));
			if(v == NvNil) return fault(e, nvheapexhausted(&e->heap) ? "system_limit" : "out_of_memory");
			regs[insn->a] = v; setpc(e, pc+1); break;
		case Obintestbinary:
			/* D077: begin a binary pattern match; a non-binary subject is a
			 * clause failure, not a fault. */
			if(nvtermkind(regs[insn->a]) != Vbin){
				setpc(e, insn->b);
				break;
			}
			e->binsubj = insn->a;
			e->binpos = 0;
			setpc(e, pc+1);
			break;
		case Obinintget:
			/* D077: decode width bytes at e->binpos; short subject is a
			 * mismatch. The subject register is re-read (it may have moved);
			 * binsubject faults bytecode that reached here without a valid
			 * bintestbinary. */
			bw = insn->c>>2; bflags = insn->c&3;
			if(binsubject(e, &bsubj) < 0) return fault(e,"bad_binary");
			bbytes = nvbinbytes(bsubj);
			blen = nvbinlen(bsubj);
			if((uvlong)bw > blen - e->binpos){
				setpc(e, insn->b);
				break;
			}
			nvbindec(bbytes+e->binpos, bw, bflags, &bval);
			v = nvint(&e->heap, bval);
			if(v == NvNil) return fault(e, nvheapexhausted(&e->heap) ? "system_limit" : "out_of_memory");
			e->binpos += (uvlong)bw;
			regs[insn->a] = v; setpc(e, pc+1); break;
		case Obinbinget:
			/* D077: extract regs[b] bytes into a fresh binary; a non-integer,
			 * negative or too-large size is a mismatch (data, not code). */
			if(binsubject(e, &bsubj) < 0) return fault(e,"bad_binary");
			if(nvtermkind(regs[insn->b]) != Vint){
				setpc(e, insn->c);
				break;
			}
			bbytes = nvbinbytes(bsubj);
			blen = nvbinlen(bsubj);
			bsizev = nvtermint(regs[insn->b]);
			if(bsizev < 0 || (uvlong)bsizev > blen - e->binpos){
				setpc(e, insn->c);
				break;
			}
			v = nvbin(&e->heap, bbytes+e->binpos, (uvlong)bsizev);
			if(v == NvNil) return fault(e, nvheapexhausted(&e->heap) ? "system_limit" : "out_of_memory");
			e->binpos += (uvlong)bsizev;
			regs[insn->a] = v; setpc(e, pc+1); break;
		case Obinremget:
			/* D077: extract all remaining bytes (possibly zero); always
			 * succeeds once the subject is a binary. */
			if(binsubject(e, &bsubj) < 0) return fault(e,"bad_binary");
			bbytes = nvbinbytes(bsubj);
			blen = nvbinlen(bsubj);
			v = nvbin(&e->heap, bbytes+e->binpos, blen - e->binpos);
			if(v == NvNil) return fault(e, nvheapexhausted(&e->heap) ? "system_limit" : "out_of_memory");
			e->binpos = blen;
			regs[insn->a] = v; setpc(e, pc+1); break;
		case Obinend:
			/* D077: a pattern with no final remainder must consume the
			 * whole value; a trailing byte is a mismatch. */
			if(binsubject(e, &bsubj) < 0) return fault(e,"bad_binary");
			if(nvbinlen(bsubj) != e->binpos){
				setpc(e, insn->a);
				break;
			}
			setpc(e, pc+1);
			break;
		default:
			/*
			 * Unreachable for a verified module (nvverify rejects unknown
			 * opcodes), so this only fires if an opcode is added to nvbc.h
			 * without a case here. Faulting makes that a visible test
			 * failure instead of a silent spin on an unadvanced pc.
			 */
			return fault(e,"bad_opcode");
		}
		/*
		 * D080: charge the traversal work this instruction did (its own
		 * equality, or a print/spawn callback's copy or print) as whole
		 * reductions, keeping the remainder for the next instruction.
		 * This is what lets one expensive instruction end its quantum:
		 * `used` may now exceed `quantum`, and the loop condition sees
		 * it. Instructions that return above (yield, fault, done, exit)
		 * skip this; none of them charged work except the terminal
		 * return/exit copies, whose process is finished either way.
		 */
		if(e->work.count >= NvWorkunit){
			used += e->work.count/NvWorkunit;
			e->reductions += e->work.count/NvWorkunit;
			e->work.count %= NvWorkunit;
		}
	}
	return NvYield;
}

/*
 * D060: run() returns NvGuardfault when a fault inside a guard has already
 * redirected the frame to the guard's fail target; resume with whatever
 * quantum remains rather than reporting it. The reduction that faulted was
 * charged (run() counts before dispatch), so a guard that faults every
 * time still consumes its quantum.
 */
int
nvexecrun(NvExec *e, uvlong quantum)
{
	uvlong start, used;
	int state;

	start = e->reductions;
	for(;;){
		used = e->reductions - start;
		if(used >= quantum)
			return e->state;
		state = run(e, quantum - used);
		if(state != NvGuardfault)
			return state;
	}
}

/*
 * Build a view of the complete D065 register root set before invoking
 * the collector. It sees neither NvExec nor module/frame metadata.
 * Capacity, rather than just sp, is charged: popped stack storage stays
 * allocated. Inactive slots are not roots. Up to eight frame views use
 * bounded C-stack scratch; deeper stacks malloc the views. The owner
 * must be stopped.
 */
int
nvexeccollect(NvExec *e, uvlong need)
{
	NvRoot *roots, local[8];
	NvTerm idx, caller;
	ulong fp, end, n, i;
	int rc;

	if(e == nil)
		return NvTermerror;
	e->heap.exhausted = 0;
	if(e->state != NvYield || e->module == nil ||
	   e->stack == nil || e->nframe == 0 || e->sp > e->nstack ||
	   e->nframe > (~0UL)/sizeof(NvRoot))
		return NvTermerror;
	roots = local;
	if(e->nframe > nelem(local)){
		roots = malloc(e->nframe*sizeof(NvRoot));
		if(roots == nil)
			return NvTermerror;
	}
	fp = e->fp;
	end = e->sp;
	rc = NvTermerror;
	for(i = 0; i < e->nframe; i++){
		if(fp > end || end-fp < NvFramehdr)
			goto out;
		idx = e->stack[fp+NvFramefunc];
		if(idx >= e->module->nfunc)
			goto out;
		n = e->module->func[idx].nreg;
		if(n != end-fp-NvFramehdr)
			goto out;
		roots[i].word = e->stack+fp+NvFramehdr;
		roots[i].nword = n;
		roots[i].next = i+1 < e->nframe ? &roots[i+1] : nil;
		caller = e->stack[fp+NvFramecaller];
		if(i+1 == e->nframe){
			if(fp != 0 || caller != NvNoframe)
				goto out;
		}else if(caller >= fp)
			goto out;
		end = fp;
		fp = caller;
	}
	rc = nvheapcollect(&e->heap, roots, e->nstack, need);
	if(rc == 0){
		e->collections++;
		e->livewords = e->heap.words;
	}
out:
	if(roots != local)
		free(roots);
	return rc;
}

void
nvexecgc(NvExec *e)
{
	int rc;

	if(!e->gcpending)
		return;
	rc = nvexeccollect(e, e->gcneed);
	e->gcpending = 0;
	e->gcretry = rc == 0 ? 1 : rc == NvTermlimit ? 2 : 3;
}

int
nvexecruninline(NvExec *e, uvlong quantum)
{
	uvlong start, used;
	int state;

	start = e->reductions;
	for(;;){
		used = e->reductions-start;
		if(used >= quantum)
			return e->state;
		state = nvexecrun(e, quantum-used);
		if(state != NvCollect)
			return state;
		nvexecgc(e);
	}
}

void
nvexecfree(NvExec *e)
{
	if(e == nil) return;
	free(e->stack);
	nvheapfree(&e->heap);
	if(e->result != nil) nvfragfree(e->result);
	if(e->exitreason != nil) nvfragfree(e->exitreason);
	memset(e,0,sizeof *e);
}
