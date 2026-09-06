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

/* Phase-separated diagnostic, not a throughput replacement for run.rc.
 * Snapshots/printing happen OUTSIDE timed phases; profiling is opt-in.
 * Each invocation is a fresh host process to keep high-water comparable. */
typedef struct Mark Mark;
struct Mark {
	uvlong dispatches, reductions, sent, gc, failed;
	uvlong input, output, execns, gcns, spawnns;
};
static Mark prev;

static void
check(int ok, char *s)
{
	if(!ok)
		sysfatal("perftest: %s", s);
}

static ulong
number(char *s, ulong max)
{
	uvlong n;
	char *p;

	n = 0;
	for(p = s; *p; p++){
		check(*p >= '0' && *p <= '9', "arguments must be unsigned decimal");
		n = n*10 + *p-'0';
		check(n <= max, "argument out of range");
	}
	check(p != s, "empty argument");
	return n;
}

static NvModule *
module(void)
{
	char *src =
		"fn waiter() { receive { 'stop => 'ok; } }\n"
		"fn ponger() {\n"
		"  receive {\n"
		"    ${'ping, from} => { from ! 'pong; ponger() }\n"
		"    'stop => 'ok;\n"
		"  }\n"
		"}\n"
		"fn pings(p, 0, got) { got }\n"
		"fn pings(p, n, got) {\n"
		"  p ! ${'ping, self};\n"
		"  receive { 'pong => pings(p, n - 1, got + 1); }\n"
		"}\n";
	Parser p;
	Program *pr;
	NvModule *m;
	char err[256];

	pr = parseprogram(&p, "perf-phases", src, strlen(src));
	check(pr != nil, p.err);
	m = nvcompile(pr, err, sizeof err);
	programfree(pr);
	check(m != nil, err);
	return m;
}

static void
phase(NvScheduler *s, char *label, uvlong start, uintptr base)
{
	NvMemstats m;
	Mark now;
	uvlong elapsed, sent, attempts;
	uintptr high;

	elapsed = uptime()-start;
	high = (uintptr)sbrk(0)-base;
	now.dispatches = s->dispatches;
	now.reductions = s->reductions;
	now.sent = s->runtime.nsent;
	now.gc = s->collections;
	now.failed = s->gcfailed;
	now.input = s->gcinputwords;
	now.output = s->gcoutputwords;
	now.execns = s->execns;
	now.gcns = s->gcns;
	now.spawnns = s->spawnns;
	sent = now.sent-prev.sent;
	attempts = now.gc-prev.gc + now.failed-prev.failed;
	nvschedmemory(s, &m);
	print("phase %s: elapsed %llud ns; sent %llud; dispatches %llud; reductions %llud",
		label, elapsed, sent, now.dispatches-prev.dispatches, now.reductions-prev.reductions);
	if(sent != 0) print("; %llud ns/message", elapsed/sent);
	print("\n");
	print("  GC: %llud successful, %llud failed; input %llud words; output %llud live words\n",
		now.gc-prev.gc, now.failed-prev.failed, now.input-prev.input, now.output-prev.output);
	if(s->profile){
		print("  timed: exec %llud ns; GC %llud ns; spawn %llud ns (overlaps exec for bytecode spawn)",
			now.execns-prev.execns, now.gcns-prev.gcns, now.spawnns-prev.spawnns);
		if(attempts != 0) print("; %llud ns/GC attempt", (now.gcns-prev.gcns)/attempts);
		print("\n");
	}
	print("  requested bytes now: total %llud; table %llud; execs %llud; heap capacity+headers %llud; stacks %llud; adopted %llud; mailbox %llud; reports %llud\n",
		m.totalbytes, m.tablebytes, m.execbytes, m.heapbytes, m.stackbytes, m.adoptedbytes, m.mailboxbytes, m.reportbytes);
	print("  subsets: heap used %llud bytes; stack used %llud bytes; execs %llud; adopted fragments %llud; mailbox fragments %llud\n",
		m.heapused, m.stackused, m.nexec, m.nadopted, m.nmailbox);
	print("  host break growth %llud bytes; slots %lud initialized/%lud capacity; table grows %llud, moves %llud, old requested bytes on moved grows %llud; reuse probes %llud\n",
		(uvlong)high, s->runtime.nslot, s->runtime.nalloc, s->runtime.tablegrows,
		s->runtime.tablemoves, s->runtime.tablemovebytes, s->runtime.slotprobes);
	prev = now;
}

static int
drive(NvScheduler *s, uvlong maxsteps)
{
	char err[256];
	uvlong i;
	int state;

	for(i = 0; i < maxsteps; i++){
		state = nvschedstep(s, err, sizeof err);
		check(state != NvSchedError, err);
		if(state != NvSchedProgress)
			return state;
	}
	sysfatal("perftest: dispatch limit reached");
}

void
main(int argc, char **argv)
{
	NvModule *m;
	NvScheduler s;
	NvLimits l;
	NvHeap h;
	NvMemstats mem;
	NvTerm arg, busyarg, pid, pong, root, elem[3], stop;
	NvExec *e;
	char err[256];
	ulong waiters, pings, headroom, profile, i;
	uvlong start, maxsteps, before;
	uintptr base;
	int state;

	if(argc != 5){
		fprint(2, "usage: %s waiters pings busy-heap-headroom profile(0|1)\n", argv[0]);
		exits("usage");
	}
	waiters = number(argv[1], 65534);
	pings = number(argv[2], 10000000);
	headroom = number(argv[3], 65536);
	profile = number(argv[4], 1);
	check(pings > 0, "pings must be positive");
	maxsteps = 1000ULL*(waiters+pings+1);
	m = module();
	nvheapinit(&h, 0);
	arg = nvtuple(&h, nil, 0);
	check(arg != NvNil, "argument allocation");
	memset(&l, 0, sizeof l);
	l.maxprocess = waiters+2;
	l.maxmailbox = 2*1024*1024;
	l.maxmessage = 128*1024;
	l.maxframe = 1024;
	l.maxtermdepth = NvMaxtermdepth;
	l.maxduration = NvMaxduration;
	l.maxatom = 65536;
	print("waiters=%lud pings=%lud busy-headroom=%lud profiling=%lud; times exclude snapshots/printing\n", waiters, pings, headroom, profile);
	base = (uintptr)sbrk(0);
	start = uptime();
	check(nvschedinit(&s, m, &l, 1, 1000, err, sizeof err) == 0, err);
	s.profile = profile;
	for(i = 0; i < waiters; i++)
		check(nvschedspawn(&s, "waiter", arg, &pid, err, sizeof err) == 0, err);
	phase(&s, "waiters-spawned", start, base);

	start = uptime();
	state = drive(&s, maxsteps);
	check(state == NvSchedIdle || (waiters == 0 && state == NvSchedDone), "waiters did not block");
	phase(&s, "waiters-blocked", start, base);

	start = uptime();
	check(nvschedspawn(&s, "ponger", arg, &pong, err, sizeof err) == 0, err);
	elem[0] = pong; elem[1] = nvint(nil, pings); elem[2] = nvint(nil, 0);
	busyarg = nvtuple(&h, elem, 3);
	check(busyarg != NvNil, "busy argument allocation");
	check(nvschedspawnroot(&s, "pings", busyarg, &root, err, sizeof err) == 0, err);
	/* A host-only sizing experiment, not a changed runtime default.
	 * These two explicit collections are setup, excluded from GC counters.
	 * No shrinking means the resulting spaces persist during traffic. */
	if(headroom != 0){
		e = s.runtime.process[nvpidslot(pong)].exec;
		check(nvexeccollect(e, headroom) == 0, "ponger pre-sizing");
		e = s.runtime.process[nvpidslot(root)].exec;
		check(nvexeccollect(e, headroom) == 0, "pinger pre-sizing");
	}
	phase(&s, "busy-setup", start, base);
	print("  busy capacities before traffic: ponger %lud words, pinger %lud words\n",
		s.runtime.process[nvpidslot(pong)].exec->heap.cur->cap,
		s.runtime.process[nvpidslot(root)].exec->heap.cur->cap);

	before = s.runtime.nsent;
	start = uptime();
	state = drive(&s, maxsteps);
	check(state == NvSchedIdle && s.rootstate == NvRootDone && s.rootvalue != nil &&
		nvtermkind(s.rootvalue->root) == Vint && nvtermint(s.rootvalue->root) == pings, "traffic result");
	check(s.runtime.nsent-before == 2ULL*pings && s.runtime.nlive == waiters+1, "traffic count/lifecycle");
	check(s.faulted == 0 && s.gcfailed == 0, "traffic fault or failed collection");
	phase(&s, "traffic", start, base);

	stop = nvatom("stop");
	check(stop != NvNil, "stop atom");
	start = uptime();
	for(i = 0; i < s.runtime.nslot; i++)
		if(s.runtime.process[i].state == Prwaiting){
			pid = nvpid(i, s.runtime.process[i].generation);
			check(nvprocsend(&s.runtime, pid, stop, err, sizeof err) == 1, "drain send");
		}
	check(drive(&s, maxsteps) == NvSchedDone && s.faulted == 0, "drain result");
	phase(&s, "drained", start, base);
	nvschedfree(&s);
	nvschedmemory(&s, &mem);
	check(mem.totalbytes == 0, "runtime storage after free");
	print("after runtime free: requested runtime bytes %llud; host break growth %llud bytes (allocator need not lower break)\n",
		mem.totalbytes, (uvlong)((uintptr)sbrk(0)-base));
	nvheapfree(&h);
	nvmodulefree(m);
	exits(nil);
}
