#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../../include/nervous.h"
#include "../../include/nvbc.h"
#include "../../include/nvvm.h"
#include "../../include/nvexec.h"
#include "../../include/nvcompile.h"
#include "../../include/nvproc.h"
#include "../../include/nvsched.h"

static void
check(int ok, char *s)
{
	if(!ok){
		fprint(2, "FAIL: automatic GC: %s\n", s);
		exits("test");
	}
}

static NvModule *
compile(char *src)
{
	Parser p;
	Program *pr;
	NvModule *m;
	char err[256];

	pr = parseprogram(&p, "automatic-gc", src, strlen(src));
	check(pr != nil, p.err);
	m = nvcompile(pr, err, sizeof err);
	programfree(pr);
	check(m != nil, err);
	return m;
}

static void
limits(NvLimits *l, uvlong maxheap, int stress)
{
	memset(l, 0, sizeof *l);
	l->maxprocess = 8;
	l->maxmailbox = 8192;
	l->maxmessage = 4096;
	l->maxheap = maxheap;
	l->gcstress = stress;
	l->maxframe = 16;
	l->maxtermdepth = NvMaxtermdepth;
	l->maxduration = NvMaxduration;
	l->maxatom = 65536;
	/* D074: already memset to zero above, so this is redundant but kept
	 * explicit for consistency with every other field. 0 = never off-process. */
	l->gcoffload = 0;
}

static void
tablegrowth(void)
{
	NvRuntime r;
	NvLimits l;
	NvTerm pid[200], p, q;
	NvFrag *msg;
	char err[128];
	ulong i, slot;

	limits(&l, 0, 0);
	l.maxprocess = nelem(pid);
	check(nvruntimeinit(&r, &l, 1, err, sizeof err) == 0, err);
	for(i = 0; i < nelem(pid); i++){
		check(nvprocspawn(&r, &pid[i], err, sizeof err) == 0, err);
		check(nvpidslot(pid[i]) == i, "append order changed");
		if(i == 3)
			check(nvprocsend(&r, pid[3], nvint(nil, 42), err, sizeof err) == 1, "growth message");
	}
	check(r.nslot == 200 && r.nalloc == 256 && r.tablegrows == 5 && r.slotprobes == 0, "table growth/search is not amortized");
	check(r.nrunnable == 200 && r.runhead == 0 && r.runtail == 199, "growth damaged queue");
	check(nvprocspawn(&r, &p, err, sizeof err) < 0 && r.nslot == 200, "spare capacity bypassed live limit");
	check(nvprocpop(&r, pid[3], &msg, err, sizeof err) == 1 && nvtermint(msg->root) == 42, "growth damaged mailbox");
	nvfragfree(msg);
	check(nvprocexit(&r, pid[150]) == 1 && nvprocexit(&r, pid[3]) == 1, "reuse setup");
	check(nvprocspawn(&r, &p, err, sizeof err) == 0 && nvpidslot(p) == 3, "lowest-free hint skipped slot");
	check(nvprocspawn(&r, &q, err, sizeof err) == 0 && nvpidslot(q) == 150, "next lowest-free slot");
	check(!nvprocalive(&r, pid[3]) && !nvprocalive(&r, pid[150]), "reused stale generation");
	check(r.runtail == 150 && r.process[150].runprev == 3 && r.nrunnable == 200, "reuse queue order");
	r.process[3].generation = NvMaxgeneration;
	p = nvpid(3, NvMaxgeneration);
	check(nvprocexit(&r, p) == 1, "retirement setup");
	check(nvprocspawn(&r, &q, err, sizeof err) == 0 && nvpidslot(q) == 200 && r.nslot == 201 && r.nalloc == 256 && r.process[3].state == Prretired, "retired slot confused capacity/live limits");
	/* Queue links survive both growth and removal from the middle. */
	for(i = 0; i < 200; i++){
		check(nvprocrunhead(&r, &slot), "missing queue member");
		p = nvpid(slot, r.process[slot].generation);
		check(nvprocexit(&r, p) == 1, "queue drain");
	}
	check(r.nlive == 0 && r.nrunnable == 0 && r.freehint == 0, "drain/hint state");
	nvruntimefree(&r);
	/* A retired slot can also force growth above a one-process limit. */
	l.maxprocess = 1;
	check(nvruntimeinit(&r, &l, 1, err, sizeof err) == 0, err);
	check(nvprocspawn(&r, &p, err, sizeof err) == 0 && r.nalloc == 1, "tiny table capacity");
	r.process[0].generation = NvMaxgeneration;
	check(nvprocexit(&r, nvpid(0, NvMaxgeneration)) == 1, "tiny retirement");
	check(nvprocspawn(&r, &p, err, sizeof err) == 0 && nvpidslot(p) == 1 && r.nalloc == 2, "tiny retired-table growth");
	nvruntimefree(&r);
	print("ok - geometric table growth and free hints preserve limits, retirement, queues and mailboxes\n");
}

static void
loops(void)
{
	char *src =
		"fn pump(0) { 'done }\n"
		"fn pump(n) {\n"
		"  self ! ${'token, n, mkref, 9223372036854775807 - n};\n"
		"  receive {\n"
		"    ${'token, x, r, b} when is_ref(r) and x == n and b == 9223372036854775807 - n => pump(n - 1);\n"
		"  }\n"
		"}\n"
		"fn main() { pump(1000) }\n";
	NvModule *m;
	NvHeap h;
	NvTerm arg, pid;
	NvLimits l;
	NvScheduler s;
	NvExec *e;
	char err[256];
	uvlong reductions, peak, cap;
	int stress, q, step, state;

	m = compile(src);
	nvheapinit(&h, 0);
	arg = nvtuple(&h, nil, 0);
	check(arg != NvNil, "loop argument");
	reductions = 0;
	for(stress = 0; stress <= 1; stress++)
		for(q = 0; q < 2; q++){
			limits(&l, 1024, stress);
			check(nvschedinit(&s, m, &l, 1, q ? 1000 : 1, err, sizeof err) == 0, err);
			s.profile = q; /* timing must not affect reductions or side effects */
			check(nvschedspawnroot(&s, "main", arg, &pid, err, sizeof err) == 0, err);
			state = NvSchedProgress;
			peak = cap = 0;
			for(step = 0; step < 1000000 && state == NvSchedProgress; step++){
				state = nvschedstep(&s, err, sizeof err);
				e = s.runtime.process[nvpidslot(pid)].exec;
				if(e != nil){
					check(e->heap.full == nil && e->heap.managed, "managed heap grew chunks");
					if(e->heap.words+e->nstack > peak) peak = e->heap.words+e->nstack;
					if(e->heap.cur->cap > cap) cap = e->heap.cur->cap;
				}
			}
			check(state == NvSchedDone && s.rootstate == NvRootDone && s.rootvalue != nil, "bounded loop completion");
			check(nvtermkind(s.rootvalue->root) == Vatom && strcmp(nvtermatom(s.rootvalue->root), "done") == 0, "loop result");
			check(s.runtime.nsent == 1000 && s.runtime.nextref == 1001, "send/mkref duplicated by retry");
			check(s.collections > 0 && s.gcfailed == 0 && peak <= 1024 && cap <= 2048, "bounded memory or collection evidence");
			if(reductions == 0) reductions = s.reductions;
			check(s.reductions == reductions, "GC/stress/quantum changed reduction count");
			nvschedfree(&s);
		}
	nvheapfree(&h);
	nvmodulefree(m);
	print("ok - automatic GC bounds a message/ref/boxed-arithmetic loop across stress and quanta\n");
}

static void
requests(void)
{
	NvModule m;
	NvFunc f[2];
	NvInsn code[3];
	NvConst k;
	NvHeap h;
	NvTerm arg, old, first, second;
	NvExec e;
	NvScheduler s;
	NvLimits l;
	ulong head;
	char err[128];

	memset(&m,0,sizeof m); memset(f,0,sizeof f);
	memset(code,0,sizeof code); memset(&k,0,sizeof k);
	m.nfunc=2; m.func=f; m.nconst=1; m.konst=&k;
	k.kind=Kfunc; k.text="large";
	f[0].name="small"; f[0].nreg=1; f[0].ninsn=1; f[0].insn=code;
	code[0].op=Otailcall; code[0].a=0; code[0].b=0;
	f[1].name="large"; f[1].nreg=100; f[1].ninsn=1; f[1].insn=code+1;
	code[1].op=Oreturn; code[1].a=0;
	nvheapinit(&h,0); arg=nvtuple(&h,nil,0);
	check(nvexecinit(&e,&m,"small",arg,64,nil,0,err,sizeof err)<0 && strcmp(err,"system_limit")==0, "initial stack charge");
	check(nvexecinit(&e,&m,"small",arg,129,nil,0,err,sizeof err)==0, "tail growth init");
	old=e.stack[NvFramehdr];
	check(nvexecrun(&e,1)==NvCollect && e.reductions==0 && e.nstack==64 && e.sp==5 && e.nframe==1, "tail growth request was not atomic");
	check(e.stack[NvFramepc]==0 && e.stack[NvFramehdr]==old && e.gcneed==64, "tail request modified pc/root or wrong capacity delta");
	check(nvexecrun(&e,1)==NvCollect && e.reductions==0, "pending request was executed");
	nvexecgc(&e);
	check(e.gcretry==1 && e.reductions==0 && e.stack[NvFramehdr]!=old, "service changed reductions or did not move root");
	check(nvexecrun(&e,1)==NvYield && e.nstack==128 && e.nframe==1 && e.reductions==1, "tail growth retry");
	check(nvexecruninline(&e,1)==NvDone && nvtuplelen(e.result->root)==0, "tail result after movement");
	nvexecfree(&e);
	check(nvexecinit(&e,&m,"small",arg,65,nil,0,err,sizeof err)==0, "tight tail init");
	check(nvexecruninline(&e,1)==NvFault && strcmp(e.fault,"system_limit")==0 && e.reductions==1 && e.nstack==64, "tail stack exhaustion");
	nvexecfree(&e);
	limits(&l,129,0);
	check(nvschedinit(&s,&m,&l,1,1,err,sizeof err)==0,err);
	check(nvschedspawnroot(&s,"small",arg,&first,err,sizeof err)==0,err);
	check(nvschedspawn(&s,"large",arg,&second,err,sizeof err)==0,err);
	check(nvschedstep(&s,err,sizeof err)==NvSchedProgress && s.runtime.nrunnable==2, "collection lost or duplicated runnable owner");
	check(nvprocrunhead(&s.runtime,&head) && head==nvpidslot(second), "collecting process bypassed queued peer");
	check(nvschedstep(&s,err,sizeof err)==NvSchedProgress && s.completed==1, "peer did not make progress before retry");
	check(s.runtime.process[nvpidslot(first)].exec->reductions==0, "owner executed before queued peer");
	nvschedfree(&s);
	/* The same retained-capacity reservation applies to a non-tail push. */
	f[0].nreg=2; f[0].ninsn=2; f[1].insn=code+2;
	code[0].op=Ocall; code[0].a=1; code[0].b=0; code[0].c=0;
	code[1].op=Oreturn; code[1].a=1;
	code[2].op=Oreturn; code[2].a=0;
	check(nvexecinit(&e,&m,"small",arg,129,nil,0,err,sizeof err)==0,"call growth init");
	check(nvexecruninline(&e,3)==NvDone && e.nstack==128 && e.reductions==3 && e.collections>0 && nvtuplelen(e.result->root)==0,"non-tail growth or return roots");
	nvexecfree(&e); nvheapfree(&h);
	print("ok - allocation requests are atomic and charge call/tail-call stack capacity exactly\n");
}

static void
guardlimit(void)
{
	NvModule m;
	NvFunc f;
	NvInsn code[6];
	NvConst k;
	NvHeap h;
	NvTerm arg;
	NvExec e;
	char err[128];
	int stress;

	memset(&m,0,sizeof m); memset(&f,0,sizeof f);
	memset(code,0,sizeof code); memset(&k,0,sizeof k);
	m.nfunc=1; m.func=&f; m.nconst=1; m.konst=&k;
	f.name="guarded"; f.nreg=2; f.ninsn=6; f.insn=code;
	k.kind=Kint; k.ival=42;
	code[0].op=Oguard; code[0].a=4;
	code[1].op=Otuple; code[1].a=1; code[1].b=0; code[1].c=1;
	code[2].op=Oguardend;
	code[3].op=Oreturn; code[3].a=1;
	code[4].op=Oloadk; code[4].a=1; code[4].b=0;
	code[5].op=Oreturn; code[5].a=1;
	nvheapinit(&h,0); arg=nvtuple(&h,nil,0);
	for(stress=0; stress<2; stress++){
		check(nvexecinit(&e,&m,"guarded",arg,65,nil,0,err,sizeof err)==0, "guard limit init");
		e.gcstress=stress;
		check(nvexecruninline(&e,4)==NvDone && e.reductions==4 && e.guardfail==-1 && e.fault[0]==0, "collection failure escaped guard");
		check(nvtermint(e.result->root)==42, "guard fallback result");
		nvexecfree(&e);
	}
	nvheapfree(&h);
	print("ok - collection exhaustion inside a guard is a charged clause failure, not process exit\n");
}

static void
receivetake(void)
{
	NvModule m;
	NvFunc f;
	NvInsn code[3];
	NvLimits l;
	NvScheduler s;
	NvHeap h;
	NvTerm arg, pid;
	NvProcess *p;
	NvFrag *candidate;
	NvMemstats mem;
	char err[128];
	int pass;

	memset(&m,0,sizeof m); memset(&f,0,sizeof f); memset(code,0,sizeof code);
	m.nfunc=1; m.func=&f;
	f.name="receiver"; f.nreg=3; f.ninsn=3; f.insn=code;
	code[0].op=Orecvbegin; code[0].a=1; code[0].b=2;
	code[1].op=Orecvtake;
	code[2].op=Oreturn; code[2].a=1;
	nvheapinit(&h,0); arg=nvtuple(&h,nil,0);
	for(pass=0; pass<2; pass++){
		limits(&l, pass ? 67 : 65, 1);
		check(nvschedinit(&s,&m,&l,1,1,err,sizeof err)==0,err);
		check(nvschedspawnroot(&s,"receiver",arg,&pid,err,sizeof err)==0,err);
		check(nvprocsend(&s.runtime,pid,arg,err,sizeof err)==1,"queue candidate");
		check(nvschedstep(&s,err,sizeof err)==NvSchedProgress,"begin scan");
		p=&s.runtime.process[nvpidslot(pid)]; candidate=p->scan;
		check(candidate!=nil && p->mailboxwords==2,"candidate setup");
		nvschedmemory(&s, &mem);
		check(mem.tablebytes == (uvlong)s.runtime.nalloc*sizeof(NvProcess) && mem.nexec == 1 && mem.execbytes == sizeof(NvExec), "snapshot table/exec accounting");
		check(mem.nmailbox == 1 && mem.mailboxbytes == sizeof(NvFrag)+sizeof(NvTerm) && mem.nadopted == 0, "snapshot queued fragment accounting");
		check(mem.stackbytes == 64*sizeof(NvTerm) && mem.heapused == sizeof(NvTerm) && mem.heapbytes == sizeof(NvChunk)+64*sizeof(NvTerm), "snapshot capacity versus used");
		check(nvschedstep(&s,err,sizeof err)==NvSchedProgress,"take reservation");
		check(p->scan==candidate && p->head==candidate && p->mailboxwords==2 && p->exec->heap.adopted==nil,"reservation consumed candidate");
		check(p->exec->reductions==1 && p->state==Prrunnable && s.runtime.nrunnable==1,"request charged or duplicate queue insertion");
		check(nvschedstep(&s,err,sizeof err)==NvSchedProgress,"take retry");
		if(pass){
			check(p->head==nil && p->mailboxwords==0 && p->exec->heap.adopted==candidate,"successful take not adopted exactly once");
			nvschedmemory(&s, &mem);
			check(mem.mailboxbytes == 0 && mem.nadopted == 1 && mem.adoptedbytes == sizeof(NvFrag)+sizeof(NvTerm), "snapshot adopted fragment accounting");
			check(mem.totalbytes == mem.tablebytes+mem.execbytes+mem.heapbytes+mem.stackbytes+mem.adoptedbytes, "snapshot total double-counted used words");
			check(nvschedstep(&s,err,sizeof err)==NvSchedProgress && s.rootstate==NvRootDone && nvtuplelen(s.rootvalue->root)==0,"taken value did not survive");
		}else
			check(s.rootstate==NvRootFault && strcmp(s.rootfault,"system_limit")==0,"take budget fault");
		check(nvschedstep(&s,err,sizeof err)==NvSchedDone,"receive completion");
		check(s.gcdemand == 1 && s.gcidle == 0 && s.gcinputwords == 1 && s.execns == 0 && s.gcns == 0 && s.spawnns == 0, "collection counters or default-off timing");
		nvschedmemory(&s, &mem);
		check(mem.nexec == 0 && mem.heapbytes == 0 && mem.stackbytes == 0 && mem.mailboxbytes == 0 && mem.adoptedbytes == 0, "snapshot retains exited process storage");
		check(mem.reportbytes == (pass ? sizeof(NvFrag)+sizeof(NvTerm) : 0), "snapshot result fragment");
		nvschedfree(&s);
	}
	nvheapfree(&h);
	print("ok - receive reservation preserves queued candidates until one successful adoption\n");
}

static void
idlecollect(void)
{
	NvModule m;
	NvFunc f;
	NvInsn code[42];
	NvLimits l;
	NvScheduler s;
	NvHeap h;
	NvTerm arg, pid;
	NvExec *e;
	char err[128];
	int i;
	uvlong before;

	memset(&m,0,sizeof m); memset(&f,0,sizeof f); memset(code,0,sizeof code);
	m.nfunc=1; m.func=&f;
	f.name="waiter"; f.nreg=3; f.ninsn=nelem(code); f.insn=code;
	for(i=0; i<40; i++){ code[i].op=Otuple; code[i].a=1; code[i].b=0; code[i].c=0; }
	code[40].op=Orecvbegin; code[40].a=1; code[40].b=2;
	code[41].op=Orecvwait; code[41].a=40;
	nvheapinit(&h,0); arg=nvtuple(&h,nil,0);
	limits(&l,1024,0);
	check(nvschedinit(&s,&m,&l,1,1000,err,sizeof err)==0,err);
	check(nvschedspawnroot(&s,"waiter",arg,&pid,err,sizeof err)==0,err);
	check(nvschedstep(&s,err,sizeof err)==NvSchedProgress && s.runtime.process[nvpidslot(pid)].state==Prwaiting,"waiter did not block");
	e=s.runtime.process[nvpidslot(pid)].exec; before=e->reductions;
	check(e->heap.words==41 && s.collections==0,"waiter demand-collected prematurely");
	check(nvschedstep(&s,err,sizeof err)==NvSchedIdle && s.collections==1 && e->heap.words==1 && e->reductions==before,"idle collection did not reclaim without execution");
	check(nvschedstep(&s,err,sizeof err)==NvSchedIdle && s.collections==1,"unchanged waiter recollected");
	nvschedfree(&s); nvheapfree(&h);
	print("ok - an idle waiting process gives back garbage without executing or spinning\n");
}

void
main(void)
{
	tablegrowth();
	requests();
	guardlimit();
	receivetake();
	idlecollect();
	loops();
	print("all automatic inline collector tests passed\n");
	exits(nil);
}
