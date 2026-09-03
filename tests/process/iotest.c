#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../../include/nvbc.h"
#include "../../include/nvvm.h"
#include "../../include/nvexec.h"
#include "../../include/nvproc.h"
#include "../../include/nvsched.h"

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

/*
 * D054-D057: a process that prints, then continues, must be scheduled
 * identically -- same number of dispatches to completion, same completion
 * accounting -- to one that executes an equal-cost instruction instead.
 * Both fixture functions below are exactly three instructions (loadk,
 * one more instruction, return); running each alone under a one-reduction
 * quantum forces one dispatch per instruction, so the step count to
 * NvSchedDone is a direct, freed-memory-safe measurement of "does print
 * cost anything beyond one ordinary reduction". It does not: both take
 * exactly three progress steps plus the final NvSchedDone step.
 */
static void
run(NvIO *io, char *entry, NvModule *m, NvLimits *limits, uvlong incarnation,
	uvlong *dispatches, uvlong *completed, uvlong *faulted, int *steps)
{
	NvScheduler sched;
	NvValue arg, pid;
	char err[128];
	int state, i;

	check(nvvaluetuple(&arg, nil, 0) == 0, "argument tuple");
	check(nvschedinit(&sched, m, limits, incarnation, 1, err, sizeof err) == 0, err);
	if(io != nil)
		nvschedsetio(&sched, io);
	check(nvschedspawn(&sched, entry, &arg, &pid, err, sizeof err) == 0, "spawn fixture entry");
	i = 0;
	do{
		state = nvschedstep(&sched, err, sizeof err);
		check(state != NvSchedError, err);
		i++;
	}while(state == NvSchedProgress);
	check(state == NvSchedDone, "fixture scheduler reaches done");
	*dispatches = sched.dispatches;
	*completed = sched.completed;
	*faulted = sched.faulted;
	*steps = i;
	nvschedfree(&sched);
	nvvaluefree(&pid);
	nvvaluefree(&arg);
}

void
main(void)
{
	NvModule module;
	NvFunc func[2];
	NvInsn insn[6];
	NvConst konst[1];
	NvLimits limits;
	NvIO io;
	Biobuf *devnull;
	uvlong pdispatch, pcompleted, pfaulted, ndispatch, ncompleted, nfaulted;
	int psteps, nsteps;

	memset(&module, 0, sizeof module);
	memset(func, 0, sizeof func);
	memset(insn, 0, sizeof insn);
	memset(konst, 0, sizeof konst);

	konst[0].kind = Katom;
	konst[0].text = "hi";

	/* printone: loadk 1 0; print 2 1; return 2 */
	func[0].name = "printone";
	func[0].nreg = 3;
	func[0].ninsn = 3;
	func[0].insn = &insn[0];
	insn[0].op = Oloadk; insn[0].a = 1; insn[0].b = 0;
	insn[1].op = Oprint; insn[1].a = 2; insn[1].b = 1;
	insn[2].op = Oreturn; insn[2].a = 2;

	/* nopone: loadk 1 0; nop; return 1 -- print's slot replaced by nop. */
	func[1].name = "nopone";
	func[1].nreg = 2;
	func[1].ninsn = 3;
	func[1].insn = &insn[3];
	insn[3].op = Oloadk; insn[3].a = 1; insn[3].b = 0;
	insn[4].op = Onop;
	insn[5].op = Oreturn; insn[5].a = 1;

	module.nconst = 1;
	module.konst = konst;
	module.nfunc = 2;
	module.func = func;

	limits.maxprocess = 4;
	limits.maxmailbox = 4096;
	limits.maxmessage = 1024;
	limits.maxframe = 64;
	limits.maxtermdepth = NvMaxtermdepth;
	limits.maxduration = NvMaxduration;

	devnull = Bopen("/dev/null", OWRITE);
	check(devnull != nil, "open /dev/null for the print sink");
	io.out = devnull;
	io.err = nil;

	run(&io, "printone", &module, &limits, 1, &pdispatch, &pcompleted, &pfaulted, &psteps);
	run(nil, "nopone", &module, &limits, 2, &ndispatch, &ncompleted, &nfaulted, &nsteps);
	Bterm(devnull);

	check(pfaulted == 0, "print under an installed sink never faults");
	check(pdispatch == 3 && pdispatch == ndispatch, "print costs no extra dispatches versus nop");
	check(pcompleted == 1 && pcompleted == ncompleted, "both fixtures complete exactly once");
	check(psteps == 4 && psteps == nsteps, "print costs no extra scheduler steps versus nop");
	print("ok - print consumes one ordinary reduction and changes no scheduling behavior\n");

	print("all io scheduling tests passed\n");
	exits(nil);
}
