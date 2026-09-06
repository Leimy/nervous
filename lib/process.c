#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../include/nvbc.h"
#include "../include/nvvm.h"
#include "../include/nvexec.h"
#include "../include/nvpat.h"
#include "../include/nvproc.h"

static NvProcess *
lookup(NvRuntime *r, NvTerm pid)
{
	NvProcess *p;
	ulong slot;

	if(nvtermkind(pid) != Vpid)
		return nil;
	slot = nvpidslot(pid);
	if(slot >= r->nslot)
		return nil;
	p = &r->process[slot];
	if(p->generation != nvpidgeneration(pid) ||
	   p->state != Prrunnable && p->state != Prrunning && p->state != Prwaiting)
		return nil;
	return p;
}

int
nvruntimeinit(NvRuntime *r, NvLimits *limits, uvlong incarnation, char *err, int nerr)
{
	memset(r, 0, sizeof *r);
	if(limits == nil || limits->maxprocess == 0 || limits->maxmailbox == 0 || limits->maxmessage == 0 ||
	   limits->maxframe == 0 || limits->maxtermdepth == 0 || limits->maxtermdepth > NvMaxtermdepth ||
	   limits->maxatom == 0 || (limits->gcstress != 0 && limits->gcstress != 1)){
		snprint(err, nerr, "bad process limits");
		return -1;
	}
	/* D061: a slot index must fit the PID representation's slot field. */
	if(limits->maxprocess > NvMaxslot+1){
		snprint(err, nerr, "bad process limits");
		return -1;
	}
	/* D062: install this runtime's atom table ceiling. */
	if(nvatomlimit(limits->maxatom) < 0){
		snprint(err, nerr, "bad process limits");
		return -1;
	}
	r->limits = *limits;
	r->incarnation = incarnation;
	r->nextref = 1;
	r->runhead = NvNoslot;
	r->runtail = NvNoslot;
	r->nrunnable = 0;
	return 0;
}

/*
 * D059: the run queue is an intrusive doubly linked FIFO threaded through
 * the process slots by index. Every transition into Prrunnable appends
 * (spawn, quantum yield, message wake, deadline wake) and every transition
 * out removes (dispatch, or exit of a not-yet-dispatched process), so the
 * queue holds exactly the Prrunnable slots and dispatch is O(1) regardless
 * of how many processes are waiting. Links are indices, not pointers,
 * because nvprocspawn may realloc the table.
 */
static void
runenq(NvRuntime *r, ulong slot)
{
	NvProcess *p;

	p = &r->process[slot];
	p->runnext = NvNoslot;
	p->runprev = r->runtail;
	if(r->runtail == NvNoslot)
		r->runhead = slot;
	else
		r->process[r->runtail].runnext = slot;
	r->runtail = slot;
	r->nrunnable++;
}

static void
runrm(NvRuntime *r, ulong slot)
{
	NvProcess *p;

	p = &r->process[slot];
	if(p->runprev == NvNoslot)
		r->runhead = p->runnext;
	else
		r->process[p->runprev].runnext = p->runnext;
	if(p->runnext == NvNoslot)
		r->runtail = p->runprev;
	else
		r->process[p->runnext].runprev = p->runprev;
	p->runnext = NvNoslot;
	p->runprev = NvNoslot;
	r->nrunnable--;
}

int
nvprocrunhead(NvRuntime *r, ulong *slot)
{
	if(r->runhead == NvNoslot)
		return 0;
	*slot = r->runhead;
	return 1;
}

int
nvprocwake(NvRuntime *r, ulong slot)
{
	NvProcess *p;

	if(slot >= r->nslot)
		return -1;
	p = &r->process[slot];
	if(p->state != Prwaiting)
		return -1;
	p->state = Prrunnable;
	runenq(r, slot);
	return 0;
}

/*
 * D074/D059: move an already-Prrunnable slot to the run-queue tail. Used
 * by the scheduler to skip a runnable slot whose heap is under
 * off-process collection without dispatching it or disturbing the
 * FIFO order of every other runnable slot.
 */
int
nvprocrequeue(NvRuntime *r, ulong slot)
{
	if(slot >= r->nslot)
		return -1;
	if(r->process[slot].state != Prrunnable)
		return -1;
	runrm(r, slot);
	runenq(r, slot);
	return 0;
}

static void
execfree(NvProcess *p)
{
	if(p->exec == nil)
		return;
	nvexecfree(p->exec);
	free(p->exec);
	p->exec = nil;
}

/* D064: the mailbox is a chain of self-contained fragments; free each whole. */
static void
mailboxfree(NvProcess *p)
{
	NvFrag *f, *next;

	for(f = p->head; f != nil; f = next){
		next = f->next;
		nvfragfree(f);
	}
	p->head = p->tail = nil;
	p->scanprev = p->scan = nil;
	p->scanning = 0;
	p->mailboxwords = 0;
}

void
nvruntimefree(NvRuntime *r)
{
	ulong i;

	if(r == nil)
		return;
	for(i = 0; i < r->nslot; i++){
		execfree(&r->process[i]);
		mailboxfree(&r->process[i]);
	}
	free(r->process);
	memset(r, 0, sizeof *r);
}

int
nvprocspawn(NvRuntime *r, NvTerm *pid, char *err, int nerr)
{
	NvProcess *p, *q;
	ulong slot, cap, max;
	uintptr oldbase;

	if(r->nlive >= r->limits.maxprocess){
		snprint(err, nerr, "system_limit");
		return -1;
	}
	/* Exits lower freehint; successful spawns advance it. Lowest-slot
	 * reuse is unchanged, but append-only creation does not rescan live slots. */
	for(slot = r->freehint; slot < r->nslot; slot++){
		r->slotprobes++;
		p = &r->process[slot];
		if(p->state == Prexited && p->generation == NvMaxgeneration){
			p->state = Prretired;
			continue;
		}
		if(p->state == Prfree || p->state == Prexited)
			break;
	}
	if(slot == r->nslot){
		max = (~0UL)/sizeof *q;
		if(max > NvMaxslot+1)
			max = NvMaxslot+1;
		if(r->nslot >= max){
			snprint(err, nerr, "system_limit");
			return -1;
		}
		if(r->nslot == r->nalloc){
			cap = r->nalloc;
			if(cap == 0){
				cap = 16;
				if(cap > r->limits.maxprocess) cap = r->limits.maxprocess;
			}else
				cap = cap > max/2 ? max : cap*2;
			if(cap > max) cap = max;
			oldbase = (uintptr)r->process;
			q = realloc(r->process, cap*sizeof *q);
			if(q == nil){ snprint(err,nerr,"system_limit"); return -1; }
			r->tablegrows++;
			if(oldbase != 0 && oldbase != (uintptr)q){
				r->tablemoves++;
				r->tablemovebytes += (uvlong)r->nalloc*sizeof *q;
			}
			r->process = q;
			r->nalloc = cap;
		}
		/* Spare capacity is not a slot until initialized here. Retired
		 * slots may require capacity beyond maxprocess (a LIVE limit). */
		p = &r->process[r->nslot++];
		memset(p, 0, sizeof *p);
	}else{
		p = &r->process[slot];
		execfree(p);
		mailboxfree(p);
		/*
		 * R2-F15: belt-and-suspenders alongside nvprocexit's own clear.
		 * nvprocexit already zeroes hasdeadline/deadline on every path
		 * that can make a slot Prexited, so this is redundant today, but
		 * it makes the invariant true by local inspection at the point
		 * of reuse rather than depending on every future exit path having
		 * remembered to clear it there instead.
		 */
		p->hasdeadline = 0;
		p->deadline = 0;
	}
	r->freehint = slot+1;
	p->generation++;
	if(p->generation == 0)
		p->generation++;
	p->state = Prrunnable;
	runenq(r, slot);
	r->nlive++;
	r->nspawned++;
	if(r->nlive > r->maxlive)
		r->maxlive = r->nlive;
	*pid = nvpid(slot, p->generation);
	return 0;
}

int
nvprocdispatch(NvRuntime *r, NvTerm pid, char *err, int nerr)
{
	NvProcess *p;

	p = lookup(r, pid);
	if(p == nil){ snprint(err, nerr, "bad_pid"); return -1; }
	if(p->state != Prrunnable){ snprint(err, nerr, "bad_state"); return -1; }
	runrm(r, nvpidslot(pid));
	p->state = Prrunning;
	return 0;
}

int
nvprocyield(NvRuntime *r, NvTerm pid, char *err, int nerr)
{
	NvProcess *p;

	p = lookup(r, pid);
	if(p == nil){ snprint(err, nerr, "bad_pid"); return -1; }
	if(p->state != Prrunning){ snprint(err, nerr, "bad_state"); return -1; }
	p->state = Prrunnable;
	runenq(r, nvpidslot(pid));
	return 0;
}

int
nvprocwait(NvRuntime *r, NvTerm pid, char *err, int nerr)
{
	NvProcess *p;

	p = lookup(r, pid);
	if(p == nil){ snprint(err, nerr, "bad_pid"); return -1; }
	if(p->state != Prrunning){ snprint(err, nerr, "bad_state"); return -1; }
	p->state = Prwaiting;
	return 0;
}

int
nvprocexit(NvRuntime *r, NvTerm pid)
{
	NvProcess *p;

	p = lookup(r, pid);
	if(p == nil)
		return 0;
	/* D059: a process that dies before its first (or next) dispatch is still queued. */
	if(p->state == Prrunnable)
		runrm(r, nvpidslot(pid));
	execfree(p);
	mailboxfree(p);
	/*
	 * R2-F15: a deadline belongs to the process slot (D050), so exit must
	 * clear it here as well as mailboxfree/execfree clearing everything
	 * else. Without this, hasdeadline/deadline survive on an exited slot
	 * and, before this fix, on a slot nvprocspawn later reuses. The idle
	 * deadline scan in nvschedstep guards on state==Prwaiting, so a dead
	 * or freshly reused (Prrunnable) slot was never actually observed by
	 * it -- but that guard, not this clear, was doing the work D050
	 * describes. Clearing it here and in nvprocspawn's reuse branch above
	 * makes D050's "cannot outlive its slot" literally true of the field,
	 * not just true of the field's only reader.
	 */
	p->hasdeadline = 0;
	p->deadline = 0;
	p->state = Prexited;
	if(nvpidslot(pid) < r->freehint)
		r->freehint = nvpidslot(pid);
	if(r->nlive > 0)
		r->nlive--;
	return 1;
}

int
nvprocalive(NvRuntime *r, NvTerm pid)
{
	return lookup(r, pid) != nil;
}

int
nvprocref(NvRuntime *r, NvHeap *heap, NvTerm *ref, char *err, int nerr)
{
	uvlong n;
	NvTerm t;

	n = r->nextref;
	if(n == 0 || n == ~0ULL){
		r->nextref = ~0ULL;
		snprint(err, nerr, "system_limit");
		return -1;
	}
	r->nextref++;
	t = nvref(heap, r->incarnation, n);
	if(t == NvNil){
		snprint(err, nerr, nvheapexhausted(heap) ? "system_limit" : "out_of_memory");
		return -1;
	}
	*ref = t;
	return 0;
}

/*
 * D047/D061: nvfragcopy always enforces the portable NvMaxtermdepth
 * ceiling, but a runtime may configure a stricter limits.maxtermdepth. This
 * walks an already-copied fragment's root to enforce that stricter ceiling;
 * it is only ever called when maxtermdepth < NvMaxtermdepth, so the
 * recursion is bounded by NvMaxtermdepth (256) either way and cannot
 * exhaust the host C stack.
 */
static int
toodeep(NvTerm t, int depth, int maxdepth)
{
	int i, n;

	if(depth > maxdepth)
		return 1;
	if(!NvBoxed(t) || nvtermkind(t) != Vtuple)
		return 0;
	n = nvtuplelen(t);
	for(i = 0; i < n; i++)
		if(toodeep(nvtupleelem(t, i), depth+1, maxdepth))
			return 1;
	return 0;
}

/*
 * D064 message boundary. value == NvNil is rejected before nvfragcopy is
 * even called: nvfragcopy reports that case as NvTermerror, which is
 * numerically identical to its generic allocation-failure return (-1), so
 * the only way to report the NvNil case as mailbox_full (as nvproc.h's
 * nvprocsend contract requires) rather than misreporting a real allocation
 * failure as mailbox_full is to catch it here first.
 */
int
nvprocsend(NvRuntime *r, NvTerm pid, NvTerm value, char *err, int nerr)
{
	NvProcess *p;
	NvFrag *f;
	uvlong budget;
	int rc;

	p = lookup(r, pid);
	if(p == nil){
		r->ndropped++;
		return 0;
	}
	if(value == NvNil){
		snprint(err, nerr, "mailbox_full");
		return -1;
	}
	budget = p->mailboxwords >= r->limits.maxmailbox ? 0 : r->limits.maxmailbox-p->mailboxwords;
	if(r->limits.maxmessage < budget)
		budget = r->limits.maxmessage;
	rc = nvfragcopy(value, budget, &f);
	if(rc == NvTermlimit){
		snprint(err, nerr, "mailbox_full");
		return -1;
	}
	if(rc < 0){
		snprint(err, nerr, "system_limit");
		return -1;
	}
	if(r->limits.maxtermdepth < NvMaxtermdepth && toodeep(f->root, 1, r->limits.maxtermdepth)){
		nvfragfree(f);
		snprint(err, nerr, "mailbox_full");
		return -1;
	}
	f->next = nil;
	if(p->tail != nil)
		p->tail->next = f;
	else
		p->head = f;
	p->tail = f;
	p->mailboxwords += nvfragwords(f);
	r->nsent++;
	if(p->state == Prwaiting)
		nvprocwake(r, nvpidslot(pid));
	return 1;
}

int
nvprocpop(NvRuntime *r, NvTerm pid, NvFrag **msg, char *err, int nerr)
{
	NvProcess *p;
	NvFrag *f;

	p = lookup(r, pid);
	if(p == nil){ snprint(err,nerr,"bad_pid"); return -1; }
	if(p->scanning){ snprint(err,nerr,"bad_state"); return -1; }
	f = p->head;
	if(f == nil)
		return 0;
	p->head = f->next;
	if(p->head == nil)
		p->tail = nil;
	p->mailboxwords -= nvfragwords(f);
	f->next = nil;
	*msg = f;
	return 1;
}

/* Bytecode receive scanning is an explicit host-owned phase. */
int
nvprocrecvbegin(NvRuntime *r, NvTerm pid, NvTerm *value, char *err, int nerr)
{
	NvProcess *p;

	p = lookup(r, pid);
	if(p == nil){ snprint(err,nerr,"bad_pid"); return -1; }
	if(p->state != Prrunning){ snprint(err,nerr,"bad_state"); return -1; }
	if(p->scanning){ snprint(err,nerr,"bad_state"); return -1; }
	p->scanning = 1;
	p->scanprev = nil;
	p->scan = p->head;
	if(p->scan == nil)
		return 0;
	*value = p->scan->root;
	return 1;
}

int
nvprocrecvnext(NvRuntime *r, NvTerm pid, NvTerm *value, char *err, int nerr)
{
	NvProcess *p;

	p = lookup(r, pid);
	if(p == nil){ snprint(err,nerr,"bad_pid"); return -1; }
	if(p->state != Prrunning || !p->scanning){ snprint(err,nerr,"bad_state"); return -1; }
	if(p->scan == nil)
		return 0;
	p->scanprev = p->scan;
	p->scan = p->scan->next;
	if(p->scan == nil)
		return 0;
	*value = p->scan->root;
	return 1;
}

/* Non-consuming reservation query; the owner alone may take this candidate. */
int
nvprocrecvneed(NvRuntime *r, NvTerm pid, uvlong *words, char *err, int nerr)
{
	NvProcess *p;

	p = lookup(r, pid);
	if(p == nil){ snprint(err,nerr,"bad_pid"); return -1; }
	if(p->state != Prrunning || !p->scanning || p->scan == nil){ snprint(err,nerr,"bad_state"); return -1; }
	*words = nvfragwords(p->scan);
	return 0;
}

int
nvprocrecvtake(NvRuntime *r, NvTerm pid, NvFrag **taken, char *err, int nerr)
{
	NvProcess *p;
	NvFrag *f;

	p = lookup(r, pid);
	if(p == nil){ snprint(err,nerr,"bad_pid"); return -1; }
	if(p->state != Prrunning || !p->scanning || p->scan == nil){ snprint(err,nerr,"bad_state"); return -1; }
	f = p->scan;
	if(p->scanprev != nil)
		p->scanprev->next = f->next;
	else
		p->head = f->next;
	if(p->tail == f)
		p->tail = p->scanprev;
	p->mailboxwords -= nvfragwords(f);
	p->scanprev = p->scan = nil;
	p->scanning = 0;
	f->next = nil;
	*taken = f;
	return 0;
}

int
nvprocrecvwait(NvRuntime *r, NvTerm pid, char *err, int nerr)
{
	NvProcess *p;

	p = lookup(r, pid);
	if(p == nil){ snprint(err,nerr,"bad_pid"); return -1; }
	if(p->state != Prrunning || !p->scanning || p->scan != nil){ snprint(err,nerr,"bad_state"); return -1; }
	/*
	 * A receive with no `after` clause never has an active deadline.
	 * Clearing it here means a deadline armed by an earlier receive in
	 * this same process, and left unconsumed because that receive
	 * matched a message instead of timing out, can never leak into an
	 * unrelated later plain receive and wake it early.
	 */
	p->hasdeadline = 0;
	/* A send may append after this scan exhausted but before recvwait runs. */
	if(p->scanprev == nil ? p->head != nil : p->scanprev->next != nil){
		p->scanprev = nil;
		p->scanning = 0;
		return 0;
	}
	p->scanprev = nil;
	p->scanning = 0;
	p->state = Prwaiting;
	return 0;
}

/*
 * D049/D050: validate and arm one absolute deadline for a receive's
 * `after` clause. An `'infinity` duration arms nothing, matching an
 * ordinary unbounded receive. Any other non-integer term, or an integer
 * outside 0..NvMaxduration, faults `bad_timeout`. The checked add keeps
 * deadline arithmetic total; NvMaxduration is chosen so this can only
 * fail if `now` itself is already corrupt.
 */
int
nvprocarmdeadline(NvRuntime *r, NvTerm pid, NvTerm duration, uvlong now, char *err, int nerr)
{
	NvProcess *p;
	uvlong deadline;
	vlong ival;

	p = lookup(r, pid);
	if(p == nil){ snprint(err,nerr,"bad_pid"); return -1; }
	if(nvtermkind(duration) == Vatom){
		if(strcmp(nvtermatom(duration), "infinity") != 0){
			snprint(err,nerr,"bad_timeout");
			return -1;
		}
		p->hasdeadline = 0;
		p->deadline = 0;
		return 0;
	}
	if(nvtermkind(duration) != Vint){
		snprint(err,nerr,"bad_timeout");
		return -1;
	}
	ival = nvtermint(duration);
	if(ival < 0 || ival > NvMaxduration){
		snprint(err,nerr,"bad_timeout");
		return -1;
	}
	deadline = now + (uvlong)ival;
	if(deadline < now){
		snprint(err,nerr,"system_limit");
		return -1;
	}
	p->hasdeadline = 1;
	p->deadline = deadline;
	return 0;
}

/*
 * D048/D050/D051: resume an exhausted receive scan. A message that
 * raced the scan (queued after it exhausted, before this call) always
 * takes priority over an expired deadline, exactly like nvprocrecvwait:
 * the process stays runnable and the caller retries the scan from the
 * oldest retained message. Otherwise, an expired armed deadline is
 * consumed (cleared so it cannot fire twice) and reported to the caller
 * so it can fall through to the timeout body instead of blocking.
 */
int
nvprocrecvwaitdeadline(NvRuntime *r, NvTerm pid, uvlong now, char *err, int nerr)
{
	NvProcess *p;

	p = lookup(r, pid);
	if(p == nil){ snprint(err,nerr,"bad_pid"); return -1; }
	if(p->state != Prrunning || !p->scanning || p->scan != nil){ snprint(err,nerr,"bad_state"); return -1; }
	if(p->scanprev == nil ? p->head != nil : p->scanprev->next != nil){
		p->scanprev = nil;
		p->scanning = 0;
		return 0;
	}
	p->scanprev = nil;
	p->scanning = 0;
	if(p->hasdeadline && now >= p->deadline){
		p->hasdeadline = 0;
		p->deadline = 0;
		return 1;
	}
	p->state = Prwaiting;
	return 0;
}

int
nvprocreceive(NvRuntime *r, NvTerm pid, NvPatClause *clause, int nclause, NvBindings *bindings, int *which, NvFrag **msg, char *err, int nerr)
{
	NvProcess *p;
	NvFrag *f, *prev;
	int selected, rc;

	p = lookup(r, pid);
	if(p == nil){ snprint(err,nerr,"bad_pid"); return -1; }
	if(p->state != Prrunning || p->scanning){ snprint(err,nerr,"bad_state"); return -1; }
	prev = nil;
	for(f = p->head; f != nil; prev = f, f = f->next){
		rc = nvclauseselect(clause, nclause, f->root, bindings, &selected, err, nerr);
		if(rc < 0)
			return -1;
		if(rc == 0)
			continue;
		if(prev != nil)
			prev->next = f->next;
		else
			p->head = f->next;
		if(p->tail == f)
			p->tail = prev;
		p->mailboxwords -= nvfragwords(f);
		f->next = nil;
		*msg = f;
		if(which != nil)
			*which = selected;
		return 1;
	}
	p->state = Prwaiting;
	if(which != nil)
		*which = -1;
	return 0;
}
