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
	/* was: r.nslot == 200 && r.nalloc == 256 && r.tablegrows == 5 && r.slotprobes == 0 */
	check(r.nslot == 200 && r.nchunk == 1 && r.tablegrows == 1 && r.slotprobes == 0, "table growth/search is not amortized");
	/* D083: a bare runtime has exactly one queue, scheduler 0's; every spawn here owns to it. */
	check(r.nrunq == 1 && r.runq[0]->nrunnable == 200 && r.runq[0]->head == 0 && r.runq[0]->tail == 199, "growth damaged queue");
	check(nvprocat(&r, 0)->owner == 0 && nvprocat(&r, 199)->owner == 0, "spawned owner");
	check(nvprocspawn(&r, &p, err, sizeof err) < 0 && r.nslot == 200, "spare capacity bypassed live limit");
	check(nvprocpop(&r, pid[3], &msg, err, sizeof err) == 1 && nvtermint(msg->root) == 42, "growth damaged mailbox");
	nvfragfree(msg);
	check(nvprocexit(&r, pid[150]) == 1 && nvprocexit(&r, pid[3]) == 1, "reuse setup");
	check(nvprocspawn(&r, &p, err, sizeof err) == 0 && nvpidslot(p) == 3, "lowest-free hint skipped slot");
	check(nvprocspawn(&r, &q, err, sizeof err) == 0 && nvpidslot(q) == 150, "next lowest-free slot");
	check(!nvprocalive(&r, pid[3]) && !nvprocalive(&r, pid[150]), "reused stale generation");
	check(r.runq[0]->tail == 150 && nvprocat(&r, 150)->runprev == 3 && r.runq[0]->nrunnable == 200, "reuse queue order");
	nvprocat(&r, 3)->generation = NvMaxgeneration;
	p = nvpid(3, NvMaxgeneration);
	check(nvprocexit(&r, p) == 1, "retirement setup");
	/* was: nvpidslot(q) == 200 && r.nslot == 201 && r.nalloc == 256 && r.process[3].state == Prretired */
	check(nvprocspawn(&r, &q, err, sizeof err) == 0 && nvpidslot(q) == 200 && r.nslot == 201 && r.nchunk == 1 && nvprocat(&r, 3)->state == Prretired, "retired slot confused capacity/live limits");
	/* Queue links survive both growth and removal from the middle. */
	for(i = 0; i < 200; i++){
		check(nvprocrunhead(&r, 0, &slot), "missing queue member");
		p = nvpid(slot, nvprocat(&r, slot)->generation);
		check(nvprocexit(&r, p) == 1, "queue drain");
	}
	check(r.nlive == 0 && r.runq[0]->nrunnable == 0 && r.freehint == 0, "drain/hint state");
	/* D083: a queue that does not exist is not a spawn target; scheduler 1 must be created first. */
	check(nvprocspawnon(&r, 1, &p, err, sizeof err) < 0 && strcmp(err, "bad_scheduler") == 0 && r.nlive == 0, "spawn onto a missing queue");
	check(nvruntimesetnrunq(&r, 2, err, sizeof err) == 0 && r.nrunq == 2 && r.runq[1]->head == NvNoslot && r.runq[1]->nrunnable == 0, "second queue");
	check(nvprocspawnon(&r, 1, &p, err, sizeof err) == 0 && nvprocat(&r, nvpidslot(p))->owner == 1, "spawn onto the second queue");
	check(r.runq[0]->nrunnable == 0 && r.runq[1]->nrunnable == 1 && r.runq[1]->head == nvpidslot(p) && r.runq[1]->tail == nvpidslot(p), "queued on the owner's queue only");
	check(nvprocrunhead(&r, 0, &slot) == 0 && nvprocrunhead(&r, 1, &slot) == 1 && slot == nvpidslot(p), "run head is per queue");
	check(nvprocexit(&r, p) == 1 && r.runq[1]->nrunnable == 0 && r.runq[1]->head == NvNoslot, "exit dequeues from the owner's queue");
	nvruntimefree(&r);
	/* A retired slot still needs a slot beyond the one-process LIVE limit
	 * (nslot 2), even though D082 no longer grows the table for it. */
	l.maxprocess = 1;
	check(nvruntimeinit(&r, &l, 1, err, sizeof err) == 0, err);
	/* was: r.nalloc == 1 */
	check(nvprocspawn(&r, &p, err, sizeof err) == 0 && r.nchunk == 1, "tiny table capacity");
	nvprocat(&r, 0)->generation = NvMaxgeneration;
	check(nvprocexit(&r, nvpid(0, NvMaxgeneration)) == 1, "tiny retirement");
	/* was: nvpidslot(p) == 1 && r.nalloc == 2 */
	check(nvprocspawn(&r, &p, err, sizeof err) == 0 && nvpidslot(p) == 1 && r.nslot == 2 && r.nchunk == 1, "tiny retired-table growth");
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
			s.sched[0]->profile = q; /* timing must not affect reductions or side effects */
			check(nvschedspawnroot(&s, "main", arg, &pid, err, sizeof err) == 0, err);
			state = NvSchedProgress;
			peak = cap = 0;
			for(step = 0; step < 1000000 && state == NvSchedProgress; step++){
				state = nvschedstep(&s, err, sizeof err);
				e = nvprocat(&s.runtime, nvpidslot(pid))->exec;
				if(e != nil){
					check(e->heap.full == nil && e->heap.managed, "managed heap grew chunks");
					if(e->heap.words+e->nstack > peak) peak = e->heap.words+e->nstack;
					if(e->heap.cur->cap > cap) cap = e->heap.cur->cap;
				}
			}
			check(state == NvSchedDone && s.rootstate == NvRootDone && s.rootvalue != nil, "bounded loop completion");
			check(nvtermkind(s.rootvalue->root) == Vatom && strcmp(nvtermatom(s.rootvalue->root), "done") == 0, "loop result");
			check(s.runtime.nsent == 1000 && s.runtime.nextref == 1001, "send/mkref duplicated by retry");
			check(s.sched[0]->collections > 0 && s.sched[0]->gcfailed == 0 && peak <= 1024 && cap <= 2048, "bounded memory or collection evidence");
			if(reductions == 0) reductions = s.sched[0]->reductions;
			check(s.sched[0]->reductions == reductions, "GC/stress/quantum changed reduction count");
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
	check(nvschedstep(&s,err,sizeof err)==NvSchedProgress && s.sched[0]->runq->nrunnable==2, "collection lost or duplicated runnable owner");
	check(nvprocrunhead(&s.runtime,0,&head) && head==nvpidslot(second), "collecting process bypassed queued peer");
	check(nvschedstep(&s,err,sizeof err)==NvSchedProgress && s.completed==1, "peer did not make progress before retry");
	check(nvprocat(&s.runtime, nvpidslot(first))->exec->reductions==0, "owner executed before queued peer");
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
		p=nvprocat(&s.runtime, nvpidslot(pid)); candidate=p->scan;
		check(candidate!=nil && p->mailboxwords==2,"candidate setup");
		nvschedmemory(&s, &mem);
		check(mem.tablebytes == (uvlong)s.runtime.nchunk*NvProcchunk*sizeof(NvProcess) && mem.nexec == 1 && mem.execbytes == sizeof(NvExec), "snapshot table/exec accounting");
		check(mem.nmailbox == 1 && mem.mailboxbytes == sizeof(NvFrag)+sizeof(NvTerm) && mem.nadopted == 0, "snapshot queued fragment accounting");
		check(mem.stackbytes == 64*sizeof(NvTerm) && mem.heapused == sizeof(NvTerm) && mem.heapbytes == sizeof(NvChunk)+64*sizeof(NvTerm), "snapshot capacity versus used");
		check(nvschedstep(&s,err,sizeof err)==NvSchedProgress,"take reservation");
		check(p->scan==candidate && p->head==candidate && p->mailboxwords==2 && p->exec->heap.adopted==nil,"reservation consumed candidate");
		check(p->exec->reductions==1 && p->state==Prrunnable && s.sched[0]->runq->nrunnable==1,"request charged or duplicate queue insertion");
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
		check(s.sched[0]->gcdemand == 1 && s.sched[0]->gcidle == 0 && s.sched[0]->gcinputwords == 1 && s.sched[0]->execns == 0 && s.sched[0]->gcns == 0 && s.sched[0]->spawnns == 0, "collection counters or default-off timing");
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
	check(nvschedstep(&s,err,sizeof err)==NvSchedProgress && nvprocat(&s.runtime, nvpidslot(pid))->state==Prwaiting,"waiter did not block");
	e=nvprocat(&s.runtime, nvpidslot(pid))->exec; before=e->reductions;
	check(e->heap.words==41 && s.sched[0]->collections==0,"waiter demand-collected prematurely");
	check(nvschedstep(&s,err,sizeof err)==NvSchedIdle && s.sched[0]->collections==1 && e->heap.words==1 && e->reductions==before,"idle collection did not reclaim without execution");
	check(nvschedstep(&s,err,sizeof err)==NvSchedIdle && s.sched[0]->collections==1,"unchanged waiter recollected");
	nvschedfree(&s); nvheapfree(&h);
	print("ok - an idle waiting process gives back garbage without executing or spinning\n");
}

/*
 * R3 gap 3e: a spawn whose argument copy cannot fit the child's own
 * maxheap budget must fault the SPAWNING process with "system_limit"
 * (not merely the child), free the child's exec/heap/table slot
 * completely (no leaked slot), and leave the runtime with no live
 * processes once the spawner itself is reaped for the same fault.
 *
 * The margin trick: nvexecinitw charges every process's own initial
 * retained stack capacity (a power-of-two floor of 64 words, doubling
 * only if the entry function's own register count needs more --
 * lib/exec.c growstack/stackcap) against the SAME maxheap budget as
 * its heap words, before the argument copy even starts (D066: "maxheap
 * is the per-process word budget: heap in use plus adopted fragments
 * plus retained frame-stack capacity"). So root's entry function is
 * tiny (nreg=4, stack stays at the 64-word floor) while child's entry
 * function has NvMaxreg (256) registers, forcing its floor one
 * doubling higher (512) -- a maxheap comfortably big enough for root
 * to build and hold the whole argument itself is still too small for
 * the CHILD to redo the equivalent copy on top of its own much bigger
 * stack charge.
 *
 * The argument is a depth-200 chain of one-element tuples
 * (${${${...1...}}}), not a flat 200-element tuple: a flat tuple's
 * elements must all sit in contiguous registers for one Otuple
 * instruction, which would force ROOT's own register count up too
 * (undoing the asymmetry above); a chain needs only a few reused
 * registers per level (D061: construction never bounds depth). 200
 * levels stays safely under NvMaxtermdepth (256), so the copy failure
 * below is genuinely the maxheap word budget, not the unrelated depth
 * ceiling -- each level costs exactly 2 words (a 1-element tuple: 1
 * header + 1 body word; gctest.c's shapes() confirms this "2 words per
 * level" costing for the identical chain shape).
 *
 * Numbers (all exact, from lib/exec.c growstack/prepare and
 * lib/value.c nvheapalloc/heapcopy):
 *   root:  stack floor 64 (nreg=4 fits in one 64-word frame) + prior
 *          live 1 (its own empty-tuple spawn argument) + worst-case
 *          charge 2 (its very last Otuple, after 199*2=398 words
 *          already built) = 465 words needed at most -- maxheap=700
 *          leaves 235 words of slack, so root completes the whole
 *          200-level build and reaches the Ospawn instruction.
 *   child: stack floor 512 (nreg=256 needs 4+256=260 words: 256 itself
 *          is not enough, so stackcap doubles once more to 512) +
 *          prior live 0 (a fresh heap) + worst-case charge 2 (its very
 *          last copied level) = 912 words needed at most -- maxheap=700
 *          is 212 words short of that, so nvheapcopyw's nvheapalloc
 *          call for some level (not necessarily the last one) returns
 *          nil, setting heap.exhausted, well before either process
 *          could ever reach the depth ceiling.
 * Both figures leave generous (>200-word) margins on both sides of
 * maxheap=700, so small errors in this accounting would have to be
 * large to flip either outcome.
 */
static void
spawncopyfail(void)
{
	NvModule m;
	NvFunc f[2];
	NvInsn code[10];
	NvConst k[4];
	NvHeap h;
	NvLimits l;
	NvScheduler s;
	NvMemstats mem;
	NvTerm arg, pid;
	char err[256];
	int stress, state, step;

	memset(&m, 0, sizeof m);
	memset(f, 0, sizeof f);
	memset(code, 0, sizeof code);
	memset(k, 0, sizeof k);
	k[0].kind = Kint; k[0].ival = 1;
	k[1].kind = Kint; k[1].ival = 200;	/* chain depth N; see comment above */
	k[2].kind = Kint; k[2].ival = 0;
	k[3].kind = Kfunc; k[3].text = "child";
	m.nconst = 4; m.konst = k; m.nfunc = 2; m.func = f;
	f[0].name = "root"; f[0].nreg = 4; f[0].ninsn = 9; f[0].insn = code;
	code[0].op = Oloadk; code[0].a = 0; code[0].b = 0;			/* r0 := 1 */
	code[1].op = Oloadk; code[1].a = 1; code[1].b = 1;			/* r1 := 200 */
	code[2].op = Oloadk; code[2].a = 2; code[2].b = 0;			/* r2 := 1 (decrement constant) */
	code[3].op = Otestint; code[3].a = 1; code[3].b = 2; code[3].c = 6;	/* r1==0 -> fall to spawn(pc 4); else loop body (pc 6) */
	code[4].op = Ospawn; code[4].a = 3; code[4].b = 3; code[4].c = 0;	/* r3 := spawn(child, r0) */
	code[5].op = Oreturn; code[5].a = 3;					/* only reached if spawn somehow succeeded */
	code[6].op = Otuple; code[6].a = 0; code[6].b = 0; code[6].c = 1;	/* r0 := ${r0} */
	code[7].op = Osub; code[7].a = 1; code[7].b = 1; code[7].c = 2;	/* r1 := r1 - r2 */
	code[8].op = Ojump; code[8].a = 3;
	f[1].name = "child"; f[1].nreg = NvMaxreg; f[1].ninsn = 1; f[1].insn = code+9;
	code[9].op = Oreturn; code[9].a = 0;	/* never actually reached: the copy fails first */

	nvheapinit(&h, 0);
	arg = nvtuple(&h, nil, 0);
	check(arg != NvNil, "spawncopyfail argument");
	for(stress = 0; stress <= 1; stress++){
		limits(&l, 700, stress);
		check(nvschedinit(&s, &m, &l, 1, 1000, err, sizeof err) == 0, err);
		check(nvschedspawnroot(&s, "root", arg, &pid, err, sizeof err) == 0, err);
		state = NvSchedProgress;
		for(step = 0; step < 100000 && state == NvSchedProgress; step++)
			state = nvschedstep(&s, err, sizeof err);
		check(state == NvSchedDone, "scheduler did not finish");
		check(s.rootstate == NvRootFault && strcmp(s.rootfault, "system_limit") == 0,
			"failed spawn argument copy did not fault the spawner with system_limit");
		check(s.runtime.nlive == 0,
			"a live process leaked after the failed spawn and the spawner's own fault-exit");
		nvschedmemory(&s, &mem);
		check(mem.nexec == 0 && mem.heapbytes == 0,
			"the failed child's exec/heap (or the reaped spawner's own) was not fully freed");
		nvschedfree(&s);
	}
	nvheapfree(&h);
	print("ok - a spawn argument copy too big for the child's own maxheap faults the spawner with system_limit and frees everything\n");
}

/*
 * R3 gap 4: memory proportional to live data. D069/lib/gc.c spacecap:
 * to-space capacity is the smallest power of two >= 64 (Minspace) in
 * which live+need is at most half, and it never shrinks. A process
 * that holds a FIXED live set while repeatedly allocating and dropping
 * garbage should therefore have its heap capacity settle at whatever
 * the first real collection chooses and stay there forever after --
 * that equality, sampled across many collections, is exactly the
 * evidence bench/README.md's largelive.c narrates but never asserts as
 * a pass/fail property (it is a benchmark, not a regression).
 *
 * The live set is a 20-element tuple of small ints (21 words: 1 header
 * + 20 immediate elements, no boxing needed), carried unchanged through
 * a tail-recursive loop; each iteration also builds and immediately
 * drops an 8-element tuple (9 words) as garbage.
 *
 * The capacity is NOT constant from the very first collection: main's
 * first collection sees live 1 + need 21 = 22 words and sizes 64, but
 * inside churn the worst reservation sees the incoming argument tuple
 * (3) + fixed (21) + the garbage tuple still sitting in its register (9)
 * + the outgoing argument tuple's need (3) = 36 > 32, so the collector
 * doubles once to 128 within the first loop iteration. After that
 * nothing in the loop's live set changes, so the property under test is
 * "settles, then never moves": sampling begins once the loop has been
 * through several collections (Settleafter), and every later sample
 * must equal the first one taken. A separate bound (<= 256 words of
 * chunk) catches a heap that settled somewhere silly.
 *
 * gcstress forces a collection at every allocating instruction AND
 * every call/tailcall reservation (lib/exec.c prepare(): Ocall/Otailcall
 * always set active=1, so gcstress applies even when the stack charge
 * is 0), so 60 outer iterations (each: one tailcall into the next
 * churn clause, one Otuple building the garbage tuple) is far more than
 * the 20 real collections this test is required to force.
 */
enum { Settleafter = 8 };
static void
churnflat(void)
{
	char *src =
		"fn churn(fixed, 0) { fixed }\n"
		"fn churn(fixed, remaining) {\n"
		"  ${1, 2, 3, 4, 5, 6, 7, 8};\n"
		"  churn(fixed, remaining - 1)\n"
		"}\n"
		"fn main() {\n"
		"  churn(${1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20}, 60)\n"
		"}\n";
	NvModule *m;
	NvHeap h;
	NvTerm arg, pid, result;
	NvLimits l;
	NvScheduler s;
	NvMemstats mem;
	char err[256];
	uvlong capbytes;
	int seenfirst, state, step, i;

	m = compile(src);
	nvheapinit(&h, 0);
	arg = nvtuple(&h, nil, 0);
	check(arg != NvNil, "churn argument");
	limits(&l, 1024, 1);
	check(nvschedinit(&s, m, &l, 1, 1000, err, sizeof err) == 0, err);
	check(nvschedspawnroot(&s, "main", arg, &pid, err, sizeof err) == 0, err);
	seenfirst = 0;
	capbytes = 0;
	state = NvSchedProgress;
	for(step = 0; step < 100000 && state == NvSchedProgress; step++){
		state = nvschedstep(&s, err, sizeof err);
		check(state != NvSchedError, err);
		nvschedmemory(&s, &mem);
		if(mem.nexec == 0)
			continue;	/* not dispatched yet, or already reaped this step */
		if(!seenfirst && s.sched[0]->collections >= Settleafter){
			seenfirst = 1;
			capbytes = mem.heapbytes;
			check(capbytes <= sizeof(NvChunk)+256*sizeof(NvTerm), "heap capacity settled far above the live set");
		}else if(seenfirst)
			check(mem.heapbytes == capbytes, "heap capacity changed after the live set had settled");
	}
	check(state == NvSchedDone, "scheduler did not finish");
	check(seenfirst, "test never observed enough collections to sample");
	check(s.sched[0]->collections >= 20, "test did not force at least 20 collections");
	check(s.sched[0]->gcfailed == 0, "unexpected collection failure under a comfortable maxheap");
	check(s.rootstate == NvRootDone && s.rootvalue != nil, "churn did not complete normally");
	result = s.rootvalue->root;
	check(nvtermkind(result) == Vtuple && nvtuplelen(result) == 20, "fixed live set did not survive as the root result");
	for(i = 0; i < 20; i++)
		check(nvtermkind(nvtupleelem(result, i)) == Vint && nvtermint(nvtupleelem(result, i)) == i+1,
			"fixed live set element corrupted across many collections");
	nvschedfree(&s);
	nvheapfree(&h);
	nvmodulefree(m);
	print("ok - a fixed live set churning garbage keeps heap capacity constant across at least 20 collections\n");
}

void
main(void)
{
	tablegrowth();
	requests();
	guardlimit();
	receivetake();
	idlecollect();
	spawncopyfail();
	churnflat();
	loops();
	print("all automatic inline collector tests passed\n");
	exits(nil);
}
