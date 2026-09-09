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
 * M08-T04r large-live-set latency baseline.
 *
 * One "owner" process holds a big live tuple (retainwords) across tail
 * calls while repeatedly allocating and discarding garbage (churn words
 * per outer iteration, iterations outer iterations total) -- a Cheney
 * collection of its heap therefore has to copy the whole big tuple every
 * time it collects, exactly the "one large collection pauses everyone
 * else" scenario D068's off-process mechanism exists for. retainwords,
 * churn and iterations are independent dials: retainwords sets what a
 * collection has to copy, churn sets how often the owner's heap fills up
 * and triggers one, iterations bounds the run's total length.
 *
 * `peers` trivial echo processes (tick -> self-tock -> block) sit
 * otherwise idle. The host measures each round trip's wall-clock latency
 * by driving nvschedstep in a loop until the global sent-message counter
 * advances by 2 (the host's tick, then the peer's self-reply) -- this is
 * the same nsent-delta technique bench/perftest.c already uses to verify
 * message counts, repurposed here as a per-round clock.
 *
 * Peers are NOT allocation-free: recvtake adopts each 1-word tick/tock
 * fragment onto the peer's own heap (D064), so a peer's heap does slowly
 * grow and will itself eventually collect -- tiny and cheap, but a real
 * collection all the same. This matters for gcoffload: a threshold low
 * enough to also catch an ordinary peer's own tiny heap (this is what
 * gcoffload=1 does -- "always off-process" means literally every real
 * collection, peers included) confounds the owner-only comparison this
 * fixture is meant to isolate, and can trigger the idle sweep to launch
 * several peer collectors in one pass. bench/largelive.rc picks a
 * threshold comfortably above any peer's own heap size and below
 * retainwords so only the OWNER's collections move off-process.
 *
 * (An earlier version of this file and its accompanying docs claimed
 * "peers never allocate" -- wrong, corrected here. That wrong premise
 * also masked a real scheduler bug this fixture found: see the D074
 * amendment in docs/decisions.md for finddispatchable/gcoutstanding
 * interacting with a low gcoffload threshold to produce a false
 * NvSchedIdle with processes still runnable.)
 *
 * The run has two phases against the SAME spawned peers: "baseline"
 * (before the owner is spawned at all) and "loaded" (once it is
 * running). This is the measurement-overhead control STATUS asks for:
 * fixed round-trip/dispatch cost cancels between the two phases, so the
 * baseline-to-loaded delta isolates the owner's effect rather than
 * requiring two separate invocations compared by hand.
 *
 * gcoffload is exposed directly so this fixture is reusable across the
 * comparison D068/questions.md calls for: 0 (never off-process, today's
 * default and what every existing benchmark measures), a threshold that
 * catches only the owner (the real T04d comparison), and 1 (forces
 * everything off-process, useful only as an extreme/diagnostic case, not
 * a clean owner-only measurement -- see the warning this prints).
 *
 * This is a measurement tool, not a policy change: it selects no
 * default and alters no runtime behavior. No result is fabricated here;
 * see bench/README.md for recorded runs once the user has executed it.
 */

enum {
	NvRoundstepcap = 1000000,	/* safety valve, not a tuned bound */
};

static void
check(int ok, char *s)
{
	if(!ok)
		sysfatal("largelive: %s", s);
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
		"fn waste(0) { 'ok }\n"
		"fn waste(n) { ${1, 2, 3, 4, 5, 6, 7, 8}; waste(n - 1) }\n"
		"fn owner(big, churn, 0) { 'ok }\n"
		"fn owner(big, churn, remaining) {\n"
		"  waste(churn);\n"
		"  owner(big, churn, remaining - 1)\n"
		"}\n";
	Parser p;
	Program *pr;
	NvModule *m;
	char err[256];

	pr = parseprogram(&p, "largelive", src, strlen(src));
	check(pr != nil, p.err);
	m = nvcompile(pr, err, sizeof err);
	programfree(pr);
	check(m != nil, err);
	return m;
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

/* Drives the scheduler one host-issued tick at a time per peer per round,
 * timing each round trip by watching the global sent-message counter
 * advance by exactly 2: the host's tick, then the peer's self-reply.
 * Owner progress (if any) interleaves via the same nvschedstep calls,
 * exactly as it would under ordinary dispatch; this loop never favors
 * the peer being measured over whatever else is runnable. On an
 * unexpected scheduler state, reports round/peer/step/nrunnable/err
 * directly rather than a bare assertion, since that is exactly the
 * information that found the D074 false-idle bug this fixture caught. */
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
						statename(state), state, r, p, steps, s->runtime.nrunnable, s->gcoutstanding, err);
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

void
main(int argc, char **argv)
{
	NvModule *m;
	NvScheduler s;
	NvLimits l;
	NvHeap h;
	NvMemstats mem;
	NvExec *ownerexec;
	NvTerm *elembig, big, elemarg[3], ownerarg, noarg, tick, stop, ownerpid;
	NvTerm *peerpid;
	uvlong retainwords, churn, iterations, rounds, gcoffload;
	uvlong nbase, nloaded, i;
	uvlong *base, *loaded;
	uvlong collectionsbefore, collectionsafter, gcinbefore, gcinafter, gcoutbefore, gcoutafter, fallbackbefore, fallbackafter;
	ulong peers, p;
	uvlong maxsteps, steps;
	char err[256];
	int state;

	if(argc != 7){
		fprint(2, "usage: %s retainwords churn iterations peers rounds gcoffload\n", argv[0]);
		exits("usage");
	}
	retainwords = number(argv[1], 2000000);
	churn = number(argv[2], 1000000);
	iterations = number(argv[3], 100000000);
	peers = (ulong)number(argv[4], 200);
	rounds = number(argv[5], 20000);
	gcoffload = number(argv[6], 100000000);
	check(peers > 0, "peers must be positive");
	check(rounds > 0, "rounds must be positive");

	print("retainwords=%llud churn=%llud iterations=%llud peers=%lud rounds=%llud gcoffload=%llud",
		retainwords, churn, iterations, peers, rounds, gcoffload);
	if(gcoffload == 0)
		print(" (never off-process)\n");
	else if(gcoffload == 1)
		print(" (always off-process, including every ordinary peer's own tiny mailbox-fragment collections -- NOT a clean owner-only comparison; use a threshold above peer heap sizes instead, see bench/README.md)\n");
	else{
		print(" (off-process at/above %llud words)", gcoffload);
		if(gcoffload < 512)
			print(" -- warning: low threshold, a peer's own tiny heap could plausibly cross it too and confound the owner-only comparison");
		print("\n");
	}

	m = module();

	nvheapinit(&h, 0);
	elembig = nil;
	if(retainwords > 0){
		elembig = malloc(retainwords*sizeof(NvTerm));
		check(elembig != nil, "big element scratch allocation");
		for(i = 0; i < retainwords; i++)
			elembig[i] = nvint(nil, (vlong)i);
	}
	big = nvtuple(&h, elembig, (int)retainwords);
	check(big != NvNil, "big tuple allocation");
	free(elembig);
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
	l.gcoffload = gcoffload;

	check(nvschedinit(&s, m, &l, 1, 1000, err, sizeof err) == 0, err);

	peerpid = malloc((uvlong)peers*sizeof(NvTerm));
	check(peerpid != nil, "peer pid array");
	for(p = 0; p < peers; p++)
		check(nvschedspawn(&s, "peer", noarg, &peerpid[p], err, sizeof err) == 0, err);

	/*
	 * Hygiene, not the fix for the gcoffload crash this fixture found
	 * (see docs/decisions.md's D074 amendment for the actual scheduler
	 * bug): drive every peer to a genuine Prwaiting block before timing
	 * anything, rather than relying on round 0's send order happening to
	 * match the run queue's spawn order.
	 */
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

	print("phase baseline: no owner running\n");
	measurerounds(&s, peerpid, peers, rounds, tick, base, &nbase, err, sizeof err);
	report("baseline", base, nbase);

	elemarg[0] = big;
	elemarg[1] = nvint(nil, (vlong)churn);
	elemarg[2] = nvint(nil, (vlong)iterations);
	ownerarg = nvtuple(&h, elemarg, 3);
	check(ownerarg != NvNil, "owner argument allocation");
	collectionsbefore = s.collections;
	gcinbefore = s.gcinputwords;
	gcoutbefore = s.gcoutputwords;
	fallbackbefore = s.gcofffallback;
	check(nvschedspawnroot(&s, "owner", ownerarg, &ownerpid, err, sizeof err) == 0, err);

	print("phase loaded: owner churning\n");
	measurerounds(&s, peerpid, peers, rounds, tick, loaded, &nloaded, err, sizeof err);
	report("loaded", loaded, nloaded);

	/*
	 * s.collections is scheduler-wide: with a gcoffload threshold low
	 * enough to also catch peers, or even at gcoffload=0 (every peer's
	 * own tiny heap still collects inline eventually), this aggregate
	 * is NOT "owner collections" -- it is owner+every peer combined.
	 * Read the owner's own NvExec fields directly for an owner-only
	 * count instead.
	 */
	ownerexec = nil;
	if(nvprocalive(&s.runtime, ownerpid))
		ownerexec = s.runtime.process[nvpidslot(ownerpid)].exec;
	if(ownerexec != nil)
		print("owner's own collections (cumulative since spawn): %llud; last live words %llud\n",
			ownerexec->collections, ownerexec->livewords);
	else
		print("owner already exited before this report; its own collection count is unavailable (nvprocexit frees the exec)\n");

	collectionsafter = s.collections;
	gcinafter = s.gcinputwords;
	gcoutafter = s.gcoutputwords;
	fallbackafter = s.gcofffallback;
	print("aggregate scheduler collections during loaded phase (owner + every peer): %llud; input words %llud; output (live) words %llud; rfork-launch fallbacks %llud\n",
		collectionsafter-collectionsbefore, gcinafter-gcinbefore, gcoutafter-gcoutbefore,
		fallbackafter-fallbackbefore);
	print("owner still running: %s (rootstate %d)\n", s.rootstate == NvRootRunning ? "yes" : "no", s.rootstate);

	nvschedmemory(&s, &mem);
	print("memory snapshot before drain: total requested %llud bytes; heap capacity+headers %llud; heap used %llud; stacks %llud; collecting-and-skipped %llud\n",
		mem.totalbytes, mem.heapbytes, mem.heapused, mem.stackbytes, mem.ncollecting);

	for(p = 0; p < peers; p++)
		if(nvprocalive(&s.runtime, peerpid[p]))
			check(nvprocsend(&s.runtime, peerpid[p], stop, err, sizeof err) >= 0, "stop send");

	maxsteps = 100000000ULL + 1000ULL*(iterations+(uvlong)peers*rounds+1);
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
	nvschedmemory(&s, &mem);
	check(mem.totalbytes == 0, "runtime storage after free");
	print("after runtime free: requested runtime bytes %llud\n", mem.totalbytes);

	nvheapfree(&h);
	nvmodulefree(m);
	free(peerpid);
	free(base);
	free(loaded);
	exits(nil);
}
