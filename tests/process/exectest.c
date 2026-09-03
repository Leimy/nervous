#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../../include/nvbc.h"
#include "../../include/nvvm.h"
#include "../../include/nvexec.h"

static void
fail(char *s)
{
	fprint(2, "FAIL: %s\n", s);
	exits("test");
}

void
main(void)
{
	NvModule m;
	NvFunc f;
	NvInsn insn[3];
	NvConst konst[1];
	NvValue arg;
	NvExec exec;
	char err[128];
	int i, state;

	memset(&m,0,sizeof m); memset(&f,0,sizeof f); memset(insn,0,sizeof insn);
	f.name="loop"; f.nreg=1; f.ninsn=1; f.insn=insn; insn[0].op=Ojump; insn[0].a=0;
	m.nfunc=1; m.func=&f;
	if(nvvaluetuple(&arg,nil,0)<0 || nvexecinit(&exec,&m,"loop",&arg,nil,0,err,sizeof err)<0) fail("loop init");
	for(i=0;i<1000;i++) if(nvexecrun(&exec,1)!=NvYield) fail("loop did not yield");
	if(exec.reductions!=1000 || exec.fault[0]!=0) fail("yield accounting");
	nvexecfree(&exec);
	print("ok - reduction exhaustion yields without fault\n");

	memset(&m,0,sizeof m); memset(&f,0,sizeof f); memset(insn,0,sizeof insn); memset(konst,0,sizeof konst);
	konst[0].kind=Kfunc; konst[0].text="recurse";
	f.name="recurse"; f.nreg=2; f.ninsn=2; f.insn=insn;
	insn[0].op=Ocall; insn[0].a=1; insn[0].b=0; insn[0].c=0;
	insn[1].op=Oreturn; insn[1].a=1;
	m.nconst=1; m.konst=konst; m.nfunc=1; m.func=&f;
	if(nvexecinit(&exec,&m,"recurse",&arg,nil,0,err,sizeof err)<0 || nvexecsetframelimit(&exec,4)<0) fail("recursive init");
	if(nvexecrun(&exec,16)!=NvFault || strcmp(exec.fault,"system_limit")!=0 || exec.nframe!=4) fail("call depth limit");
	nvexecfree(&exec);
	print("ok - call depth exhaustion faults offending execution\n");

	memset(&m,0,sizeof m); memset(&f,0,sizeof f); memset(insn,0,sizeof insn); memset(konst,0,sizeof konst);
	konst[0].kind=Kfunc; konst[0].text="grow";
	f.name="grow"; f.nreg=2; f.ninsn=2; f.insn=insn;
	insn[0].op=Otuple; insn[0].a=1; insn[0].b=0; insn[0].c=1;
	insn[1].op=Otailcall; insn[1].a=0; insn[1].b=1;
	m.nconst=1; m.konst=konst; m.nfunc=1; m.func=&f;
	if(nvexecinit(&exec,&m,"grow",&arg,nil,0,err,sizeof err)<0) fail("term-limit init");
	if(nvexecrun(&exec,2*NvMaxtermdepth+2)!=NvFault || strcmp(exec.fault,"system_limit")!=0) fail("term depth fault reason");
	nvexecfree(&exec);
	print("ok - term depth exhaustion reports system_limit\n");

	memset(&m,0,sizeof m); memset(&f,0,sizeof f); memset(insn,0,sizeof insn); memset(konst,0,sizeof konst);
	konst[0].kind=Kint; konst[0].ival=42;
	f.name="main"; f.nreg=2; f.ninsn=3; f.insn=insn;
	insn[0].op=Onop; insn[1].op=Oloadk; insn[1].a=1; insn[1].b=0; insn[2].op=Oreturn; insn[2].a=1;
	m.nconst=1; m.konst=konst; m.nfunc=1; m.func=&f;
	if(nvexecinit(&exec,&m,"main",&arg,nil,0,err,sizeof err)<0) fail("finite init");
	state=NvYield;
	for(i=0;i<3 && state==NvYield;i++) state=nvexecrun(&exec,1);
	if(state!=NvDone || exec.result.kind!=Vint || exec.result.i!=42 || exec.reductions!=3) fail("finite resume result");
	print("ok - execution resumes across quanta\n");
	nvexecfree(&exec);
	nvvaluefree(&arg);
	print("all resumable execution tests passed\n");
	exits(nil);
}
