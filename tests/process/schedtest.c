#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../../include/nvbc.h"
#include "../../include/nvvm.h"
#include "../../include/nvexec.h"
#include "../../include/nvproc.h"
#include "../../include/nvsched.h"

static void
fail(char *s)
{
	fprint(2, "FAIL: %s\n", s);
	exits("test");
}

static void
check(int ok, char *s)
{
	if(!ok)
		fail(s);
}

static void
makemodule(NvModule *m, NvFunc *f, NvInsn *insn, NvConst *k)
{
	memset(m, 0, sizeof *m);
	memset(f, 0, 12*sizeof *f);
	memset(insn, 0, 71*sizeof *insn);
	memset(k, 0, 11*sizeof *k);
	k[0].kind = Kint;
	k[0].ival = 42;
	k[1].kind = Katom;
	k[1].text = "boom";
	k[2].kind = Kfunc;
	k[2].text = "child";
	k[3].kind = Katom;
	k[3].text = "true";
	k[4].kind = Katom;
	k[4].text = "take";
	k[5].kind = Kint;
	k[5].ival = 5;
	k[6].kind = Katom;
	k[6].text = "timedout";
	k[7].kind = Katom;
	k[7].text = "infinity";
	k[8].kind = Kint;
	k[8].ival = 2;
	k[9].kind = Katom;
	k[9].text = "soon";
	k[10].kind = Kint;
	k[10].ival = 0;
	f[0].name = "loop";
	f[0].nreg = 1;
	f[0].ninsn = 1;
	f[0].insn = &insn[0];
	insn[0].op = Ojump;
	insn[0].a = 0;
	f[1].name = "done";
	f[1].nreg = 2;
	f[1].ninsn = 2;
	f[1].insn = &insn[1];
	insn[1].op = Oloadk;
	insn[1].a = 1;
	insn[1].b = 0;
	insn[2].op = Oreturn;
	insn[2].a = 1;
	f[2].name = "boom";
	f[2].nreg = 1;
	f[2].ninsn = 1;
	f[2].insn = &insn[3];
	insn[3].op = Ofail;
	insn[3].a = 1;
	f[3].name = "process";
	f[3].nreg = 5;
	f[3].ninsn = 5;
	f[3].insn = &insn[4];
	insn[4].op = Oself;
	insn[4].a = 1;
	insn[5].op = Omakeref;
	insn[5].a = 2;
	insn[6].op = Ospawn;
	insn[6].a = 3;
	insn[6].b = 2;
	insn[6].c = 0;
	insn[7].op = Osend;
	insn[7].a = 4;
	insn[7].b = 3;
	insn[7].c = 2;
	insn[8].op = Ojump;
	insn[8].a = 4;
	f[4].name = "child";
	f[4].nreg = 1;
	f[4].ninsn = 1;
	f[4].insn = &insn[9];
	insn[9].op = Oreturn;
	insn[9].a = 0;
	f[5].name = "receiver";
	f[5].nreg = 3;
	f[5].ninsn = 8;
	f[5].insn = &insn[10];
	insn[10].op = Orecvbegin;
	insn[10].a = 1;
	insn[10].b = 2;
	insn[11].op = Otestatom;
	insn[11].a = 2;
	insn[11].b = 3;
	insn[11].c = 7;
	insn[12].op = Otestatom;
	insn[12].a = 1;
	insn[12].b = 4;
	insn[12].c = 5;
	insn[13].op = Orecvtake;
	insn[14].op = Oreturn;
	insn[14].a = 1;
	insn[15].op = Orecvnext;
	insn[15].a = 1;
	insn[15].b = 2;
	insn[16].op = Ojump;
	insn[16].a = 1;
	insn[17].op = Orecvwait;
	insn[17].a = 0;
	f[6].name = "exiter";
	f[6].nreg = 2;
	f[6].ninsn = 2;
	f[6].insn = &insn[18];
	insn[18].op = Oloadk;
	insn[18].a = 1;
	insn[18].b = 1;
	insn[19].op = Oexit;
	insn[19].a = 1;
	/*
	 * D051 fixture: receive { 'take => return 'take; after 5 => return
	 * 'timedout; }. Local pc within this function: 0 loadk dur=5,
	 * 1 recvdeadline, 2 recvbegin (begin), 3 testatom-found (check,
	 * fails to 9), 4 testatom-candidate=='take (fails to 7), 5 recvtake,
	 * 6 return candidate, 7 recvnext (advance), 8 jump back to check,
	 * 9 recvwaitdeadline retry=begin (two successors: blocks/rescans at
	 * 2, or falls through here when expired), 10 loadk 'timedout,
	 * 11 return.
	 */
	f[7].name = "timedreceiver";
	f[7].nreg = 4;
	f[7].ninsn = 12;
	f[7].insn = &insn[20];
	insn[20].op = Oloadk;
	insn[20].a = 3;
	insn[20].b = 5;
	insn[21].op = Orecvdeadline;
	insn[21].a = 3;
	insn[22].op = Orecvbegin;
	insn[22].a = 1;
	insn[22].b = 2;
	insn[23].op = Otestatom;
	insn[23].a = 2;
	insn[23].b = 3;
	insn[23].c = 9;
	insn[24].op = Otestatom;
	insn[24].a = 1;
	insn[24].b = 4;
	insn[24].c = 7;
	insn[25].op = Orecvtake;
	insn[26].op = Oreturn;
	insn[26].a = 1;
	insn[27].op = Orecvnext;
	insn[27].a = 1;
	insn[27].b = 2;
	insn[28].op = Ojump;
	insn[28].a = 3;
	insn[29].op = Orecvwaitdeadline;
	insn[29].a = 2;
	insn[30].op = Oloadk;
	insn[30].a = 1;
	insn[30].b = 6;
	insn[31].op = Oreturn;
	insn[31].a = 1;
	/*
	 * Three more copies of the same 12-instruction shape as
	 * timedreceiver (local pc layout identical: begin=2, check fail
	 * target=9, pattern fail target=7, advance jump target=3, retry
	 * target=2), differing only in which constant local pc 0 loads as
	 * the duration: 'infinity, a shorter integer, or zero.
	 */
	f[8].name = "infinityreceiver";
	f[8].nreg = 4;
	f[8].ninsn = 12;
	f[8].insn = &insn[32];
	insn[32].op = Oloadk; insn[32].a = 3; insn[32].b = 7;
	insn[33].op = Orecvdeadline; insn[33].a = 3;
	insn[34].op = Orecvbegin; insn[34].a = 1; insn[34].b = 2;
	insn[35].op = Otestatom; insn[35].a = 2; insn[35].b = 3; insn[35].c = 9;
	insn[36].op = Otestatom; insn[36].a = 1; insn[36].b = 4; insn[36].c = 7;
	insn[37].op = Orecvtake;
	insn[38].op = Oreturn; insn[38].a = 1;
	insn[39].op = Orecvnext; insn[39].a = 1; insn[39].b = 2;
	insn[40].op = Ojump; insn[40].a = 3;
	insn[41].op = Orecvwaitdeadline; insn[41].a = 2;
	insn[42].op = Oloadk; insn[42].a = 1; insn[42].b = 6;
	insn[43].op = Oreturn; insn[43].a = 1;

	f[9].name = "shortreceiver";
	f[9].nreg = 4;
	f[9].ninsn = 12;
	f[9].insn = &insn[44];
	insn[44].op = Oloadk; insn[44].a = 3; insn[44].b = 8;
	insn[45].op = Orecvdeadline; insn[45].a = 3;
	insn[46].op = Orecvbegin; insn[46].a = 1; insn[46].b = 2;
	insn[47].op = Otestatom; insn[47].a = 2; insn[47].b = 3; insn[47].c = 9;
	insn[48].op = Otestatom; insn[48].a = 1; insn[48].b = 4; insn[48].c = 7;
	insn[49].op = Orecvtake;
	insn[50].op = Oreturn; insn[50].a = 1;
	insn[51].op = Orecvnext; insn[51].a = 1; insn[51].b = 2;
	insn[52].op = Ojump; insn[52].a = 3;
	insn[53].op = Orecvwaitdeadline; insn[53].a = 2;
	insn[54].op = Oloadk; insn[54].a = 1; insn[54].b = 6;
	insn[55].op = Oreturn; insn[55].a = 1;

	/* A duration that resolves to neither an integer nor 'infinity faults bad_timeout at recvdeadline itself. */
	f[10].name = "badtimeoutreceiver";
	f[10].nreg = 2;
	f[10].ninsn = 3;
	f[10].insn = &insn[56];
	insn[56].op = Oloadk; insn[56].a = 1; insn[56].b = 9;
	insn[57].op = Orecvdeadline; insn[57].a = 1;
	insn[58].op = Oreturn; insn[58].a = 1;

	f[11].name = "zeroreceiver";
	f[11].nreg = 4;
	f[11].ninsn = 12;
	f[11].insn = &insn[59];
	insn[59].op = Oloadk; insn[59].a = 3; insn[59].b = 10;
	insn[60].op = Orecvdeadline; insn[60].a = 3;
	insn[61].op = Orecvbegin; insn[61].a = 1; insn[61].b = 2;
	insn[62].op = Otestatom; insn[62].a = 2; insn[62].b = 3; insn[62].c = 9;
	insn[63].op = Otestatom; insn[63].a = 1; insn[63].b = 4; insn[63].c = 7;
	insn[64].op = Orecvtake;
	insn[65].op = Oreturn; insn[65].a = 1;
	insn[66].op = Orecvnext; insn[66].a = 1; insn[66].b = 2;
	insn[67].op = Ojump; insn[67].a = 3;
	insn[68].op = Orecvwaitdeadline; insn[68].a = 2;
	insn[69].op = Oloadk; insn[69].a = 1; insn[69].b = 6;
	insn[70].op = Oreturn; insn[70].a = 1;

	m->nconst = 11;
	m->konst = k;
	m->nfunc = 12;
	m->func = f;
}

typedef struct Testclock Testclock;
struct Testclock {
	uvlong now;
};

static uvlong
tcnow(void *aux)
{
	return ((Testclock*)aux)->now;
}

static void
tcwait(void *aux, uvlong deadline)
{
	Testclock *tc;

	tc = aux;
	if(deadline > tc->now)
		tc->now = deadline;
}

void
main(void)
{
	NvModule module;
	NvFunc func[12];
	NvInsn insn[71];
	NvConst konst[11];
	NvLimits limits;
	NvScheduler sched;
	NvValue arg, p1, p2, msg, pid[8];
	NvExec *e1, *e2;
	NvProcess *parent, *child;
	Testclock tc;
	NvClock clock;
	char err[128];
	int i, state;
	ulong slot;

	makemodule(&module, func, insn, konst);
	check(nvvaluetuple(&arg, nil, 0) == 0, "argument tuple");
	limits.maxprocess = 16;
	limits.maxmailbox = 4096;
	limits.maxmessage = 1024;
	limits.maxframe = 64;
	limits.maxtermdepth = NvMaxtermdepth;

	insn[0].a = 99;
	check(nvschedinit(&sched, &module, &limits, 1, 1, err, sizeof err) < 0, "scheduler rejects invalid module");
	insn[0].a = 0;
	check(nvschedinit(&sched, &module, &limits, 1, 4, err, sizeof err) == 0, err);
	check(nvschedspawn(&sched, "process", &arg, &p1, err, sizeof err) == 0, "spawn process-op parent");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress, "run process operations");
	parent = &sched.runtime.process[p1.pid.slot];
	e1 = parent->exec;
	check(e1->frame->reg[1].kind == Vpid && nvvalueequal(&e1->frame->reg[1], &p1), "self returns current pid");
	check(e1->frame->reg[2].kind == Vref && e1->frame->reg[3].kind == Vpid, "make_ref and spawn return opaque values");
	check(nvvalueequal(&e1->frame->reg[2], &e1->frame->reg[4]), "send returns sent value");
	child = &sched.runtime.process[e1->frame->reg[3].pid.slot];
	/* D059: the child was enqueued at spawn, the parent re-enqueued behind it when its quantum ended. */
	check(nvprocrunhead(&sched.runtime, &slot) && slot == e1->frame->reg[3].pid.slot, "spawned child is next in dispatch order");
	check(sched.runtime.nrunnable == 2 && sched.runtime.runtail == p1.pid.slot, "yielded parent is queued behind its child");
	check(child->head != nil && child->head->value.kind == Vref && nvvalueequal(&child->head->value, &e1->frame->reg[2]), "send copies value to spawned child mailbox");
	nvschedfree(&sched);
	nvvaluefree(&p1);
	print("ok - process opcodes use scheduler host context\n");

	check(nvschedinit(&sched, &module, &limits, 4, 20, err, sizeof err) == 0, err);
	check(nvschedspawn(&sched, "receiver", &arg, &p1, err, sizeof err) == 0, "spawn receiver");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress && sched.runtime.process[p1.pid.slot].state == Prwaiting, "empty receive blocks");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedIdle, "waiting receiver makes scheduler idle");
	memset(&msg, 0, sizeof msg);
	msg.valid = 1;
	msg.kind = Vint;
	msg.i = 7;
	check(nvprocsend(&sched.runtime, &p1, &msg, err, sizeof err) == 1, "wake with unmatched message");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress && sched.runtime.process[p1.pid.slot].state == Prwaiting, "unmatched message rescans then blocks");
	check(sched.runtime.process[p1.pid.slot].head != nil && sched.runtime.process[p1.pid.slot].head->value.kind == Vint, "unmatched message remains queued");
	check(nvvalueatom(&msg, "take") == 0, "matching message");
	check(nvprocsend(&sched.runtime, &p1, &msg, err, sizeof err) == 1, "wake with matching message");
	nvvaluefree(&msg);
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress && sched.completed == 1, "matching receive takes message and completes");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedDone, "receiver scheduler done");
	nvschedfree(&sched);
	nvvaluefree(&p1);
	print("ok - receive bytecode blocks, rescans, preserves, and takes\n");

	for(i = 1; i <= 4; i++){
		check(nvschedinit(&sched, &module, &limits, 10+i, i, err, sizeof err) == 0, err);
		check(nvschedspawn(&sched, "receiver", &arg, &p1, err, sizeof err) == 0, "spawn quantum-sweep receiver");
		check(nvvalueatom(&msg, "take") == 0, "quantum-sweep message");
		check(nvprocsend(&sched.runtime, &p1, &msg, err, sizeof err) == 1, "queue quantum-sweep message");
		nvvaluefree(&msg);
		state = NvSchedProgress;
		while(state == NvSchedProgress)
			state = nvschedstep(&sched, err, sizeof err);
		check(state == NvSchedDone && sched.completed == 1, "receive result depends on quantum");
		nvschedfree(&sched);
		nvvaluefree(&p1);
	}
	print("ok - receive outcome is independent of quanta one through four\n");

	check(nvschedinit(&sched, &module, &limits, 9, 1, err, sizeof err) == 0, err);
	check(nvschedspawn(&sched, "receiver", &arg, &p1, err, sizeof err) == 0, "spawn quantum-one receiver");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress, "suspend exhausted scan before wait");
	check(nvvalueatom(&msg, "take") == 0, "quantum-boundary message");
	check(nvprocsend(&sched.runtime, &p1, &msg, err, sizeof err) == 1, "send during suspended scan");
	nvvaluefree(&msg);
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress, "advance suspended scan to wait");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress && sched.runtime.process[p1.pid.slot].state == Prrunnable, "message appended after exhaustion prevents sleep");
	state = NvSchedProgress;
	for(i = 0; i < 8 && state == NvSchedProgress; i++)
		state = nvschedstep(&sched, err, sizeof err);
	check(state == NvSchedDone && sched.completed == 1, "quantum-one receiver consumes appended message");
	nvschedfree(&sched);
	nvvaluefree(&p1);
	print("ok - receive wakeup is safe across quantum boundary\n");

	check(nvschedinit(&sched, &module, &limits, 5, 10, err, sizeof err) == 0, err);
	check(nvschedspawn(&sched, "exiter", &arg, &p1, err, sizeof err) == 0, "spawn explicit exiter");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress && sched.exited == 1 && sched.lastexit.kind == Vatom && strcmp(sched.lastexit.atom, "boom") == 0, "explicit exit preserves term reason");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedDone, "explicit exit scheduler done");
	nvschedfree(&sched);
	nvvaluefree(&p1);
	print("ok - explicit exit transfers arbitrary term reason\n");

	check(nvschedinit(&sched, &module, &limits, 6, 10, err, sizeof err) == 0, err);
	check(nvschedspawnroot(&sched, "done", &arg, &p1, err, sizeof err) == 0, "spawn root");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress && sched.rootstate == NvRootDone && sched.rootvalue.kind == Vint && sched.rootvalue.i == 42, "root return value retained after reap");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedDone, "root return scheduler done");
	nvschedfree(&sched);
	nvvaluefree(&p1);
	check(nvschedinit(&sched, &module, &limits, 7, 10, err, sizeof err) == 0, err);
	check(nvschedspawnroot(&sched, "boom", &arg, &p1, err, sizeof err) == 0, "spawn faulting root");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress && sched.rootstate == NvRootFault && strcmp(sched.rootfault, "boom") == 0, "root fault retained after reap");
	nvschedfree(&sched);
	nvvaluefree(&p1);
	check(nvschedinit(&sched, &module, &limits, 8, 10, err, sizeof err) == 0, err);
	check(nvschedspawnroot(&sched, "exiter", &arg, &p1, err, sizeof err) == 0, "spawn exiting root");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress && sched.rootstate == NvRootExit && sched.rootvalue.kind == Vatom && strcmp(sched.rootvalue.atom, "boom") == 0, "root exit reason retained after reap");
	nvschedfree(&sched);
	nvvaluefree(&p1);
	print("ok - root outcomes survive process reaping\n");

	check(nvschedinit(&sched, &module, &limits, 1, 1, err, sizeof err) == 0, err);
	check(nvschedstep(&sched, err, sizeof err) == NvSchedDone, "empty scheduler is done");
	check(nvschedspawn(&sched, "loop", &arg, &p1, err, sizeof err) == 0, "spawn loop one");
	check(nvschedspawn(&sched, "loop", &arg, &p2, err, sizeof err) == 0, "spawn loop two");
	e1 = sched.runtime.process[p1.pid.slot].exec;
	e2 = sched.runtime.process[p2.pid.slot].exec;
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress && e1->reductions == 1 && e2->reductions == 0, "first process receives first quantum");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress && e1->reductions == 1 && e2->reductions == 1, "second process receives second quantum");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress && e1->reductions == 2 && e2->reductions == 1, "round robin returns to first process");
	check(sched.runtime.process[p1.pid.slot].state == Prrunnable && sched.runtime.process[p2.pid.slot].state == Prrunnable, "yielded processes remain runnable");
	nvschedfree(&sched);
	nvvaluefree(&p1);
	nvvaluefree(&p2);
	print("ok - deterministic round-robin quanta\n");

	check(nvschedinit(&sched, &module, &limits, 2, 1, err, sizeof err) == 0, err);
	check(nvschedspawn(&sched, "done", &arg, &p1, err, sizeof err) == 0, "spawn finite process");
	check(nvschedspawn(&sched, "boom", &arg, &p2, err, sizeof err) == 0, "spawn faulting process");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress, "finite first quantum");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress, "fault process quantum");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress, "finite completion quantum");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedDone, "scheduler completes after exits");
	check(sched.completed == 1 && sched.faulted == 1 && strcmp(sched.lastfault, "boom") == 0 && sched.dispatches == 3, "completion and fault accounting");
	nvschedfree(&sched);
	nvvaluefree(&p1);
	nvvaluefree(&p2);
	print("ok - completion and faults exit processes\n");

	check(nvschedinit(&sched, &module, &limits, 3, 1, err, sizeof err) == 0, err);
	for(i = 0; i < nelem(pid); i++)
		check(nvschedspawn(&sched, "done", &arg, &pid[i], err, sizeof err) == 0, "spawn progress process");
	state = NvSchedProgress;
	for(i = 0; i < 32 && state == NvSchedProgress; i++)
		state = nvschedstep(&sched, err, sizeof err);
	check(state == NvSchedDone && sched.completed == nelem(pid) && sched.dispatches == 2*nelem(pid), "many finite processes make progress");
	for(i = 0; i < nelem(pid); i++)
		nvvaluefree(&pid[i]);
	nvschedfree(&sched);
	nvvaluefree(&arg);
	print("ok - many scheduled processes make progress\n");

	/* The previous test freed the shared argument tuple; the deadline tests below need it again. */
	check(nvvaluetuple(&arg, nil, 0) == 0, "re-create argument tuple for deadline tests");

	/* D050: an idle scheduler with an armed deadline advances the clock and wakes; no message ever arrives. */
	memset(&tc, 0, sizeof tc);
	tc.now = 1000;
	clock.aux = &tc;
	clock.now = tcnow;
	clock.wait = tcwait;
	check(nvschedinit(&sched, &module, &limits, 20, 10, err, sizeof err) == 0, err);
	nvschedsetclock(&sched, &clock);
	check(nvschedspawnroot(&sched, "timedreceiver", &arg, &p1, err, sizeof err) == 0, "spawn timed root");
	check(nvvalueatom(&msg, "other") == 0, "unrelated unmatched message");
	check(nvprocsend(&sched.runtime, &p1, &msg, err, sizeof err) == 1, "queue an unmatched message before any dispatch");
	nvvaluefree(&msg);
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress && sched.runtime.process[p1.pid.slot].state == Prwaiting, "timed receive arms, rescans past the unmatched message, and blocks");
	check(sched.runtime.process[p1.pid.slot].hasdeadline && sched.runtime.process[p1.pid.slot].deadline == 1005, "deadline armed from installed clock");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress && tc.now == 1005, "idle scheduler advances clock to the earliest deadline instead of polling");
	check(sched.runtime.process[p1.pid.slot].head != nil && sched.runtime.process[p1.pid.slot].head->value.kind == Vatom &&
		strcmp(sched.runtime.process[p1.pid.slot].head->value.atom, "other") == 0,
		"the unmatched message survives the timer wakeup, still queued, right before the timeout body runs");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress && sched.rootstate == NvRootDone &&
		sched.rootvalue.kind == Vatom && strcmp(sched.rootvalue.atom, "timedout") == 0, "expired deadline runs the timeout body");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedDone, "timed root scheduler done");
	nvschedfree(&sched);
	nvvaluefree(&p1);
	print("ok - idle scheduler waits for an armed deadline, preserves an unmatched message, and runs the timeout body\n");

	/* D048: a message that arrives after a purely timer-driven wakeup, but before the rescan, still wins. */
	memset(&tc, 0, sizeof tc);
	tc.now = 2000;
	clock.aux = &tc;
	clock.now = tcnow;
	clock.wait = tcwait;
	check(nvschedinit(&sched, &module, &limits, 21, 1, err, sizeof err) == 0, err);
	nvschedsetclock(&sched, &clock);
	check(nvschedspawnroot(&sched, "timedreceiver", &arg, &p1, err, sizeof err) == 0, "spawn timed root for race test");
	for(i = 0; i < 5; i++)
		check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress, "single-step to blocked timed receive");
	check(sched.runtime.process[p1.pid.slot].state == Prwaiting && tc.now == 2000, "blocked before any clock advance");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress && tc.now == 2005 && sched.runtime.process[p1.pid.slot].state == Prrunnable,
		"idle step wakes the process purely from an expired timer");
	check(nvvalueatom(&msg, "take") == 0, "race message");
	check(nvprocsend(&sched.runtime, &p1, &msg, err, sizeof err) == 1, "message queued after the timer wakeup, before the rescan");
	nvvaluefree(&msg);
	state = NvSchedProgress;
	for(i = 0; i < 8 && state == NvSchedProgress; i++)
		state = nvschedstep(&sched, err, sizeof err);
	check(state == NvSchedDone && sched.rootstate == NvRootDone &&
		sched.rootvalue.kind == Vatom && strcmp(sched.rootvalue.atom, "take") == 0,
		"a message beats an already-expired deadline even when the wakeup was purely timer-driven");
	nvschedfree(&sched);
	nvvaluefree(&p1);
	print("ok - a message queued after a timer wakeup still wins over the timeout body\n");

	/* D050: equal deadlines are woken together by one idle advance, not one at a time. */
	memset(&tc, 0, sizeof tc);
	tc.now = 3000;
	clock.aux = &tc;
	clock.now = tcnow;
	clock.wait = tcwait;
	check(nvschedinit(&sched, &module, &limits, 22, 10, err, sizeof err) == 0, err);
	nvschedsetclock(&sched, &clock);
	check(nvschedspawn(&sched, "timedreceiver", &arg, &p1, err, sizeof err) == 0, "spawn tie-break process one");
	check(nvschedspawn(&sched, "timedreceiver", &arg, &p2, err, sizeof err) == 0, "spawn tie-break process two");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress && sched.runtime.process[p1.pid.slot].state == Prwaiting, "process one blocks");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress && sched.runtime.process[p2.pid.slot].state == Prwaiting, "process two blocks with the same deadline");
	check(sched.runtime.process[p1.pid.slot].deadline == sched.runtime.process[p2.pid.slot].deadline, "both armed the same absolute deadline");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress &&
		sched.runtime.process[p1.pid.slot].state == Prrunnable && sched.runtime.process[p2.pid.slot].state == Prrunnable,
		"one idle advance wakes both tied deadlines together");
	state = NvSchedProgress;
	for(i = 0; i < 16 && state == NvSchedProgress; i++)
		state = nvschedstep(&sched, err, sizeof err);
	check(state == NvSchedDone && sched.completed == 2, "both tied timed receivers complete");
	nvschedfree(&sched);
	nvvaluefree(&p1);
	nvvaluefree(&p2);
	print("ok - equal deadlines wake together instead of one idle step at a time\n");

	/* D048: `after 0` performs exactly one nonblocking scan; an empty mailbox never blocks at all. */
	memset(&tc, 0, sizeof tc);
	tc.now = 4000;
	clock.aux = &tc;
	clock.now = tcnow;
	clock.wait = tcwait;
	check(nvschedinit(&sched, &module, &limits, 23, 10, err, sizeof err) == 0, err);
	nvschedsetclock(&sched, &clock);
	check(nvschedspawnroot(&sched, "zeroreceiver", &arg, &p1, err, sizeof err) == 0, "spawn zero-timeout root");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress && sched.rootstate == NvRootDone &&
		sched.rootvalue.kind == Vatom && strcmp(sched.rootvalue.atom, "timedout") == 0 && tc.now == 4000,
		"a zero duration times out immediately without ever blocking or advancing the clock");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedDone, "zero-timeout scheduler done");
	nvschedfree(&sched);
	nvvaluefree(&p1);
	print("ok - a zero duration performs exactly one nonblocking scan\n");

	/* D049/D046: `'infinity` arms nothing, so blocking is ordinary deadlock, not a phantom timer wakeup. */
	memset(&tc, 0, sizeof tc);
	tc.now = 5000;
	clock.aux = &tc;
	clock.now = tcnow;
	clock.wait = tcwait;
	check(nvschedinit(&sched, &module, &limits, 24, 10, err, sizeof err) == 0, err);
	nvschedsetclock(&sched, &clock);
	check(nvschedspawn(&sched, "infinityreceiver", &arg, &p1, err, sizeof err) == 0, "spawn infinity receiver");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress && sched.runtime.process[p1.pid.slot].state == Prwaiting &&
		!sched.runtime.process[p1.pid.slot].hasdeadline, "infinity blocks without arming a deadline");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedIdle && tc.now == 5000,
		"an infinitely waiting process is ordinary deadlock, not a phantom timer wakeup");
	check(nvvalueatom(&msg, "take") == 0, "infinity wake message");
	check(nvprocsend(&sched.runtime, &p1, &msg, err, sizeof err) == 1, "wake the infinitely waiting receiver");
	nvvaluefree(&msg);
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress && sched.completed == 1, "infinity receiver still answers an actual message");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedDone, "infinity scheduler done");
	nvschedfree(&sched);
	nvvaluefree(&p1);
	print("ok - 'infinity blocks without arming a deadline and is ordinary deadlock, not a timer\n");

	/* D050: differing deadlines wake in order -- the earlier one alone, while the later one keeps waiting. */
	memset(&tc, 0, sizeof tc);
	tc.now = 6000;
	clock.aux = &tc;
	clock.now = tcnow;
	clock.wait = tcwait;
	check(nvschedinit(&sched, &module, &limits, 25, 10, err, sizeof err) == 0, err);
	nvschedsetclock(&sched, &clock);
	check(nvschedspawn(&sched, "timedreceiver", &arg, &p1, err, sizeof err) == 0, "spawn longer-duration receiver");
	check(nvschedspawn(&sched, "shortreceiver", &arg, &p2, err, sizeof err) == 0, "spawn shorter-duration receiver");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress && sched.runtime.process[p1.pid.slot].state == Prwaiting, "longer-duration receiver blocks");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress && sched.runtime.process[p2.pid.slot].state == Prwaiting, "shorter-duration receiver blocks");
	check(sched.runtime.process[p1.pid.slot].deadline == 6005 && sched.runtime.process[p2.pid.slot].deadline == 6002,
		"different durations from the same installed now arm different absolute deadlines");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress && tc.now == 6002 &&
		sched.runtime.process[p2.pid.slot].state == Prrunnable && sched.runtime.process[p1.pid.slot].state == Prwaiting,
		"idle advance wakes only the earlier deadline, leaving the later one still waiting");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress && sched.completed == 1, "shorter-duration receiver completes first");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress && tc.now == 6005 && sched.runtime.process[p1.pid.slot].state == Prrunnable,
		"a second idle advance wakes the remaining later deadline");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress && sched.completed == 2, "longer-duration receiver completes after its own deadline");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedDone, "differing-deadline scheduler done");
	nvschedfree(&sched);
	nvvaluefree(&p1);
	nvvaluefree(&p2);
	print("ok - differing deadlines wake in earliest-first order rather than together\n");

	/* A duration that resolves to neither an integer nor 'infinity faults bad_timeout through the full execute path. */
	check(nvschedinit(&sched, &module, &limits, 26, 10, err, sizeof err) == 0, err);
	check(nvschedspawn(&sched, "badtimeoutreceiver", &arg, &p1, err, sizeof err) == 0, "spawn bad-duration receiver");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress && sched.faulted == 1 && strcmp(sched.lastfault, "bad_timeout") == 0,
		"a non-'infinity, non-integer duration faults bad_timeout through recvdeadline dispatch");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedDone, "bad-duration scheduler done");
	nvschedfree(&sched);
	nvvaluefree(&p1);
	print("ok - a runtime duration outside the accepted domain faults bad_timeout\n");

	nvvaluefree(&arg);
	print("all scheduler tests passed\n");
	exits(nil);
}
