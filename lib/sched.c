#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../include/nvbc.h"
#include "../include/nvvm.h"
#include "../include/nvexec.h"
#include "../include/nvproc.h"
#include "../include/nvsched.h"

static int
hostself(void *aux, NvValue *value, char *err, int nerr)
{
	NvScheduler *s;

	s = aux;
	if(!s->currentvalid){ snprint(err,nerr,"bad_process_context"); return -1; }
	return nvvaluepid(value, s->currentslot, s->currentgeneration);
}

static int
hostref(void *aux, NvValue *value, char *err, int nerr)
{
	NvScheduler *s;

	s = aux;
	return nvprocref(&s->runtime, value, err, nerr);
}

static int
hostsend(void *aux, NvValue *pid, NvValue *value, char *err, int nerr)
{
	NvScheduler *s;
	int rc;

	s = aux;
	rc = nvprocsend(&s->runtime, pid, value, err, nerr);
	return rc < 0 ? -1 : 0;
}

static int
hostspawn(void *aux, char *entry, NvValue *arg, NvValue *pid, char *err, int nerr)
{
	return nvschedspawn(aux, entry, arg, pid, err, nerr);
}

static int
currentpid(NvScheduler *s, NvValue *pid, char *err, int nerr)
{
	if(!s->currentvalid){ snprint(err,nerr,"bad_process_context"); return -1; }
	return nvvaluepid(pid, s->currentslot, s->currentgeneration);
}

/* Receive callbacks operate only on the currently dispatched process. */
static int
hostrecvbegin(void *aux, NvValue *value, char *err, int nerr)
{
	NvScheduler *s;
	NvValue pid;

	s = aux;
	if(currentpid(s, &pid, err, nerr) < 0)
		return -1;
	return nvprocrecvbegin(&s->runtime, &pid, value, err, nerr);
}

static int
hostrecvnext(void *aux, NvValue *value, char *err, int nerr)
{
	NvScheduler *s;
	NvValue pid;

	s = aux;
	if(currentpid(s, &pid, err, nerr) < 0)
		return -1;
	return nvprocrecvnext(&s->runtime, &pid, value, err, nerr);
}

static int
hostrecvtake(void *aux, char *err, int nerr)
{
	NvScheduler *s;
	NvValue pid;

	s = aux;
	if(currentpid(s, &pid, err, nerr) < 0)
		return -1;
	return nvprocrecvtake(&s->runtime, &pid, err, nerr);
}

static int
hostrecvwait(void *aux, char *err, int nerr)
{
	NvScheduler *s;
	NvValue pid;

	s = aux;
	if(currentpid(s, &pid, err, nerr) < 0)
		return -1;
	return nvprocrecvwait(&s->runtime, &pid, err, nerr);
}

static int
hostrecvdeadline(void *aux, NvValue *duration, char *err, int nerr)
{
	NvScheduler *s;
	NvValue pid;

	s = aux;
	if(currentpid(s, &pid, err, nerr) < 0)
		return -1;
	return nvprocarmdeadline(&s->runtime, &pid, duration, s->clock.now(s->clock.aux), err, nerr);
}

static int
hostrecvwaitdeadline(void *aux, char *err, int nerr)
{
	NvScheduler *s;
	NvValue pid;

	s = aux;
	if(currentpid(s, &pid, err, nerr) < 0)
		return -1;
	return nvprocrecvwaitdeadline(&s->runtime, &pid, s->clock.now(s->clock.aux), err, nerr);
}

/*
 * D054-D057: print/eprint write one value to the installed stream, followed
 * by a newline, then flush. A flush failure (e.g. output redirected to a
 * closed file descriptor) is reported as io_error; nvexecrun turns that
 * into the process fault and otherwise writes 'ok itself, so these
 * callbacks never touch the destination register. nvschedspawn only wires
 * these callbacks when the corresponding Biobuf is installed, so a nil
 * check here would be dead code; if that invariant is ever violated, the
 * only consequence is the write functions faulting on a nil Biobuf, which
 * would show up immediately in testing rather than corrupting anything.
 */
static int
hostprint(void *aux, NvValue *value, char *err, int nerr)
{
	NvScheduler *s;

	s = aux;
	nvvalueprint(s->io.out, value);
	Bputc(s->io.out, '\n');
	if(Bflush(s->io.out) < 0){ snprint(err,nerr,"io_error"); return -1; }
	return 0;
}

static int
hosteprint(void *aux, NvValue *value, char *err, int nerr)
{
	NvScheduler *s;

	s = aux;
	nvvalueprint(s->io.err, value);
	Bputc(s->io.err, '\n');
	if(Bflush(s->io.err) < 0){ snprint(err,nerr,"io_error"); return -1; }
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
 * polling, which is correct for the single scheduler milestone 06 has;
 * milestone 09 replaces it with the D011 semaphore wakeup.
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
	s->module = m;
	s->quantum = quantum;
	nvschedsetclock(s, nil);
	return 0;
}

void
nvschedfree(NvScheduler *s)
{
	if(s == nil)
		return;
	nvruntimefree(&s->runtime);
	nvvaluefree(&s->lastexit);
	nvvaluefree(&s->rootvalue);
	memset(s, 0, sizeof *s);
}

int
nvschedspawn(NvScheduler *s, char *entry, NvValue *arg, NvValue *pid, char *err, int nerr)
{
	NvExec *e;
	NvExecHost host;
	NvProcess *p;

	if(err != nil && nerr > 0)
		err[0] = 0;
	if(nvprocspawn(&s->runtime, pid, err, nerr) < 0)
		return -1;
	e = mallocz(sizeof *e, 1);
	if(e == nil){
		snprint(err, nerr, "system_limit");
		nvprocexit(&s->runtime, pid);
		return -1;
	}
	if(nvexecinit(e, s->module, entry, arg, nil, 0, err, nerr) < 0 ||
	   nvexecsetframelimit(e, s->runtime.limits.maxframe) < 0){
		if(err[0] == 0)
			snprint(err, nerr, "bad process limits");
		nvexecfree(e);
		free(e);
		nvprocexit(&s->runtime, pid);
		return -1;
	}
	memset(&host, 0, sizeof host);
	host.aux = s;
	host.self = hostself;
	host.makeref = hostref;
	host.send = hostsend;
	host.spawn = hostspawn;
	host.recvbegin = hostrecvbegin;
	host.recvnext = hostrecvnext;
	host.recvtake = hostrecvtake;
	host.recvwait = hostrecvwait;
	host.recvdeadline = hostrecvdeadline;
	host.recvwaitdeadline = hostrecvwaitdeadline;
	host.print = s->io.out != nil ? hostprint : nil;
	host.eprint = s->io.err != nil ? hosteprint : nil;
	nvexecsethost(e, &host);
	p = &s->runtime.process[pid->pid.slot];
	p->exec = e;
	return 0;
}

int
nvschedspawnroot(NvScheduler *s, char *entry, NvValue *arg, NvValue *pid, char *err, int nerr)
{
	if(s->rootvalid){
		snprint(err, nerr, "scheduler root already set");
		return -1;
	}
	if(nvschedspawn(s, entry, arg, pid, err, nerr) < 0)
		return -1;
	s->rootslot = pid->pid.slot;
	s->rootgeneration = pid->pid.generation;
	s->rootvalid = 1;
	s->rootstate = NvRootRunning;
	return 0;
}

int
nvschedstep(NvScheduler *s, char *err, int nerr)
{
	NvRuntime *r;
	NvProcess *p;
	NvExec *e;
	NvValue pid;
	ulong i, slot;
	uvlong before;
	int state, isroot;

	r = &s->runtime;
	if(r->nlive == 0)
		return NvSchedDone;
	if(r->nslot == 0){
		snprint(err, nerr, "scheduler has live processes but no slots");
		return NvSchedError;
	}
	/*
	 * D059: dispatch is the head of the runtime's FIFO run queue, O(1) no
	 * matter how many processes are waiting. Order is "runnable longest
	 * first": spawn, quantum yield, and every wakeup append at the tail.
	 * The slot scans below run only when nothing is runnable at all.
	 */
	if(!nvprocrunhead(r, &slot)){
		uvlong earliest;
		ulong nwake;
		int havedeadline;

		for(i = 0; i < r->nslot; i++)
			if(r->process[i].state == Prrunning){
				snprint(err, nerr, "process left running outside scheduler dispatch");
				return NvSchedError;
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
	p = &r->process[slot];
	if(p->exec == nil){
		snprint(err, nerr, "runnable process has no execution state");
		return NvSchedError;
	}
	e = p->exec;
	nvvaluepid(&pid, slot, p->generation);
	if(nvprocdispatch(r, &pid, err, nerr) < 0)
		return NvSchedError;
	s->dispatches++;
	s->currentslot = slot;
	s->currentgeneration = p->generation;
	s->currentvalid = 1;
	before = e->reductions;
	state = nvexecrun(e, s->quantum);
	s->reductions += e->reductions - before;
	s->currentvalid = 0;
	/* Process operations may grow the slot table; never retain its old address. */
	p = &r->process[slot];
	if(state == NvYield){
		if(p->state == Prrunning){
			if(nvprocyield(r, &pid, err, nerr) < 0)
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
			nvvaluefree(&s->rootvalue);
			s->rootvalue = e->result;
			memset(&e->result, 0, sizeof e->result);
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
		nvvaluefree(&s->lastexit);
		if(isroot){
			s->rootstate = NvRootExit;
			nvvaluefree(&s->rootvalue);
			if(nvvaluecopy(&s->rootvalue, &e->exitreason) < 0){
				s->rootstate = NvRootFault;
				snprint(s->rootfault, sizeof s->rootfault, "system_limit");
			}
		}
		s->lastexit = e->exitreason;
		memset(&e->exitreason, 0, sizeof e->exitreason);
	}else{
		snprint(err, nerr, "bad execution state");
		return NvSchedError;
	}
	if(nvprocexit(r, &pid) != 1){
		snprint(err, nerr, "failed to exit completed process");
		return NvSchedError;
	}
	return NvSchedProgress;
}
