#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../include/nvbc.h"
#include "../include/nvvm.h"
#include "../include/nvexec.h"
#include "../include/nvproc.h"
#include "../include/nvsched.h"

/*
 * Sharding, first slice. A local operation takes ONLY its owner's QLock.
 * Structural/cross-owner operations take the machine gate followed by ALL
 * shards, ascending, and release in reverse. Never upgrade a held shard:
 * release it before requesting the machine gate. No lock across execution,
 * collection, rfork, blocking semaphore waits or I/O.
 *
 * Directory growth/reuse, owner changes and machine-wide fields are written
 * only behind the all-shard gate. Thus ANY held shard stabilizes the chunk
 * directory, nslot/nlive and ownership; it does NOT authorize reading another
 * owner's mutable state/exec. Table walks must test owner BEFORE those reads.
 * An on-CPU process cannot migrate/reap, so its exec survives callback gaps.
 * Remote send/steal/idle remain all-shard operations in this first slice.
 */
static void
lockrt(NvScheduler *s, NvSched *sc)
{
	uvlong start;
	int i;

	if(!s->locking)
		return;
	start = sc != nil && sc->profile ? uptime() : 0;
	qlock(&s->lock);
	if(s->sharded)
		for(i = 0; i < s->nsched; i++)
			qlock(&s->sched[i]->lock);
	if(sc != nil){
		sc->lockacq++;
		sc->globalacq++;
		if(sc->profile){
			start = uptime()-start;
			sc->lockwaitns += start;
			sc->globalwaitns += start;
		}
	}
}

static void
unlockrt(NvScheduler *s)
{
	int i;

	if(!s->locking)
		return;
	if(s->sharded)
		for(i = s->nsched-1; i >= 0; i--)
			qunlock(&s->sched[i]->lock);
	qunlock(&s->lock);
}

static void
locklocal(NvScheduler *s, NvSched *sc)
{
	uvlong start;

	if(!s->locking)
		return;
	if(!s->sharded){
		lockrt(s, sc);
		return;
	}
	start = sc->profile ? uptime() : 0;
	qlock(&sc->lock);
	sc->lockacq++;
	sc->localacq++;
	if(sc->profile){
		start = uptime()-start;
		sc->lockwaitns += start;
		sc->localwaitns += start;
	}
}

static void
unlocklocal(NvScheduler *s, NvSched *sc)
{
	if(!s->locking)
		return;
	if(s->sharded)
		qunlock(&sc->lock);
	else
		unlockrt(s);
}

/* D065/D087: e->sched is valid only during this exec's quantum. */
static int
currentpid(NvExec *e, NvTerm *pid, char *err, int nerr)
{
	NvSched *sc;

	sc = e->sched;
	if(sc == nil || !sc->currentvalid){ snprint(err,nerr,"bad_process_context"); return -1; }
	*pid = nvpid(sc->currentslot, sc->currentgeneration);
	return 0;
}

/* Remote wake is called behind the all-shard gate; a local deadline wake
 * returns before touching global idle state. The semaphore credit persists
 * across the set-idle / release-lock / sleep window (D085). */
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
		s->idlemask &= ~(1ULL<<owner);
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

static int
hostspawn(NvExec *e, char *entry, NvTerm arg, NvTerm *pid, char *err, int nerr)
{
	NvScheduler *s;
	int rc;

	s = e->host->aux;
	if(e->sched == nil){ snprint(err,nerr,"bad_process_context"); return -1; }
	/* Slot allocation, directory growth and exec publication are structural. */
	lockrt(s, e->sched);
	rc = spawntimed(s, e->sched, entry, arg, pid, &e->work, err, nerr);
	unlockrt(s);
	return rc;
}

/* Receive callbacks mutate only the on-CPU process owned by this scheduler.
 * Remote send holds every shard, so scan/append/wait retain D081's handshake.
 * The owner cannot change while oncpu, including the Prwaiting yield window. */
static int
hostrecvbegin(NvExec *e, NvTerm *value, char *err, int nerr)
{
	NvScheduler *s;
	NvTerm pid;
	int rc;

	s = e->host->aux;
	if(currentpid(e, &pid, err, nerr) < 0)
		return -1;
	locklocal(s, e->sched);
	rc = nvprocrecvbegin(&s->runtime, pid, value, err, nerr);
	unlocklocal(s, e->sched);
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
	locklocal(s, e->sched);
	rc = nvprocrecvnext(&s->runtime, pid, value, err, nerr);
	unlocklocal(s, e->sched);
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
	locklocal(s, e->sched);
	rc = nvprocrecvneed(&s->runtime, pid, words, err, nerr);
	unlocklocal(s, e->sched);
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
	locklocal(s, e->sched);
	rc = nvprocrecvtake(&s->runtime, pid, taken, err, nerr);
	unlocklocal(s, e->sched);
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
	locklocal(s, e->sched);
	rc = nvprocrecvwait(&s->runtime, pid, err, nerr);
	unlocklocal(s, e->sched);
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
	locklocal(s, e->sched);
	rc = nvprocarmdeadline(&s->runtime, pid, duration, now, err, nerr);
	unlocklocal(s, e->sched);
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
	locklocal(s, e->sched);
	rc = nvprocrecvwaitdeadline(&s->runtime, pid, now, err, nerr);
	unlocklocal(s, e->sched);
	return rc;
}

/* D086: print + newline + flush is one line. No runtime lock with iolock.
 * Flush before returning a term-depth fault; I/O failure takes priority. */
static int
printline(NvScheduler *s, Biobuf *b, NvExec *e, NvTerm value, char *err, int nerr)
{
	int rc, flushed;

	if(b == nil){ snprint(err,nerr,"bad_process_context"); return -1; }
	qlock(&s->iolock);
	rc = nvtermprintw(b, value, &e->work);
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

/* D074/D087: all shared semaphore words and shards are malloc'd. RFMEM
 * does not share an ordinary caller stack. A failed allocation publishes none. */
static NvSched *
schedalloc(NvScheduler *s, int i)
{
	NvSched *sc;

	sc = mallocz(sizeof *sc, 1);
	if(sc == nil)
		return nil;
	sc->gcsem = mallocz(sizeof(long), 1);
	sc->gchold = mallocz(sizeof(long), 1);
	sc->sem = mallocz(sizeof(long), 1);
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
	if(s->nsched > 1)
		on = 1;
	s->locking = on != 0;
}

int
nvschedinit(NvScheduler *s, NvModule *m, NvLimits *limits, uvlong incarnation, uvlong quantum, char *err, int nerr)
{
	NvSched *sc;

	memset(s, 0, sizeof *s);
	s->sharded = 1;
	if(m == nil || quantum == 0){
		snprint(err, nerr, "bad scheduler configuration");
		return -1;
	}
	if(nvverify(m, err, nerr) < 0)
		return -1;
	/* D090: immutable atom table on every interpreter path. */
	if(nvexecinternmodule(m) < 0){
		snprint(err, nerr, "out of memory");
		return -1;
	}
	if(nvruntimeinit(&s->runtime, limits, incarnation, err, nerr) < 0)
		return -1;
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
	s->runtime.wakehook = wakehook;
	s->runtime.wakeaux = s;
	s->module = m;
	s->quantum = quantum;
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

/* Explicit quiescent snapshot only. A collector may still be rewriting its
 * heap: the synchronized owner check, not an unlocked flag, excludes it. */
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

static void gcfoldall(NvScheduler *, NvSched *);
static int gcwait(NvSched *, ulong);

enum {
	NvGcdrainwaitms = 20,
	NvGcdrainmax = 500,
};

/* Semaphore credits are NOT a completion count (R3-F06). Fold synchronized
 * heap completion first, then consume every child's final credit before free.
 * The 10s bound detects lost collectors; it is not a work budget. */
static int
gcdrain(NvScheduler *s, NvSched *sc, char *err, int nerr)
{
	uvlong tries;

	tries = 0;
	locklocal(s, sc);
	gcfoldall(s, sc);
	unlocklocal(s, sc);
	while(sc->gcoutstanding > 0 && tries < NvGcdrainmax){
		gcwait(sc, NvGcdrainwaitms);
		locklocal(s, sc);
		gcfoldall(s, sc);
		unlocklocal(s, sc);
		tries++;
	}
	if(sc->gcoutstanding > 0){
		snprint(err, nerr, "%llud collector(s) never completed", sc->gcoutstanding);
		return -1;
	}
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

/* Spawn and failure rollback are behind the all-shard gate. D080 charges
 * argument traversal to the spawning exec, host spawns remain uncounted. */
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

int
nvschedspawn(NvScheduler *s, char *entry, NvTerm arg, NvTerm *pid, char *err, int nerr)
{
	int rc;

	lockrt(s, nil);
	rc = spawntimed(s, s->sched[0], entry, arg, pid, nil, err, nerr);
	unlockrt(s);
	return rc;
}

void
nvschedsetworklimit(NvScheduler *s, uvlong max)
{
	/* Unlike IO/clock configuration, this API also permits an update for
	 * future spawns. Serialize it with spawn's all-shard read. */
	lockrt(s, nil);
	s->maxtermwork = max;
	unlockrt(s);
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

static int
shouldoffload(NvScheduler *s, NvExec *e)
{
	uvlong limit;

	limit = s->runtime.limits.gcoffload;
	return limit != 0 && e->heap.words >= limit;
}

enum {
	NvGcholdpollms = 5,
};

/* D074: child gets only exec and shared signalling words, no runtime access.
 * Idle collection is a real nvexeccollect, not no-op nvexecgc (R3-F05).
 * Publication uses heap.lock; semrelease is its final shared-memory touch. */
static void
collectorchild(NvExec *e, int demand, long *gcsem, long *gchold)
{
	int rc;

	if(demand){
		nvexecgc(e);
		e->gcresult = e->gcretry;
	}else{
		rc = nvexeccollect(e, 0);
		e->gcresult = rc == 0 ? 1 : rc == NvTermlimit ? 2 : 3;
	}
	if(gchold != nil)
		while(*gchold != 0)
			sleep(NvGcholdpollms);
	lock(&e->heap.lock);
	e->heap.owner = NvHeapIdle;
	unlock(&e->heap.lock);
	semrelease(gcsem, 1);
	_exits(nil);
}

static int
gcwait(NvSched *sc, ulong ms)
{
	int rc;

	rc = tsemacquire(sc->gcsem, ms);
	if(rc > 0)
		sc->gccredits++;
	return rc;
}

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

/* Enter/return with ONLY the local shard held. Mark collecting before
 * releasing it: all-shard steal then refuses this exec. An inline collector
 * writes owner only here; an off-process child publishes with heap.lock.
 * The two kinds cannot overlap; offlaunched stays set until synchronized fold.
 * Collector counters are published behind the local shard for idle detection. */
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
	lock(&e->heap.lock);
	e->heap.owner = NvHeapCollecting;
	unlock(&e->heap.lock);
	if(allowoffload && shouldoffload(s, e)){
		e->offlaunched = 1;
		sc->gcoutstanding++;
		unlocklocal(s, sc);
		pid = rfork(RFPROC|RFMEM|RFNOWAIT);
		if(pid == 0)
			collectorchild(e, demand, sc->gcsem, sc->gchold);
		locklocal(s, sc);
		if(pid > 0){
			sc->gclaunched++;
			return;
		}
		sc->gcofffallback++;
		sc->gcoutstanding--;
		e->offlaunched = 0;
	}
	unlocklocal(s, sc);
	start = sc->profile ? uptime() : 0;
	doinline(sc, e, demand);
	if(sc->profile)
		sc->gcns += uptime()-start;
	locklocal(s, sc);
	lock(&e->heap.lock);
	e->heap.owner = NvHeapIdle;
	unlock(&e->heap.lock);
}

/* D074: this is the authoritative synchronized witness of child completion.
 * Never re-read owner unlocked to decide dispatch. heap.lock also orders every
 * collector write before the interpreter reads gcretry/livewords on arm64. */
static int
gcfold(NvSched *sc, NvExec *e)
{
	int idle, ok, retry;
	uvlong live;

	if(!e->offlaunched)
		return 1;
	retry = 0;
	live = 0;
	lock(&e->heap.lock);
	idle = e->heap.owner == NvHeapIdle;
	if(idle){
		retry = e->gcresult;
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

/* Fold waiting execs too: they may never reach finddispatchable. Ownership
 * changes hold all shards, so owner is stable here. Test it BEFORE reading
 * exec/offlaunched: another shard may be dispatching/collecting concurrently. */
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
		if(p->owner != sc->index)
			continue;
		e = p->exec;
		if(e != nil && e->offlaunched)
			gcfold(sc, e);
	}
}

/* Bounded by queue length: an all-collecting queue is not busy-polled. */
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
			*outslot = slot;
			return 1;
		}
		if(!gcfold(sc, e)){
			nvprocrequeue(r, slot);
			continue;
		}
		*outslot = slot;
		return 1;
	}
	return 0;
}

enum {
	NvGcwaitmaxms = 60000,
	NvGcsweepcap = 8,
	NvGcwaitms = 20,
	NvStealmin = 2,
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

/* Enter with local shard; every return releases it. N=1 preserves clock.wait
 * and GC semaphore behavior. N>1 returns a sleep bound, never blocks here.
 * No foreign state/exec read in these walks; table/owner changes hold all shards. */
static int
idlestep(NvScheduler *s, NvSched *sc, char *err, int nerr)
{
	NvRuntime *r;
	NvProcess *p;
	NvExec *e;
	ulong i, nwake, sweeplaunched;
	uvlong earliest, now;
	int multi, havedeadline, allow;

	r = &s->runtime;
	multi = s->nsched > 1;
	sc->wakeatvalid = 0;
	if(sc->gcidlestep != nil)
		sc->gcidlestep(s);
	for(i = 0; i < r->nslot; i++){
		p = nvprocat(r, i);
		if(p->owner == sc->index && p->state == Prrunning){
			unlocklocal(s, sc);
			snprint(err, nerr, "process left running outside scheduler dispatch");
			return NvSchedError;
		}
	}
	sweeplaunched = 0;
	for(i = 0; i < r->nslot; i++){
		p = nvprocat(r, i);
		if(p->owner != sc->index)
			continue;
		e = p->exec;
		/* offlaunched, not unlocked heap.owner, prevents double launch. */
		if(p->state == Prwaiting && e != nil && !e->offlaunched &&
		   e->heap.cur != nil && e->heap.words > e->livewords && e->heap.words > e->heap.cur->cap/2){
			allow = sweeplaunched < NvGcsweepcap;
			if(allow && shouldoffload(s, e))
				sweeplaunched++;
			collect(s, sc, e, 0, allow);
		}
	}
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
	gcfoldall(s, sc);
	if(sc->gcoutstanding == 0){
		/* A queue just freed from collection is progress, not false idle. */
		if(sc->runq->nrunnable != 0){
			unlocklocal(s, sc);
			return NvSchedProgress;
		}
		if(!havedeadline){
			unlocklocal(s, sc);
			return NvSchedIdle;
		}
		if(s->clock.now == nil || s->clock.wait == nil){
			unlocklocal(s, sc);
			snprint(err, nerr, "scheduler has an armed deadline but no clock installed");
			return NvSchedError;
		}
		if(multi){
			now = s->clock.now(s->clock.aux);
			if(now < earliest){
				sc->wakeat = earliest;
				sc->wakeatvalid = 1;
				unlocklocal(s, sc);
				return NvSchedIdle;
			}
		}else{
			unlocklocal(s, sc);
			s->clock.wait(s->clock.aux, earliest);
			locklocal(s, sc);
			now = earliest;
		}
		nwake = 0;
		for(i = 0; i < r->nslot; i++){
			p = nvprocat(r, i);
			if(p->owner == sc->index && p->state == Prwaiting && p->hasdeadline && p->deadline <= now){
				if(nvprocwakefrom(r, sc->index, i) < 0){
					unlocklocal(s, sc);
					snprint(err, nerr, "deadline wake of a non-waiting process");
					return NvSchedError;
				}
				nwake++;
			}
		}
		if(nwake == 0){
			unlocklocal(s, sc);
			snprint(err, nerr, "clock did not advance past the earliest deadline");
			return NvSchedError;
		}
		sc->timerwakes += nwake;
		unlocklocal(s, sc);
		return NvSchedProgress;
	}
	/* Drain stale credits and re-fold before sleeping: credits are fungible,
	 * not per-exec completions. Never spin on a credit left by dispatch fold. */
	while(gcwait(sc, 0) > 0)
		;
	gcfoldall(s, sc);
	if(sc->gcoutstanding == 0){
		unlocklocal(s, sc);
		return NvSchedProgress;
	}
	if(havedeadline && s->clock.now != nil){
		now = s->clock.now(s->clock.aux);
		if(now >= earliest){
			nwake = 0;
			for(i = 0; i < r->nslot; i++){
				p = nvprocat(r, i);
				if(p->owner == sc->index && p->state == Prwaiting && p->hasdeadline && p->deadline <= now){
					if(nvprocwakefrom(r, sc->index, i) < 0){
						unlocklocal(s, sc);
						snprint(err, nerr, "deadline wake of a non-waiting process");
						return NvSchedError;
					}
					nwake++;
				}
			}
			if(nwake != 0)
				sc->timerwakes += nwake;
			unlocklocal(s, sc);
			return NvSchedProgress;
		}
		if(multi){
			sc->wakeat = earliest;
			sc->wakeatvalid = 1;
			unlocklocal(s, sc);
			return NvSchedIdle;
		}
		unlocklocal(s, sc);
		gcwait(sc, deadlinems(s, earliest));
		return NvSchedProgress;
	}
	unlocklocal(s, sc);
	if(multi)
		return NvSchedIdle;
	gcwait(sc, NvGcwaitms);
	return NvSchedProgress;
}

/* All-shard gate held: wake one idle potential thief, once per sleep. */
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

/* Local dispatch/quantum-end/GC, structural reap behind all shards. No held
 * shard is upgraded: oncpu protects the kick gap; Prrunning protects reap gap. */
static int
step(NvScheduler *s, NvSched *sc, char *err, int nerr)
{
	NvRuntime *r;
	NvProcess *p;
	NvExec *e;
	NvTerm pid;
	ulong slot;
	uvlong before, start;
	int state, isroot, woke, kick;

	r = &s->runtime;
	locklocal(s, sc);
	if(r->nlive == 0){
		unlocklocal(s, sc);
		return NvSchedDone;
	}
	if(r->nslot == 0){
		unlocklocal(s, sc);
		snprint(err, nerr, "scheduler has live processes but no slots");
		return NvSchedError;
	}
	if(!finddispatchable(s, sc, &slot))
		return idlestep(s, sc, err, nerr);
	p = nvprocat(r, slot);
	if(p->exec == nil){
		unlocklocal(s, sc);
		snprint(err, nerr, "runnable process has no execution state");
		return NvSchedError;
	}
	e = p->exec;
	pid = nvpid(slot, p->generation);
	if(nvprocdispatch(r, pid, err, nerr) < 0){
		unlocklocal(s, sc);
		return NvSchedError;
	}
	sc->dispatches++;
	sc->currentslot = slot;
	sc->currentgeneration = p->generation;
	sc->currentvalid = 1;
	e->sched = sc;
	kick = s->idlemask != 0 && sc->runq->nrunnable >= NvStealmin;
	unlocklocal(s, sc);
	if(kick){
		lockrt(s, sc);
		if(sc->runq->nrunnable >= NvStealmin)
			kickidle(s, sc);
		unlockrt(s);
	}
	before = e->reductions;
	start = sc->profile ? uptime() : 0;
	state = nvexecrun(e, s->quantum);
	if(sc->profile)
		sc->execns += uptime()-start;
	sc->reductions += e->reductions - before;
	e->sched = nil;
	sc->currentvalid = 0;
	locklocal(s, sc);
	p = nvprocat(r, slot);
	woke = nvprocoffcpu(r, pid);
	if(woke < 0){
		unlocklocal(s, sc);
		snprint(err, nerr, "dispatched process vanished during its quantum");
		return NvSchedError;
	}
	if(state == NvCollect){
		if(p->state != Prrunning || nvprocyield(r, pid, err, nerr) < 0){
			unlocklocal(s, sc);
			return NvSchedError;
		}
		collect(s, sc, e, 1, 1);
		unlocklocal(s, sc);
		return NvSchedProgress;
	}
	if(state == NvYield){
		if(p->state == Prrunning){
			if(nvprocyield(r, pid, err, nerr) < 0){
				unlocklocal(s, sc);
				return NvSchedError;
			}
		}else if(p->state != Prwaiting && !(woke && p->state == Prrunnable)){
			unlocklocal(s, sc);
			snprint(err, nerr, "yielded process has bad lifecycle state");
			return NvSchedError;
		}
		unlocklocal(s, sc);
		return NvSchedProgress;
	}
	/* A terminal interpreter result must still be running, not queued.
	 * It cannot be stolen/reaped while we drop local protection for the gate. */
	if(p->state != Prrunning){
		unlocklocal(s, sc);
		snprint(err, nerr, "completed process has bad lifecycle state");
		return NvSchedError;
	}
	unlocklocal(s, sc);
	lockrt(s, sc);
	p = nvprocat(r, slot);
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

			s->rootstate = NvRootExit;
			if(s->rootvalue != nil)
				nvfragfree(s->rootvalue);
			s->rootvalue = nil;
			/* Once-per-root independent fragment, as in stage 1. */
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

int
nvschedstep(NvScheduler *s, char *err, int nerr)
{
	return step(s, s->sched[0], err, nerr);
}

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

	/* Note handler: no lock/allocation, unchanged interrupt protocol. */
	if(s->stopping == NvStopNone)
		s->stopping = NvStopInterrupt;
	for(i = 0; i < s->nsched; i++)
		semrelease(s->sched[i]->sem, 1);
}

/* All shards held: owner transfer, both queues and GC exclusion atomic. */
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

/* Exact snapshot behind every shard: no publication or ownership in flight.
 * A scheduler executing/collecting/reaping is not marked idle. */
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

/* D085's recheck + set-idle + semaphore protocol remains one all-shard
 * transaction. No partially enqueued process or lost wake window is added. */
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
		/* Commit the idle decision while the snapshot is still protected.
		 * Do not drop the gate then reacquire it to set stopping. */
		if(s->stopping == NvStopNone)
			s->stopping = NvStopIdle;
		for(rc = 0; rc < s->nsched; rc++)
			semrelease(s->sched[rc]->sem, 1);
		unlockrt(s);
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
