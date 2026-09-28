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

/*
 * M09-T05 (part 1): term-API latency on shared chains.
 *
 * Chain: x0 = 5; x(k+1) = ${x(k), x(k)}.  Depth d holds only d tuple
 * objects (both elements one shared pointer), but the traversals
 * (nvtermequal, nvtermprint, nvfragcopy) have no visited set: each
 * follows both element pointers into the same subtree, so the work is
 * 2^(d+1)-1 node visits -- exponential in d, not linear.  The depth
 * series is capped at 24 (2^25-1 ~ 33.5M visits, a few seconds per
 * sample at most).
 *
 * NvMaxtermdepth (256) is a per-path limit: on a shared chain it is
 * unreachable in any finite time, because the exponential work
 * overflows long before 256 levels are reached.  So the limit is
 * probed separately on a LINEAR chain (x(k+1) = ${x(k)}, no sharing),
 * where each traversal is O(d): depth 255 completes, depth 256 is
 * refused after ~257 steps and reports NvTermlimit -- nvfragcopy
 * allocates nothing when it refuses.
 *
 * nvfragcopy is timed with maxwords = ~0ULL (for nvfragcopy, 0 is a
 * hard zero budget, not unlimited); each sample's fragment is freed
 * after timing.  Equality compares two independently built chains in
 * separate heaps, so no pointer is shared and the word fast path
 * never fires.  11 samples per cell; min and median reported, with
 * the return value (checked consistent across samples) so a refusal
 * is visible, not silent.
 */

enum {
	Nsamps = 11,
	NvRoundstepcap = 1000000,	/* safety valve, not a tuned bound */
};

static ulong
depths[] = {1, 2, 4, 8, 12, 16, 20, 24};

static int
check(int ok, char *s)
{
	if(!ok)
		sysfatal("latency: %s", s);
	return 0;
}

static int
cmpu(void *a, void *b)
{
	uvlong x, y;

	x = *(uvlong*)a;
	y = *(uvlong*)b;
	if(x < y)
		return -1;
	if(x > y)
		return 1;
	return 0;
}

/* shared: ${x,x} (2 elems, one pointer); linear: ${x} (1 elem). */
static NvTerm
build(NvHeap *h, int d, int shared)
{
	NvTerm cur, elem[2];
	int i;

	cur = nvint(nil, 5);
	check(cur != NvNil, "small int");
	for(i = 0; i < d; i++){
		elem[0] = cur;
		if(shared)
			elem[1] = cur;
		cur = nvtuple(h, elem, shared ? 2 : 1);
		check(cur != NvNil, "tuple");
	}
	return cur;
}

/* Time each op Nsamps times on two independently built chains. */
static void
measure(int d, int shared, Biobuf *bp, uvlong *eq, uvlong *pr, uvlong *cp, int *ret)
{
	NvHeap ha, hb;
	NvTerm a, b;
	uvlong i;
	vlong t0, t1;
	int r;
	NvFrag *f;

	nvheapinit(&ha, 0);
	nvheapinit(&hb, 0);
	a = build(&ha, d, shared);
	b = build(&hb, d, shared);

	for(i = 0; i < Nsamps; i++){
		t0 = nsec();
		r = nvtermequal(a, b);
		t1 = nsec();
		check(i == 0 || r == ret[0], "nvtermequal return changed");
		ret[0] = r;
		eq[i] = t1 - t0;
	}
	for(i = 0; i < Nsamps; i++){
		t0 = nsec();
		r = nvtermprint(bp, a);
		t1 = nsec();
		check(i == 0 || r == ret[1], "nvtermprint return changed");
		ret[1] = r;
		pr[i] = t1 - t0;
	}
	for(i = 0; i < Nsamps; i++){
		f = nil;
		t0 = nsec();
		r = nvfragcopy(a, ~0ULL, &f);
		t1 = nsec();
		check(i == 0 || r == ret[2], "nvfragcopy return changed");
		ret[2] = r;
		cp[i] = t1 - t0;
		if(r == 0){
			check(f != nil, "nvfragcopy set no fragment");
			/*
			 * Unshared copy: shared has 2^d-1 tuples of 3 words,
			 * linear has d tuples of 2 words; plus the root word.
			 * 1ULL: ulong is 32 bits on Plan 9.
			 */
			if(shared)
				check(nvfragwords(f) == 3*((1ULL<<d)-1)+1, "fragment word count");
			else
				check(nvfragwords(f) == 2*(uvlong)d+1, "fragment word count");
			nvfragfree(f);
		}
	}
	nvheapfree(&ha);
	nvheapfree(&hb);
}

static void
row(char *shape, uvlong d, uvlong *eq, uvlong *pr, uvlong *cp, int *ret)
{
	qsort(eq, Nsamps, sizeof(uvlong), cmpu);
	qsort(pr, Nsamps, sizeof(uvlong), cmpu);
	qsort(cp, Nsamps, sizeof(uvlong), cmpu);
	print("%-6s %4llud %8llud  %9llud %9llud  %9llud %9llud  %9llud %9llud  %4d %4d %4d\n",
		shape, d, shape[0] == 's' ? (1ULL<<(d+1))-1 : d+1,
		eq[0], eq[Nsamps/2], pr[0], pr[Nsamps/2], cp[0], cp[Nsamps/2],
		ret[0], ret[1], ret[2]);
}

static void
terms(void)
{
	Biobuf *bp;
	uvlong eq[Nsamps], pr[Nsamps], cp[Nsamps];
	int ret[3];
	ulong i;

	bp = Bopen("/dev/null", OWRITE);
	check(bp != nil, "Bopen /dev/null");

	print("shape  depth    nodes   eq_min    eq_med   pr_min    pr_med   cp_min    cp_med   eq_ret pr_ret cp_ret\n");
	for(i = 0; i < nelem(depths); i++){
		measure((int)depths[i], 1, bp, eq, pr, cp, ret);
		row("shared", depths[i], eq, pr, cp, ret);
	}
	/*
	 * Limit probe: linear chain (no sharing), O(d) traversals.
	 * 255 = deepest term a traversal accepts; 256 = refused with
	 * NvTermlimit (nvfragcopy allocates nothing).
	 */
	measure(255, 0, bp, eq, pr, cp, ret);
	row("linear", 255, eq, pr, cp, ret);
	measure(256, 0, bp, eq, pr, cp, ret);
	row("linear", 256, eq, pr, cp, ret);

	check(Bterm(bp) >= 0, "Bterm");
}

static uvlong
number(char *s, uvlong max)
{
	uvlong n;
	char *p;

	n = 0;
	for(p = s; *p; p++){
		check(*p >= '0' && *p <= '9', "arguments must be unsigned decimal");
		n = n*10 + (uvlong)(*p-'0');
		check(n <= max, "argument out of range");
	}
	check(p != s, "empty argument");
	return n;
}

static uvlong
percentile(uvlong *sorted, uvlong n, double frac)
{
	uvlong idx;

	if(n == 0)
		return 0;
	idx = (uvlong)(frac*(double)(n-1));
	if(idx >= n)
		idx = n-1;
	return sorted[idx];
}

static void
report(char *label, uvlong *samples, uvlong n)
{
	uvlong i, sum, min, max;
	double mean;

	if(n == 0){
		print("%s: no samples\n", label);
		return;
	}
	qsort(samples, n, sizeof(uvlong), cmpu);
	sum = 0;
	for(i = 0; i < n; i++)
		sum += samples[i];
	mean = (double)sum/(double)n;
	min = samples[0];
	max = samples[n-1];
	print("%s: n=%llud min=%llud ns mean=%.0f ns p50=%llud ns p90=%llud ns p99=%llud ns p99.9=%llud ns max=%llud ns\n",
		label, n, min, mean,
		percentile(samples, n, 0.50),
		percentile(samples, n, 0.90),
		percentile(samples, n, 0.99),
		percentile(samples, n, 0.999),
		max);
}

static char *
statename(int state)
{
	switch(state){
	case NvSchedProgress: return "Progress";
	case NvSchedIdle: return "Idle";
	case NvSchedDone: return "Done";
	case NvSchedError: return "Error";
	}
	return "unknown";
}

static NvModule *
module(void)
{
	char *src =
		"fn peer() {\n"
		"  receive {\n"
		"    'tick => { self ! 'tock; peer() }\n"
		"    'tock => peer();\n"
		"    'stop => 'ok;\n"
		"  }\n"
		"}\n"
		"fn chain(x, 0) { x }\n"
		"fn chain(x, n) { chain(${x, x}, n - 1) }\n"
		"fn hog(a, b) {\n"
		"  a == b;\n"
		"  receive {\n"
		"    'stop => 'ok;\n"
		"    after 0 => hog(a, b);\n"
		"  }\n"
		"}\n"
		"fn hogstart(d) { hog(chain(1, d), chain(1, d)) }\n";
	Parser p;
	Program *pr;
	NvModule *m;
	char err[256];

	pr = parseprogram(&p, "latency", src, strlen(src));
	check(pr != nil, p.err);
	m = nvcompile(pr, err, sizeof err);
	programfree(pr);
	check(m != nil, err);
	return m;
}

/* Drives the scheduler one host-issued tick at a time per peer per round,
 * timing each round trip by watching the global sent-message counter
 * advance by exactly 2: the host's tick, then the peer's self-reply.
 * This loop never favors the peer being measured over whatever else is
 * runnable. */
static void
measurerounds(NvScheduler *s, NvTerm *peerpid, ulong peers, uvlong rounds,
	NvTerm tick, uvlong *samples, uvlong *nsamples, char *err, int nerr)
{
	uvlong r, before, t0, t1, steps, idx;
	ulong p;
	int state;
	char diag[320];

	idx = 0;
	for(r = 0; r < rounds; r++){
		for(p = 0; p < peers; p++){
			before = s->runtime.nsent;
			t0 = uptime();
			check(nvprocsend(&s->runtime, peerpid[p], tick, err, nerr) == 1, "tick send");
			steps = 0;
			while(s->runtime.nsent-before < 2){
				state = nvschedstep(s, err, nerr);
				if(state != NvSchedProgress){
					snprint(diag, sizeof diag,
						"unexpected scheduler state %s (%d) during round trip: round=%llud peer=%lud steps=%llud nrunnable=%lud gcoutstanding=%llud err=\"%s\"",
						statename(state), state, r, p, steps, s->sched[0]->runq->nrunnable, s->sched[0]->gcoutstanding, err);
					check(0, diag);
				}
				steps++;
				check(steps < NvRoundstepcap, "round trip exceeded step budget");
			}
			t1 = uptime();
			samples[idx++] = t1-t0;
		}
	}
	*nsamples = idx;
}

/*
 * Part 2: peer round-trip latency with a hog process running a == b on
 * two independently built depth-d shared chains. One equality walks
 * 2^(d+1)-1 nodes. Before D080 it cost one reduction, so one hog
 * dispatch (quantum 1000) ran ~50 equalities before a peer ran; since
 * D080 the visits are charged at NvWorkunit per reduction, so the
 * printed hog reduction count now includes that charge and one deep
 * equality should end a dispatch on its own.
 */
static void
hop(uvlong depth, ulong peers, uvlong rounds)
{
	NvHeap ha, hb, h;
	NvTerm a, b, noarg, elem[1], hogarg, hogpid, tick, stop, *peerpid;
	NvScheduler s;
	NvModule *m;
	NvLimits l;
	NvExec *hogexec;
	uvlong *base, *loaded, nbase, nloaded, i, min, maxsteps, steps;
	vlong t0, t1;
	ulong p;
	char err[256];
	int state;

	check(depth >= 1 && peers >= 1 && rounds >= 1, "depth, peers and rounds must be positive");
	print("hop depth=%llud peers=%lud rounds=%llud\n", depth, peers, rounds);

	/* Host cost of one equality at this depth, for comparison. */
	nvheapinit(&ha, 0);
	nvheapinit(&hb, 0);
	a = build(&ha, (int)depth, 1);
	b = build(&hb, (int)depth, 1);
	min = ~0ULL;
	for(i = 0; i < Nsamps; i++){
		t0 = nsec();
		check(nvtermequal(a, b) == 1, "host equality of independent chains");
		t1 = nsec();
		if((uvlong)(t1-t0) < min)
			min = t1-t0;
	}
	nvheapfree(&ha);
	nvheapfree(&hb);
	print("host nvtermequal at depth %llud: min %llud ns\n", depth, min);

	m = module();
	nvheapinit(&h, 0);
	noarg = nvtuple(&h, nil, 0);
	check(noarg != NvNil, "empty argument allocation");

	memset(&l, 0, sizeof l);
	l.maxprocess = peers+2;
	l.maxmailbox = 4096;
	l.maxmessage = 512;
	l.maxframe = 1024;
	l.maxtermdepth = NvMaxtermdepth;
	l.maxduration = NvMaxduration;
	l.maxatom = 65536;
	l.gcoffload = 0;

	check(nvschedinit(&s, m, &l, 1, 1000, err, sizeof err) == 0, err);

	peerpid = malloc((uvlong)peers*sizeof(NvTerm));
	check(peerpid != nil, "peer pid array");
	for(p = 0; p < peers; p++)
		check(nvschedspawn(&s, "peer", noarg, &peerpid[p], err, sizeof err) == 0, err);

	/* Drive to idle. */
	steps = 0;
	for(;;){
		state = nvschedstep(&s, err, sizeof err);
		check(state != NvSchedError, err);
		if(state == NvSchedIdle)
			break;
		steps++;
		check(steps < 10000000ULL, "peers did not reach idle after spawn");
	}

	tick = nvatom("tick");
	stop = nvatom("stop");
	check(tick != NvNil && stop != NvNil, "atoms");

	base = malloc((uvlong)peers*rounds*sizeof(uvlong));
	loaded = malloc((uvlong)peers*rounds*sizeof(uvlong));
	check(base != nil && loaded != nil, "sample arrays");

	print("phase baseline: no hog running\n");
	measurerounds(&s, peerpid, peers, rounds, tick, base, &nbase, err, sizeof err);
	report("baseline", base, nbase);

	elem[0] = nvint(nil, (vlong)depth);
	hogarg = nvtuple(&h, elem, 1);
	check(hogarg != NvNil, "hog argument allocation");
	check(nvschedspawn(&s, "hogstart", hogarg, &hogpid, err, sizeof err) == 0, err);

	print("phase loaded: hog running a == b on two independent depth-%llud chains\n", depth);
	measurerounds(&s, peerpid, peers, rounds, tick, loaded, &nloaded, err, sizeof err);
	report("loaded", loaded, nloaded);

	/* Hog reductions ~ equalities evaluated (a few instructions per loop). */
	hogexec = nil;
	if(nvprocalive(&s.runtime, hogpid))
		hogexec = nvprocat(&s.runtime, nvpidslot(hogpid))->exec;
	if(hogexec != nil)
		print("hog reductions during loaded phase: %llud\n", (uvlong)hogexec->reductions);

	/* Drain. */
	for(p = 0; p < peers; p++)
		if(nvprocalive(&s.runtime, peerpid[p]))
			check(nvprocsend(&s.runtime, peerpid[p], stop, err, sizeof err) >= 0, "stop send");
	if(nvprocalive(&s.runtime, hogpid))
		check(nvprocsend(&s.runtime, hogpid, stop, err, sizeof err) >= 0, "stop hog");

	maxsteps = 100000000ULL + 1000ULL*((uvlong)peers*rounds+1);
	steps = 0;
	for(;;){
		state = nvschedstep(&s, err, sizeof err);
		check(state != NvSchedError, err);
		if(state == NvSchedDone)
			break;
		steps++;
		check(steps < maxsteps, "drain exceeded step budget");
	}
	check(s.faulted == 0, "unexpected process fault during run");

	nvschedfree(&s);
	nvheapfree(&h);
	nvmodulefree(m);
	free(peerpid);
	free(base);
	free(loaded);
}

void
main(int argc, char **argv)
{
	if(argc == 1){
		terms();
		exits(nil);
	}
	if(argc == 5 && strcmp(argv[1], "hop") == 0){
		hop(number(argv[2], 24), (ulong)number(argv[3], 64), number(argv[4], 100000));
		exits(nil);
	}
	fprint(2, "usage: %s [hop depth peers rounds]\n", argv[0]);
	exits("usage");
}
