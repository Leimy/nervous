#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../include/nvbc.h"
#include "../include/nvvm.h"
#include "../include/nvexec.h"
#include "../include/nvproc.h"
#include "../include/nvsched.h"

/*
 * D065/D087: every NvExec refers to this one table by pointer
 * (nvexecsethost), so a callback recovers the shared machine through
 * e->host->aux; the scheduler proc that dispatched it -- the only one
 * whose currentslot/generation describe this exec -- comes from
 * e->sched, set for exactly the duration of the quantum by nvschedstep.
 * The pid a callback acts on is always the currently dispatched one,
 * built on the fly from that scheduler's currentslot/generation.
 */
static int
currentpid(NvExec *e, NvTerm *pid, char *err, int nerr)
{
	NvSched *sc;

	sc = e->sched;
	if(sc == nil || !sc->currentvalid){ snprint(err,nerr,"bad_process_context"); return -1; }
	*pid = nvpid(sc->currentslot, sc->currentgeneration);
	return 0;
}

/*
 * D081 stage 1: the global runtime lock. sc is the scheduler proc taking
 * it, for the D088 counters (nil from a host caller with no scheduler
 * context). The wait-time clock read is gated on profile like every
 * other timing in this file.
 */
static void
lockrt(NvScheduler *s, NvSched *sc)
{
	uvlong start;

	if(!s->locking)
		return;
	if(sc == nil){
		qlock(&s->lock);
		return;
	}
	start = sc->profile ? uptime() : 0;
	qlock(&s->lock);
	sc->lockacq++;
	if(sc->profile)
		sc->lockwaitns += uptime()-start;
}

static void
unlockrt(NvScheduler *s)
{
	if(s->locking)
		qunlock(&s->lock);
}

/*
 * D085: installed as the runtime's wakehook; runs inside nvprocwakefrom,
 * under the runtime lock, after the woken slot is on its owner's queue.
 * Only a remote enqueue onto an idle owner costs a semrelease: the
 * owner's idle bit is set under this same lock before the owner
 * re-checks its queue and sleeps, so either we see the bit (and release)
 * or the owner sees the process (and does not sleep). Never both lost.
 */
static void
wakehook(void *aux, int from, int owner)
{
	NvScheduler *s;

	s = aux;
	if(from == owner)
		return;
	if(from >= 0)
		s->sched[from]->remoteenq++;
	if(s->idlemask & (1ULL<<owner)){
		s->idlemask &= ~(1ULL<<owner);	/* one kick per sleep; the sleeper clears it too */
		s->nidle--;
		semrelease(s->sched[owner]->sem, 1);
		if(from >= 0)
			s->sched[from]->wakessent++;
	}
}

static int
hostself(NvExec *e, NvTerm *value, char *err, int nerr)
{
	return currentpid(e, value, err, nerr);
}

static int
hostref(NvExec *e, NvTerm *value, char *err, int nerr)
{
	NvScheduler *s;
	int rc;

	s = e->host->aux;
	lockrt(s, e->sched);
	rc = nvprocref(&s->runtime, &e->heap, value, err, nerr);
	unlockrt(s);
	return rc;
}

static int
hostsend(NvExec *e, NvTerm pid, NvTerm value, char *err, int nerr)
{
	NvScheduler *s;
	NvSched *sc;
	int rc;

	s = e->host->aux;
	sc = e->sched;
	lockrt(s, sc);
	rc = nvprocsendfrom(&s->runtime, sc == nil ? -1 : sc->index, pid, value, err, nerr);
	unlockrt(s);
	return rc < 0 ? -1 : 0;
}

static int spawntimed(NvScheduler *, NvSched *, char *, NvTerm, NvTerm *, NvWork *, char *, int);

/*
 * D080: a bytecode spawn charges the child's argument copy to the
 * spawning process and bounds it by that process's work ceiling; host
 * spawns (nvschedspawn) are uncounted. D083: the child is owned by the
 * scheduler running the spawner.
 */
static int
hostspawn(NvExec *e, char *entry, NvTerm arg, NvTerm *pid, char *err, int nerr)
{
	NvScheduler *s;
	int rc;

	s = e->host->aux;
	if(e->sched == nil){ snprint(err,nerr,"bad_process_context"); return -1; }
	/* D081: the table, a queue and the child's exec setup, all under the lock. */
	lockrt(s, e->sched);
	rc = spawntimed(s, e->sched, entry, arg, pid, &e->work, err, nerr);
	unlockrt(s);
	return rc;
}

/*
 * Receive callbacks operate only on the currently dispatched process.
 * D081: each takes the runtime lock for its own mailbox step -- a sender
 * on another scheduler appends to this mailbox under the same lock, and
 * nvprocrecvwait's "did a send race the scan" check (process.c) is what
 * makes per-step locking, rather than one lock across the whole receive,
 * free of lost wakeups.
 */
static int
hostrecvbegin(NvExec *e, NvTerm *value, char *err, int nerr)
{
	NvScheduler *s;
	NvTerm pid;
	int rc;

	s = e->host->aux;
	if(currentpid(e, &pid, err, nerr) < 0)
		return -1;
	lockrt(s, e->sched);
	rc = nvprocrecvbegin(&s->runtime, pid, value, err, nerr);
	unlockrt(s);
	return rc;
}

static int
hostrecvnext(NvExec *e, NvTerm *value, char *err, int nerr)
{
	NvScheduler *s;
	NvTerm pid;
	int rc;

	s = e->host->aux;
	if(currentpid(e, &pid, err, nerr) < 0)
		return -1;
	lockrt(s, e->sched);
	rc = nvprocrecvnext(&s->runtime, pid, value, err, nerr);
	unlockrt(s);
	return rc;
}

static int
hostrecvneed(NvExec *e, uvlong *words, char *err, int nerr)
{
	NvScheduler *s;
	NvTerm pid;
	int rc;

	s = e->host->aux;
	if(currentpid(e, &pid, err, nerr) < 0)
		return -1;
	lockrt(s, e->sched);
	rc = nvprocrecvneed(&s->runtime, pid, words, err, nerr);
	unlockrt(s);
	return rc;
}

static int
hostrecvtake(NvExec *e, NvFrag **taken, char *err, int nerr)
{
	NvScheduler *s;
	NvTerm pid;
	int rc;

	s = e->host->aux;
	if(currentpid(e, &pid, err, nerr) < 0)
		return -1;
	lockrt(s, e->sched);
	rc = nvprocrecvtake(&s->runtime, pid, taken, err, nerr);
	unlockrt(s);
	return rc;
}

static int
hostrecvwait(NvExec *e, char *err, int nerr)
{
	NvScheduler *s;
	NvTerm pid;
	int rc;

	s = e->host->aux;
	if(currentpid(e, &pid, err, nerr) < 0)
		return -1;
	lockrt(s, e->sched);
	rc = nvprocrecvwait(&s->runtime, pid, err, nerr);
	unlockrt(s);
	return rc;
}

static int
hostrecvdeadline(NvExec *e, NvTerm duration, char *err, int nerr)
{
	NvScheduler *s;
	NvTerm pid;
	uvlong now;
	int rc;

	s = e->host->aux;
	if(currentpid(e, &pid, err, nerr) < 0)
		return -1;
	now = s->clock.now(s->clock.aux);
	lockrt(s, e->sched);
	rc = nvprocarmdeadline(&s->runtime, pid, duration, now, err, nerr);
	unlockrt(s);
	return rc;
}

static int
hostrecvwaitdeadline(NvExec *e, char *err, int nerr)
{
	NvScheduler *s;
	NvTerm pid;
	uvlong now;
	int rc;

	s = e->host->aux;
	if(currentpid(e, &pid, err, nerr) < 0)
		return -1;
	now = s->clock.now(s->clock.aux);
	lockrt(s, e->sched);
	rc = nvprocrecvwaitdeadline(&s->runtime, pid, now, err, nerr);
	unlockrt(s);
	return rc;
}

/*
 * D054-D057/D065: print/eprint write one value to the installed stream,
 * followed by a newline, then flush. The host table is now shared by every
 * process (D065), so the "is a stream installed" check that used to be
 * made once per spawn happens here on every call instead: a nil stream
 * faults bad_process_context exactly as a nil callback slot would. The
 * value is printed and the line is flushed before a NvTermlimit result is
 * turned into an error, so a value too deep to print in full still leaves
 * well-formed output (nvtermprint itself prints a "<deep>" marker in place
 * of the subterm it refused to descend into) instead of a truncated line.
 * A flush failure is reported as io_error and takes priority: it means the
 * stream itself is broken, which matters more than whether the value was
 * fully printed.
 */
/*
 * D086: one lock around print + newline + flush, so a line is never
 * interleaved with another scheduler's. iolock is never held with the
 * runtime lock, and the write may block: this is why the two are separate.
 */
static int
printline(NvScheduler *s, Biobuf *b, NvExec *e, NvTerm value, char *err, int nerr)
{
	int rc, flushed;

	if(b == nil){ snprint(err,nerr,"bad_process_context"); return -1; }
	qlock(&s->iolock);
	rc = nvtermprintw(b, value, &e->work);	/* D080: charged and bounded */
	Bputc(b, '\n');
	flushed = Bflush(b);
	qunlock(&s->iolock);
	if(flushed < 0){ snprint(err,nerr,"io_error"); return -1; }
	if(rc == NvTermlimit){ snprint(err,nerr,"system_limit"); return -1; }
	return 0;
}

static int
hostprint(NvExec *e, NvTerm value, char *err, int nerr)
{
	NvScheduler *s;

	s = e->host->aux;
	return printline(s, s->io.out, e, value, err, nerr);
}

static int
hosteprint(NvExec *e, NvTerm value, char *err, int nerr)
{
	NvScheduler *s;

	s = e->host->aux;
	return printline(s, s->io.err, e, value, err, nerr);
}

void
nvschedsetio(NvScheduler *s, NvIO *io)
{
	if(io == nil){
		s->io.out = nil;
		s->io.err = nil;
	}else
		s->io = *io;
}

/*
 * D050: the production clock. `uptime` is monotonic nanoseconds since
 * boot; `nsec` is deliberately not used here because it tracks settable
 * wall-clock time. Waiting blocks the whole host process rather than
 * polling, which is correct for the single scheduler this milestone has;
 * milestone 10 replaces it with the D011 semaphore wakeup.
 */
static uvlong
prodnow(void *aux)
{
	USED(aux);
	return uptime();
}

static void
prodwait(void *aux, uvlong deadline)
{
	uvlong now;
	vlong ms;

	USED(aux);
	now = uptime();
	if(deadline <= now)
		return;
	ms = (deadline-now+999999)/1000000;
	if(ms < 1)
		ms = 1;
	sleep(ms);
}

void
nvschedsetclock(NvScheduler *s, NvClock *clock)
{
	if(clock == nil){
		s->clock.aux = nil;
		s->clock.now = prodnow;
		s->clock.wait = prodwait;
	}else
		s->clock = *clock;
}

/*
 * D087: allocate scheduler proc i's NvSched and store it in s->sched[i].
 * Requires runtime.runq[i] to exist (nvruntimeinit makes queue 0,
 * nvruntimesetnrunq the rest). gcsem/gchold are malloc'd, not fields --
 * D074 "Shared-memory placement": rfork(RFPROC|RFMEM) shares data but
 * not the caller's stack, and NvScheduler is stack-allocated by every
 * caller. Both start at 0: gcsem an empty counting semaphore, gchold
 * released. Returns nil, having allocated nothing, on failure.
 */
static NvSched *
schedalloc(NvScheduler *s, int i)
{
	NvSched *sc;

	sc = mallocz(sizeof *sc, 1);
	if(sc == nil)
		return nil;
	sc->gcsem = mallocz(sizeof(long), 1);
	sc->gchold = mallocz(sizeof(long), 1);
	sc->sem = mallocz(sizeof(long), 1);	/* D085: the idle semaphore, empty */
	if(sc->gcsem == nil || sc->gchold == nil || sc->sem == nil){
		free(sc->gcsem);
		free(sc->gchold);
		free(sc->sem);
		free(sc);
		return nil;
	}
	sc->index = i;
	sc->runq = s->runtime.runq[i];
	s->sched[i] = sc;
	return sc;
}

/*
 * M10-T01 step 1: configure N scheduler procs. Legal only before the
 * root is spawned (every process is owned by, and queued on, a specific
 * scheduler from its spawn, D083), and only once. Grows the runtime's
 * run queues to N first, so a later spawn onto any of them is legal, then
 * allocates sched[1..n-1]. nvschedinit's N=1 state is the n == 1 case.
 * Failure leaves the machine at its previous nsched, still usable.
 */
int
nvschedsetnsched(NvScheduler *s, int n, char *err, int nerr)
{
	int i;

	if(n < 1 || n > NvMaxsched){
		snprint(err, nerr, "bad scheduler count");
		return -1;
	}
	if(s->rootvalid || s->runtime.nlive != 0){
		snprint(err, nerr, "scheduler count fixed once a process exists");
		return -1;
	}
	if(n <= s->nsched)
		return 0;
	if(nvruntimesetnrunq(&s->runtime, n, err, nerr) < 0)
		return -1;
	for(i = s->nsched; i < n; i++){
		if(schedalloc(s, i) == nil){
			snprint(err, nerr, "out of memory");
			return -1;
		}
		s->nsched = i+1;
	}
	s->locking = 1;
	return 0;
}

void
nvschedsetlocking(NvScheduler *s, int on)
{
	/* Never off at N>1: that is not a mode, it is a race. */
	if(s->nsched > 1)
		on = 1;
	s->locking = on != 0;
}

/* The scheduler borrows the verified module for its complete lifetime. */
int
nvschedinit(NvScheduler *s, NvModule *m, NvLimits *limits, uvlong incarnation, uvlong quantum, char *err, int nerr)
{
	NvSched *sc;

	memset(s, 0, sizeof *s);
	if(m == nil || quantum == 0){
		snprint(err, nerr, "bad scheduler configuration");
		return -1;
	}
	if(nvverify(m, err, nerr) < 0)
		return -1;
	/*
	 * D090: every atom the module can name and the four fixed atoms are
	 * interned here, once, while this is still one process. From now on
	 * no interpreter path writes the atom table, so N scheduler procs
	 * can share it under RFMEM without a lock (D070's lock remains a
	 * host/test obligation for nvatom calls made outside the machine).
	 */
	if(nvexecinternmodule(m) < 0){
		snprint(err, nerr, "out of memory");
		return -1;
	}
	if(nvruntimeinit(&s->runtime, limits, incarnation, err, nerr) < 0)
		return -1;
	/* D087: sched[] sized for NvMaxsched; one NvSched until nvschedsetnsched. */
	s->sched = mallocz(NvMaxsched*sizeof *s->sched, 1);
	if(s->sched == nil){
		nvruntimefree(&s->runtime);
		snprint(err, nerr, "out of memory");
		return -1;
	}
	sc = schedalloc(s, 0);
	s->finished = mallocz(sizeof(long), 1);
	if(sc == nil || s->finished == nil){
		if(sc != nil){
			free(sc->gcsem);
			free(sc->gchold);
			free(sc->sem);
			free(sc);
		}
		free(s->finished);
		free(s->sched);
		s->sched = nil;
		nvruntimefree(&s->runtime);
		snprint(err, nerr, "out of memory");
		return -1;
	}
	s->nsched = 1;
	/* D085: told of every wake so an idle owner can be kicked (a no-op at N=1). */
	s->runtime.wakehook = wakehook;
	s->runtime.wakeaux = s;
	s->module = m;
	s->quantum = quantum;
	/*
	 * D065: one host table, shared by every process's NvExec. print/eprint
	 * are always installed; they check s->io.out/s->io.err themselves and
	 * fault bad_process_context when the corresponding stream is nil, so
	 * there is nothing left to decide at spawn time.
	 */
	s->host.aux = s;
	s->host.self = hostself;
	s->host.makeref = hostref;
	s->host.send = hostsend;
	s->host.spawn = hostspawn;
	s->host.recvbegin = hostrecvbegin;
	s->host.recvnext = hostrecvnext;
	s->host.recvneed = hostrecvneed;
	s->host.recvtake = hostrecvtake;
	s->host.recvwait = hostrecvwait;
	s->host.recvdeadline = hostrecvdeadline;
	s->host.recvwaitdeadline = hostrecvwaitdeadline;
	s->host.print = hostprint;
	s->host.eprint = hosteprint;
	nvschedsetclock(s, nil);
	return 0;
}

static uvlong
fragmentbytes(NvFrag *f)
{
	return f == nil ? 0 : sizeof *f + (uvlong)f->nword*sizeof(NvTerm);
}

/* Explicit diagnostic snapshot only. The normal dispatch path never scans
 * the process table for statistics. Excludes allocator/module/atom storage,
 * transient GC scratch and the caller-owned scheduler struct itself. */
void
nvschedmemory(NvScheduler *s, NvMemstats *m)
{
	NvProcess *p;
	NvExec *e;
	NvChunk *c;
	NvFrag *f;
	ulong i;
	int collecting;

	memset(m, 0, sizeof *m);
	m->tablebytes = (uvlong)s->runtime.nchunk*NvProcchunk*sizeof(NvProcess);
	for(i = 0; i < s->runtime.nslot; i++){
		p = nvprocat(&s->runtime, i);
		for(f = p->head; f != nil; f = f->next){
			m->nmailbox++;
			m->mailboxbytes += fragmentbytes(f);
		}
		e = p->exec;
		if(e == nil)
			continue;
		/*
		 * D074 "Diagnostics", coordinator-review fix: a collector proc
		 * may be actively rewriting e->heap.cur/full/adopted right
		 * now; skip this exec entirely rather than read it racily.
		 * The owner check itself must be taken under the heap's Lock,
		 * not read raw -- an unlocked read here has the same ordering
		 * gap the dispatch path's gcfold exists to close (see gcfold's
		 * comment): only a scheduler proc ever launches a NEW
		 * collection, so no collector can start between this lock and
		 * unlock, but observing NvHeapIdle without the lock does not
		 * guarantee the collector's chunk rewrites are visible yet.
		 * Once observed idle under this lock, the collector is
		 * provably done and the chunk reads below are safe.
		 */
		lock(&e->heap.lock);
		collecting = e->heap.owner == NvHeapCollecting;
		unlock(&e->heap.lock);
		if(collecting){
			m->ncollecting++;
			continue;
		}
		m->nexec++;
		m->execbytes += sizeof *e;
		m->stackbytes += (uvlong)e->nstack*sizeof(NvTerm);
		m->stackused += (uvlong)e->sp*sizeof(NvTerm);
		c = e->heap.cur;
		if(c != nil){
			m->heapbytes += sizeof *c + (uvlong)c->cap*sizeof(NvTerm);
			m->heapused += (uvlong)c->top*sizeof(NvTerm);
		}
		for(c = e->heap.full; c != nil; c = c->next){
			m->heapbytes += sizeof *c + (uvlong)c->cap*sizeof(NvTerm);
			m->heapused += (uvlong)c->top*sizeof(NvTerm);
		}
		for(f = e->heap.adopted; f != nil; f = f->next){
			m->nadopted++;
			m->adoptedbytes += fragmentbytes(f);
		}
		m->reportbytes += fragmentbytes(e->result) + fragmentbytes(e->exitreason);
	}
	m->reportbytes += fragmentbytes(s->rootvalue) + fragmentbytes(s->lastexit);
	m->totalbytes = m->tablebytes + m->execbytes + m->heapbytes + m->stackbytes +
		m->adoptedbytes + m->mailboxbytes + m->reportbytes;
}

/* Forward declaration: gcfoldall is defined later (it calls gcfold,
 * which needs collect()'s helpers in between), but gcdrain above it
 * needs to call it. */
static void gcfoldall(NvScheduler *, NvSched *);

/*
 * Coordinator-review fix: gcdrain previously decremented gcoutstanding
 * once per successful tsemacquire, treating the semaphore's count as a
 * 1:1 proxy for "one more completion has been folded". That invariant
 * is false: gcfold (below) decrements gcoutstanding whenever it
 * observes an exec's heap go idle, entirely independent of whether
 * anyone ever consumed that exec's semrelease -- a completion folded
 * via the ordinary dispatch path (finddispatchable) leaves its
 * semrelease uncollected, a stale credit sitting in the semaphore's
 * count. A later gcdrain tsemacquire can consume that STALE credit
 * (semaphores are fungible integers, not tagged per-completion) and
 * decrement gcoutstanding to 0 while a DIFFERENT, still-running
 * collector proc is genuinely still rewriting memory nvschedfree is
 * about to free -- a real use-after-free. The semaphore is now used
 * purely as a wakeup/sleep primitive here, never as a completion
 * counter: every iteration re-derives the true outstanding count via
 * gcfoldall, which is the same locked-owner-check source of truth
 * dispatch already relies on.
 */
/*
 * The bound is a failure detector, not a budget: a collector works on
 * one process heap and is done in milliseconds, so one still missing
 * after 10 s is lost (spinning on freed memory, killed, never forked),
 * and waiting 100 s -- the original value -- only delayed the sysfatal
 * that reports it (M10-T00b, found via offloadtest's holdfalseidle).
 */
enum {
	NvGcdrainwaitms = 20,
	NvGcdrainmax = 500,	/* 500 * 20ms = 10s worst case before giving up */
};

static int gcwait(NvSched *, ulong);

/*
 * D087: drains the collectors one scheduler proc launched. Returns -1
 * with a message if a collector never completed or never signalled;
 * nvschedfree makes that fatal, while a forked scheduler proc (M10-T01)
 * reports it as a machine error instead, since a sysfatal there would
 * leave the main proc waiting on `finished` forever. Every scheduler
 * proc drains its own collectors before it finishes; nvschedfree's
 * later call per scheduler is then a no-op that re-verifies.
 */
static int
gcdrain(NvScheduler *s, NvSched *sc, char *err, int nerr)
{
	uvlong tries;

	/*
	 * D081: a scheduler proc drains while its siblings may still be
	 * finishing a step (exiting processes, freeing execs), so the table
	 * walk in gcfoldall takes the lock; the wait between walks does not.
	 */
	tries = 0;
	lockrt(s, sc);
	gcfoldall(s, sc);
	unlockrt(s);
	while(sc->gcoutstanding > 0 && tries < NvGcdrainmax){
		gcwait(sc, NvGcdrainwaitms);
		lockrt(s, sc);
		gcfoldall(s, sc);
		unlockrt(s);
		tries++;
	}
	if(sc->gcoutstanding > 0){
		snprint(err, nerr, "%llud collector(s) never completed", sc->gcoutstanding);
		return -1;
	}
	/*
	 * R3-F06: every heap is idle, but a child that published idle and
	 * was descheduled before its semrelease still holds gcsem. Freeing
	 * gcsem now would let that release land in whatever malloc hands
	 * the address to next -- in the test binaries, the next scheduler's
	 * gcsem or gchold. Wait until every launched child's single credit
	 * has been consumed; only then is no collector touching anything.
	 */
	while(sc->gccredits < sc->gclaunched && tries < NvGcdrainmax){
		if(gcwait(sc, NvGcdrainwaitms) <= 0)
			tries++;
	}
	if(sc->gccredits < sc->gclaunched){
		snprint(err, nerr, "%llud collector(s) never signalled", sc->gclaunched-sc->gccredits);
		return -1;
	}
	return 0;
}

void
nvschedfree(NvScheduler *s)
{
	NvSched *sc;
	char err[128];
	int i;

	if(s == nil)
		return;
	/* Every proc's collectors must be done before any heap they touch is freed. */
	for(i = 0; i < s->nsched; i++)
		if(gcdrain(s, s->sched[i], err, sizeof err) < 0)
			sysfatal("nervous: nvschedfree: sched %d: %s", i, err);
	nvruntimefree(&s->runtime);
	if(s->lastexit != nil)
		nvfragfree(s->lastexit);
	if(s->rootvalue != nil)
		nvfragfree(s->rootvalue);
	for(i = 0; i < s->nsched; i++){
		sc = s->sched[i];
		free(sc->gcsem);
		free(sc->gchold);
		free(sc->sem);
		free(sc);
	}
	free(s->sched);
	free(s->finished);
	memset(s, 0, sizeof *s);
}

/*
 * D074 "Test determinism": nil-safe test hook, a no-op unless a test has
 * called it. See the declaration in nvsched.h for the full contract.
 * Engages the hold on every scheduler proc's collectors at once.
 */
void
nvschedgchold(NvScheduler *s, int hold)
{
	NvSched *sc;
	int i;

	if(s == nil || s->sched == nil)
		return;
	for(i = 0; i < s->nsched; i++){
		sc = s->sched[i];
		if(sc != nil && sc->gchold != nil)
			*sc->gchold = hold != 0;
	}
}

/* D083: the new process is owned by, and queued on, scheduler sc. */
static int
spawn(NvScheduler *s, NvSched *sc, char *entry, NvTerm arg, NvTerm *pid, NvWork *w, char *err, int nerr)
{
	NvExec *e;

	if(err != nil && nerr > 0)
		err[0] = 0;
	if(nvprocspawnon(&s->runtime, sc->index, pid, err, nerr) < 0)
		return -1;
	e = mallocz(sizeof *e, 1);
	if(e == nil){
		snprint(err, nerr, "system_limit");
		nvprocexit(&s->runtime, *pid);
		return -1;
	}
	/*
	 * The argument is a word in the spawning process's own registers
	 * (D065's host callback contract); nvexecinitw copies it into the
	 * fresh child heap, so nothing here retains it past this call. D080:
	 * the copy's visits go to w -- the spawner's work when a process
	 * spawned, nil when the host did.
	 */
	if(nvexecinitw(e, s->module, entry, arg, s->runtime.limits.maxheap, nil, 0, w, err, nerr) < 0 ||
	   nvexecsetframelimit(e, s->runtime.limits.maxframe) < 0){
		if(err[0] == 0)
			snprint(err, nerr, "bad process limits");
		nvexecfree(e);
		free(e);
		nvprocexit(&s->runtime, *pid);
		return -1;
	}
	nvexecsethost(e, &s->host);
	nvexecsetworklimit(e, s->maxtermwork);
	e->gcstress = s->runtime.limits.gcstress;
	/* Re-fetch by slot: harmless now that D082's segmented table never
	 * moves an existing slot, but kept since nvprocat is one indirection
	 * regardless and this stays correct even if that ever changed again. */
	nvprocat(&s->runtime, nvpidslot(*pid))->exec = e;
	return 0;
}

static int
spawntimed(NvScheduler *s, NvSched *sc, char *entry, NvTerm arg, NvTerm *pid, NvWork *w, char *err, int nerr)
{
	uvlong start;
	int rc;

	start = sc->profile ? uptime() : 0;
	rc = spawn(s, sc, entry, arg, pid, w, err, nerr);
	if(sc->profile)
		sc->spawnns += uptime()-start;
	return rc;
}

/* A host spawn (no running process) is owned by scheduler 0, the N=1 facade's. */
int
nvschedspawn(NvScheduler *s, char *entry, NvTerm arg, NvTerm *pid, char *err, int nerr)
{
	int rc;

	lockrt(s, nil);
	rc = spawntimed(s, s->sched[0], entry, arg, pid, nil, err, nerr);
	unlockrt(s);
	return rc;
}

/*
 * D080: per-traversal visit ceiling installed into every process spawned
 * from now on (0 = none). Kept on the scheduler rather than NvLimits
 * because every NvLimits construction site in this tree assigns fields
 * without zeroing the struct (D074), so a new field there is stack
 * garbage at each site not explicitly updated; a scheduler field is
 * zero from nvschedinit's memset.
 */
void
nvschedsetworklimit(NvScheduler *s, uvlong max)
{
	s->maxtermwork = max;
}

int
nvschedspawnroot(NvScheduler *s, char *entry, NvTerm arg, NvTerm *pid, char *err, int nerr)
{
	if(s->rootvalid){
		snprint(err, nerr, "scheduler root already set");
		return -1;
	}
	if(nvschedspawn(s, entry, arg, pid, err, nerr) < 0)
		return -1;
	s->rootslot = nvpidslot(*pid);
	s->rootgeneration = nvpidgeneration(*pid);
	s->rootvalid = 1;
	s->rootstate = NvRootRunning;
	return 0;
}

static void
gcsample(NvSched *sc, NvExec *e, int ok)
{
	if(!ok){
		sc->gcfailed++;
		return;
	}
	sc->collections++;
	sc->lastlivewords = e->livewords;
	if(e->livewords > sc->maxlivewords)
		sc->maxlivewords = e->livewords;
}

/* D074 "gcoffload polarity": 0 means never off-process; a threshold N
 * means a collection scanning at least N used+adopted words goes
 * off-process. Every real execution heap holds at least its argument
 * tuple (>=1 word) after nvexecinit, so a threshold of 1 already means
 * "always off-process", the distinct force-all-off-process test/stress
 * setting D068 originally spelled as gcoffload==0. */
static int
shouldoffload(NvScheduler *s, NvExec *e)
{
	uvlong limit;

	limit = s->runtime.limits.gcoffload;
	return limit != 0 && e->heap.words >= limit;
}

/*
 * D074 "Collector child argument scope": this function's only parameters
 * are the one NvExec to collect and the malloc'd pointers it must touch
 * to signal completion and honor the test hold point. It has no path to
 * NvRuntime, NvProcess, or NvScheduler -- not because RFMEM fails to
 * share that memory (it shares all of it), but so that the restriction
 * is visible from the function signature alone, matching D068's
 * ownership split as a source-level discipline in the collector's own
 * code, not merely a runtime property. Runs in the forked collector proc
 * only; never called directly by the scheduler proc.
 */
enum {
	NvGcholdpollms = 5,
};

static void
collectorchild(NvExec *e, int demand, long *gcsem, long *gchold)
{
	int rc;

	/*
	 * R3-F05: mirror doinline exactly. nvexecgc services a *pending
	 * demand request* and returns without collecting when there is none
	 * -- which is every idle-sweep launch. The original child called
	 * nvexecgc unconditionally, so every off-process idle collection was
	 * a fork that collected nothing, reported failure to the fold, and
	 * left the waiting process eligible to be "collected" again on the
	 * next idle pass, forever. The outcome goes to gcresult, not
	 * gcretry, for the reason given at that field.
	 */
	if(demand){
		nvexecgc(e);
		e->gcresult = e->gcretry;
	}else{
		rc = nvexeccollect(e, 0);
		e->gcresult = rc == 0 ? 1 : rc == NvTermlimit ? 2 : 3;
	}
	/* D074 "Test determinism": absent/no-op unless a test engaged it. */
	if(gchold != nil)
		while(*gchold != 0)
			sleep(NvGcholdpollms);
	lock(&e->heap.lock);
	e->heap.owner = NvHeapIdle;
	unlock(&e->heap.lock);
	/* Last touch of shared memory; the parent counts it (R3-F06). */
	semrelease(gcsem, 1);
	_exits(nil);
}

/*
 * R3-F06: the one way to consume a completion credit. Counting them is
 * what lets nvschedfree know every forked child has executed its final
 * semrelease before gcsem is freed; see gcdrain.
 */
static int
gcwait(NvSched *sc, ulong ms)
{
	int rc;

	rc = tsemacquire(sc->gcsem, ms);
	if(rc > 0)
		sc->gccredits++;
	return rc;
}

/*
 * D074 "Locked completion fold" and "gcoffload polarity"/"Launch". Runs
 * only in the scheduler proc. ok/demand collection bookkeeping shared by
 * the ordinary inline path and by an off-process launch that failed
 * (rfork) and fell back to collecting immediately in the parent.
 */
static void
doinline(NvSched *sc, NvExec *e, int demand)
{
	int ok;

	if(demand){
		nvexecgc(e);
		ok = e->gcretry == 1;
	}else
		ok = nvexeccollect(e, 0) == 0;
	if(ok)
		sc->gcoutputwords += e->livewords;
	gcsample(sc, e, ok);
}

/*
 * demand/idle collection dispatch. allowoffload lets a capped idle sweep
 * (D074 "Idle-sweep concurrency is bounded") force an otherwise-eligible
 * collection inline once its per-sweep fork budget is spent, without
 * skipping it outright.
 */
/*
 * M10-T01/D081: called with the runtime lock held and returns with it
 * held, but releases it across the two long operations -- the rfork and
 * an inline collection -- so a collection pauses only this scheduler's
 * queue (D089), not every scheduler's runtime access. Before releasing,
 * the heap is marked NvHeapCollecting under the lock (offlaunched too,
 * for the off-process case), which is what nvprocsteal checks: the
 * process is unmovable until this scheduler marks the heap idle again
 * under the lock. Nothing else can touch a Prrunnable/Prwaiting
 * process's heap, so the released window is safe (D068).
 */
static void
collect(NvScheduler *s, NvSched *sc, NvExec *e, int demand, int allowoffload)
{
	uvlong start;
	int pid;

	sc->gcinputwords += e->heap.words;
	if(demand)
		sc->gcdemand++;
	else
		sc->gcidle++;
	/*
	 * D074 "Launch". Precondition (assert rather than merely trust):
	 * the heap must be idle and the process runnable or waiting, never
	 * running -- true here by construction, since this is called only
	 * from the demand path right after nvprocyield and from the idle
	 * sweep over Prwaiting slots.
	 */
	lock(&e->heap.lock);
	e->heap.owner = NvHeapCollecting;
	unlock(&e->heap.lock);
	if(allowoffload && shouldoffload(s, e)){
		e->offlaunched = 1;
		sc->gcoutstanding++;
		unlockrt(s);
		pid = rfork(RFPROC|RFMEM|RFNOWAIT);
		if(pid == 0)
			collectorchild(e, demand, sc->gcsem, sc->gchold);	/* never returns */
		lockrt(s, sc);
		if(pid > 0){
			/* Parent: completion is discovered lazily by gcfold. */
			sc->gclaunched++;
			return;
		}
		/* Launch failed: fall back to inline, counted separately
		 * from a deliberate policy choice to collect inline. */
		sc->gcofffallback++;
		sc->gcoutstanding--;
		e->offlaunched = 0;
	}
	unlockrt(s);
	start = sc->profile ? uptime() : 0;
	doinline(sc, e, demand);
	if(sc->profile)
		sc->gcns += uptime()-start;
	lockrt(s, sc);
	lock(&e->heap.lock);
	e->heap.owner = NvHeapIdle;
	unlock(&e->heap.lock);
}

/*
 * D074 "Locked completion fold": read owner and, only if idle with
 * offlaunched still set, gcretry/livewords, all while holding the heap's
 * Lock -- an unlocked read after observing owner==idle is not guaranteed
 * to see the collector child's earlier writes on every architecture
 * (this project builds with 7c, i.e. arm64). One lock acquisition per
 * dispatch attempt of a slot with offlaunched set, not per dispatch in
 * general, so it costs nothing on the common path. A no-op until the
 * collector has actually published completion.
 */
/*
 * Returns 1 if this exec is safe to dispatch (never launched off-process,
 * or its collector has completed and been folded), 0 if a collector is
 * still holding this heap. This is the ONLY read of e->heap.owner that
 * may decide a dispatch outcome; callers must not re-read owner
 * unlocked afterward and treat it as authoritative. An exec that was
 * never launched off-process (offlaunched==0) is idle by the invariant
 * that owner is set to NvHeapCollecting only in the same critical
 * section that sets offlaunched=1 (see collect()), and reset to
 * NvHeapIdle together with offlaunched=0 on every path that clears one
 * (the rfork-failure fallback, and the fold below) -- so no lock is
 * needed to know an unlaunched exec's heap is not collecting.
 *
 * The locked read here is what the rest of nvexecrun's retry logic
 * (prepare()'s unlocked `retry = e->gcretry`) depends on for its memory
 * ordering: once this function has synchronized via the lock and
 * observed NvHeapIdle, the collector proc is provably dead (it never
 * writes gcretry/livewords again after publishing idle), so every
 * later unlocked read of those fields by this same scheduler proc, in
 * its own program order after this point, is safe -- there is no
 * remaining concurrent writer left to race against. Skipping this
 * synchronized witness and instead re-reading owner unlocked (as an
 * earlier draft of this function did) would let the interpreter's
 * first observation of the collector's writes be an unlocked one,
 * which is not ordering-safe on architectures without strong store
 * ordering (7c/arm64).
 */
static int
gcfold(NvSched *sc, NvExec *e)
{
	int idle, ok;
	uvlong live;
	int retry;

	if(!e->offlaunched)
		return 1;
	retry = 0;
	live = 0;
	lock(&e->heap.lock);
	idle = e->heap.owner == NvHeapIdle;
	if(idle){
		retry = e->gcresult;	/* R3-F05: the child's report for either path */
		live = e->livewords;
	}
	unlock(&e->heap.lock);
	if(!idle)
		return 0;
	e->offlaunched = 0;
	sc->gcoutstanding--;
	ok = retry == 1;
	if(!ok){
		sc->gcfailed++;
		return 1;
	}
	sc->gcoutputwords += live;
	sc->collections++;
	sc->lastlivewords = live;
	if(live > sc->maxlivewords)
		sc->maxlivewords = live;
	return 1;
}

/*
 * Coordinator-review fix: gcfold is otherwise reached only through
 * finddispatchable, which examines only Prrunnable slots. A collection
 * launched by the opportunistic idle sweep (D067) targets a Prwaiting
 * process, which stays Prwaiting for as long as no message or deadline
 * wakes it -- possibly forever, in a genuinely deadlocked program. Its
 * completion would then never be folded, offlaunched would never clear,
 * and gcoutstanding would never return to zero, permanently disabling
 * the D046 idle/deadlock report (the "never falsely report idle"
 * corollary would be trivially true only because idle could never be
 * reported again at all). This walks every initialized slot -- runnable
 * or waiting, dispatched or not -- and gives every offlaunched exec a
 * chance to fold, restoring gcoutstanding to an accurate count
 * regardless of run-queue membership.
 */
static void
gcfoldall(NvScheduler *s, NvSched *sc)
{
	NvRuntime *r;
	NvProcess *p;
	NvExec *e;
	ulong i;

	r = &s->runtime;
	for(i = 0; i < r->nslot; i++){
		p = nvprocat(r, i);
		e = p->exec;
		/*
		 * D083/D085: a collector is launched by the process's owner and
		 * the process cannot change owner while offlaunched, so the
		 * owner is the launcher, and gcoutstanding/gclaunched are the
		 * launcher's. Fold only this scheduler's. (Every owner is 0 at N=1.)
		 */
		if(e != nil && e->offlaunched && p->owner == sc->index)
			gcfold(sc, e);
	}
}

/*
 * D074 "Completion"/"Never spin". Finds the head-most runnable slot
 * whose heap is not currently under off-process collection, folding any
 * just-completed off-process collection's stats along the way and
 * requeuing a still-collecting slot to the tail instead of dispatching
 * or blocking on it. Bounded by the run queue's own length, so a queue
 * that is entirely collecting is discovered in exactly nrunnable tries,
 * never spun on. Returns 0 if nothing is currently dispatchable (queue
 * empty, or every runnable slot is collecting).
 */
static int
finddispatchable(NvScheduler *s, NvSched *sc, ulong *outslot)
{
	NvRuntime *r;
	NvProcess *p;
	NvExec *e;
	ulong slot, tries, bound;

	r = &s->runtime;
	bound = sc->runq->nrunnable;
	for(tries = 0; tries < bound; tries++){
		if(!nvprocrunhead(r, sc->index, &slot))
			return 0;
		p = nvprocat(r, slot);
		e = p->exec;
		if(e == nil){
			/* Let the caller's existing nil-exec error check fire. */
			*outslot = slot;
			return 1;
		}
		/*
		 * gcfold's own locked determination is authoritative; do not
		 * re-read e->heap.owner unlocked afterward (see gcfold's
		 * comment for why that would reopen the ordering gap this
		 * function exists to close).
		 */
		if(!gcfold(sc, e)){
			nvprocrequeue(r, slot);
			continue;
		}
		*outslot = slot;
		return 1;
	}
	return 0;
}

/*
 * D074 "Never spin": convert an armed deadline to a bounded millisecond
 * wait for tsemacquire, using the installed clock's own now() (whose
 * contract, D050, is always nanoseconds, real or simulated) rather than
 * a raw OS clock -- consistent whether the installed clock is production
 * or a deterministic test clock. Clamped so this is always a bounded
 * wait, never "forever": at least 1ms, at most a generous ceiling so a
 * far-future deadline does not starve a check for collector progress.
 */
enum {
	NvGcwaitmaxms = 60000,
};

static ulong
deadlinems(NvScheduler *s, uvlong earliest)
{
	uvlong now, diff, ms;

	now = s->clock.now(s->clock.aux);
	if(earliest <= now)
		return 1;
	diff = earliest-now;
	ms = (diff+999999)/1000000;
	if(ms < 1)
		ms = 1;
	if(ms > NvGcwaitmaxms)
		ms = NvGcwaitmaxms;
	return (ulong)ms;
}

/* D074: bound on off-process collectors launched by one idle-sweep pass
 * (a demand-path collection never bursts, since only one process yields
 * NvCollect per dispatch). Beyond the cap, a qualifying waiter is still
 * collected this pass, just inline rather than forked, so the sweep
 * always makes progress -- the cap bounds worst-case Plan 9 process-table
 * pressure, not how much garbage gets reclaimed. Implementer's choice
 * (flagged in the handoff): a compile-time constant rather than a new
 * NvLimits field. */
enum {
	NvGcsweepcap = 8,
};

/* D074 "Never spin": bounded wait granularity when nothing armed a
 * deadline but a collector is outstanding (or a runnable slot is
 * blocked on one); never "forever". */
enum {
	NvGcwaitms = 20,
};

/*
 * The idle half of a step: nothing on this scheduler's queue is
 * dispatchable. Entered with the runtime lock held; every return
 * releases it. At N=1 this blocks exactly where the single-scheduler
 * code always did (clock.wait for a deadline, gcwait for a collector),
 * with the lock released around the wait. At N>1 (M10-T01/D085) it never
 * blocks: it returns NvSchedIdle and leaves sc->wakeat/wakeatvalid for
 * the proc loop, which sleeps on the idle semaphore instead -- so a
 * remote wake can interrupt the sleep, which a clock.wait or a wait on
 * gcsem could not.
 */
static int
idlestep(NvScheduler *s, NvSched *sc, char *err, int nerr)
{
	NvRuntime *r;
	NvProcess *p;
	NvExec *e;
	ulong i;
	int multi;
	{
		uvlong earliest, now;
		ulong nwake, sweeplaunched;
		int havedeadline, allow;

		r = &s->runtime;
		multi = s->nsched > 1;
		sc->wakeatvalid = 0;
		/*
		 * D074 amendment/M08-T04d "Test determinism": see the field
		 * comment in nvsched.h. nil in production; called exactly once
		 * per idle-branch entry, before anything else in this branch
		 * runs.
		 */
		if(sc->gcidlestep != nil)
			sc->gcidlestep(s);

		for(i = 0; i < r->nslot; i++){
			p = nvprocat(r, i);
			if(p->owner == sc->index && p->state == Prrunning){
				unlockrt(s);
				snprint(err, nerr, "process left running outside scheduler dispatch");
				return NvSchedError;
			}
		}
		/* Reclaim waiting-process garbage without touching mailbox state.
		 * The live watermark prevents recollecting an unchanged live set.
		 * D074: cap how many of these launch off-process in one sweep;
		 * beyond the cap, still collect, just inline.
		 *
		 * Coordinator-review fix: eligibility is gated on
		 * `!e->offlaunched`, not an unlocked `e->heap.owner !=
		 * NvHeapCollecting` read (the earlier draft of this loop).
		 * offlaunched is scheduler-proc-only state -- only this proc
		 * ever sets or clears it (collect() sets it, gcfold clears it
		 * after a locked, synchronized check) -- so it needs no lock
		 * and is authoritative in a way a raw read of collector-written
		 * `owner` is not. This matters because `owner` alone can read
		 * NvHeapIdle for an exec whose completion this scheduler has
		 * not yet folded (the collector already published idle, but
		 * gcfoldall has not run since): launching a SECOND collector
		 * for that exec before its first completion is folded would
		 * double-increment gcoutstanding, silently discard the first
		 * collection's gcretry/livewords, and -- far worse -- could run
		 * two collector procs concurrently against the same heap if the
		 * timing were ever unlucky enough. Gating on offlaunched instead
		 * makes "a launch/fold cycle is already in flight for this exec"
		 * the single authoritative reason to skip, matching gcfold's own
		 * treatment of the same flag. */
		sweeplaunched = 0;
		for(i = 0; i < r->nslot; i++){
			p = nvprocat(r, i);
			e = p->exec;
			if(p->owner == sc->index && p->state == Prwaiting && e != nil && e->heap.cur != nil &&
			   !e->offlaunched &&
			   e->heap.words > e->livewords && e->heap.words > e->heap.cur->cap/2){
				allow = sweeplaunched < NvGcsweepcap;
				if(allow && shouldoffload(s, e))
					sweeplaunched++;
				collect(s, sc, e, 0, allow);
			}
		}
		/*
		 * D050: idle with an armed deadline is progress, not deadlock.
		 * Ties (equal deadlines) wake together, matching "advances to
		 * the earliest deadline, makes those processes runnable"; the
		 * ascending-slot-index tie order in D050 is preserved because the
		 * wake loop below enqueues in ascending slot order (D059).
		 */
		havedeadline = 0;
		earliest = 0;
		for(i = 0; i < r->nslot; i++){
			p = nvprocat(r, i);
			if(p->owner != sc->index || p->state != Prwaiting || !p->hasdeadline)
				continue;
			if(!havedeadline || p->deadline < earliest){
				earliest = p->deadline;
				havedeadline = 1;
			}
		}
		/*
		 * Coordinator-review fix: a collection launched by the sweep
		 * above (or an earlier idle pass) may target a Prwaiting
		 * process that never reaches finddispatchable on its own --
		 * that path is the ONLY other place gcfold runs, and it only
		 * ever sees Prrunnable slots. Fold any such already-completed
		 * collection now, so the gcoutstanding check immediately below
		 * reflects reality instead of a count that can only ever have
		 * been incremented, never decremented, for a waiting process
		 * that stays waiting (see gcfoldall's comment).
		 */
		gcfoldall(s, sc);
		if(sc->gcoutstanding == 0){
			/*
			 * D074 amendment (found via bench/largelive.c during
			 * M08-T04r): finddispatchable can return 0 not because the
			 * runnable queue is empty but because every runnable slot's
			 * heap was NvHeapCollecting at that moment. gcfoldall just
			 * proved every offlaunched exec has completed and been
			 * folded, so any such runnable slot is now genuinely
			 * dispatchable -- the very next finddispatchable call would
			 * find it immediately. Reporting NvSchedIdle here with
			 * nrunnable != 0 would be exactly the false idle the "never
			 * falsely report idle or deadlock" corollary above forbids.
			 * Report progress instead and let the caller step again.
			 */
			if(sc->runq->nrunnable != 0){
				unlockrt(s);
				return NvSchedProgress;
			}
			/*
			 * D074 corollary: with no collector a factor, behave
			 * exactly as before off-process collection existed.
			 */
			if(!havedeadline){
				unlockrt(s);
				return NvSchedIdle;
			}
			if(s->clock.now == nil || s->clock.wait == nil){
				unlockrt(s);
				snprint(err, nerr, "scheduler has an armed deadline but no clock installed");
				return NvSchedError;
			}
			if(multi){
				/*
				 * D085: do not block here -- another scheduler may wake
				 * one of this scheduler's processes meanwhile. If the
				 * deadline has not passed, hand the bound to the proc
				 * loop; if it has, fall through to the wake below.
				 */
				now = s->clock.now(s->clock.aux);
				if(now < earliest){
					sc->wakeat = earliest;
					sc->wakeatvalid = 1;
					unlockrt(s);
					return NvSchedIdle;
				}
			}else{
				/* D050: the single scheduler sleeps until the deadline. */
				unlockrt(s);
				s->clock.wait(s->clock.aux, earliest);
				lockrt(s, sc);
				now = earliest;
			}
			nwake = 0;
			for(i = 0; i < r->nslot; i++){
				p = nvprocat(r, i);
				if(p->owner == sc->index && p->state == Prwaiting && p->hasdeadline && p->deadline <= now){
					if(nvprocwakefrom(r, sc->index, i) < 0){
						unlockrt(s);
						snprint(err, nerr, "deadline wake of a non-waiting process");
						return NvSchedError;
					}
					nwake++;
				}
			}
			if(nwake == 0){
				unlockrt(s);
				snprint(err, nerr, "clock did not advance past the earliest deadline");
				return NvSchedError;
			}
			sc->timerwakes += nwake;
			unlockrt(s);
			return NvSchedProgress;
		}
		/*
		 * Coordinator-review fix: drain any stale completion credits
		 * before blocking. gcfold, when reached through the ordinary
		 * dispatch path (finddispatchable), decrements gcoutstanding
		 * without ever consuming that exec's semrelease -- the credit
		 * is left sitting in gcsem's count. Left undrained, the
		 * tsemacquire below would return immediately on that stale
		 * credit on every single idle pass for the rest of this
		 * scheduler's life, turning this "never spin" wait into
		 * exactly the busy loop it exists to prevent (gcfoldall still
		 * keeps gcoutstanding correct regardless, so this was never a
		 * correctness bug, only a performance one -- but an unbounded
		 * one). Draining and re-folding here also closes the narrow
		 * lost-wakeup window between the gcfoldall above and this
		 * point: a completion landing in that window is caught by the
		 * immediate recheck below instead of costing a full wait
		 * period.
		 */
		while(gcwait(sc, 0) > 0)
			;
		gcfoldall(s, sc);
		if(sc->gcoutstanding == 0){
			unlockrt(s);
			return NvSchedProgress;
		}
		/*
		 * D074 "Never spin"/corollary: a collector is outstanding (which
		 * is also true whenever a runnable slot is blocked collecting).
		 * NvSchedIdle/deadlock may never be reported here. If the
		 * installed clock already shows the earliest deadline expired,
		 * wake it directly rather than waiting behind an unrelated
		 * collector; otherwise block on the completion semaphore,
		 * bounded by the deadline (converted to milliseconds) if one is
		 * armed, or by NvGcwaitms if not. tsemacquire returning 0
		 * (timeout) or -1 (interrupted) just costs another recheck on
		 * the caller's next call, never a lost wakeup, because
		 * semrelease's count persists.
		 */
		if(havedeadline && s->clock.now != nil){
			now = s->clock.now(s->clock.aux);
			if(now >= earliest){
				nwake = 0;
				for(i = 0; i < r->nslot; i++){
					p = nvprocat(r, i);
					if(p->owner == sc->index && p->state == Prwaiting && p->hasdeadline && p->deadline <= now){
						if(nvprocwakefrom(r, sc->index, i) < 0){
							unlockrt(s);
							snprint(err, nerr, "deadline wake of a non-waiting process");
							return NvSchedError;
						}
						nwake++;
					}
				}
				if(nwake != 0)
					sc->timerwakes += nwake;
				unlockrt(s);
				return NvSchedProgress;
			}
			if(multi){
				/* D085: the proc loop sleeps on the idle semaphore, bounded
				 * by this deadline and by the collector wait; gcoutstanding
				 * > 0 keeps it out of termination detection. */
				sc->wakeat = earliest;
				sc->wakeatvalid = 1;
				unlockrt(s);
				return NvSchedIdle;
			}
			unlockrt(s);
			gcwait(sc, deadlinems(s, earliest));
			return NvSchedProgress;
		}
		if(multi){
			unlockrt(s);
			return NvSchedIdle;
		}
		unlockrt(s);
		gcwait(sc, NvGcwaitms);
		return NvSchedProgress;
	}
}

/*
 * D083 (M10-T01, measured): a queue is worth stealing from, and worth
 * waking a sleeper for, only when it holds a BACKLOG -- at least this
 * many runnable processes after the one being dispatched. The first
 * landing stole at >= 1, and on a ring (exactly one runnable process in
 * the whole machine) the two schedulers stole the token from each other
 * 120K times per 2M hops, migrating it across cores on every hop for a
 * 2x slowdown at -p 2. One runnable process is simply run by its owner
 * after its current quantum; a remote wake onto an idle owner still
 * kicks unconditionally (that is delivery, not stealing).
 */
enum {
	NvStealmin = 2,
};

/*
 * D085: a backlog is left on this scheduler's queue and someone is
 * asleep -- kick the lowest idle scheduler so it can steal. Under the
 * runtime lock. Clearing the bit here means one kick per sleep; the
 * sleeper clears it too if it wakes by timeout.
 */
static void
kickidle(NvScheduler *s, NvSched *sc)
{
	int i;

	for(i = 0; i < s->nsched; i++)
		if(s->idlemask & (1ULL<<i)){
			s->idlemask &= ~(1ULL<<i);
			s->nidle--;
			semrelease(s->sched[i]->sem, 1);
			sc->wakessent++;
			return;
		}
}

/*
 * D087: one scheduling step of scheduler proc sc. nvschedstep is the
 * N=1 facade (sc = sched[0]); nvmachinerun's per-proc loop calls this
 * with its own sc. Everything that scans the process table is filtered
 * to the slots sc owns (D083) -- a no-op at N=1, where every owner is 0.
 * D081: the runtime lock is held from entry to the dispatch, released
 * for nvexecrun, and held again for the post-run bookkeeping.
 */
static int
step(NvScheduler *s, NvSched *sc, char *err, int nerr)
{
	NvRuntime *r;
	NvProcess *p;
	NvExec *e;
	NvTerm pid;
	ulong slot;
	uvlong before, start;
	int state, isroot, woke;

	r = &s->runtime;
	lockrt(s, sc);
	if(r->nlive == 0){
		unlockrt(s);
		return NvSchedDone;
	}
	if(r->nslot == 0){
		unlockrt(s);
		snprint(err, nerr, "scheduler has live processes but no slots");
		return NvSchedError;
	}
	/*
	 * D059/D074/D083: dispatch is the head of this scheduler's FIFO run queue,
	 * discovered in O(runnable) even under off-process collection
	 * (finddispatchable requeues a collecting slot to the tail and folds
	 * any just-completed collection's stats along the way, but never
	 * dispatches or blocks on one). idlestep's scans run only when
	 * nothing is currently dispatchable: the queue is empty, or every
	 * runnable slot is collecting.
	 */
	if(!finddispatchable(s, sc, &slot))
		return idlestep(s, sc, err, nerr);	/* releases the lock */
	p = nvprocat(r, slot);
	if(p->exec == nil){
		unlockrt(s);
		snprint(err, nerr, "runnable process has no execution state");
		return NvSchedError;
	}
	e = p->exec;
	pid = nvpid(slot, p->generation);
	if(nvprocdispatch(r, pid, err, nerr) < 0){
		unlockrt(s);
		return NvSchedError;
	}
	sc->dispatches++;
	sc->currentslot = slot;
	sc->currentgeneration = p->generation;
	sc->currentvalid = 1;
	e->sched = sc;	/* D087: valid for this quantum and its host callbacks only */
	if(s->idlemask != 0 && sc->runq->nrunnable >= NvStealmin)
		kickidle(s, sc);
	unlockrt(s);
	before = e->reductions;
	start = sc->profile ? uptime() : 0;
	state = nvexecrun(e, s->quantum);
	if(sc->profile)
		sc->execns += uptime()-start;
	sc->reductions += e->reductions - before;
	e->sched = nil;
	sc->currentvalid = 0;
	lockrt(s, sc);
	/* D082: the segmented table never moves a slot, so this re-fetch is
	 * merely harmless, not required as it was before. */
	p = nvprocat(r, slot);
	/*
	 * D081: off the CPU. A sender on another scheduler that found this
	 * process Prwaiting during the quantum deferred its wake; this is
	 * where that wake lands (woke == 1: the process is now Prrunnable on
	 * this queue, which the NvYield check below must accept).
	 */
	woke = nvprocoffcpu(r, pid);
	if(woke < 0){
		unlockrt(s);
		snprint(err, nerr, "dispatched process vanished during its quantum");
		return NvSchedError;
	}
	if(state == NvCollect){
		if(p->state != Prrunning || nvprocyield(r, pid, err, nerr) < 0){
			unlockrt(s);
			return NvSchedError;
		}
		/* D074: stopped owner, enqueued once, no host callback active;
		 * decides inline vs. off-process from gcoffload. Releases and
		 * retakes the lock around the collection itself. */
		collect(s, sc, e, 1, 1);
		unlockrt(s);
		return NvSchedProgress;
	}
	if(state == NvYield){
		if(p->state == Prrunning){
			if(nvprocyield(r, pid, err, nerr) < 0){
				unlockrt(s);
				return NvSchedError;
			}
		}else if(p->state != Prwaiting && !(woke && p->state == Prrunnable)){
			unlockrt(s);
			snprint(err, nerr, "yielded process has bad lifecycle state");
			return NvSchedError;
		}
		unlockrt(s);
		return NvSchedProgress;
	}
	isroot = s->rootvalid && slot == s->rootslot && p->generation == s->rootgeneration;
	if(state == NvDone){
		s->completed++;
		if(isroot){
			s->rootstate = NvRootDone;
			if(s->rootvalue != nil)
				nvfragfree(s->rootvalue);
			s->rootvalue = e->result;
			e->result = nil;
		}
	}else if(state == NvFault){
		s->faulted++;
		snprint(s->lastfault, sizeof s->lastfault, "%s", e->fault);
		if(isroot){
			s->rootstate = NvRootFault;
			snprint(s->rootfault, sizeof s->rootfault, "%s", e->fault);
		}
	}else if(state == NvExit){
		s->exited++;
		if(s->lastexit != nil)
			nvfragfree(s->lastexit);
		s->lastexit = e->exitreason;
		e->exitreason = nil;
		if(isroot){
			NvFrag *copy;

			/*
			 * The same fragment cannot be owned by both lastexit and
			 * rootvalue, so rootvalue gets an independent copy of the
			 * reason lastexit now owns. A copy failure is reported the
			 * same way the old NvValue-based deep copy reported it:
			 * rootstate becomes NvRootFault with system_limit rather
			 * than leaving rootvalue holding stale or absent data.
			 * D081 audit note: this copy runs under the runtime lock.
			 * It is the one allocation of unbounded size held under
			 * it, and it happens once per machine lifetime (the root's
			 * exit only), so it is accepted rather than bracketed.
			 */
			s->rootstate = NvRootExit;
			if(s->rootvalue != nil)
				nvfragfree(s->rootvalue);
			s->rootvalue = nil;
			if(nvfragcopy(s->lastexit->root, ~0ULL, &copy) < 0){
				s->rootstate = NvRootFault;
				snprint(s->rootfault, sizeof s->rootfault, "system_limit");
			}else
				s->rootvalue = copy;
		}
	}else{
		unlockrt(s);
		snprint(err, nerr, "bad execution state");
		return NvSchedError;
	}
	if(nvprocexit(r, pid) != 1){
		unlockrt(s);
		snprint(err, nerr, "failed to exit completed process");
		return NvSchedError;
	}
	unlockrt(s);
	return NvSchedProgress;
}

/* D087: the N=1 facade every deterministic test drives; the same code nvmachinerun's proc loop runs. */
int
nvschedstep(NvScheduler *s, char *err, int nerr)
{
	return step(s, s->sched[0], err, nerr);
}

/*
 * D085: record why the machine stops (first reason wins) and wake every
 * sleeping scheduler so it notices. Takes the runtime lock.
 */
static void
stopmachine(NvScheduler *s, int why, char *err)
{
	int i;

	lockrt(s, nil);
	if(s->stopping == NvStopNone){
		s->stopping = why;
		if(err != nil)
			snprint(s->stoperr, sizeof s->stoperr, "%s", err);
	}
	for(i = 0; i < s->nsched; i++)
		semrelease(s->sched[i]->sem, 1);
	unlockrt(s);
}

void
nvmachineinterrupt(NvScheduler *s)
{
	int i;

	/* From a note handler: no lock. A racing stopmachine keeps its own reason. */
	if(s->stopping == NvStopNone)
		s->stopping = NvStopInterrupt;
	for(i = 0; i < s->nsched; i++)
		semrelease(s->sched[i]->sem, 1);
}

/*
 * D083: take the head of the most loaded other queue. Under the runtime
 * lock. Bounded: one candidate; an unmovable head (collecting) means
 * this scheduler sleeps and is kicked again if work remains.
 */
static int
trysteal(NvScheduler *s, NvSched *sc)
{
	NvRuntime *r;
	NvRunq *q;
	ulong head;
	int i, victim;

	r = &s->runtime;
	victim = -1;
	for(i = 0; i < s->nsched; i++){
		if(i == sc->index)
			continue;
		q = r->runq[i];
		if(q->nrunnable >= NvStealmin && (victim < 0 || q->nrunnable > r->runq[victim]->nrunnable))
			victim = i;
	}
	if(victim < 0)
		return 0;
	if(!nvprocrunhead(r, victim, &head) || nvprocsteal(r, head, sc->index) < 0)
		return 0;
	sc->stealstaken++;
	s->sched[victim]->stealsgiven++;
	return 1;
}

/*
 * D085/D046 termination: every scheduler is idle, none has a collector
 * outstanding or a deadline pending, and no queue holds a runnable
 * process. Exact under the stage-1 lock: every transition into runnable
 * and every idle transition happens under it, and an idle scheduler's
 * gcoutstanding/wakeatvalid are written only by itself, which is asleep.
 */
static int
machineidle(NvScheduler *s)
{
	NvSched *sc;
	int i;

	if(s->nidle != s->nsched)
		return 0;
	for(i = 0; i < s->nsched; i++){
		sc = s->sched[i];
		if(sc->gcoutstanding != 0 || sc->wakeatvalid || sc->runq->nrunnable != 0)
			return 0;
	}
	return 1;
}

/*
 * D085: the idle protocol, entered after step() returned NvSchedIdle.
 * Under the lock: re-check our own queue; try to steal; else drain stale
 * credits, set the idle bit, decide termination, compute the sleep
 * bound. Then sleep on the idle semaphore with the lock released. A wake
 * between the bit being set and the sleep cannot be lost: the waker sees
 * the bit under the same lock and its semrelease persists until the
 * tsemacquire.
 *
 * The own-queue re-check closes the one window the bit does not cover
 * (M10-T01 round 2, found by examples/ring.nv at -p 2): idlestep decided
 * "nothing to run" and released the lock; a remote wake then enqueued
 * onto this queue and, seeing no idle bit, sent no kick; without the
 * re-check this scheduler would set its bit and sleep the full bound
 * with a runnable process on its own queue -- and the D046 termination
 * test would rightly refuse to end the machine. Round 1 hid this because
 * a steal threshold of 1 let the other scheduler take the lone runnable
 * off the sleeper's queue (part of the 120K steals on the ring);
 * NvStealmin = 2 removed that accident and exposed the stall.
 * finddispatchable rather than nrunnable, so a queue whose runnable
 * slots are all under off-process collection still sleeps (bounded by
 * NvGcwaitms below) rather than spinning until the collector is done.
 */
static void
idle(NvScheduler *s, NvSched *sc)
{
	uvlong start, bit;
	ulong ms, slot;
	int rc;

	bit = 1ULL<<sc->index;
	lockrt(s, sc);
	if(s->stopping != NvStopNone || finddispatchable(s, sc, &slot) || trysteal(s, sc)){
		unlockrt(s);
		return;
	}
	while(tsemacquire(sc->sem, 0) > 0)
		;
	s->idlemask |= bit;
	s->nidle++;
	if(machineidle(s)){
		s->idlemask &= ~bit;
		s->nidle--;
		unlockrt(s);
		stopmachine(s, NvStopIdle, nil);
		return;
	}
	ms = sc->wakeatvalid ? deadlinems(s, sc->wakeat) : NvGcwaitmaxms;
	if(sc->gcoutstanding != 0 && ms > NvGcwaitms)
		ms = NvGcwaitms;
	sc->sleeps++;
	unlockrt(s);
	start = uptime();
	rc = tsemacquire(sc->sem, ms);
	sc->sleepns += uptime()-start;
	lockrt(s, sc);
	if(s->idlemask & bit){
		s->idlemask &= ~bit;
		s->nidle--;
	}
	if(rc > 0)
		sc->wakesrecv++;
	unlockrt(s);
}

/*
 * D087: one scheduler proc's whole life. sched[0] runs this on the
 * calling proc; the others in rfork'd procs sharing memory. Ends when
 * the machine is stopping; drains its own collectors (D074/D089) and,
 * if forked, signals `finished` as its last touch of shared memory.
 */
static void
schedproc(NvScheduler *s, NvSched *sc)
{
	char err[256];
	int rc;

	while(s->stopping == NvStopNone){
		rc = step(s, sc, err, sizeof err);
		if(rc == NvSchedProgress)
			continue;
		if(rc == NvSchedDone){
			stopmachine(s, NvStopDone, nil);
			break;
		}
		if(rc == NvSchedError){
			stopmachine(s, NvStopError, err);
			break;
		}
		idle(s, sc);
	}
	if(gcdrain(s, sc, err, sizeof err) < 0){
		lockrt(s, sc);
		s->stopping = NvStopError;
		snprint(s->stoperr, sizeof s->stoperr, "sched %d: %s", sc->index, err);
		unlockrt(s);
	}
	if(sc->index != 0)
		semrelease(s->finished, 1);
}

/* A forked scheduler proc that never finishes is a bug, not something to wait forever on. */
enum {
	NvFinishwaitms = 60000,
};

int
nvmachinerun(NvScheduler *s, char *err, int nerr)
{
	int i, started, pid, rc;

	s->stopping = NvStopNone;
	s->stoperr[0] = 0;
	s->idlemask = 0;
	s->nidle = 0;
	started = 0;
	for(i = 1; i < s->nsched; i++){
		pid = rfork(RFPROC|RFMEM|RFNOWAIT);
		if(pid < 0){
			stopmachine(s, NvStopError, "system_limit: cannot fork a scheduler proc");
			break;
		}
		if(pid == 0){
			schedproc(s, s->sched[i]);
			_exits(nil);
		}
		started++;
	}
	schedproc(s, s->sched[0]);
	/*
	 * D085: a note (the interrupt handler in main.c, or any other) makes
	 * tsemacquire return -1 without consuming a credit; that is not a
	 * missing scheduler, so retry. Only a genuine timeout (0) is fatal.
	 */
	for(i = 0; i < started; i++){
		while((rc = tsemacquire(s->finished, NvFinishwaitms)) < 0)
			;
		if(rc == 0)
			sysfatal("nervous: nvmachinerun: a scheduler proc never finished");
	}
	switch(s->stopping){
	case NvStopDone:
		return NvSchedDone;
	case NvStopIdle:
		return NvSchedIdle;
	case NvStopInterrupt:
		snprint(err, nerr, "interrupted");
		return NvSchedError;
	default:
		snprint(err, nerr, "%s", s->stoperr[0] ? s->stoperr : "scheduler stopped for no recorded reason");
		return NvSchedError;
	}
}
