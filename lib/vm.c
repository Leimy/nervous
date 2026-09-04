#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../include/nvbc.h"
#include "../include/nvvm.h"
#include "../include/nvexec.h"

/*
 * D064: the legacy one-process wrapper's result now leaves as a
 * fragment the caller owns and frees, not an NvValue the wrapper deep
 * copied. maxheap 0 (unlimited) matches this stage's "no accounting
 * yet" scope; term exits still render as a fixed diagnostic.
 */
int
nvexecute(Biobuf *trace, NvModule *module, char *entry, NvTerm arg, int traceon, vlong limit, NvFrag **result, char *err, int nerr)
{
	NvExec exec;
	int state;

	*result = nil;
	if(limit < 0){
		snprint(err, nerr, "bad reduction limit");
		return -1;
	}
	if(nvexecinit(&exec, module, entry, arg, 0, trace, traceon, err, nerr) < 0)
		return -1;
	state = nvexecrun(&exec, limit);
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
