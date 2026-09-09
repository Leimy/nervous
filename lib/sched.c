#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../include/nvbc.h"
#include "../include/nvvm.h"
#include "../include/nvexec.h"
#include "../include/nvproc.h"
#include "../include/nvsched.h"

/*
 * D065: every NvExec refers to this one table by pointer (nvexecsethost),
 * so a callback recovers its scheduler through e->host->aux rather than
 * through a per-process copy. The pid a callback acts on is always the
 * currently dispatched one, built on the fly from currentslot/generation.
 */
static int
currentpid(NvScheduler *s, NvTerm *pid, char *err, int nerr)
{
	if(!s->currentvalid){ snprint(err,nerr,"bad_process_context"); return -1; }
	*pid = nvpid(s->currentslot, s->currentgeneration);
	return 0;
}

static int
hostself(NvExec *e, NvTerm *value, char *err, int nerr)
{
	NvScheduler *s;

	s = e->host->aux;
	return currentpid(s, value, err, nerr);
}

static int
hostref(NvExec *e, NvTerm *value, char *err, int nerr)
{
	NvScheduler *s;

	s = e->host->aux;
	return nvprocref(&s->runtime, &e->heap, value, err, nerr);
}

static int
hostsend(NvExec *e, NvTerm pid, NvTerm value, char *err, int nerr)
{
	NvScheduler *s;

	s = e->host->aux;
	return nvprocsend(&s->runtime, pid, value, err, nerr) < 0 ? -1 : 0;
}

static int
hostspawn(NvExec *e, char *entry, NvTerm arg, NvTerm *pid, char *err, int nerr)
{
	NvScheduler *s;

	s = e->host->aux;
	return nvschedspawn(s, entry, arg, pid, err, nerr);
}

/* Receive callbacks operate only on the currently dispatched process. */
static int
hostrecvbegin(NvExec *e, NvTerm *value, char *err, int nerr)
{
	NvScheduler *s;
	NvTerm pid;

	s = e->host->aux;
	if(currentpid(s, &pid, err, nerr) < 0)
		return -1;
	return nvprocrecvbegin(&s->runtime, pid, value, err, nerr);
}

static int
hostrecvnext(NvExec *e, NvTerm *value, char *err, int nerr)
{
	NvScheduler *s;
	NvTerm pid;

	s = e->host->aux;
	if(currentpid(s, &pid, err, nerr) < 0)
		return -1;
	return nvprocrecvnext(&s->runtime, pid, value, err, nerr);
}

static int
hostrecvneed(NvExec *e, uvlong *words, char *err, int nerr)
{
	NvScheduler *s;
	NvTerm pid;

	s = e->host->aux;
	if(currentpid(s, &pid, err, nerr) < 0)
		return -1;
	return nvprocrecvneed(&s->runtime, pid, words, err, nerr);
}

static int
hostrecvtake(NvExec *e, NvFrag **taken, char *err, int nerr)
{
	NvScheduler *s;
	NvTerm pid;

	s = e->host->aux;
	if(currentpid(s, &pid, err, nerr) < 0)
		return -1;
	return nvprocrecvtake(&s->runtime, pid, taken, err, nerr);
}

static int
hostrecvwait(NvExec *e, char *err, int nerr)
{
	NvScheduler *s;
	NvTerm pid;

	s = e->host->aux;
	if(currentpid(s, &pid, err, nerr) < 0)
		return -1;
	return nvprocrecvwait(&s->runtime, pid, err, nerr);
}

static int
hostrecvdeadline(NvExec *e, NvTerm duration, char *err, int nerr)
{
	NvScheduler *s;
	NvTerm pid;

	s = e->host->aux;
	if(currentpid(s, &pid, err, nerr) < 0)
		return -1;
	return nvprocarmdeadline(&s->runtime, pid, duration, s->clock.now(s->clock.aux), err, nerr);
}

static int
hostrecvwaitdeadline(NvExec *e, char *err, int nerr)
{
	NvScheduler *s;
	NvTerm pid;

	s = e->host->aux;
	if(currentpid(s, &pid, err, nerr) < 0)
		return -1;
	return nvprocrecvwaitdeadline(&s->runtime, pid, s->clock.now(s->clock.aux), err, nerr);
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
static int
hostprint(NvExec *e, NvTerm value, char *err, int nerr)
{
	NvScheduler *s;
	int rc;

	s = e->host->aux;
	if(s->io.out == nil){ snprint(err,nerr,"bad_process_context"); return -1; }
	rc = nvtermprint(s->io.out, value);
	Bputc(s->io.out, '\n');
	if(Bflush(s->io.out) < 0){ snprint(err,nerr,"io_error"); return -1; }
	if(rc == NvTermlimit){ snprint(err,nerr,"system_limit"); return -1; }
	return 0;
}

static int
hosteprint(NvExec *e, NvTerm value, char *err, int nerr)
{
	NvScheduler *s;
	int rc;

	s = e->host->aux;
	if(s->io.err == nil){ snprint(err,nerr,"bad_process_context"); return -1; }
	rc = nvtermprint(s->io.err, value);
	Bputc(s->io.err, '\n');
	if(Bflush(s->io.err) < 0){ snprint(err,nerr,"io_error"); return -1; }
	if(rc == NvTermlimit){ snprint(err,nerr,"system_limit"); return -1; }
	return 0;
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

/* The scheduler borrows the verified module for its complete lifetime. */
int
nvschedinit(NvScheduler *s, NvModule *m, NvLimits *limits, uvlong incarnation, uvlong quantum, char *err, int nerr)
{
	memset(s, 0, sizeof *s);
	if(m == nil || quantum == 0){
		snprint(err, nerr, "bad scheduler configuration");
		return -1;
	}
	if(nvverify(m, err, nerr) < 0)
		return -1;
	if(nvruntimeinit(&s->runtime, limits, incarnation, err, nerr) < 0)
		return -1;
	/*
	 * D074 "Shared-memory placement": these must be malloc'd, not plain
	 * NvScheduler fields -- see the field comments in nvsched.h. Both
	 * start at 0: gcsem is an ordinary empty counting semaphore, gchold
	 * starts released (no test hold engaged).
	 */
	s->gcsem = mallocz(sizeof(long), 1);
	s->gchold = mallocz(sizeof(long), 1);
	if(s->gcsem == nil || s->gchold == nil){
		free(s->gcsem);
		free(s->gchold);
		nvruntimefree(&s->runtime);
		snprint(err, nerr, "out of memory");
		return -1;
	}
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
	m->tablebytes = (uvlong)s->runtime.nalloc*sizeof(NvProcess);
	for(i = 0; i < s->runtime.nslot; i++){
		p = &s->runtime.process[i];
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
static void gcfoldall(NvScheduler *);

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
enum {
	NvGcdrainwaitms = 20,
	NvGcdrainmax = 5000,	/* 5000 * 20ms = 100s worst case before giving up */
};

static void
gcdrain(NvScheduler *s)
{
	uvlong tries;

	tries = 0;
	gcfoldall(s);
	while(s->gcoutstanding > 0 && tries < NvGcdrainmax){
		tsemacquire(s->gcsem, NvGcdrainwaitms);
		gcfoldall(s);
		tries++;
	}
	if(s->gcoutstanding > 0)
		sysfatal("nervous: nvschedfree: %llud collector(s) never completed", s->gcoutstanding);
}

void
nvschedfree(NvScheduler *s)
{
	if(s == nil)
		return;
	gcdrain(s);
	nvruntimefree(&s->runtime);
	if(s->lastexit != nil)
		nvfragfree(s->lastexit);
	if(s->rootvalue != nil)
		nvfragfree(s->rootvalue);
	free(s->gcsem);
	free(s->gchold);
	memset(s, 0, sizeof *s);
}

/*
 * D074 "Test determinism": nil-safe test hook, a no-op unless a test has
 * called it. See the declaration in nvsched.h for the full contract.
 */
void
nvschedgchold(NvScheduler *s, int hold)
{
	if(s == nil || s->gchold == nil)
		return;
	*s->gchold = hold != 0;
}

static int
spawn(NvScheduler *s, char *entry, NvTerm arg, NvTerm *pid, char *err, int nerr)
{
	NvExec *e;

	if(err != nil && nerr > 0)
		err[0] = 0;
	if(nvprocspawn(&s->runtime, pid, err, nerr) < 0)
		return -1;
	e = mallocz(sizeof *e, 1);
	if(e == nil){
		snprint(err, nerr, "system_limit");
		nvprocexit(&s->runtime, *pid);
		return -1;
	}
	/*
	 * The argument is a word in the spawning process's own registers
	 * (D065's host callback contract); nvexecinit copies it into the
	 * fresh child heap, so nothing here retains it past this call.
	 */
	if(nvexecinit(e, s->module, entry, arg, s->runtime.limits.maxheap, nil, 0, err, nerr) < 0 ||
	   nvexecsetframelimit(e, s->runtime.limits.maxframe) < 0){
		if(err[0] == 0)
			snprint(err, nerr, "bad process limits");
		nvexecfree(e);
		free(e);
		nvprocexit(&s->runtime, *pid);
		return -1;
	}
	nvexecsethost(e, &s->host);
	e->gcstress = s->runtime.limits.gcstress;
	/* Re-fetch by slot: nvprocspawn/nvexecinit may have grown the table. */
	s->runtime.process[nvpidslot(*pid)].exec = e;
	return 0;
}

int
nvschedspawn(NvScheduler *s, char *entry, NvTerm arg, NvTerm *pid, char *err, int nerr)
{
	uvlong start;
	int rc;

	start = s->profile ? uptime() : 0;
	rc = spawn(s, entry, arg, pid, err, nerr);
	if(s->profile)
		s->spawnns += uptime()-start;
	return rc;
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
gcsample(NvScheduler *s, NvExec *e, int ok)
{
	if(!ok){
		s->gcfailed++;
		return;
	}
	s->collections++;
	s->lastlivewords = e->livewords;
	if(e->livewords > s->maxlivewords)
		s->maxlivewords = e->livewords;
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
collectorchild(NvExec *e, long *gcsem, long *gchold)
{
	nvexecgc(e);
	/* D074 "Test determinism": absent/no-op unless a test engaged it. */
	if(gchold != nil)
		while(*gchold != 0)
			sleep(NvGcholdpollms);
	lock(&e->heap.lock);
	e->heap.owner = NvHeapIdle;
	unlock(&e->heap.lock);
	semrelease(gcsem, 1);
	_exits(nil);
}

/*
 * D074 "Locked completion fold" and "gcoffload polarity"/"Launch". Runs
 * only in the scheduler proc. ok/demand collection bookkeeping shared by
 * the ordinary inline path and by an off-process launch that failed
 * (rfork) and fell back to collecting immediately in the parent.
 */
static void
doinline(NvScheduler *s, NvExec *e, int demand)
{
	int ok;

	if(demand){
		nvexecgc(e);
		ok = e->gcretry == 1;
	}else
		ok = nvexeccollect(e, 0) == 0;
	if(ok)
		s->gcoutputwords += e->livewords;
	gcsample(s, e, ok);
}

/*
 * demand/idle collection dispatch. allowoffload lets a capped idle sweep
 * (D074 "Idle-sweep concurrency is bounded") force an otherwise-eligible
 * collection inline once its per-sweep fork budget is spent, without
 * skipping it outright.
 */
static void
collect(NvScheduler *s, NvExec *e, int demand, int allowoffload)
{
	uvlong start;
	int pid;

	s->gcinputwords += e->heap.words;
	if(demand)
		s->gcdemand++;
	else
		s->gcidle++;
	if(allowoffload && shouldoffload(s, e)){
		/*
		 * D074 "Launch". Precondition (assert rather than merely
		 * trust): the heap must be idle and the process runnable or
		 * waiting, never running -- true here by construction, since
		 * this is called only from the demand path right after
		 * nvprocyield and from the idle sweep over Prwaiting slots.
		 */
		lock(&e->heap.lock);
		e->heap.owner = NvHeapCollecting;
		unlock(&e->heap.lock);
		e->offlaunched = 1;
		s->gcoutstanding++;
		pid = rfork(RFPROC|RFMEM|RFNOWAIT);
		if(pid < 0){
			/* Launch failed: fall back to inline, counted separately
			 * from a deliberate policy choice to collect inline. */
			s->gcofffallback++;
			s->gcoutstanding--;
			e->offlaunched = 0;
			lock(&e->heap.lock);
			e->heap.owner = NvHeapIdle;
			unlock(&e->heap.lock);
			doinline(s, e, demand);
			return;
		}
		if(pid == 0)
			collectorchild(e, s->gcsem, s->gchold);	/* never returns */
		/* Parent: completion is discovered lazily by gcfold. */
		return;
	}
	start = s->profile ? uptime() : 0;
	doinline(s, e, demand);
	if(s->profile)
		s->gcns += uptime()-start;
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
gcfold(NvScheduler *s, NvExec *e)
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
		retry = e->gcretry;
		live = e->livewords;
	}
	unlock(&e->heap.lock);
	if(!idle)
		return 0;
	e->offlaunched = 0;
	s->gcoutstanding--;
	ok = retry == 1;
	if(!ok){
		s->gcfailed++;
		return 1;
	}
	s->gcoutputwords += live;
	s->collections++;
	s->lastlivewords = live;
	if(live > s->maxlivewords)
		s->maxlivewords = live;
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
gcfoldall(NvScheduler *s)
{
	NvRuntime *r;
	NvExec *e;
	ulong i;

	r = &s->runtime;
	for(i = 0; i < r->nslot; i++){
		e = r->process[i].exec;
		if(e != nil && e->offlaunched)
			gcfold(s, e);
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
finddispatchable(NvScheduler *s, ulong *outslot)
{
	NvRuntime *r;
	NvProcess *p;
	NvExec *e;
	ulong slot, tries, bound;

	r = &s->runtime;
	bound = r->nrunnable;
	for(tries = 0; tries < bound; tries++){
		if(!nvprocrunhead(r, &slot))
			return 0;
		p = &r->process[slot];
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
		if(!gcfold(s, e)){
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

int
nvschedstep(NvScheduler *s, char *err, int nerr)
{
	NvRuntime *r;
	NvProcess *p;
	NvExec *e;
	NvTerm pid;
	ulong i, slot;
	uvlong before, start;
	int state, isroot;

	r = &s->runtime;
	if(r->nlive == 0)
		return NvSchedDone;
	if(r->nslot == 0){
		snprint(err, nerr, "scheduler has live processes but no slots");
		return NvSchedError;
	}
	/*
	 * D059/D074: dispatch is the head of the runtime's FIFO run queue,
	 * discovered in O(runnable) even under off-process collection
	 * (finddispatchable requeues a collecting slot to the tail and folds
	 * any just-completed collection's stats along the way, but never
	 * dispatches or blocks on one). The scans below run only when
	 * nothing is currently dispatchable: the queue is empty, or every
	 * runnable slot is collecting.
	 */
	if(!finddispatchable(s, &slot)){
		uvlong earliest, now;
		ulong nwake, sweeplaunched;
		int havedeadline, allow;

		/*
		 * D074 amendment/M08-T04d "Test determinism": see the field
		 * comment in nvsched.h. nil in production; called exactly once
		 * per idle-branch entry, before anything else in this branch
		 * runs.
		 */
		if(s->gcidlestep != nil)
			s->gcidlestep(s);

		for(i = 0; i < r->nslot; i++)
			if(r->process[i].state == Prrunning){
				snprint(err, nerr, "process left running outside scheduler dispatch");
				return NvSchedError;
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
			p = &r->process[i];
			e = p->exec;
			if(p->state == Prwaiting && e != nil && e->heap.cur != nil &&
			   !e->offlaunched &&
			   e->heap.words > e->livewords && e->heap.words > e->heap.cur->cap/2){
				allow = sweeplaunched < NvGcsweepcap;
				if(allow && shouldoffload(s, e))
					sweeplaunched++;
				collect(s, e, 0, allow);
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
			p = &r->process[i];
			if(p->state != Prwaiting || !p->hasdeadline)
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
		gcfoldall(s);
		if(s->gcoutstanding == 0){
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
			if(r->nrunnable != 0)
				return NvSchedProgress;
			/*
			 * D074 corollary: with no collector a factor, behave
			 * exactly as before off-process collection existed.
			 */
			if(!havedeadline)
				return NvSchedIdle;
			if(s->clock.now == nil || s->clock.wait == nil){
				snprint(err, nerr, "scheduler has an armed deadline but no clock installed");
				return NvSchedError;
			}
			s->clock.wait(s->clock.aux, earliest);
			nwake = 0;
			for(i = 0; i < r->nslot; i++){
				p = &r->process[i];
				if(p->state == Prwaiting && p->hasdeadline && p->deadline <= earliest){
					if(nvprocwake(r, i) < 0){
						snprint(err, nerr, "deadline wake of a non-waiting process");
						return NvSchedError;
					}
					nwake++;
				}
			}
			if(nwake == 0){
				snprint(err, nerr, "clock did not advance past the earliest deadline");
				return NvSchedError;
			}
			s->timerwakes += nwake;
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
		while(tsemacquire(s->gcsem, 0) > 0)
			;
		gcfoldall(s);
		if(s->gcoutstanding == 0)
			return NvSchedProgress;
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
					p = &r->process[i];
					if(p->state == Prwaiting && p->hasdeadline && p->deadline <= now){
						if(nvprocwake(r, i) < 0){
							snprint(err, nerr, "deadline wake of a non-waiting process");
							return NvSchedError;
						}
						nwake++;
					}
				}
				if(nwake != 0)
					s->timerwakes += nwake;
				return NvSchedProgress;
			}
			tsemacquire(s->gcsem, deadlinems(s, earliest));
		}else
			tsemacquire(s->gcsem, NvGcwaitms);
		return NvSchedProgress;
	}
	p = &r->process[slot];
	if(p->exec == nil){
		snprint(err, nerr, "runnable process has no execution state");
		return NvSchedError;
	}
	e = p->exec;
	pid = nvpid(slot, p->generation);
	if(nvprocdispatch(r, pid, err, nerr) < 0)
		return NvSchedError;
	s->dispatches++;
	s->currentslot = slot;
	s->currentgeneration = p->generation;
	s->currentvalid = 1;
	before = e->reductions;
	start = s->profile ? uptime() : 0;
	state = nvexecrun(e, s->quantum);
	if(s->profile)
		s->execns += uptime()-start;
	s->reductions += e->reductions - before;
	s->currentvalid = 0;
	/* Process operations may grow the slot table; never retain its old address. */
	p = &r->process[slot];
	if(state == NvCollect){
		if(p->state != Prrunning || nvprocyield(r, pid, err, nerr) < 0)
			return NvSchedError;
		/* D074: stopped owner, enqueued once, no host callback active;
		 * decides inline vs. off-process from gcoffload. */
		collect(s, e, 1, 1);
		return NvSchedProgress;
	}
	if(state == NvYield){
		if(p->state == Prrunning){
			if(nvprocyield(r, pid, err, nerr) < 0)
				return NvSchedError;
		}else if(p->state != Prwaiting){
			snprint(err, nerr, "yielded process has bad lifecycle state");
			return NvSchedError;
		}
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
		snprint(err, nerr, "bad execution state");
		return NvSchedError;
	}
	if(nvprocexit(r, pid) != 1){
		snprint(err, nerr, "failed to exit completed process");
		return NvSchedError;
	}
	return NvSchedProgress;
}
