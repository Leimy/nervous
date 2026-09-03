#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../../include/nervous.h"
#include "../../include/nvbc.h"
#include "../../include/nvvm.h"
#include "../../include/nvcompile.h"
#include "../../include/nvexec.h"
#include "../../include/nvproc.h"
#include "../../include/nvsched.h"

/*
 * R2 formal-gate regressions (docs/review-findings.md, R2-F15 and the
 * NEEDS-TEST items closed alongside it). Every fixture here is real Nervous
 * source compiled through the full frontend (parseprogram -> nvcompile),
 * not hand-assembled NvInsn arrays, so there is no risk of miscounting
 * operands or instruction offsets the way a schedtest.c-style literal
 * bytecode array would carry.
 */

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

static NvModule *
compilesrc(char *name, char *src)
{
	Parser p;
	Program *pr;
	NvModule *m;
	char err[256];

	pr = parseprogram(&p, name, src, strlen(src));
	if(pr == nil){
		fprint(2, "FAIL: %s: parse: %s\n", name, p.err);
		exits("test");
	}
	m = nvcompile(pr, err, sizeof err);
	programfree(pr);
	if(m == nil){
		fprint(2, "FAIL: %s: compile: %s\n", name, err);
		exits("test");
	}
	return m;
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

/*
 * R2-F15: a timed receive that matches a message on its very first scan
 * never calls recvwait/recvwaitdeadline, so hasdeadline/deadline stay
 * armed (never consumed) all the way to process exit. Before the fix,
 * nvprocexit and nvprocspawn's slot-reuse path left those fields stale on
 * the dead slot; a later process reusing that slot would inherit them.
 */
static void
h1regression(void)
{
	char *src =
		"fn timedreceiver() {\n"
		"	receive {\n"
		"		'take => 'take;\n"
		"		after 1000000000 => 'timedout;\n"
		"	}\n"
		"}\n"
		"fn done() {\n"
		"	42\n"
		"}\n";
	NvModule *m;
	NvLimits limits;
	NvScheduler sched;
	NvValue arg, pid1, pid2, msg;
	Testclock tc;
	NvClock clock;
	char err[256];
	int state;
	ulong slot;

	m = compilesrc("h1regression", src);

	limits.maxprocess = 4;
	limits.maxmailbox = 4096;
	limits.maxmessage = 1024;
	limits.maxframe = 64;
	limits.maxtermdepth = NvMaxtermdepth;
	limits.maxduration = NvMaxduration;

	memset(&tc, 0, sizeof tc);
	tc.now = 1000;
	clock.aux = &tc; clock.now = tcnow; clock.wait = tcwait;

	check(nvvaluetuple(&arg, nil, 0) == 0, "h1: argument tuple");
	check(nvschedinit(&sched, m, &limits, 40, 1000, err, sizeof err) == 0, err);
	nvschedsetclock(&sched, &clock);
	check(nvschedspawn(&sched, "timedreceiver", &arg, &pid1, err, sizeof err) == 0, "h1: spawn timedreceiver");
	slot = pid1.pid.slot;
	check(nvvalueatom(&msg, "take") == 0, "h1: take atom");
	check(nvprocsend(&sched.runtime, &pid1, &msg, err, sizeof err) == 1, "h1: queue take before dispatch");
	nvvaluefree(&msg);

	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress && sched.completed == 1,
		"h1: receive matches immediately and completes without ever blocking");
	check(sched.runtime.process[slot].state == Prexited, "h1: slot exited");
	check(sched.runtime.process[slot].hasdeadline == 0 && sched.runtime.process[slot].deadline == 0,
		"h1: nvprocexit cleared the armed-but-never-consumed deadline");

	check(nvschedspawn(&sched, "done", &arg, &pid2, err, sizeof err) == 0, "h1: spawn done into reused slot");
	check(pid2.pid.slot == slot, "h1: confirmed slot reuse");
	check(sched.runtime.process[slot].hasdeadline == 0 && sched.runtime.process[slot].deadline == 0,
		"h1: reused slot does not inherit a stale deadline");
	state = NvSchedProgress;
	while(state == NvSchedProgress)
		state = nvschedstep(&sched, err, sizeof err);
	check(state == NvSchedDone, "h1: scheduler reaches done");
	check(sched.completed == 2, "h1: reused-slot process also completes");

	nvschedfree(&sched);
	nvvaluefree(&pid1);
	nvvaluefree(&pid2);
	nvvaluefree(&arg);
	nvmodulefree(m);
	print("ok - R2-F15: slot reuse and exit both clear a stale armed deadline\n");
}

/*
 * The other closed NEEDS-TEST from the timers/lifecycle lenses: a stale
 * deadline left by a matched timed receive must not leak into a later
 * plain receive in the same process. nvprocrecvwait already clears
 * hasdeadline unconditionally (pre-existing code); this exercises that
 * path end to end rather than only by inspection.
 */
static void
crossreceiveleak(void)
{
	char *src =
		"fn timedthenplain() {\n"
		"	receive {\n"
		"		'take => receive {\n"
		"			'done => 'ok;\n"
		"		}\n"
		"		after 1000000000 => 'timedout;\n"
		"	}\n"
		"}\n";
	NvModule *m;
	NvLimits limits;
	NvScheduler sched;
	NvValue arg, pid, msg;
	Testclock tc;
	NvClock clock;
	char err[256];
	int state;
	ulong slot;

	m = compilesrc("crossreceiveleak", src);

	limits.maxprocess = 2;
	limits.maxmailbox = 4096;
	limits.maxmessage = 1024;
	limits.maxframe = 64;
	limits.maxtermdepth = NvMaxtermdepth;
	limits.maxduration = NvMaxduration;

	memset(&tc, 0, sizeof tc);
	tc.now = 2000;
	clock.aux = &tc; clock.now = tcnow; clock.wait = tcwait;

	check(nvvaluetuple(&arg, nil, 0) == 0, "cross: argument tuple");
	check(nvschedinit(&sched, m, &limits, 41, 1000, err, sizeof err) == 0, err);
	nvschedsetclock(&sched, &clock);
	check(nvschedspawn(&sched, "timedthenplain", &arg, &pid, err, sizeof err) == 0, "cross: spawn");
	slot = pid.pid.slot;
	check(nvvalueatom(&msg, "take") == 0, "cross: take atom");
	check(nvprocsend(&sched.runtime, &pid, &msg, err, sizeof err) == 1, "cross: queue take before dispatch");
	nvvaluefree(&msg);

	/*
	 * One step: the outer receive arms a (never-expiring) deadline, finds
	 * 'take immediately, takes it, and enters the nested plain receive,
	 * which finds nothing, exhausts its scan, and blocks -- all within
	 * this single quantum, since the quantum (1000) is far larger than
	 * the handful of instructions this requires.
	 */
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress &&
		sched.runtime.process[slot].state == Prwaiting,
		"cross: outer receive matches, inner plain receive blocks");
	check(sched.runtime.process[slot].hasdeadline == 0,
		"cross: nvprocrecvwait cleared the stale deadline left by the matched outer receive");

	/* With hasdeadline correctly cleared, an idle step reports deadlock, not a phantom timer. */
	check(nvschedstep(&sched, err, sizeof err) == NvSchedIdle,
		"cross: no armed deadline is tracked for this process; idle step reports plain deadlock");
	check(tc.now == 2000, "cross: idle step did not advance the clock");

	check(nvvalueatom(&msg, "done") == 0, "cross: done atom");
	check(nvprocsend(&sched.runtime, &pid, &msg, err, sizeof err) == 1, "cross: wake the inner receive");
	nvvaluefree(&msg);
	state = NvSchedProgress;
	while(state == NvSchedProgress)
		state = nvschedstep(&sched, err, sizeof err);
	check(state == NvSchedDone, "cross: scheduler reaches done");
	check(sched.completed == 1, "cross: fixture completes");

	nvschedfree(&sched);
	nvvaluefree(&pid);
	nvvaluefree(&arg);
	nvmodulefree(m);
	print("ok - R2: a stale deadline from a matched timed receive is cleared before a later plain receive blocks\n");
}

/*
 * Mailbox lens Q1 NEEDS-TEST: a message appended to the tail while a scan
 * cursor is mid-flight (sitting on an earlier, not-yet-exhausted candidate)
 * must be found by that same scan's recvnext, not lost or reordered. This
 * is distinct from R2-F05, which covers only the post-exhaustion race.
 */
static void
midscanappend(void)
{
	char *src =
		"fn scanner() {\n"
		"	receive {\n"
		"		'c => 'matched;\n"
		"	}\n"
		"}\n";
	NvModule *m;
	NvLimits limits;
	NvScheduler sched;
	NvValue arg, pid, msg;
	char err[256];
	int state, i;
	ulong slot;

	m = compilesrc("midscanappend", src);

	limits.maxprocess = 2;
	limits.maxmailbox = 4096;
	limits.maxmessage = 1024;
	limits.maxframe = 64;
	limits.maxtermdepth = NvMaxtermdepth;
	limits.maxduration = NvMaxduration;

	check(nvvaluetuple(&arg, nil, 0) == 0, "midscan: argument tuple");
	check(nvschedinit(&sched, m, &limits, 42, 1, err, sizeof err) == 0, err);
	check(nvschedspawnroot(&sched, "scanner", &arg, &pid, err, sizeof err) == 0, "midscan: spawn root");
	slot = pid.pid.slot;
	check(nvvalueatom(&msg, "a") == 0, "midscan: pre-queued non-matching message");
	check(nvprocsend(&sched.runtime, &pid, &msg, err, sizeof err) == 1, "midscan: queue 'a before dispatch");
	nvvaluefree(&msg);

	/*
	 * Quantum 1: each dispatch executes exactly one instruction. The
	 * function's own head test (D058: `fn scanner()` is a clause with an
	 * empty head, so `testarity` precedes the receive) runs first, then
	 * recvbegin leaves the scan cursor sitting on the 'a candidate,
	 * neither matched nor exhausted. Step one instruction at a time
	 * until that point, checking the process never blocks on the way.
	 */
	for(i = 0; i < 4 && !sched.runtime.process[slot].scanning; i++)
		check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress &&
			sched.runtime.process[slot].state != Prwaiting,
			"midscan: single-stepping toward recvbegin");
	check(sched.runtime.process[slot].scanning == 1 &&
		sched.runtime.process[slot].scan != nil,
		"midscan: scan is mid-flight right after recvbegin");
	check(nvvalueatom(&msg, "c") == 0, "midscan: matching message");
	check(nvprocsend(&sched.runtime, &pid, &msg, err, sizeof err) == 1,
		"midscan: append 'c to the tail while the scan cursor still sits on 'a");
	nvvaluefree(&msg);

	state = NvSchedProgress;
	while(state == NvSchedProgress)
		state = nvschedstep(&sched, err, sizeof err);
	check(state == NvSchedDone && sched.rootstate == NvRootDone &&
		sched.rootvalue.kind == Vatom && strcmp(sched.rootvalue.atom, "matched") == 0,
		"midscan: the scan matches 'c and returns 'matched -- not lost, not misrouted");

	nvschedfree(&sched);
	nvvaluefree(&pid);
	nvvaluefree(&arg);
	nvmodulefree(m);
	print("ok - R2: a message appended while a scan is mid-flight is found by that scan's recvnext\n");
}

/*
 * Mailbox lens Q3 NEEDS-TEST: a sender that returns immediately after
 * send (the ordinary, non-exit form of "immediate sender exit") must
 * leave the receiver's copied message fully intact after the sender is
 * reaped -- no aliasing into the sender's freed frame/registers.
 */
static void
sendthenexit(void)
{
	char *src =
		"fn sender(target) {\n"
		"	target ! ${'msg, 'payload, 99}\n"
		"}\n"
		"fn receiver() {\n"
		"	receive {\n"
		"		${'msg, tag, n} => ${tag, n};\n"
		"	}\n"
		"}\n";
	NvModule *m;
	NvLimits limits;
	NvScheduler sched;
	NvValue arg, rootpid, senderpid, senderarg;
	char err[256];
	int state;

	m = compilesrc("sendthenexit", src);

	limits.maxprocess = 4;
	limits.maxmailbox = 4096;
	limits.maxmessage = 1024;
	limits.maxframe = 64;
	limits.maxtermdepth = NvMaxtermdepth;
	limits.maxduration = NvMaxduration;

	check(nvvaluetuple(&arg, nil, 0) == 0, "sendexit: empty argument tuple");
	check(nvschedinit(&sched, m, &limits, 43, 1000, err, sizeof err) == 0, err);
	check(nvschedspawnroot(&sched, "receiver", &arg, &rootpid, err, sizeof err) == 0,
		"sendexit: spawn receiver as root");
	check(nvvaluetuple(&senderarg, &rootpid, 1) == 0, "sendexit: sender argument tuple");
	check(nvschedspawn(&sched, "sender", &senderarg, &senderpid, err, sizeof err) == 0,
		"sendexit: spawn sender");
	nvvaluefree(&senderarg);

	/*
	 * Step 1 dispatches the receiver (spawned first, lower slot): it
	 * finds nothing and blocks. Step 2 dispatches the sender: it copies
	 * the message into the receiver's mailbox, wakes the receiver, then
	 * returns normally -- an ordinary function return following send is
	 * exactly "the sender exits immediately after send." nvschedstep
	 * reaps the sender (execfree/mailboxfree) before the receiver ever
	 * runs again.
	 */
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress &&
		sched.runtime.process[rootpid.pid.slot].state == Prwaiting,
		"sendexit: receiver blocks first");
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress && sched.completed == 1,
		"sendexit: sender sends, wakes the receiver, and exits normally");
	check(!nvprocalive(&sched.runtime, &senderpid), "sendexit: sender is fully reaped");

	state = NvSchedProgress;
	while(state == NvSchedProgress)
		state = nvschedstep(&sched, err, sizeof err);
	check(state == NvSchedDone, "sendexit: scheduler reaches done");
	check(sched.rootstate == NvRootDone && sched.rootvalue.kind == Vtuple &&
		sched.rootvalue.tuple->n == 2 &&
		sched.rootvalue.tuple->elem[0].kind == Vatom &&
		strcmp(sched.rootvalue.tuple->elem[0].atom, "payload") == 0 &&
		sched.rootvalue.tuple->elem[1].kind == Vint &&
		sched.rootvalue.tuple->elem[1].i == 99,
		"sendexit: receiver's value is intact after the sender was fully reaped");

	nvschedfree(&sched);
	nvvaluefree(&rootpid);
	nvvaluefree(&senderpid);
	nvvaluefree(&arg);
	nvmodulefree(m);
	print("ok - R2: a copied message survives immediate sender exit and reaping with no aliasing\n");
}

/*
 * Fairness lens: a process spawned into a slot BELOW every other live
 * process (because an earlier, lower-slotted process already exited and
 * freed that slot) must still be dispatched within one full round, and
 * that earlier exit must not disturb the order of processes already
 * queued. Under the original rotating slot scan this was the "spawned
 * below the cursor" case; under the D059 FIFO run queue the property is
 * that slot number never matters at all -- D joins at the tail behind B
 * and C, whatever slot it reuses -- so the same fixture now checks queue
 * order directly instead of a cursor.
 */
static void
belowcursorfairness(void)
{
	char *src =
		"fn quick() {\n"
		"	1\n"
		"}\n"
		"fn idle() {\n"
		"	receive {\n"
		"		'go => 'done;\n"
		"	}\n"
		"}\n";
	NvModule *m;
	NvLimits limits;
	NvScheduler sched;
	NvValue arg, pidA, pidB, pidC, pidD;
	char err[256];
	int i;
	ulong slot;

	m = compilesrc("belowcursorfairness", src);

	limits.maxprocess = 8;
	limits.maxmailbox = 4096;
	limits.maxmessage = 1024;
	limits.maxframe = 64;
	limits.maxtermdepth = NvMaxtermdepth;
	limits.maxduration = NvMaxduration;

	check(nvvaluetuple(&arg, nil, 0) == 0, "belowcursor: argument tuple");
	check(nvschedinit(&sched, m, &limits, 44, 1000, err, sizeof err) == 0, err);

	check(nvschedspawn(&sched, "quick", &arg, &pidA, err, sizeof err) == 0, "belowcursor: spawn A (quick)");
	check(nvschedspawn(&sched, "idle", &arg, &pidB, err, sizeof err) == 0, "belowcursor: spawn B (idle)");
	check(nvschedspawn(&sched, "idle", &arg, &pidC, err, sizeof err) == 0, "belowcursor: spawn C (idle)");

	/* Dispatch A (slot 0, queue head): it returns immediately and is reaped, freeing slot 0. */
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress && sched.completed == 1,
		"belowcursor: A completes and frees slot 0");
	check(nvprocrunhead(&sched.runtime, &slot) && slot == pidB.pid.slot && sched.runtime.nrunnable == 2,
		"belowcursor: B is now at the head with C behind it; A's exit left the queue intact");

	/*
	 * Spawning D now reuses slot 0 -- the lowest free slot, below every
	 * other live process. Slot number must not buy it a place in line.
	 */
	check(nvschedspawn(&sched, "idle", &arg, &pidD, err, sizeof err) == 0, "belowcursor: spawn D (idle)");
	check(pidD.pid.slot == 0 && pidD.pid.slot < pidB.pid.slot, "belowcursor: D reused slot 0, below B and C");
	check(sched.runtime.runtail == pidD.pid.slot && sched.runtime.nrunnable == 3,
		"belowcursor: D joined the tail of the run queue, behind B and C");

	/*
	 * One full round (nslot steps) must dispatch every one of B, C, D at
	 * least once, in FIFO order, regardless of D's low slot number.
	 */
	for(i = 0; i < (int)sched.runtime.nslot; i++)
		check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress, "belowcursor: sweep step");
	check(sched.runtime.process[pidB.pid.slot].exec != nil &&
		sched.runtime.process[pidB.pid.slot].exec->reductions > 0, "belowcursor: B was dispatched");
	check(sched.runtime.process[pidC.pid.slot].exec != nil &&
		sched.runtime.process[pidC.pid.slot].exec->reductions > 0, "belowcursor: C was dispatched");
	check(sched.runtime.process[pidD.pid.slot].exec != nil &&
		sched.runtime.process[pidD.pid.slot].exec->reductions > 0,
		"belowcursor: D was dispatched despite landing below the cursor");
	check(sched.runtime.process[pidB.pid.slot].state == Prwaiting &&
		sched.runtime.process[pidC.pid.slot].state == Prwaiting &&
		sched.runtime.process[pidD.pid.slot].state == Prwaiting,
		"belowcursor: B, C, and D are all blocked on their own receive, none skipped or corrupted");

	nvschedfree(&sched);
	nvvaluefree(&pidA);
	nvvaluefree(&pidB);
	nvvaluefree(&pidC);
	nvvaluefree(&pidD);
	nvvaluefree(&arg);
	nvmodulefree(m);
	print("ok - R2: a process spawned into the lowest free slot joins the run queue tail and is dispatched within one round; an earlier exit does not disturb queue order\n");
}

/*
 * Fairness/limits lens, required-work item 5: a representative slice of
 * the "tiny limits" stress plan, using the smallest legal or
 * near-boundary value for each limit in turn. Not every design item the
 * fairness investigator proposed is implemented here (see
 * milestones/R2-concurrency-review.md's closing note for the rest); this
 * covers the process/mailbox/frame/term-depth boundaries directly.
 */
static void
tinylimits(void)
{
	NvRuntime r;
	NvLimits limits;
	NvValue pid1, pid2, pid3, stale, v, got;
	char err[128];
	uvlong bytes;
	int i;

	/* T5-01: maxprocess=2 is an exact boundary, and slot reuse works at the minimum useful limit. */
	limits.maxprocess = 2;
	limits.maxmailbox = 4096;
	limits.maxmessage = 1024;
	limits.maxframe = 64;
	limits.maxtermdepth = NvMaxtermdepth;
	limits.maxduration = NvMaxduration;
	check(nvruntimeinit(&r, &limits, 50, err, sizeof err) == 0, err);
	check(nvprocspawn(&r, &pid1, err, sizeof err) == 0, "tiny: spawn 1 of 2");
	check(nvprocspawn(&r, &pid2, err, sizeof err) == 0, "tiny: spawn 2 of 2 (at the limit)");
	check(nvprocspawn(&r, &pid3, err, sizeof err) < 0 && strcmp(err, "system_limit") == 0,
		"tiny: a third process is rejected exactly at maxprocess=2");
	check(nvvaluecopy(&stale, &pid1) == 0, "tiny: copy pid1 before exit");
	check(nvprocexit(&r, &pid1) == 1, "tiny: exit pid1");
	check(nvprocspawn(&r, &pid3, err, sizeof err) == 0, "tiny: spawn after exit reuses the freed slot");
	check(pid3.pid.slot == stale.pid.slot && pid3.pid.generation == stale.pid.generation+1,
		"tiny: reused slot, incremented generation");
	check(!nvprocalive(&r, &stale), "tiny: stale pid is not alive after reuse");
	nvvaluefree(&stale);
	nvvaluefree(&pid1);
	nvvaluefree(&pid2);
	nvvaluefree(&pid3);
	nvruntimefree(&r);
	print("ok - tiny: maxprocess=2 boundary and slot reuse\n");

	/* T5-02: mailbox exactly one message wide, cycled 1000 times without drift. */
	bytes = sizeof(NvValue);
	limits.maxprocess = 1;
	limits.maxmailbox = bytes;
	limits.maxmessage = bytes;
	limits.maxframe = 64;
	limits.maxtermdepth = NvMaxtermdepth;
	limits.maxduration = NvMaxduration;
	check(nvruntimeinit(&r, &limits, 51, err, sizeof err) == 0, err);
	check(nvprocspawn(&r, &pid1, err, sizeof err) == 0, "tiny: spawn mailbox-boundary process");
	memset(&v, 0, sizeof v);
	v.valid = 1; v.kind = Vint; v.i = 7;
	for(i = 0; i < 1000; i++){
		check(nvprocsend(&r, &pid1, &v, err, sizeof err) == 1, "tiny: send at exact mailbox capacity");
		check(nvprocsend(&r, &pid1, &v, err, sizeof err) < 0 && strcmp(err, "mailbox_full") == 0,
			"tiny: second send rejected while the one slot is full");
		check(nvprocpop(&r, &pid1, &got, err, sizeof err) == 1, "tiny: pop frees the budget");
		nvvaluefree(&got);
		check(r.process[pid1.pid.slot].mailboxbytes == 0,
			"tiny: mailboxbytes returns to exactly zero every cycle, no drift");
	}
	nvvaluefree(&pid1);
	nvruntimefree(&r);
	print("ok - tiny: mailbox exact-boundary accounting is stable across 1000 cycles\n");

	/* T5-04: maxframe=2 allows exactly one call beyond the entry frame. */
	{
		NvModule fm;
		NvFunc ff[1];
		NvInsn finsn[2];
		NvConst fk[1];
		NvExec exec;
		NvValue farg;
		char ferr[128];

		memset(&fm, 0, sizeof fm);
		memset(ff, 0, sizeof ff);
		memset(finsn, 0, sizeof finsn);
		memset(fk, 0, sizeof fk);
		fk[0].kind = Kfunc; fk[0].text = "recurse";
		ff[0].name = "recurse"; ff[0].nreg = 2; ff[0].ninsn = 2; ff[0].insn = finsn;
		finsn[0].op = Ocall; finsn[0].a = 1; finsn[0].b = 0; finsn[0].c = 0;
		finsn[1].op = Oreturn; finsn[1].a = 1;
		fm.nconst = 1; fm.konst = fk; fm.nfunc = 1; fm.func = ff;
		check(nvvaluetuple(&farg, nil, 0) == 0, "tiny: frame-limit argument tuple");
		check(nvexecinit(&exec, &fm, "recurse", &farg, nil, 0, ferr, sizeof ferr) == 0, "tiny: frame-limit init");
		check(nvexecsetframelimit(&exec, 2) == 0, "tiny: maxframe=2 is the tightest legal limit");
		check(nvexecrun(&exec, 16) == NvFault && strcmp(exec.fault, "system_limit") == 0 && exec.nframe == 2,
			"tiny: exactly one call beyond the entry frame is allowed before maxframe=2 faults");
		nvexecfree(&exec);
		nvvaluefree(&farg);
	}
	print("ok - tiny: maxframe=2 allows exactly one call before faulting\n");

	/* T5-05: limits.maxtermdepth=2 rejects sending a depth-3 value, distinct from the 256 construction ceiling. */
	{
		NvRuntime r2;
		NvLimits lim2;
		NvValue pid, leaf, depth2, depth3;
		char err2[128];

		memset(&leaf, 0, sizeof leaf);
		leaf.valid = 1; leaf.kind = Vint; leaf.i = 1;
		check(nvvaluetuple(&depth2, &leaf, 1) == 0, "tiny: depth-2 tuple construction succeeds");
		check(nvvaluetuple(&depth3, &depth2, 1) == 0,
			"tiny: depth-3 tuple construction succeeds (construction ceiling is 256, not the send ceiling)");

		lim2.maxprocess = 1;
		lim2.maxmailbox = 65536;
		lim2.maxmessage = 65536;
		lim2.maxframe = 64;
		lim2.maxtermdepth = 2;
		lim2.maxduration = NvMaxduration;
		check(nvruntimeinit(&r2, &lim2, 52, err2, sizeof err2) == 0, err2);
		check(nvprocspawn(&r2, &pid, err2, sizeof err2) == 0, "tiny: spawn maxtermdepth=2 process");
		check(nvprocsend(&r2, &pid, &depth2, err2, sizeof err2) == 1,
			"tiny: sending a depth-2 value is accepted at maxtermdepth=2");
		check(nvprocsend(&r2, &pid, &depth3, err2, sizeof err2) < 0 && strcmp(err2, "mailbox_full") == 0,
			"tiny: sending a depth-3 value is rejected exactly at maxtermdepth=2");
		nvvaluefree(&depth2);
		nvvaluefree(&depth3);
		nvvaluefree(&pid);
		nvruntimefree(&r2);
	}
	print("ok - tiny: send-time term-depth ceiling (limits.maxtermdepth) is exact and distinct from the construction ceiling\n");
}

/*
 * D060: a receive clause guard runs on the tentative bindings before the
 * candidate is taken. A candidate whose guard fails stays in the mailbox
 * in its original position; the scan moves on to the next candidate, and
 * a later message can be selected ahead of it. A guard that faults (the
 * 'a candidate under `x > 3`) is false, not a process fault.
 */
static void
receiveguard(void)
{
	char *src =
		"fn pick() {\n"
		"	first = receive {\n"
		"		${'n, x} when x > 3 => x;\n"
		"	};\n"
		"	receive {\n"
		"		${'n, y} => ${first, y};\n"
		"	}\n"
		"}\n";
	NvModule *m;
	NvLimits limits;
	NvScheduler sched;
	NvValue arg, pid, msg, elem[2];
	char err[256];
	int state;

	m = compilesrc("receiveguard", src);

	limits.maxprocess = 2;
	limits.maxmailbox = 4096;
	limits.maxmessage = 1024;
	limits.maxframe = 64;
	limits.maxtermdepth = NvMaxtermdepth;
	limits.maxduration = NvMaxduration;

	check(nvvaluetuple(&arg, nil, 0) == 0, "recvguard: argument tuple");
	check(nvschedinit(&sched, m, &limits, 60, 1000, err, sizeof err) == 0, err);
	check(nvschedspawnroot(&sched, "pick", &arg, &pid, err, sizeof err) == 0, "recvguard: spawn root");

	/* Queue ${'n, 1}, ${'n, 'a}, ${'n, 5} before the first dispatch. */
	check(nvvalueatom(&elem[0], "n") == 0 && nvvalueint(&elem[1], "1") == 0, "recvguard: message 1 parts");
	check(nvvaluetuple(&msg, elem, 2) == 0, "recvguard: message 1");
	nvvaluefree(&elem[1]);
	check(nvprocsend(&sched.runtime, &pid, &msg, err, sizeof err) == 1, "recvguard: queue ${'n, 1}");
	nvvaluefree(&msg);
	check(nvvalueatom(&elem[1], "a") == 0, "recvguard: message 2 parts");
	check(nvvaluetuple(&msg, elem, 2) == 0, "recvguard: message 2");
	nvvaluefree(&elem[1]);
	check(nvprocsend(&sched.runtime, &pid, &msg, err, sizeof err) == 1, "recvguard: queue ${'n, 'a}");
	nvvaluefree(&msg);
	check(nvvalueint(&elem[1], "5") == 0, "recvguard: message 3 parts");
	check(nvvaluetuple(&msg, elem, 2) == 0, "recvguard: message 3");
	nvvaluefree(&elem[1]);
	nvvaluefree(&elem[0]);
	check(nvprocsend(&sched.runtime, &pid, &msg, err, sizeof err) == 1, "recvguard: queue ${'n, 5}");
	nvvaluefree(&msg);

	/*
	 * One quantum: the guarded receive scans 1 (guard false), 'a (guard
	 * faults badarith -> false), 5 (selected); the plain receive then
	 * takes the oldest remaining candidate, which must still be 1.
	 */
	check(nvschedstep(&sched, err, sizeof err) == NvSchedProgress && sched.rootstate == NvRootDone &&
		sched.rootvalue.kind == Vtuple && sched.rootvalue.tuple->n == 2 &&
		sched.rootvalue.tuple->elem[0].kind == Vint && sched.rootvalue.tuple->elem[0].i == 5 &&
		sched.rootvalue.tuple->elem[1].kind == Vint && sched.rootvalue.tuple->elem[1].i == 1,
		"recvguard: guard skips 1 and the faulting 'a, selects 5; 1 is still first in the mailbox afterwards");
	check(sched.completed == 1 && sched.faulted == 0, "recvguard: the faulting guard did not fault the process");
	state = nvschedstep(&sched, err, sizeof err);
	check(state == NvSchedDone, "recvguard: scheduler done");

	nvschedfree(&sched);
	nvvaluefree(&pid);
	nvvaluefree(&arg);
	nvmodulefree(m);
	print("ok - D060: a receive guard skips non-selecting and faulting candidates, leaving them queued, and selects a later one\n");
}

void
main(void)
{
	receiveguard();
	h1regression();
	crossreceiveleak();
	midscanappend();
	sendthenexit();
	belowcursorfairness();
	tinylimits();
	print("all R2 regression tests passed\n");
	exits(nil);
}
