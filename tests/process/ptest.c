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

static NvValue
integer(vlong n)
{
	NvValue v;
	memset(&v, 0, sizeof v);
	v.valid = 1;
	v.kind = Vint;
	v.i = n;
	return v;
}

static void
mailboxlimits(void)
{
	NvRuntime r;
	NvLimits limits;
	NvValue pid, v, got, bad;
	char err[128];
	uvlong bytes;

	bytes = sizeof(NvValue);
	limits.maxprocess = 1;
	limits.maxmessage = bytes;
	limits.maxmailbox = bytes;
	limits.maxframe = 64;
	limits.maxtermdepth = NvMaxtermdepth;
	check(nvruntimeinit(&r, &limits, 1, err, sizeof err) == 0, err);
	check(nvprocspawn(&r, &pid, err, sizeof err) == 0, "limit-test spawn");
	v = integer(1);
	check(nvprocsend(&r, &pid, &v, err, sizeof err) == 1, "exact mailbox boundary");
	check(nvprocsend(&r, &pid, &v, err, sizeof err) < 0 && strcmp(err, "mailbox_full") == 0, "mailbox aggregate boundary");
	check(nvprocpop(&r, &pid, &got, err, sizeof err) == 1, "limit-test pop");
	nvvaluefree(&got);
	check(nvprocsend(&r, &pid, &v, err, sizeof err) == 1, "mailbox budget restored");
	nvvaluefree(&pid);
	nvruntimefree(&r);

	limits.maxmessage = bytes*2;
	limits.maxmailbox = bytes-1;
	check(nvruntimeinit(&r, &limits, 2, err, sizeof err) == 0, err);
	check(nvprocspawn(&r, &pid, err, sizeof err) == 0, "underflow-test spawn");
	check(nvprocsend(&r, &pid, &v, err, sizeof err) < 0 && strcmp(err, "mailbox_full") == 0, "message larger than mailbox rejected");
	memset(&bad, 0, sizeof bad);
	bad.valid = 1;
	bad.kind = Vatom;
	check(nvprocsend(&r, &pid, &bad, err, sizeof err) < 0 && strcmp(err, "mailbox_full") == 0, "malformed value rejected");
	nvvaluefree(&pid);
	nvruntimefree(&r);
	print("ok - mailbox accounting boundaries and malformed values\n");
}

static void
termdepth(void)
{
	NvValue v, next;
	int i;

	v = integer(1);
	for(i = 0; i < NvMaxtermdepth-1; i++){
		check(nvvaluetuple(&next, &v, 1) == 0, "term depth below limit");
		nvvaluefree(&v);
		v = next;
	}
	check(nvvaluetuple(&next, &v, 1) == NvValuelimit, "term depth limit has distinct result");
	nvvaluefree(&v);
	print("ok - term construction depth is controlled\n");
}

static void
timeouts(void)
{
	NvRuntime r;
	NvLimits limits;
	NvValue pid, dur, inf, bad, v, got;
	NvProcess *p;
	char err[128];

	limits.maxprocess = 1;
	limits.maxmailbox = 4096;
	limits.maxmessage = 1024;
	limits.maxframe = 64;
	limits.maxtermdepth = NvMaxtermdepth;
	check(nvruntimeinit(&r, &limits, 1, err, sizeof err) == 0, err);
	check(nvprocspawn(&r, &pid, err, sizeof err) == 0, "spawn timeout tester");
	p = &r.process[pid.pid.slot];
	check(nvprocdispatch(&r, &pid, err, sizeof err) == 0, "dispatch timeout tester");

	dur = integer(100);
	check(nvprocarmdeadline(&r, &pid, &dur, 1000, err, sizeof err) == 0, "arm deadline");
	check(p->hasdeadline && p->deadline == 1100, "deadline computed from now plus duration");
	check(nvprocrecvbegin(&r, &pid, &v, err, sizeof err) == 0, "empty scan begin");
	check(nvprocrecvwaitdeadline(&r, &pid, 1050, err, sizeof err) == 0 && p->state == Prwaiting, "not yet expired blocks");

	/* A scheduler would make this runnable again on wakeup; simulate that directly. */
	p->state = Prrunnable;
	check(nvprocdispatch(&r, &pid, err, sizeof err) == 0, "redispatch after not-yet-expired wait");
	check(nvprocrecvbegin(&r, &pid, &v, err, sizeof err) == 0, "second empty scan begin");
	check(nvprocrecvwaitdeadline(&r, &pid, 1100, err, sizeof err) == 1 && p->state == Prrunning && !p->hasdeadline,
		"expired deadline reports fallthrough and clears itself so it cannot fire twice");

	/* A message queued before the exhausted scan resumes always wins, even past expiry. */
	check(nvprocarmdeadline(&r, &pid, &dur, 2000, err, sizeof err) == 0, "re-arm deadline");
	check(nvprocrecvbegin(&r, &pid, &v, err, sizeof err) == 0, "third empty scan begin");
	check(nvvalueatom(&got, "queued") == 0, "queued message atom");
	check(nvprocsend(&r, &pid, &got, err, sizeof err) == 1, "message races exhausted scan");
	nvvaluefree(&got);
	check(nvprocrecvwaitdeadline(&r, &pid, 2500, err, sizeof err) == 0 && p->state == Prrunning && p->hasdeadline,
		"message beats an already-expired deadline and keeps it armed for the retry");
	check(nvprocpop(&r, &pid, &got, err, sizeof err) == 1 && got.kind == Vatom && strcmp(got.atom, "queued") == 0,
		"raced message retained for the retry scan");
	nvvaluefree(&got);

	/* `'infinity` arms nothing, and a plain recvwait clears any stale deadline. */
	check(nvvalueatom(&inf, "infinity") == 0, "infinity atom");
	check(nvprocarmdeadline(&r, &pid, &inf, 3000, err, sizeof err) == 0 && !p->hasdeadline, "infinity arms nothing");
	nvvaluefree(&inf);
	check(nvprocarmdeadline(&r, &pid, &dur, 3000, err, sizeof err) == 0 && p->hasdeadline, "re-arm before plain recvwait");
	check(nvprocrecvbegin(&r, &pid, &v, err, sizeof err) == 0, "fourth empty scan begin");
	check(nvprocrecvwait(&r, &pid, err, sizeof err) == 0 && !p->hasdeadline && p->state == Prwaiting,
		"plain recvwait clears a stale deadline left over from an earlier after-clause");

	/* Bad duration domains fault distinctly and never arm anything. */
	check(nvvalueatom(&bad, "soon") == 0, "bad atom duration");
	check(nvprocarmdeadline(&r, &pid, &bad, 4000, err, sizeof err) < 0 && strcmp(err, "bad_timeout") == 0,
		"non-infinity atom duration rejected");
	nvvaluefree(&bad);
	bad = integer(-1);
	check(nvprocarmdeadline(&r, &pid, &bad, 4000, err, sizeof err) < 0 && strcmp(err, "bad_timeout") == 0,
		"negative duration rejected");
	bad = integer(NvMaxduration+1);
	check(nvprocarmdeadline(&r, &pid, &bad, 4000, err, sizeof err) < 0 && strcmp(err, "bad_timeout") == 0,
		"duration above the accepted bound rejected");
	check(!p->hasdeadline, "rejected durations never arm a deadline");

	nvvaluefree(&pid);
	nvruntimefree(&r);
	print("ok - receive deadlines arm, expire, lose to queued messages, and reject bad durations\n");
}

void
main(void)
{
	NvRuntime runtime;
	NvLimits limits;
	NvValue p1, p2, stale, ref1, ref2, v, got, elem[2];
	NvPattern pattern[2], pe[2];
	NvPatClause clause[2];
	NvBindings bindings;
	char err[128];
	int which, rc;

	mailboxlimits();
	termdepth();
	timeouts();

	limits.maxprocess = 2;
	limits.maxmailbox = 4096;
	limits.maxmessage = 1024;
	limits.maxframe = 64;
	limits.maxtermdepth = NvMaxtermdepth;
	check(nvruntimeinit(&runtime, &limits, 0x1234, err, sizeof err) == 0, err);
	check(nvprocspawn(&runtime, &p1, err, sizeof err) == 0, "spawn p1");
	check(nvprocspawn(&runtime, &p2, err, sizeof err) == 0, "spawn p2");
	check(p1.kind == Vpid && p2.kind == Vpid && !nvvalueequal(&p1, &p2), "opaque pid identity");
	check(nvprocspawn(&runtime, &v, err, sizeof err) < 0 && strcmp(err, "system_limit") == 0, "process limit");
	print("ok - process limit and PID identity\n");

	check(nvprocref(&runtime, &ref1, err, sizeof err) == 0, "ref1");
	check(nvprocref(&runtime, &ref2, err, sizeof err) == 0, "ref2");
	check(ref1.kind == Vref && !nvvalueequal(&ref1, &ref2), "unique refs");
	print("ok - opaque unique refs\n");

	v = integer(1);
	check(nvprocsend(&runtime, &p2, &v, err, sizeof err) == 1, "send 1");
	v = integer(2);
	check(nvprocsend(&runtime, &p2, &v, err, sizeof err) == 1, "send 2");
	check(nvprocpop(&runtime, &p2, &got, err, sizeof err) == 1 && got.i == 1, "fifo first");
	nvvaluefree(&got);
	check(nvprocpop(&runtime, &p2, &got, err, sizeof err) == 1 && got.i == 2, "fifo second");
	nvvaluefree(&got);
	print("ok - FIFO and per-sender order\n");

	elem[0].valid = 1;
	elem[0].kind = Vatom;
	elem[0].atom = strdup("keep");
	elem[1] = integer(1);
	check(elem[0].atom != nil && nvvaluetuple(&v, elem, 2) == 0, "message tuple");
	free(elem[0].atom);
	check(nvprocsend(&runtime, &p2, &v, err, sizeof err) == 1, "send keep");
	nvvaluefree(&v);
	elem[0].valid = 1;
	elem[0].kind = Vatom;
	elem[0].atom = strdup("take");
	elem[1] = integer(9);
	check(elem[0].atom != nil && nvvaluetuple(&v, elem, 2) == 0, "message tuple");
	free(elem[0].atom);
	check(nvprocsend(&runtime, &p2, &v, err, sizeof err) == 1, "send take");
	nvvaluefree(&v);
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
	check(nvprocreceive(&runtime, &p2, clause, 1, &bindings, &which, &got, err, sizeof err) < 0 && strcmp(err, "bad_state") == 0, "receive requires running process");
	check(nvprocdispatch(&runtime, &p2, err, sizeof err) == 0, "dispatch selective receiver");
	check(nvprocdispatch(&runtime, &p2, err, sizeof err) < 0 && strcmp(err, "bad_state") == 0, "duplicate dispatch rejected");
	rc = nvprocreceive(&runtime, &p2, clause, 1, &bindings, &which, &got, err, sizeof err);
	check(rc == 1 && which == 0 && nvbinding(&bindings, "x") != nil && nvbinding(&bindings, "x")->i == 9, "selective receive");
	check(nvprocyield(&runtime, &p2, err, sizeof err) == 0, "yield selective receiver");
	nvbindingsfree(&bindings);
	nvvaluefree(&got);
	check(nvprocpop(&runtime, &p2, &got, err, sizeof err) == 1, "unmatched retained");
	check(got.kind == Vtuple && strcmp(got.tuple->elem[0].atom, "keep") == 0, "unmatched order");
	nvvaluefree(&got);
	print("ok - oldest matching receive preserves unmatched messages\n");

	v = integer(7);
	check(nvprocsend(&runtime, &p2, &v, err, sizeof err) == 1, "clause send");
	memset(&bindings, 0, sizeof bindings);
	check(nvprocdispatch(&runtime, &p2, err, sizeof err) == 0, "dispatch wildcard receiver");
	rc = nvprocreceive(&runtime, &p2, clause+1, 1, &bindings, &which, &got, err, sizeof err);
	check(rc == 1 && which == 0 && got.i == 7, "clause wildcard");
	check(nvprocyield(&runtime, &p2, err, sizeof err) == 0, "yield wildcard receiver");
	nvvaluefree(&got);
	nvbindingsfree(&bindings);
	print("ok - clause order within candidate\n");

	pattern[1].kind = Patom;
	pattern[1].name = "wake";
	clause[1].pattern = &pattern[1];
	memset(&bindings, 0, sizeof bindings);
	check(nvprocdispatch(&runtime, &p2, err, sizeof err) == 0, "dispatch waiting receiver");
	check(nvprocreceive(&runtime, &p2, clause+1, 1, &bindings, &which, &got, err, sizeof err) == 0 && runtime.process[p2.pid.slot].state == Prwaiting, "receive transitions running to waiting");
	check(nvprocyield(&runtime, &p2, err, sizeof err) < 0 && strcmp(err, "bad_state") == 0, "waiting process cannot yield");
	check(nvvalueatom(&v, "wake") == 0, "wake atom");
	check(nvprocsend(&runtime, &p2, &v, err, sizeof err) == 1 && runtime.process[p2.pid.slot].state == Prrunnable, "send wakes waiting process once");
	nvvaluefree(&v);
	check(nvprocdispatch(&runtime, &p2, err, sizeof err) == 0, "redispatch woken receiver");
	check(nvprocreceive(&runtime, &p2, clause+1, 1, &bindings, &which, &got, err, sizeof err) == 1, "woken receive consumes message");
	check(nvprocyield(&runtime, &p2, err, sizeof err) == 0, "yield woken receiver");
	nvvaluefree(&got);
	nvbindingsfree(&bindings);
	print("ok - explicit process lifecycle transitions\n");

	check(nvvaluecopy(&stale, &p1) == 0, "pid copy");
	check(nvprocexit(&runtime, &p1) == 1, "exit p1");
	check(nvprocsend(&runtime, &stale, &v, err, sizeof err) == 0, "dead pid drop");
	check(nvprocspawn(&runtime, &p1, err, sizeof err) == 0, "reuse slot");
	check(!nvvalueequal(&stale, &p1) && !nvprocalive(&runtime, &stale), "stale generation");
	nvvaluefree(&stale);
	runtime.process[p1.pid.slot].generation = ~0UL;
	p1.pid.generation = ~0UL;
	check(nvvaluecopy(&stale, &p1) == 0, "exhausted pid copy");
	check(nvprocexit(&runtime, &p1) == 1, "exit exhausted generation");
	check(nvprocspawn(&runtime, &p1, err, sizeof err) == 0, "spawn after generation exhaustion");
	check(p1.pid.slot != stale.pid.slot && runtime.process[stale.pid.slot].state == Prretired && !nvprocalive(&runtime, &stale), "exhausted generation retires slot");
	print("ok - dead sends, stale PIDs, and exhausted generations do not alias\n");

	nvvaluefree(&stale);
	nvvaluefree(&p1);
	nvvaluefree(&p2);
	nvvaluefree(&ref1);
	nvvaluefree(&ref2);
	nvruntimefree(&runtime);
	print("all process core tests passed\n");
	exits(nil);
}
