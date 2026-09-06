#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../../include/nvbc.h"
#include "../../include/nvvm.h"
#include "../../include/nvexec.h"
#include "../../include/nvpat.h"
#include "../../include/nvproc.h"

static void
fail(char *s)
{
	fprint(2, "FAIL: %s\n", s);
	exits("test");
}

static void
check(int ok, char *s)
{
	if(!ok)
		fail(s);
}

/* All test values are small, so nvint never needs a heap to box them. */
static NvTerm
integer(vlong n)
{
	return nvint(nil, n);
}

/*
 * Builds a chain of nested 1-element tuples of the given depth (depth 1 is
 * the bare integer, with no tuple wrapper) in heap h, by repeated nvtuple.
 * D061: construction never traverses or bounds depth, so this never fails,
 * however deep -- that is exactly the property termdepth() below tests.
 */
static NvTerm
chain(NvHeap *h, int depth)
{
	NvTerm v;
	int i;

	v = integer(1);
	for(i = 1; i < depth; i++)
		v = nvtuple(h, &v, 1);
	return v;
}

static void
mailboxlimits(void)
{
	NvRuntime r;
	NvLimits limits;
	NvHeap h;
	NvTerm pid, v, elem[2];
	NvFrag *got;
	char err[128];
	uvlong words;

	/* D064/D066: an integer message fragment is 1 word: nword 0, plus the root word. */
	words = 1;
	limits.maxprocess = 1;
	limits.maxmessage = words;
	limits.maxmailbox = words;
	limits.maxheap = 0;
	limits.gcstress = 0;
	limits.maxframe = 64;
	limits.maxtermdepth = NvMaxtermdepth;
	limits.maxduration = NvMaxduration;
	limits.maxatom = 65536;
	limits.gcoffload = 0;
	check(nvruntimeinit(&r, &limits, 1, err, sizeof err) == 0, err);
	check(nvprocspawn(&r, &pid, err, sizeof err) == 0, "limit-test spawn");
	v = integer(1);
	check(nvprocsend(&r, pid, v, err, sizeof err) == 1, "exact mailbox boundary");
	check(nvprocsend(&r, pid, v, err, sizeof err) < 0 && strcmp(err, "mailbox_full") == 0, "mailbox aggregate boundary");
	check(nvprocpop(&r, pid, &got, err, sizeof err) == 1, "limit-test pop");
	nvfragfree(got);
	check(nvprocsend(&r, pid, v, err, sizeof err) == 1, "mailbox budget restored");
	nvruntimefree(&r);

	/*
	 * D066: limits are word counts now, not the old byte estimate, so the
	 * previous "maxmailbox = bytes-1" underflow probe has no equivalent --
	 * a word limit of 0 is simply rejected by nvruntimeinit, and there is
	 * no smaller-than-one nonzero word count to underflow into by
	 * accident. Instead this exercises a message that fits the (larger)
	 * per-message limit but not the (smaller) mailbox limit: a 2-tuple of
	 * two ints is 1 header word + 2 element words = 3 body words, so
	 * nvfragwords() is 4, which exceeds maxmailbox (2) even though it is
	 * within maxmessage (4).
	 */
	nvheapinit(&h, 0);
	limits.maxmessage = 4;
	limits.maxmailbox = 2;
	check(nvruntimeinit(&r, &limits, 2, err, sizeof err) == 0, err);
	check(nvprocspawn(&r, &pid, err, sizeof err) == 0, "underflow-test spawn");
	elem[0] = integer(1);
	elem[1] = integer(2);
	v = nvtuple(&h, elem, 2);
	check(nvprocsend(&r, pid, v, err, sizeof err) < 0 && strcmp(err, "mailbox_full") == 0, "message larger than mailbox rejected");
	/* D064: NvNil ("no term") is malformed input; nvprocsend rejects it as mailbox_full without ever calling nvfragcopy. */
	check(nvprocsend(&r, pid, NvNil, err, sizeof err) < 0 && strcmp(err, "mailbox_full") == 0, "malformed value rejected");
	nvruntimefree(&r);
	nvheapfree(&h);
	print("ok - mailbox accounting boundaries and malformed values\n");
}

/*
 * D061: construction no longer bounds depth, so the old "term construction
 * depth is controlled" test (which expected the (K+1)th nvtuple to fail)
 * has nothing left to test -- every nvtuple below succeeds. What D061
 * moved the ceiling to is the traversals: nvfragcopy (the process-boundary
 * copy that used to run inside construction) and nvtermequal both refuse
 * to recurse past NvMaxtermdepth, reporting NvTermlimit, while a value at
 * exactly the ceiling still copies and compares normally.
 */
static void
termdepth(void)
{
	NvHeap h;
	NvTerm v, a, b, c, d;
	NvFrag *f;

	nvheapinit(&h, 0);

	v = chain(&h, NvMaxtermdepth+1);
	check(nvfragcopy(v, ~0ULL, &f) == NvTermlimit, "copy past the depth ceiling is rejected");

	check(nvtermequal(v, v) == 1, "identical word is a fast equality path, no traversal needed");

	a = chain(&h, NvMaxtermdepth+1);
	b = chain(&h, NvMaxtermdepth+1);
	check(nvtermequal(a, b) == NvTermlimit, "comparing two too-deep values also hits the ceiling");

	c = chain(&h, NvMaxtermdepth);
	d = chain(&h, NvMaxtermdepth);
	check(nvfragcopy(c, ~0ULL, &f) == 0, "a chain of exactly the maximum depth copies fine");
	nvfragfree(f);
	check(nvtermequal(c, d) == 1, "and compares equal to an independently built twin");

	nvheapfree(&h);
	print("ok - term depth is enforced at copy and comparison, not construction\n");
}

static void
timeouts(void)
{
	NvRuntime r;
	NvLimits limits;
	NvTerm pid, dur, inf, bad, v;
	NvFrag *got;
	NvProcess *p;
	char err[128];

	limits.maxprocess = 1;
	limits.maxmailbox = 4096;
	limits.maxmessage = 1024;
	limits.maxheap = 0;
	limits.maxframe = 64;
	limits.maxtermdepth = NvMaxtermdepth;
	limits.maxduration = NvMaxduration;
	limits.maxatom = 65536;
	limits.gcstress = 0;
	limits.gcoffload = 0;
	check(nvruntimeinit(&r, &limits, 1, err, sizeof err) == 0, err);
	check(nvprocspawn(&r, &pid, err, sizeof err) == 0, "spawn timeout tester");
	p = &r.process[nvpidslot(pid)];
	check(nvprocdispatch(&r, pid, err, sizeof err) == 0, "dispatch timeout tester");

	dur = integer(100);
	check(nvprocarmdeadline(&r, pid, dur, 1000, err, sizeof err) == 0, "arm deadline");
	check(p->hasdeadline && p->deadline == 1100, "deadline computed from now plus duration");
	check(nvprocrecvbegin(&r, pid, &v, err, sizeof err) == 0, "empty scan begin");
	check(nvprocrecvwaitdeadline(&r, pid, 1050, err, sizeof err) == 0 && p->state == Prwaiting, "not yet expired blocks");

	/* A scheduler would make this runnable again on wakeup; simulate that directly. */
	p->state = Prrunnable;
	check(nvprocdispatch(&r, pid, err, sizeof err) == 0, "redispatch after not-yet-expired wait");
	check(nvprocrecvbegin(&r, pid, &v, err, sizeof err) == 0, "second empty scan begin");
	check(nvprocrecvwaitdeadline(&r, pid, 1100, err, sizeof err) == 1 && p->state == Prrunning && !p->hasdeadline,
		"expired deadline reports fallthrough and clears itself so it cannot fire twice");

	/* A message queued before the exhausted scan resumes always wins, even past expiry. */
	check(nvprocarmdeadline(&r, pid, dur, 2000, err, sizeof err) == 0, "re-arm deadline");
	check(nvprocrecvbegin(&r, pid, &v, err, sizeof err) == 0, "third empty scan begin");
	check(nvprocsend(&r, pid, nvatom("queued"), err, sizeof err) == 1, "message races exhausted scan");
	check(nvprocrecvwaitdeadline(&r, pid, 2500, err, sizeof err) == 0 && p->state == Prrunning && p->hasdeadline,
		"message beats an already-expired deadline and keeps it armed for the retry");
	check(nvprocpop(&r, pid, &got, err, sizeof err) == 1 && nvtermkind(got->root) == Vatom && strcmp(nvtermatom(got->root), "queued") == 0,
		"raced message retained for the retry scan");
	nvfragfree(got);

	/* `'infinity` arms nothing, and a plain recvwait clears any stale deadline. */
	inf = nvatom("infinity");
	check(nvprocarmdeadline(&r, pid, inf, 3000, err, sizeof err) == 0 && !p->hasdeadline, "infinity arms nothing");
	check(nvprocarmdeadline(&r, pid, dur, 3000, err, sizeof err) == 0 && p->hasdeadline, "re-arm before plain recvwait");
	check(nvprocrecvbegin(&r, pid, &v, err, sizeof err) == 0, "fourth empty scan begin");
	check(nvprocrecvwait(&r, pid, err, sizeof err) == 0 && !p->hasdeadline && p->state == Prwaiting,
		"plain recvwait clears a stale deadline left over from an earlier after-clause");

	/* Bad duration domains fault distinctly and never arm anything. */
	bad = nvatom("soon");
	check(nvprocarmdeadline(&r, pid, bad, 4000, err, sizeof err) < 0 && strcmp(err, "bad_timeout") == 0,
		"non-infinity atom duration rejected");
	bad = integer(-1);
	check(nvprocarmdeadline(&r, pid, bad, 4000, err, sizeof err) < 0 && strcmp(err, "bad_timeout") == 0,
		"negative duration rejected");
	bad = integer(NvMaxduration+1);
	check(nvprocarmdeadline(&r, pid, bad, 4000, err, sizeof err) < 0 && strcmp(err, "bad_timeout") == 0,
		"duration above the accepted bound rejected");
	check(!p->hasdeadline, "rejected durations never arm a deadline");

	nvruntimefree(&r);
	print("ok - receive deadlines arm, expire, lose to queued messages, and reject bad durations\n");
}

void
main(void)
{
	NvRuntime runtime;
	NvLimits limits;
	NvHeap h;
	NvTerm p1, p2, stale, ref1, ref2, v, elem[2];
	NvFrag *got;
	NvPattern pattern[2], pe[2];
	NvPatClause clause[2];
	NvBindings bindings;
	char err[128];
	int which, rc;

	mailboxlimits();
	termdepth();
	timeouts();

	nvheapinit(&h, 0);

	limits.maxprocess = 2;
	limits.maxmailbox = 4096;
	limits.maxmessage = 1024;
	limits.maxheap = 0;
	limits.maxframe = 64;
	limits.maxtermdepth = NvMaxtermdepth;
	limits.maxduration = NvMaxduration;
	limits.maxatom = 65536;
	limits.gcstress = 0;
	limits.gcoffload = 0;
	check(nvruntimeinit(&runtime, &limits, 0x1234, err, sizeof err) == 0, err);
	check(nvprocspawn(&runtime, &p1, err, sizeof err) == 0, "spawn p1");
	check(nvprocspawn(&runtime, &p2, err, sizeof err) == 0, "spawn p2");
	check(nvtermkind(p1) == Vpid && nvtermkind(p2) == Vpid && !nvtermequal(p1, p2), "opaque pid identity");
	check(nvprocspawn(&runtime, &v, err, sizeof err) < 0 && strcmp(err, "system_limit") == 0, "process limit");
	print("ok - process limit and PID identity\n");

	check(nvprocref(&runtime, &h, &ref1, err, sizeof err) == 0, "ref1");
	check(nvprocref(&runtime, &h, &ref2, err, sizeof err) == 0, "ref2");
	check(nvtermkind(ref1) == Vref && !nvtermequal(ref1, ref2), "unique refs");
	print("ok - opaque unique refs\n");

	v = integer(1);
	check(nvprocsend(&runtime, p2, v, err, sizeof err) == 1, "send 1");
	v = integer(2);
	check(nvprocsend(&runtime, p2, v, err, sizeof err) == 1, "send 2");
	check(nvprocpop(&runtime, p2, &got, err, sizeof err) == 1 && nvtermint(got->root) == 1, "fifo first");
	nvfragfree(got);
	check(nvprocpop(&runtime, p2, &got, err, sizeof err) == 1 && nvtermint(got->root) == 2, "fifo second");
	nvfragfree(got);
	print("ok - FIFO and per-sender order\n");

	/*
	 * D062: atoms are interned words, not owned storage, so building the
	 * message tuple's atom element is just nvatom -- there is no separate
	 * free-and-dangle hazard the way a private strdup used to have.
	 */
	elem[0] = nvatom("keep");
	elem[1] = integer(1);
	v = nvtuple(&h, elem, 2);
	check(nvprocsend(&runtime, p2, v, err, sizeof err) == 1, "send keep");
	elem[0] = nvatom("take");
	elem[1] = integer(9);
	v = nvtuple(&h, elem, 2);
	check(nvprocsend(&runtime, p2, v, err, sizeof err) == 1, "send take");
	memset(pattern, 0, sizeof pattern);
	memset(pe, 0, sizeof pe);
	pe[0].kind = Patom;
	pe[0].name = "take";
	pe[1].kind = Pvar;
	pe[1].name = "x";
	pattern[0].kind = Ptuple;
	pattern[0].n = 2;
	pattern[0].elem = pe;
	pattern[1].kind = Pwild;
	clause[0].pattern = &pattern[0];
	clause[1].pattern = &pattern[1];
	memset(&bindings, 0, sizeof bindings);
	check(nvprocreceive(&runtime, p2, clause, 1, &bindings, &which, &got, err, sizeof err) < 0 && strcmp(err, "bad_state") == 0, "receive requires running process");
	check(nvprocdispatch(&runtime, p2, err, sizeof err) == 0, "dispatch selective receiver");
	check(nvprocdispatch(&runtime, p2, err, sizeof err) < 0 && strcmp(err, "bad_state") == 0, "duplicate dispatch rejected");
	rc = nvprocreceive(&runtime, p2, clause, 1, &bindings, &which, &got, err, sizeof err);
	check(rc == 1 && which == 0 && nvbinding(&bindings, "x") != nil && nvtermint(*nvbinding(&bindings, "x")) == 9, "selective receive");
	check(nvprocyield(&runtime, p2, err, sizeof err) == 0, "yield selective receiver");
	nvbindingsfree(&bindings);
	nvfragfree(got);
	check(nvprocpop(&runtime, p2, &got, err, sizeof err) == 1, "unmatched retained");
	check(nvtermkind(got->root) == Vtuple && strcmp(nvtermatom(nvtupleelem(got->root, 0)), "keep") == 0, "unmatched order");
	nvfragfree(got);
	print("ok - oldest matching receive preserves unmatched messages\n");

	v = integer(7);
	check(nvprocsend(&runtime, p2, v, err, sizeof err) == 1, "clause send");
	memset(&bindings, 0, sizeof bindings);
	check(nvprocdispatch(&runtime, p2, err, sizeof err) == 0, "dispatch wildcard receiver");
	rc = nvprocreceive(&runtime, p2, clause+1, 1, &bindings, &which, &got, err, sizeof err);
	check(rc == 1 && which == 0 && nvtermint(got->root) == 7, "clause wildcard");
	check(nvprocyield(&runtime, p2, err, sizeof err) == 0, "yield wildcard receiver");
	nvfragfree(got);
	nvbindingsfree(&bindings);
	print("ok - clause order within candidate\n");

	pattern[1].kind = Patom;
	pattern[1].name = "wake";
	clause[1].pattern = &pattern[1];
	memset(&bindings, 0, sizeof bindings);
	check(nvprocdispatch(&runtime, p2, err, sizeof err) == 0, "dispatch waiting receiver");
	check(nvprocreceive(&runtime, p2, clause+1, 1, &bindings, &which, &got, err, sizeof err) == 0 && runtime.process[nvpidslot(p2)].state == Prwaiting, "receive transitions running to waiting");
	check(nvprocyield(&runtime, p2, err, sizeof err) < 0 && strcmp(err, "bad_state") == 0, "waiting process cannot yield");
	v = nvatom("wake");
	check(nvprocsend(&runtime, p2, v, err, sizeof err) == 1 && runtime.process[nvpidslot(p2)].state == Prrunnable, "send wakes waiting process once");
	check(nvprocdispatch(&runtime, p2, err, sizeof err) == 0, "redispatch woken receiver");
	check(nvprocreceive(&runtime, p2, clause+1, 1, &bindings, &which, &got, err, sizeof err) == 1, "woken receive consumes message");
	check(nvprocyield(&runtime, p2, err, sizeof err) == 0, "yield woken receiver");
	nvfragfree(got);
	nvbindingsfree(&bindings);
	print("ok - explicit process lifecycle transitions\n");

	/* D061: a PID is an immediate word, so "copying" one is a bare assignment. */
	stale = p1;
	check(nvprocexit(&runtime, p1) == 1, "exit p1");
	check(nvprocsend(&runtime, stale, v, err, sizeof err) == 0, "dead pid drop");
	check(nvprocspawn(&runtime, &p1, err, sizeof err) == 0, "reuse slot");
	check(!nvtermequal(stale, p1) && !nvprocalive(&runtime, stale), "stale generation");

	runtime.process[nvpidslot(p1)].generation = NvMaxgeneration;
	p1 = nvpid(nvpidslot(p1), NvMaxgeneration);
	stale = p1;
	check(nvpidslot(stale) == nvpidslot(p1) && nvpidgeneration(stale) == NvMaxgeneration, "exhausted pid copy");
	check(nvprocexit(&runtime, p1) == 1, "exit exhausted generation");
	check(nvprocspawn(&runtime, &p1, err, sizeof err) == 0, "spawn after generation exhaustion");
	check(nvpidslot(p1) != nvpidslot(stale) && runtime.process[nvpidslot(stale)].state == Prretired && !nvprocalive(&runtime, stale),
		"exhausted generation retires slot");
	print("ok - dead sends, stale PIDs, and exhausted generations do not alias\n");

	nvheapfree(&h);
	nvruntimefree(&runtime);
	print("all process core tests passed\n");
	exits(nil);
}
