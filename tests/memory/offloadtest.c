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

/*
 * D074 off-process collection: ownership/wakeup/teardown lifecycle
 * tests (M08-T04c). Every scenario uses nvschedgchold to park a launched
 * collector deterministically before it publishes completion (before it
 * sets the heap idle and semrelease's), instead of racing real
 * scheduling timing: engage the hold, observe state that must be true
 * regardless of timing, release, then observe the fold. The concurrency
 * underneath is real -- genuine rfork(RFPROC|RFMEM) collector procs and
 * a genuine tsemacquire/semrelease completion semaphore in malloc'd
 * shared memory, not a simulation -- these tests pin only the *moment*
 * each observation happens, not the mechanism.
 *
 * gcoffload polarity (D074): 0 never offloads -- the new default,
 * already exercised incidentally by every other C test in this tree via
 * the mechanical ".gcoffload = 0" additions accompanying this task. 1
 * forces every collection off-process, since every execution heap holds
 * at least one live word (its argument tuple) after nvexecinit. These
 * tests force 1 to exercise the mechanism; T04d's later measurement
 * work chooses the real default threshold.
 */

static NvHeap hostheap;

static void
check(int ok, char *why)
{
	if(!ok){
		fprint(2, "FAIL: off-process lifecycle: %s\n", why);
		exits("test");
	}
}

static NvModule *
compile(char *name, char *src)
{
	Parser p;
	Program *pr;
	NvModule *m;
	char err[256];

	pr = parseprogram(&p, name, src, strlen(src));
	check(pr != nil, p.err);
	m = nvcompile(pr, err, sizeof err);
	programfree(pr);
	check(m != nil, err);
	return m;
}

static void
limits(NvLimits *l, uvlong gcoffload)
{
	memset(l, 0, sizeof *l);
	l->maxprocess = 32;
	l->maxmailbox = 8192;
	l->maxmessage = 4096;
	l->maxheap = 0;
	l->gcstress = 0;
	l->maxframe = 16;
	l->maxtermdepth = NvMaxtermdepth;
	l->maxduration = NvMaxduration;
	l->maxatom = 65536;
	l->gcoffload = gcoffload;
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

/* Bounded safety net: drive to scheduler quiescence. Every fixture here
 * finishes in a handful of steps when the mechanism is correct; this
 * cap only guards against a real regression hanging the test binary. */
enum {
	NvDrivemax = 200000,
};

static int
drive(NvScheduler *s, char *err, int nerr)
{
	int state, i;

	state = NvSchedProgress;
	for(i = 0; i < NvDrivemax && state == NvSchedProgress; i++)
		state = nvschedstep(s, err, nerr);
	return state;
}

/* Bounded poll while *flag is still set (an offlaunched bit, or
 * gcoutstanding > 0 cast through a helper below). Once a hold is
 * released, a real collector completes in microseconds, so this is a
 * generous safety margin, not a timing dependency: correctness does not
 * rely on how many iterations are actually needed. */
enum {
	NvReapmax = 500,
};

static void
reapflag(NvScheduler *s, int *flag, char *err, int nerr)
{
	int i;

	for(i = 0; i < NvReapmax && *flag; i++)
		check(nvschedstep(s, err, nerr) != NvSchedError, err);
}

/*
 * D074 "Launch"/"Completion"/"Never spin"/corollary, plus "Diagnostics".
 * One process, gcstress forces a collection at its very first
 * allocation, gcoffload forces it off-process. Held before it publishes
 * completion: heap owner, offlaunched and gcoutstanding must already
 * reflect the launch, deterministically, because the scheduler proc
 * itself sets them before forking -- the child cannot unset them while
 * held, regardless of real scheduling timing. Also checks that a send
 * during collection is an ordinary send (D068), that nvschedmemory
 * skips a collecting heap and counts it, and that the scheduler never
 * reports idle/deadlock while this lone process's collector is
 * outstanding.
 */
static void
holdbasic(void)
{
	char *src =
		"fn holder() {\n"
		"	x = ${1, 2, 3};\n"
		"	receive {\n"
		"		'go => 'done;\n"
		"	}\n"
		"}\n";
	NvModule *m;
	NvLimits l;
	NvScheduler s;
	NvTerm pid, msg;
	NvExec *e;
	NvProcess *p;
	NvMemstats mem;
	char err[256];
	int i;

	m = compile("holdbasic", src);
	limits(&l, 1);
	check(nvschedinit(&s, m, &l, 1, 1000, err, sizeof err) == 0, err);
	check(nvschedspawnroot(&s, "holder", nvtuple(&hostheap, nil, 0), &pid, err, sizeof err) == 0, err);
	e = s.runtime.process[nvpidslot(pid)].exec;
	e->gcstress = 1;

	nvschedgchold(&s, 1);
	check(nvschedstep(&s, err, sizeof err) == NvSchedProgress, "launch step");
	p = &s.runtime.process[nvpidslot(pid)];
	check(e->heap.owner == NvHeapCollecting, "heap not marked collecting immediately after launch");
	check(e->offlaunched, "offlaunched not set immediately after launch");
	check(s.gcoutstanding == 1, "gcoutstanding not incremented immediately after launch");
	check(p->state == Prrunnable, "collecting process not left runnable");

	/* D068: the mailbox is scheduler/sender territory, never touched by
	 * a collector; a send during collection is an ordinary send. */
	msg = nvatom("unrelated");
	check(nvprocsend(&s.runtime, pid, msg, err, sizeof err) == 1, "send during collection failed");
	check(p->head != nil && nvtermkind(p->head->root) == Vatom, "sent message not queued during collection");

	/* D074 "Diagnostics": a collecting heap is skipped, not read racily. */
	nvschedmemory(&s, &mem);
	check(mem.ncollecting == 1, "nvschedmemory did not report the collecting heap as skipped");
	check(mem.nexec == 0, "nvschedmemory counted a collecting heap's exec");

	/* D074 corollary: a collector outstanding with nothing else
	 * dispatchable must report progress, never idle/deadlock. */
	for(i = 0; i < 3; i++)
		check(nvschedstep(&s, err, sizeof err) == NvSchedProgress,
			"reported idle/deadlock while a collector was outstanding");
	check(e->heap.owner == NvHeapCollecting && s.gcoutstanding == 1, "held collector completed despite the hold");

	nvschedgchold(&s, 0);
	reapflag(&s, &e->offlaunched, err, sizeof err);
	check(!e->offlaunched && e->heap.owner == NvHeapIdle, "collector never completed after release");
	check(s.gcoutstanding == 0, "gcoutstanding not decremented after fold");
	check(s.collections >= 1, "completion fold did not credit a collection");

	check(nvprocsend(&s.runtime, pid, nvatom("go"), err, sizeof err) == 1, "wake message failed");
	check(drive(&s, err, sizeof err) == NvSchedDone, "scheduler did not finish");
	check(s.rootstate == NvRootDone, "holder did not complete normally");

	nvschedfree(&s);
	nvmodulefree(m);
	print("ok - send during off-process collection, diagnostics skip it honestly, and the scheduler never reports idle while a collector is outstanding\n");
}

/*
 * D074 "Never spin"/corollary interaction with D048-D050 deadlines: a
 * deadline must still fire for an unrelated waiting process while
 * another process's off-process collector remains outstanding and held
 * indefinitely. Manipulating the fake clock directly (no scheduler call)
 * to already be past the deadline makes this deterministic: the
 * "already expired" fast path fires on the very next nvschedstep call
 * without ever needing to wait on the completion semaphore at all.
 */
static void
holddeadline(void)
{
	char *src =
		"fn holder() {\n"
		"	x = ${1, 2, 3};\n"
		"	receive {\n"
		"		'go => 'done;\n"
		"	}\n"
		"}\n"
		"fn waiter() {\n"
		"	receive {\n"
		"		'go => 'done;\n"
		"		after 5 => 'timedout;\n"
		"	}\n"
		"}\n";
	NvModule *m;
	NvLimits l;
	NvScheduler s;
	NvTerm holderpid, waiterpid;
	NvExec *he;
	Testclock tc;
	NvClock clock;
	char err[256];

	m = compile("holddeadline", src);
	limits(&l, 1);
	memset(&tc, 0, sizeof tc);
	tc.now = 1000;
	clock.aux = &tc;
	clock.now = tcnow;
	clock.wait = tcwait;
	check(nvschedinit(&s, m, &l, 1, 1000, err, sizeof err) == 0, err);
	nvschedsetclock(&s, &clock);
	check(nvschedspawn(&s, "holder", nvtuple(&hostheap, nil, 0), &holderpid, err, sizeof err) == 0, err);
	he = s.runtime.process[nvpidslot(holderpid)].exec;
	he->gcstress = 1;
	check(nvschedspawnroot(&s, "waiter", nvtuple(&hostheap, nil, 0), &waiterpid, err, sizeof err) == 0, err);

	nvschedgchold(&s, 1);
	check(nvschedstep(&s, err, sizeof err) == NvSchedProgress, "holder launch step");
	check(he->heap.owner == NvHeapCollecting && s.gcoutstanding == 1, "holder collector not launched");
	check(nvschedstep(&s, err, sizeof err) == NvSchedProgress &&
		s.runtime.process[nvpidslot(waiterpid)].state == Prwaiting,
		"waiter did not block on its deadline receive");
	check(s.runtime.process[nvpidslot(waiterpid)].hasdeadline &&
		s.runtime.process[nvpidslot(waiterpid)].deadline == 1005, "deadline not armed as expected");

	/* Advance the fake clock directly: no scheduler call, so this cannot
	 * race anything. holder's collector is still held throughout. */
	tc.now = 1005;
	check(nvschedstep(&s, err, sizeof err) == NvSchedProgress, "deadline fire step");
	check(he->heap.owner == NvHeapCollecting && s.gcoutstanding == 1,
		"holder's held collector was disturbed by an unrelated deadline");
	check(s.timerwakes >= 1, "deadline did not wake through the collector-outstanding path");

	/* One more step dispatches the now-runnable waiter to completion. */
	check(nvschedstep(&s, err, sizeof err) == NvSchedProgress && s.rootstate == NvRootDone &&
		s.rootvalue != nil && nvtermkind(s.rootvalue->root) == Vatom &&
		strcmp(nvtermatom(s.rootvalue->root), "timedout") == 0,
		"deadline did not fire correctly while an unrelated collector was outstanding");

	nvschedgchold(&s, 0);
	reapflag(&s, &he->offlaunched, err, sizeof err);
	check(nvprocsend(&s.runtime, holderpid, nvatom("go"), err, sizeof err) == 1, "holder wake failed");
	check(drive(&s, err, sizeof err) == NvSchedDone, "scheduler did not finish");

	nvschedfree(&s);
	nvmodulefree(m);
	print("ok - a deadline fires for an unrelated process while another process's collector remains outstanding and held\n");
}

/*
 * D074 "Teardown": nvschedfree must drain an outstanding collector
 * before freeing anything it might still hold a pointer into. Releasing
 * the hold before calling nvschedfree, rather than leaving it engaged,
 * is what makes this deterministic (a real collector then completes in
 * microseconds and nvschedfree's bounded drain picks it up on one of its
 * first iterations) instead of depending on nvschedfree's generous but
 * finite drain bound, or hanging teardown -- the real concurrency (a
 * genuine forked collector completing and semrelease'ing a genuinely
 * shared, malloc'd semaphore) still runs underneath and is exactly what
 * this test exercises: nvschedfree does not crash, hang, or sysfatal.
 */
static void
holdteardown(void)
{
	char *src =
		"fn holder() {\n"
		"	x = ${1, 2, 3};\n"
		"	receive {\n"
		"		'go => 'done;\n"
		"	}\n"
		"}\n";
	NvModule *m;
	NvLimits l;
	NvScheduler s;
	NvTerm pid;
	NvExec *e;
	char err[256];

	m = compile("holdteardown", src);
	limits(&l, 1);
	check(nvschedinit(&s, m, &l, 1, 1000, err, sizeof err) == 0, err);
	check(nvschedspawnroot(&s, "holder", nvtuple(&hostheap, nil, 0), &pid, err, sizeof err) == 0, err);
	e = s.runtime.process[nvpidslot(pid)].exec;
	e->gcstress = 1;

	nvschedgchold(&s, 1);
	check(nvschedstep(&s, err, sizeof err) == NvSchedProgress, "launch step");
	check(e->heap.owner == NvHeapCollecting && s.gcoutstanding == 1, "collector not launched before teardown");

	nvschedgchold(&s, 0);
	nvschedfree(&s);
	nvmodulefree(m);
	print("ok - nvschedfree drains an outstanding off-process collector before freeing runtime memory\n");
}

/*
 * D074 corollary, "every runnable process collecting at once": several
 * independent processes each launch their own off-process collector via
 * the demand path (nvprocyield already re-enqueues each at the run-queue
 * tail before its collector launches), so after one launch step per
 * process the entire run queue is runnable-but-collecting. The
 * scheduler must still report progress, never idle, and must still make
 * genuine progress once released.
 */
static void
holdall(void)
{
	char *src =
		"fn holder() {\n"
		"	x = ${1, 2, 3};\n"
		"	receive {\n"
		"		'go => 'done;\n"
		"	}\n"
		"}\n";
	enum { N = 4 };
	NvModule *m;
	NvLimits l;
	NvScheduler s;
	NvTerm pid[N];
	NvExec *e[N];
	char err[256];
	int i, state;

	m = compile("holdall", src);
	limits(&l, 1);
	check(nvschedinit(&s, m, &l, 1, 1000, err, sizeof err) == 0, err);
	for(i = 0; i < N; i++){
		check(nvschedspawn(&s, "holder", nvtuple(&hostheap, nil, 0), &pid[i], err, sizeof err) == 0, err);
		e[i] = s.runtime.process[nvpidslot(pid[i])].exec;
		e[i]->gcstress = 1;
	}

	nvschedgchold(&s, 1);
	/* One launch step per process: FIFO order means each dispatch in
	 * turn is the one not-yet-touched process, since a just-launched
	 * one is re-enqueued at the tail before the next is dispatched. */
	for(i = 0; i < N; i++)
		check(nvschedstep(&s, err, sizeof err) == NvSchedProgress, "launch step");
	for(i = 0; i < N; i++)
		check(e[i]->heap.owner == NvHeapCollecting && e[i]->offlaunched, "a process did not launch its collector");
	check(s.gcoutstanding == N, "not every process launched a collector");

	for(i = 0; i < 3; i++)
		check(nvschedstep(&s, err, sizeof err) == NvSchedProgress,
			"reported idle/deadlock with every runnable process collecting");

	nvschedgchold(&s, 0);
	for(i = 0; i < N; i++)
		reapflag(&s, &e[i]->offlaunched, err, sizeof err);
	check(s.gcoutstanding == 0, "gcoutstanding did not drain to zero after release");

	for(i = 0; i < N; i++)
		check(nvprocsend(&s.runtime, pid[i], nvatom("go"), err, sizeof err) == 1, "wake message failed");
	state = drive(&s, err, sizeof err);
	check(state == NvSchedDone, "scheduler did not finish");
	check(s.completed == N, "not every holder completed");

	nvschedfree(&s);
	nvmodulefree(m);
	print("ok - every runnable process collecting at once still makes progress, never idle, and finishes correctly\n");
}

/* Builds "fn dirty() { x = ${1,1,...,1}; receive { 'go => 'done; } }"
 * with n elements: enough that, once blocked, used words exceed half
 * the initial 64-word space (the D067/D072 opportunistic-collect
 * threshold), without ever needing a collection just to construct it. */
static char *
dirtysrc(void)
{
	static char buf[2048];
	char *p, *e;
	int i;

	p = buf;
	e = buf+sizeof buf;
	p = seprint(p, e, "fn dirty() {\n\tx = ${1");
	for(i = 1; i < 40; i++)
		p = seprint(p, e, ",1");
	seprint(p, e, "};\n\treceive {\n\t\t'go => 'done;\n\t}\n}\n");
	return buf;
}

/*
 * D074 "Idle-sweep concurrency is bounded": several waiting processes
 * with a dirty-enough heap all qualify for opportunistic collection in
 * one idle sweep; with gcoffload forcing every eligible collection
 * off-process, the sweep must cap how many collectors it forks in one
 * pass rather than forking one per qualifying waiter unconditionally.
 * Beyond the cap it still collects, just inline, so every waiter is
 * reclaimed this same pass regardless. The exact cap value is a private
 * constant in lib/sched.c (NvGcsweepcap); this test checks the shape of
 * the behavior (some forked, some inline, all accounted for) rather
 * than hardcoding that constant's value.
 */
static void
holdsweepcap(void)
{
	enum { N = 10 };
	NvModule *m;
	NvLimits l;
	NvScheduler s;
	NvTerm pid[N];
	NvExec *e[N];
	char err[256];
	int i, offloaded, inln, state;

	m = compile("holdsweepcap", dirtysrc());
	limits(&l, 1);
	check(nvschedinit(&s, m, &l, 1, 1000, err, sizeof err) == 0, err);
	for(i = 0; i < N; i++){
		check(nvschedspawn(&s, "dirty", nvtuple(&hostheap, nil, 0), &pid[i], err, sizeof err) == 0, err);
		e[i] = s.runtime.process[nvpidslot(pid[i])].exec;
	}
	/* Dispatch each once: builds its tuple (fits without collecting)
	 * then blocks in receive, dirty and waiting. */
	for(i = 0; i < N; i++)
		check(nvschedstep(&s, err, sizeof err) == NvSchedProgress &&
			s.runtime.process[nvpidslot(pid[i])].state == Prwaiting,
			"dirty process did not block");

	nvschedgchold(&s, 1);
	/* The run queue is empty (every process Prwaiting): this is the
	 * opportunistic idle sweep, not the demand path. */
	check(nvschedstep(&s, err, sizeof err) == NvSchedProgress, "idle sweep step");
	offloaded = 0;
	inln = 0;
	for(i = 0; i < N; i++){
		if(e[i]->heap.owner == NvHeapCollecting)
			offloaded++;
		else
			inln++;
	}
	check(offloaded == (int)s.gcoutstanding, "gcoutstanding disagrees with the number of collecting heaps");
	check(offloaded > 0 && offloaded < N, "the idle sweep did not cap off-process launches");
	check(offloaded+inln == N, "some waiter was neither offloaded nor collected inline this sweep");

	/* D074 corollary: queue empty, but collectors outstanding -- must
	 * not report idle/deadlock. */
	for(i = 0; i < 3; i++)
		check(nvschedstep(&s, err, sizeof err) == NvSchedProgress,
			"reported idle/deadlock with waiting processes still collecting");

	nvschedgchold(&s, 0);
	/*
	 * These processes stay Prwaiting throughout their collection (D067:
	 * the opportunistic path never touches lifecycle state), so they
	 * never reach the run queue's fold path on their own. Wake them
	 * first, THEN drive: once Prrunnable, the ordinary dispatch path
	 * folds each in turn exactly as the demand-path tests above do.
	 */
	for(i = 0; i < N; i++)
		check(nvprocsend(&s.runtime, pid[i], nvatom("go"), err, sizeof err) == 1, "wake message failed");
	state = drive(&s, err, sizeof err);
	check(state == NvSchedDone, "scheduler did not finish");
	check(s.completed == N, "not every dirty process completed");
	check(s.gcoutstanding == 0, "gcoutstanding did not drain to zero");

	nvschedfree(&s);
	nvmodulefree(m);
	print("ok - the idle sweep caps off-process launches per pass, still collects every eligible waiter, and every process finishes\n");
}

/*
 * Coordinator-review fix regression: a collection launched by the
 * opportunistic idle sweep targets a Prwaiting process, which (unlike
 * every other fixture above) never becomes Prrunnable on its own here --
 * this process has no message coming and no armed deadline, i.e. it is
 * genuinely, permanently deadlocked. Its completion can therefore only
 * ever be folded by gcfoldall, never by finddispatchable (which sees
 * only Prrunnable slots). Before the fix, gcoutstanding would never
 * return to 0 for this process and the scheduler would report
 * NvSchedProgress forever instead of NvSchedIdle, silently disabling
 * D046 deadlock detection for the rest of this scheduler's life. This
 * is the scenario D074's "never falsely report idle" corollary is
 * really about, from the other direction: idle must eventually be
 * reported once a collector genuinely finishes and nothing else is
 * outstanding, not just "never falsely" while one is still running.
 */
static void
holddeadlock(void)
{
	NvModule *m;
	NvLimits l;
	NvScheduler s;
	NvTerm pid;
	NvExec *e;
	char err[256];
	int i;

	m = compile("holddeadlock", dirtysrc());
	limits(&l, 1);
	check(nvschedinit(&s, m, &l, 1, 1000, err, sizeof err) == 0, err);
	check(nvschedspawn(&s, "dirty", nvtuple(&hostheap, nil, 0), &pid, err, sizeof err) == 0, err);
	e = s.runtime.process[nvpidslot(pid)].exec;

	check(nvschedstep(&s, err, sizeof err) == NvSchedProgress &&
		s.runtime.process[nvpidslot(pid)].state == Prwaiting,
		"dirty process did not block");

	nvschedgchold(&s, 1);
	/* Empty run queue, one dirty waiter: the opportunistic idle sweep. */
	check(nvschedstep(&s, err, sizeof err) == NvSchedProgress, "idle sweep step");
	check(e->heap.owner == NvHeapCollecting && e->offlaunched && s.gcoutstanding == 1,
		"waiting process's collector was not launched off-process");
	check(s.runtime.process[nvpidslot(pid)].state == Prwaiting,
		"D067: opportunistic collection touched lifecycle state");

	for(i = 0; i < 3; i++)
		check(nvschedstep(&s, err, sizeof err) == NvSchedProgress,
			"reported idle/deadlock while this collector was outstanding");

	nvschedgchold(&s, 0);
	reapflag(&s, &e->offlaunched, err, sizeof err);
	check(!e->offlaunched && e->heap.owner == NvHeapIdle, "collector never completed after release");

	/*
	 * The fix under test: with no message ever sent and no deadline
	 * ever armed, this process stays Prwaiting forever and would never
	 * reach finddispatchable's gcfold on its own. Without gcfoldall in
	 * the idle path (and in nvschedfree's drain), gcoutstanding would
	 * still read 1 here and every following nvschedstep call would
	 * loop on tsemacquire indefinitely instead of ever reporting
	 * NvSchedIdle -- a real hang for any caller (including the CLI's
	 * own driving loop) once T04d selects a nonzero gcoffload default.
	 */
	check(s.gcoutstanding == 0,
		"gcoutstanding never reached zero for a waiting process's completed collector "
		"(deadlock detection would be permanently disabled)");
	check(nvschedstep(&s, err, sizeof err) == NvSchedIdle,
		"scheduler did not report deadlock after a waiting process's off-process "
		"collection completed with nothing else outstanding");

	nvschedfree(&s);
	nvmodulefree(m);
	print("ok - a waiting process's off-process collection is folded even though it never reaches the run queue, and deadlock is still reported once nothing is outstanding\n");
}

/*
 * Regression safety for the corrected D074 polarity: gcoffload==0 must
 * never launch a collector, even under gcstress forcing many inline
 * collections back to back.
 */
static void
nooffloaddefault(void)
{
	char *src =
		"fn loop(0) { 'done }\n"
		"fn loop(n) {\n"
		"	x = ${n, n, n};\n"
		"	loop(n - 1)\n"
		"}\n";
	NvModule *m;
	NvLimits l;
	NvScheduler s;
	NvTerm n0, arg, pid;
	NvExec *e;
	char err[256];

	m = compile("nooffloaddefault", src);
	limits(&l, 0);
	check(nvschedinit(&s, m, &l, 1, 1000, err, sizeof err) == 0, err);
	n0 = nvint(&hostheap, 200);
	arg = nvtuple(&hostheap, &n0, 1);
	check(nvschedspawnroot(&s, "loop", arg, &pid, err, sizeof err) == 0, err);
	e = s.runtime.process[nvpidslot(pid)].exec;
	e->gcstress = 1;

	check(drive(&s, err, sizeof err) == NvSchedDone, "scheduler did not finish");
	check(s.rootstate == NvRootDone, "loop did not complete normally");
	check(s.gcoutstanding == 0 && s.gcofffallback == 0, "off-process activity occurred under gcoffload=0");
	check(s.collections > 0, "gcstress produced no inline collections");

	nvschedfree(&s);
	nvmodulefree(m);
	print("ok - gcoffload=0 never launches a collector, even under repeated gcstress-forced collection\n");
}

void
main(void)
{
	nvheapinit(&hostheap, 0);
	holdbasic();
	holddeadline();
	holdteardown();
	holdall();
	holdsweepcap();
	holddeadlock();
	nooffloaddefault();
	nvheapfree(&hostheap);
	print("all off-process lifecycle tests passed\n");
	exits(nil);
}
