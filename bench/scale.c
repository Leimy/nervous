#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../include/nervous.h"
#include "../include/nvbc.h"
#include "../include/nvvm.h"
#include "../include/nvexec.h"
#include "../include/nvcompile.h"
#include "../include/nvproc.h"
#include "../include/nvsched.h"

/* Diagnostic only: use the real cpubound.nv worker/collector, but create
 * every process before timing so its heap and initial owner can be set.
 * No runtime defaults change. All shared machine state is heap allocated.
 */
static NvScheduler *machine;

static void
check(int ok, char *why)
{
	if(!ok)
		sysfatal("scale: %s", why);
}

static ulong
number(char *s, ulong max)
{
	uvlong n;
	char *p;

	n = 0;
	for(p = s; *p; p++){
		check(*p >= '0' && *p <= '9', "unsigned decimal arguments required");
		n = n*10 + *p-'0';
		check(n <= max, "argument out of range");
	}
	check(p != s, "empty argument");
	return n;
}

static int
interrupt(void *ureg, char *note)
{
	USED(ureg);
	if(machine == nil || strcmp(note, "interrupt") != 0)
		return 0;
	nvmachineinterrupt(machine);
	return 1;
}

static NvModule *
module(char *file)
{
	Parser p;
	Program *pr;
	NvModule *m;
	Dir *d;
	char *src, err[256];
	long n;
	int fd;

	fd = open(file, OREAD);
	check(fd >= 0, "open source");
	d = dirfstat(fd);
	check(d != nil, "stat source");
	check(d->length > 0 && d->length <= 1024*1024, "source size");
	n = d->length;
	free(d);
	src = malloc(n+1);
	check(src != nil, "source allocation");
	check(readn(fd, src, n) == n, "read source");
	close(fd);
	src[n] = 0;
	pr = parseprogram(&p, file, src, n);
	free(src);
	check(pr != nil, p.err);
	m = nvcompile(pr, err, sizeof err);
	programfree(pr);
	check(m != nil, err);
	return m;
}

/* Independent oracle for sum(i*i % 7), i=1..n; no VM or recursive loop. */
static vlong
expected(ulong workers, ulong n)
{
	static int prefix[] = {0, 1, 5, 7, 9, 13, 14};

	return (vlong)workers*(14ULL*(n/7) + prefix[n%7]);
}

void
main(int argc, char **argv)
{
	NvModule *m;
	NvScheduler *s;
	NvSched *sc;
	NvLimits l;
	NvHeap h;
	NvExec *e;
	NvTerm root, pid, arg, av[2];
	char err[256];
	ulong nsched, workers, iters, headroom, locking, profile, balanced, sharded;
	ulong i, capmin, capmax, cap;
	uvlong start, elapsed, dispatches, reductions, gc, acq, waits;
	uvlong input, output;
	vlong want;
	int state, owner;

	if(argc != 9 && argc != 10){
		fprint(2, "usage: %s source schedulers workers iters headroom lock(0|1) profile(0|1) balanced(0|1) [sharded(0|1)]\n", argv[0]);
		exits("usage");
	}
	nsched = number(argv[2], NvMaxsched);
	workers = number(argv[3], 256);
	iters = number(argv[4], 10000000);
	headroom = number(argv[5], 65536);
	locking = number(argv[6], 1);
	profile = number(argv[7], 1);
	balanced = number(argv[8], 1);
	sharded = argc == 10 ? number(argv[9], 1) : 1;
	check(nsched > 0 && workers > 0 && iters > 0, "positive schedulers/workers/iters required");
	check(nsched == 1 || locking == 1, "N>1 requires locking");
	m = module(argv[1]);
	s = mallocz(sizeof *s, 1);
	check(s != nil, "machine allocation");
	memset(&l, 0, sizeof l);
	l.maxprocess = workers+1;
	l.maxmailbox = 2*1024*1024;
	l.maxmessage = 128*1024;
	l.maxframe = 1024;
	l.maxtermdepth = NvMaxtermdepth;
	l.maxduration = NvMaxduration;
	l.maxatom = 65536;
	check(nvschedinit(s, m, &l, 1, 1000, err, sizeof err) == 0, err);
	check(nvschedsetnsched(s, nsched, err, sizeof err) == 0, err);
	nvschedsetlocking(s, locking);
	s->sharded = sharded;	/* Diagnostic control only, before any execution. */
	nvheapinit(&h, 0);
	av[0] = nvint(nil, workers);
	av[1] = nvint(nil, 0);
	arg = nvtuple(&h, av, 2);
	check(arg != NvNil, "collector args");
	check(nvschedspawnroot(s, "collect", arg, &root, err, sizeof err) == 0, err);
	av[0] = root;
	av[1] = nvint(nil, iters);
	arg = nvtuple(&h, av, 2);
	check(arg != NvNil, "worker args");
	capmin = ~0UL;
	capmax = 0;
	for(i = 0; i < workers; i++){
		check(nvschedspawn(s, "worker", arg, &pid, err, sizeof err) == 0, err);
		e = nvprocat(&s->runtime, nvpidslot(pid))->exec;
		if(headroom != 0)
			check(nvexeccollect(e, headroom) == 0, "worker pre-sizing");
		cap = e->heap.cur->cap;
		if(cap < capmin) capmin = cap;
		if(cap > capmax) capmax = cap;
		owner = balanced ? i%nsched : 0;
		/* No procs running yet: safe setup-only transfer, not a measured steal. */
		if(owner != 0)
			check(nvprocsteal(&s->runtime, nvpidslot(pid), owner) == 0, "initial placement");
	}
	nvheapfree(&h);
	for(i = 0; i < nsched; i++)
		s->sched[i]->profile = profile;
	print("scale: schedulers %lud workers %lud iters %lud headroom %lud lock %lud profile %lud balanced %lud\n",
		nsched, workers, iters, headroom, locking, profile, balanced);
	print("setup: worker capacities %lud..%lud words; quantum 1000; sharded %lud; setup excluded\n", capmin, capmax, sharded);

	machine = s;
	atnotify(interrupt, 1);
	start = uptime();
	state = nvmachinerun(s, err, sizeof err);
	elapsed = uptime()-start;
	atnotify(interrupt, 0);
	machine = nil;
	check(state != NvSchedError, err);
	check(state == NvSchedDone && s->rootstate == NvRootDone, "machine/collector completion");
	want = expected(workers, iters);
	check(s->rootvalue != nil && nvtermkind(s->rootvalue->root) == Vint &&
		nvtermint(s->rootvalue->root) == want, "arithmetic result");
	check(s->runtime.nspawned == workers+1 && s->completed == workers+1 &&
		s->runtime.nlive == 0 && s->faulted == 0 && s->exited == 0, "process accounting");
	check(s->runtime.nsent == workers && s->runtime.ndropped == 0, "message accounting");
	dispatches = reductions = gc = acq = waits = input = output = 0;
	for(i = 0; i < nsched; i++){
		sc = s->sched[i];
		check(sc->gcfailed == 0 && sc->gcoutstanding == 0, "collection accounting");
		check(sc->localacq + sc->globalacq == sc->lockacq, "lock domain accounting");
		check(sc->localwaitns + sc->globalwaitns == sc->lockwaitns, "lock timing accounting");
		dispatches += sc->dispatches;
		reductions += sc->reductions;
		gc += sc->collections;
		acq += sc->lockacq;
		waits += sc->lockwaitns;
		input += sc->gcinputwords;
		output += sc->gcoutputwords;
		print("sched %d: dispatches %llud reductions %llud GC %llud (demand %llud idle %llud) steals %llud sleeps %llud sleepns %llud lockacq %llud\n",
			sc->index, sc->dispatches, sc->reductions, sc->collections, sc->gcdemand, sc->gcidle,
			sc->stealstaken, sc->sleeps, sc->sleepns, sc->lockacq);
		print("lock domains %d: local %llud global %llud\n", sc->index, sc->localacq, sc->globalacq);
		if(profile)
			print("profile domains %d: local-acquire-elapsed %llud ns global-acquire-elapsed %llud ns\n",
				sc->index, sc->localwaitns, sc->globalwaitns);
		if(profile)
			print("profile %d: lock-acquire-elapsed %llud ns exec %llud ns GC %llud ns\n",
				sc->index, sc->lockwaitns, sc->execns, sc->gcns);
	}
	print("result: %lld verified; elapsed %llud ns; dispatches %llud reductions %llud GC %llud lockacq %llud\n",
		want, elapsed, dispatches, reductions, gc, acq);
	print("GC words: input %llud output %llud; messages %llud; processes %llud completed\n",
		input, output, s->runtime.nsent, s->completed);
	if(profile)
		print("profile total: lock-acquire-elapsed %llud ns (overlapping across procs, includes acquisition and timing overhead)\n", waits);
	nvschedfree(s);
	free(s);
	nvmodulefree(m);
	exits(nil);
}
