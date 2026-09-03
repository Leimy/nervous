#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../include/nvbc.h"
#include "../include/nvvm.h"
#include "../include/nvexec.h"

static NvFunc *
findfunc(NvModule *m, char *name)
{
	int i;
	for(i = 0; i < m->nfunc; i++)
		if(strcmp(m->func[i].name, name) == 0)
			return &m->func[i];
	return nil;
}

static NvExecFrame *
framealloc(NvFunc *f, NvValue *arg, NvExecFrame *caller, int dst)
{
	NvExecFrame *p;
	p = mallocz(sizeof *p, 1);
	if(p == nil) return nil;
	p->reg = mallocz(f->nreg*sizeof *p->reg, 1);
	if(p->reg == nil){ free(p); return nil; }
	p->func = f;
	p->caller = caller;
	p->dst = dst;
	if(nvvaluecopy(&p->reg[0], arg) < 0){ free(p->reg); free(p); return nil; }
	return p;
}

static void
framefree(NvExecFrame *f)
{
	int i;
	if(f == nil) return;
	for(i = 0; i < f->func->nreg; i++) nvvaluefree(&f->reg[i]);
	free(f->reg);
	free(f);
}

static int
putvalue(NvValue *d, NvValue *s)
{
	NvValue v;
	if(!s->valid || nvvaluecopy(&v, s) < 0) return -1;
	nvvaluefree(d);
	*d = v;
	return 0;
}

static int
loadconst(NvValue *v, NvConst *k)
{
	if(k->kind == Kint){
		memset(v, 0, sizeof *v);
		v->valid = 1; v->kind = Vint; v->i = k->ival;
		return 0;
	}
	if(k->kind == Katom) return nvvalueatom(v, k->text);
	return -1;
}

static int
intresult(NvValue *d, vlong n)
{
	NvValue v;
	memset(&v, 0, sizeof v);
	v.valid = 1; v.kind = Vint; v.i = n;
	nvvaluefree(d); *d = v;
	return 0;
}

static int
atomresult(NvValue *d, int truth)
{
	NvValue v;
	if(nvvalueatom(&v, truth ? "true" : "false") < 0) return -1;
	nvvaluefree(d); *d = v;
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
 * guard mode ends. e->frame is always the executing frame at a fault site
 * (every frame change stores it), and guards cannot call, so the target is
 * in this frame. NvGuardfault is private to this file; run() returns it
 * and nvexecrun resumes the loop instead of reporting it.
 */
enum {
	NvGuardfault = 100,
};

static int
fault(NvExec *e, char *s)
{
	if(e->guardfail >= 0){
		e->frame->pc = e->guardfail;
		e->guardfail = -1;
		return NvGuardfault;
	}
	snprint(e->fault, sizeof e->fault, "%s", s);
	e->state = NvFault;
	return NvFault;
}

int
nvexecinit(NvExec *e, NvModule *m, char *entry, NvValue *arg, Biobuf *trace, int traceon, char *err, int nerr)
{
	NvFunc *f;
	memset(e, 0, sizeof *e);
	f = findfunc(m, entry);
	if(f == nil){ snprint(err,nerr,"bad_function"); return -1; }
	e->frame = framealloc(f, arg, nil, -1);
	if(e->frame == nil){ snprint(err,nerr,"out_of_memory"); return -1; }
	e->module = m;
	e->trace = trace;
	e->traceon = traceon;
	e->nframe = 1;
	e->maxframe = 1024;
	e->state = NvYield;
	e->guardfail = -1;
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
	if(host == nil)
		memset(&e->host, 0, sizeof e->host);
	else
		e->host = *host;
}

static int
run(NvExec *e, uvlong quantum)
{
	NvExecFrame *f, *n, *caller;
	NvInsn *i;
	NvConst *k;
	NvFunc *target;
	NvValue v;
	char hosterr[128];
	uvlong used;
	vlong left, right, ir;
	int dst, truth;

	if(e->state != NvYield) return e->state;
	if(quantum == 0) return NvYield;
	used = 0;
	f = e->frame;
	while(used < quantum){
		i = &f->func->insn[f->pc];
		if(e->traceon && e->trace != nil)
			Bprint(e->trace, "%llud %s:%d %s\n", e->reductions, f->func->name, f->pc, nvopname(i->op));
		used++; e->reductions++;
		switch(i->op){
		case Oloadk:
			memset(&v, 0, sizeof v);
			if(loadconst(&v, &e->module->konst[i->b]) < 0) return fault(e,"bad_constant");
			nvvaluefree(&f->reg[i->a]); f->reg[i->a] = v; f->pc++; break;
		case Omove:
			if(putvalue(&f->reg[i->a], &f->reg[i->b]) < 0) return fault(e,"out_of_memory");
			f->pc++; break;
		case Otuple:
			dst = nvvaluetuple(&v, &f->reg[i->b], i->c);
			if(dst < 0) return fault(e,dst == NvValuelimit ? "system_limit" : "out_of_memory");
			nvvaluefree(&f->reg[i->a]); f->reg[i->a] = v; f->pc++; break;
		case Ojump: f->pc = i->a; break;
		case Ocall:
			k = &e->module->konst[i->b]; target = findfunc(e->module, k->text);
			if(target == nil) return fault(e,"bad_function");
			if(e->nframe >= e->maxframe) return fault(e,"system_limit");
			f->pc++; n = framealloc(target, &f->reg[i->c], f, i->a);
			if(n == nil) return fault(e,"out_of_memory");
			e->nframe++;
			f = n; e->frame = f; break;
		case Otailcall:
			k = &e->module->konst[i->a]; target = findfunc(e->module, k->text);
			if(target == nil) return fault(e,"bad_function");
			n = framealloc(target, &f->reg[i->b], f->caller, f->dst);
			if(n == nil) return fault(e,"out_of_memory");
			framefree(f); f = n; e->frame = f; break;
		case Oreturn:
			if(f->caller == nil){
				if(nvvaluecopy(&e->result, &f->reg[i->a]) < 0) return fault(e,"out_of_memory");
				framefree(f); e->frame = nil; e->state = NvDone; return NvDone;
			}
			caller = f->caller; dst = f->dst;
			if(putvalue(&caller->reg[dst], &f->reg[i->a]) < 0) return fault(e,"out_of_memory");
			framefree(f); e->nframe--; f = caller; e->frame = f; break;
		case Otestatom:
			k = &e->module->konst[i->b]; f->pc = f->reg[i->a].kind == Vatom && strcmp(f->reg[i->a].atom,k->text)==0 ? f->pc+1 : i->c; break;
		case Otestint:
			k = &e->module->konst[i->b]; f->pc = f->reg[i->a].kind == Vint && f->reg[i->a].i == k->ival ? f->pc+1 : i->c; break;
		case Otesteq: f->pc = nvvalueequal(&f->reg[i->a],&f->reg[i->b]) ? f->pc+1 : i->c; break;
		case Otestarity: f->pc = f->reg[i->a].kind == Vtuple && f->reg[i->a].tuple->n == i->b ? f->pc+1 : i->c; break;
		case Ogetelem:
			if(f->reg[i->b].kind != Vtuple) return fault(e,"bad_tuple");
			if(i->c >= f->reg[i->b].tuple->n) return fault(e,"bad_element");
			if(putvalue(&f->reg[i->a],&f->reg[i->b].tuple->elem[i->c]) < 0) return fault(e,"out_of_memory");
			f->pc++; break;
		case Oadd: case Osub: case Omul: case Odiv: case Orem:
		case Olt: case Ole: case Ogt: case Oge:
			if(f->reg[i->b].kind != Vint || f->reg[i->c].kind != Vint) return fault(e,"badarith");
			left=f->reg[i->b].i; right=f->reg[i->c].i;
			if(i->op==Oadd){ if(addok(left,right,&ir)<0) return fault(e,"overflow"); intresult(&f->reg[i->a],ir); }
			else if(i->op==Osub){ if(subok(left,right,&ir)<0) return fault(e,"overflow"); intresult(&f->reg[i->a],ir); }
			else if(i->op==Omul){ if(mulok(left,right,&ir)<0) return fault(e,"overflow"); intresult(&f->reg[i->a],ir); }
			else if(i->op==Odiv || i->op==Orem){
				if(right==0) return fault(e,"divide_by_zero");
				if(i->op==Odiv && left==(vlong)0x8000000000000000LL && right==-1) return fault(e,"overflow");
				ir=i->op==Odiv ? left/right : left==(vlong)0x8000000000000000LL && right==-1 ? 0 : left%right;
				intresult(&f->reg[i->a],ir);
			}else{
				truth=i->op==Olt?left<right:i->op==Ole?left<=right:i->op==Ogt?left>right:left>=right;
				if(atomresult(&f->reg[i->a],truth)<0) return fault(e,"out_of_memory");
			}
			f->pc++; break;
		case Ofail:
			k=&e->module->konst[i->a]; return fault(e,k->kind==Katom?k->text:"explicit_fail");
		case Oself:
			if(e->host.self == nil) return fault(e,"bad_process_context");
			memset(&v,0,sizeof v); hosterr[0]=0;
			if(e->host.self(e->host.aux,&v,hosterr,sizeof hosterr)<0){ nvvaluefree(&v); return fault(e,hosterr[0]?hosterr:"system_limit"); }
			nvvaluefree(&f->reg[i->a]); f->reg[i->a]=v; f->pc++; break;
		case Omakeref:
			if(e->host.makeref == nil) return fault(e,"bad_process_context");
			memset(&v,0,sizeof v); hosterr[0]=0;
			if(e->host.makeref(e->host.aux,&v,hosterr,sizeof hosterr)<0){ nvvaluefree(&v); return fault(e,hosterr[0]?hosterr:"system_limit"); }
			nvvaluefree(&f->reg[i->a]); f->reg[i->a]=v; f->pc++; break;
		case Osend:
			if(e->host.send == nil) return fault(e,"bad_process_context");
			memset(&v,0,sizeof v); hosterr[0]=0;
			if(nvvaluecopy(&v,&f->reg[i->c])<0) return fault(e,"out_of_memory");
			if(e->host.send(e->host.aux,&f->reg[i->b],&f->reg[i->c],hosterr,sizeof hosterr)<0){ nvvaluefree(&v); return fault(e,hosterr[0]?hosterr:"system_limit"); }
			nvvaluefree(&f->reg[i->a]); f->reg[i->a]=v; f->pc++; break;
		case Ospawn:
			if(e->host.spawn == nil) return fault(e,"bad_process_context");
			k=&e->module->konst[i->b]; memset(&v,0,sizeof v); hosterr[0]=0;
			if(e->host.spawn(e->host.aux,k->text,&f->reg[i->c],&v,hosterr,sizeof hosterr)<0){ nvvaluefree(&v); return fault(e,hosterr[0]?hosterr:"system_limit"); }
			nvvaluefree(&f->reg[i->a]); f->reg[i->a]=v; f->pc++; break;
		case Orecvbegin: case Orecvnext:
			if(i->op==Orecvbegin && e->host.recvbegin==nil || i->op==Orecvnext && e->host.recvnext==nil) return fault(e,"bad_process_context");
			memset(&v,0,sizeof v); hosterr[0]=0;
			dst=i->op==Orecvbegin ? e->host.recvbegin(e->host.aux,&v,hosterr,sizeof hosterr) : e->host.recvnext(e->host.aux,&v,hosterr,sizeof hosterr);
			if(dst<0){ nvvaluefree(&v); return fault(e,hosterr[0]?hosterr:"system_limit"); }
			if(dst==0 && nvvalueatom(&v,"undefined")<0) return fault(e,"out_of_memory");
			nvvaluefree(&f->reg[i->a]); f->reg[i->a]=v;
			if(atomresult(&f->reg[i->b],dst!=0)<0) return fault(e,"out_of_memory");
			f->pc++; break;
		case Orecvtake:
			if(e->host.recvtake==nil) return fault(e,"bad_process_context");
			hosterr[0]=0;
			if(e->host.recvtake(e->host.aux,hosterr,sizeof hosterr)<0) return fault(e,hosterr[0]?hosterr:"bad_state");
			f->pc++; break;
		case Orecvwait:
			if(e->host.recvwait==nil) return fault(e,"bad_process_context");
			hosterr[0]=0;
			if(e->host.recvwait(e->host.aux,hosterr,sizeof hosterr)<0) return fault(e,hosterr[0]?hosterr:"bad_state");
			f->pc=i->a; e->frame=f; return NvYield;
		case Orecvdeadline:
			if(e->host.recvdeadline==nil) return fault(e,"bad_process_context");
			hosterr[0]=0;
			if(e->host.recvdeadline(e->host.aux,&f->reg[i->a],hosterr,sizeof hosterr)<0) return fault(e,hosterr[0]?hosterr:"bad_timeout");
			f->pc++; break;
		case Orecvwaitdeadline:
			if(e->host.recvwaitdeadline==nil) return fault(e,"bad_process_context");
			hosterr[0]=0;
			dst=e->host.recvwaitdeadline(e->host.aux,hosterr,sizeof hosterr);
			if(dst<0) return fault(e,hosterr[0]?hosterr:"bad_state");
			if(dst==0){ f->pc=i->a; e->frame=f; return NvYield; }
			f->pc++; break;
		case Oexit:
			if(nvvaluecopy(&e->exitreason,&f->reg[i->a])<0) return fault(e,"out_of_memory");
			e->state=NvExit; e->frame=f; return NvExit;
		case Oprint: case Oeprint:
			if((i->op==Oprint ? e->host.print : e->host.eprint)==nil) return fault(e,"bad_process_context");
			hosterr[0]=0;
			dst=i->op==Oprint ? e->host.print(e->host.aux,&f->reg[i->b],hosterr,sizeof hosterr)
			                  : e->host.eprint(e->host.aux,&f->reg[i->b],hosterr,sizeof hosterr);
			if(dst<0) return fault(e,hosterr[0]?hosterr:"io_error");
			if(nvvalueatom(&v,"ok")<0) return fault(e,"out_of_memory");
			nvvaluefree(&f->reg[i->a]); f->reg[i->a]=v; f->pc++; break;
		case Onop: f->pc++; break;
		case Oguard: e->guardfail = i->a; f->pc++; break;
		case Oguardend: e->guardfail = -1; f->pc++; break;
		case Oistype:
			if(atomresult(&f->reg[i->a], f->reg[i->b].valid && f->reg[i->b].kind == i->c) < 0) return fault(e,"out_of_memory");
			f->pc++; break;
		default:
			/*
			 * Unreachable for a verified module (nvverify rejects unknown
			 * opcodes), so this only fires if an opcode is added to nvbc.h
			 * without a case here. Faulting makes that a visible test
			 * failure instead of a silent spin on an unadvanced pc.
			 */
			return fault(e,"bad_opcode");
		}
	}
	e->frame = f;
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

void
nvexecfree(NvExec *e)
{
	NvExecFrame *f, *next;
	if(e == nil) return;
	for(f=e->frame; f!=nil; f=next){ next=f->caller; framefree(f); }
	nvvaluefree(&e->result);
	nvvaluefree(&e->exitreason);
	memset(e,0,sizeof *e);
}
