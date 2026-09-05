#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../include/nvbc.h"
#include "../include/nvvm.h"
#include "../include/nvexec.h"

/*
 * D064: the legacy one-process wrapper's result now leaves as a
 * fragment the caller owns and frees, not an NvValue the wrapper deep
 * copied. The compatibility wrapper uses unlimited maxheap; the limits
 * variant enforces its supplied budget. Both service requests inline
 * without spending reductions. Term exits render as a fixed diagnostic.
 */
int
nvexecute(Biobuf *trace, NvModule *module, char *entry, NvTerm arg, int traceon, vlong limit, NvFrag **result, char *err, int nerr)
{
	return nvexecutelimits(trace, module, entry, arg, traceon, limit, 0, 0, result, err, nerr);
}

int
nvexecutelimits(Biobuf *trace, NvModule *module, char *entry, NvTerm arg, int traceon, vlong limit, uvlong maxheap, int gcstress, NvFrag **result, char *err, int nerr)
{
	NvExec exec;
	int state;

	*result = nil;
	if(limit < 0){
		snprint(err, nerr, "bad reduction limit");
		return -1;
	}
	if(nvexecinit(&exec, module, entry, arg, maxheap, trace, traceon, err, nerr) < 0)
		return -1;
	exec.gcstress = gcstress;
	state = nvexecruninline(&exec, limit);
	if(state == NvDone){
		*result = exec.result;
		exec.result = nil;
		nvexecfree(&exec);
		return 0;
	}
	if(state == NvYield)
		snprint(err, nerr, "reduction_limit");
	else if(state == NvExit)
		snprint(err, nerr, "explicit_exit");
	else
		snprint(err, nerr, "%s", exec.fault);
	nvexecfree(&exec);
	return -1;
}
