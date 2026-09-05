#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../../include/nvbc.h"
#include "../../include/nvvm.h"
#include "../../include/nvexec.h"

/* These tests measure bytecode quanta, not collector scheduling. */
#define nvexecrun nvexecruninline

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
	NvHeap h;
	NvTerm arg;
	NvExec exec;
	char err[128];
	int i, state;

	/*
	 * A small host-owned heap for the (trivial, shared) argument tuple.
	 * nvexecinit copies arg into each exec's own process heap, so one
	 * host heap and one built term safely serve every test below; it is
	 * freed once at the very end.
	 */
	nvheapinit(&h, 0);
	arg = nvtuple(&h, nil, 0);

	memset(&m,0,sizeof m); memset(&f,0,sizeof f); memset(insn,0,sizeof insn);
	f.name="loop"; f.nreg=1; f.ninsn=1; f.insn=insn; insn[0].op=Ojump; insn[0].a=0;
	m.nfunc=1; m.func=&f;
	if(nvexecinit(&exec,&m,"loop",arg,0,nil,0,err,sizeof err)<0) fail("loop init");
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
	if(nvexecinit(&exec,&m,"recurse",arg,0,nil,0,err,sizeof err)<0 || nvexecsetframelimit(&exec,4)<0) fail("recursive init");
	if(nvexecrun(&exec,16)!=NvFault || strcmp(exec.fault,"system_limit")!=0 || exec.nframe!=4) fail("call depth limit");
	nvexecfree(&exec);
	print("ok - call depth exhaustion faults offending execution\n");

	/*
	 * D061: construction no longer traverses, so Otuple can nest a tuple
	 * arbitrarily deep without faulting -- the old version of this test,
	 * which expected Otuple itself to fault at depth 256, no longer
	 * applies. The NvMaxtermdepth ceiling instead lives at the
	 * traversals that must recurse: nvfragcopy (which Oreturn uses to
	 * hand the root result to its caller), nvtermequal, and nvtermprint.
	 * This test rebuilds the old "grow" loop -- Otuple nests one level
	 * (register 1 := ${register 0}), Otailcall restarts the same
	 * function at pc 0 with register 0 := register 1 (D065) -- and
	 * proves the new boundary instead: run it long enough to nest a
	 * value deeper than NvMaxtermdepth, then turn the tail call into a
	 * return of that too-deep value and confirm *that* faults
	 * system_limit.
	 *
	 * Nothing here ever finishes normally on its own (grow only ever
	 * tail-calls itself), so it is driven with an exact quantum and then
	 * patched in place: the module's own instruction array is read live
	 * by nvexecrun on every dispatch, so overwriting insn[1] after the
	 * quantum changes what the *next* dispatch does.
	 *
	 * A whole number of grow iterations always leaves pc back at 0 (the
	 * Otuple slot), never at 1, because Otailcall resets pc to 0 on
	 * every iteration; only an odd reduction count -- a whole number of
	 * iterations plus one further lone Otuple dispatch -- leaves pc at 1
	 * with register 1 already holding the freshly nested value. Running
	 * NvMaxtermdepth+1 complete iterations plus that one extra Otuple
	 * dispatch is 2*(NvMaxtermdepth+2)-1 reductions (one short of the
	 * naive "2*(NvMaxtermdepth+2)" full-iteration figure) and nests the
	 * value to depth NvMaxtermdepth+3 -- comfortably past the ceiling.
	 */
	memset(&m,0,sizeof m); memset(&f,0,sizeof f); memset(insn,0,sizeof insn); memset(konst,0,sizeof konst);
	konst[0].kind=Kfunc; konst[0].text="grow";
	f.name="grow"; f.nreg=2; f.ninsn=2; f.insn=insn;
	insn[0].op=Otuple; insn[0].a=1; insn[0].b=0; insn[0].c=1;
	insn[1].op=Otailcall; insn[1].a=0; insn[1].b=1;
	m.nconst=1; m.konst=konst; m.nfunc=1; m.func=&f;
	if(nvexecinit(&exec,&m,"grow",arg,0,nil,0,err,sizeof err)<0) fail("term-limit init");
	if(nvexecrun(&exec,2*(NvMaxtermdepth+2)-1)!=NvYield) fail("term-limit growth did not yield");
	insn[1].op=Oreturn; insn[1].a=1; insn[1].b=0; insn[1].c=0;
	if(nvexecrun(&exec,1)!=NvFault || strcmp(exec.fault,"system_limit")!=0) fail("term depth fault reason");
	nvexecfree(&exec);
	print("ok - term depth is enforced at the return boundary, not construction\n");

	memset(&m,0,sizeof m); memset(&f,0,sizeof f); memset(insn,0,sizeof insn); memset(konst,0,sizeof konst);
	konst[0].kind=Kint; konst[0].ival=42;
	f.name="main"; f.nreg=2; f.ninsn=3; f.insn=insn;
	insn[0].op=Onop; insn[1].op=Oloadk; insn[1].a=1; insn[1].b=0; insn[2].op=Oreturn; insn[2].a=1;
	m.nconst=1; m.konst=konst; m.nfunc=1; m.func=&f;
	if(nvexecinit(&exec,&m,"main",arg,0,nil,0,err,sizeof err)<0) fail("finite init");
	state=NvYield;
	for(i=0;i<3 && state==NvYield;i++) state=nvexecrun(&exec,1);
	if(state!=NvDone || exec.result==nil || nvtermkind(exec.result->root)!=Vint || nvtermint(exec.result->root)!=42 || exec.reductions!=3)
		fail("finite resume result");
	print("ok - execution resumes across quanta\n");
	/* nvexecfree owns and frees exec.result since we never took it. */
	nvexecfree(&exec);
	nvheapfree(&h);
	print("all resumable execution tests passed\n");
	exits(nil);
}
