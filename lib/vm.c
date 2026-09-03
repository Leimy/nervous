#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../include/nvbc.h"
#include "../include/nvvm.h"
#include "../include/nvexec.h"

/* The legacy one-process wrapper renders term exits as a fixed diagnostic. */
int
nvexecute(Biobuf *trace, NvModule *module, char *entry, NvValue *arg, int traceon, vlong limit, NvValue *result, char *err, int nerr)
{
	NvExec exec;
	int state;

	memset(result, 0, sizeof *result);
	if(limit < 0){
		snprint(err, nerr, "bad reduction limit");
		return -1;
	}
	if(nvexecinit(&exec, module, entry, arg, trace, traceon, err, nerr) < 0)
		return -1;
	state = nvexecrun(&exec, limit);
	if(state == NvDone){
		*result = exec.result;
		memset(&exec.result, 0, sizeof exec.result);
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
