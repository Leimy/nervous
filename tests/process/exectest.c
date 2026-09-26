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

/*
 * D076-D079: VM-level tests for the nine binary opcodes. Each case
 * hand-writes a tiny module (no Kbin constants exist, so binaries are
 * built at runtime with binalloc + binappint/binappbin), runs it to a
 * terminal state, and inspects the result or fault string.
 */

static int
binisbin(NvTerm t, int *len)
{
	if(nvtermkind(t) != Vbin)
		return 0;
	if(len != nil)
		*len = (int)nvbinlen(t);
	return 1;
}

static int
binbyteseq(NvTerm t, uchar *want, int n)
{
	int i;

	if(!binisbin(t, nil) || (int)nvbinlen(t) != n)
		return 0;
	for(i = 0; i < n; i++)
		if(((uchar*)nvbinbytes(t))[i] != want[i])
			return 0;
	return 1;
}

static int
binisatom(NvTerm t, char *text)
{
	return nvtermkind(t) == Vatom && strcmp(nvtermatom(t), text) == 0;
}

/* Run exec until it reaches a terminal state (NvDone/NvFault/NvExit). */
static int
bintestrun(NvExec *exec)
{
	uvlong q;

	for(q = 16; ; ){
		int st = nvexecruninline(exec, q);
		if(st != NvYield)
			return st;
		q = 1;
	}
}

void
bintests(void)
{
	NvHeap h;
	NvTerm arg;
	NvModule m; NvFunc f; NvInsn insn[20]; NvConst konst[4];
	NvExec exec; int len;
	uchar want[8];
	NvTerm r;

	nvheapinit(&h, 0);
	arg = nvtuple(&h, nil, 0);

	/* A. Construction: binalloc, binappint width 1 (unsigned BE),
	 * binappint width 2 (unsigned BE), binappint width 2 (signed LE). */
	memset(&m,0,sizeof m); memset(&f,0,sizeof f); memset(insn,0,sizeof insn); memset(konst,0,sizeof konst);
	f.name="a"; f.nreg=6; f.ninsn=8; f.insn=insn;
	konst[0].kind=Kint; konst[0].ival=0x12;
	konst[1].kind=Kint; konst[1].ival=0x3456;
	konst[2].kind=Kint; konst[2].ival=-2;
	m.nconst=3; m.konst=konst; m.nfunc=1; m.func=&f;
	insn[0].op=Obinalloc;  insn[0].a=1;
	insn[1].op=Oloadk;     insn[1].a=2; insn[1].b=0;
	insn[2].op=Obinappint; insn[2].a=1; insn[2].b=1; insn[2].c=2; insn[2].d=(1<<2);
	insn[3].op=Oloadk;     insn[3].a=2; insn[3].b=1;
	insn[4].op=Obinappint; insn[4].a=1; insn[4].b=1; insn[4].c=2; insn[4].d=(2<<2);
	insn[5].op=Oloadk;     insn[5].a=2; insn[5].b=2;
	insn[6].op=Obinappint; insn[6].a=1; insn[6].b=1; insn[6].c=2; insn[6].d=(2<<2)|3;
	insn[7].op=Oreturn;    insn[7].a=1;
	if(nvexecinit(&exec,&m,"a",arg,0,nil,0,nil,0)<0) fail("bin A init");
	if(nvexecsetframelimit(&exec,8)<0) fail("bin A framelimit");
	if(bintestrun(&exec)!=NvDone) fail("bin A did not finish");
	r=exec.result->root;
	if(!binisbin(r,&len)||len!=5) fail("bin A result");
	want[0]=0x12; want[1]=0x34; want[2]=0x56; want[3]=0xFE; want[4]=0xFF;
	if(!binbyteseq(r, want, 5)) fail("bin A bytes");
	nvexecfree(&exec);
	print("ok - binary construction (binalloc + binappint widths)\n");

	/* B. Append: build <<9>> and <<1,2,3>>; binappend -> <<9,1,2,3>>;
	 * binappbin size 2 -> <<9,1,2>>; return both as a tuple. */
	memset(&m,0,sizeof m); memset(&f,0,sizeof f); memset(insn,0,sizeof insn); memset(konst,0,sizeof konst);
	f.name="b"; f.nreg=5; f.ninsn=15; f.insn=insn;
	konst[0].kind=Kint; konst[0].ival=1;
	konst[1].kind=Kint; konst[1].ival=2;
	konst[2].kind=Kint; konst[2].ival=3;
	konst[3].kind=Kint; konst[3].ival=9;
	m.nconst=4; m.konst=konst; m.nfunc=1; m.func=&f;
	insn[0].op=Obinalloc;   insn[0].a=1;
	insn[1].op=Obinalloc;   insn[1].a=2;
	insn[2].op=Oloadk;      insn[2].a=3; insn[2].b=3;
	insn[3].op=Obinappint;  insn[3].a=1; insn[3].b=1; insn[3].c=3; insn[3].d=(1<<2);
	insn[4].op=Oloadk;      insn[4].a=3; insn[4].b=0;
	insn[5].op=Obinappint;  insn[5].a=2; insn[5].b=2; insn[5].c=3; insn[5].d=(1<<2);
	insn[6].op=Oloadk;      insn[6].a=3; insn[6].b=1;
	insn[7].op=Obinappint;  insn[7].a=2; insn[7].b=2; insn[7].c=3; insn[7].d=(1<<2);
	insn[8].op=Oloadk;      insn[8].a=3; insn[8].b=2;
	insn[9].op=Obinappint;  insn[9].a=2; insn[9].b=2; insn[9].c=3; insn[9].d=(1<<2);
	insn[10].op=Obinappend; insn[10].a=3; insn[10].b=1; insn[10].c=2;
	/* r4 := 2 (size), then r4 := r1 ++ first 2 bytes of r2 = <<9,1,2>>. */
	insn[11].op=Oloadk;     insn[11].a=4; insn[11].b=1;
	insn[12].op=Obinappbin; insn[12].a=4; insn[12].b=1; insn[12].c=2; insn[12].d=4;
	insn[13].op=Otuple;     insn[13].a=0; insn[13].b=3; insn[13].c=2;
	insn[14].op=Oreturn;    insn[14].a=0;
	if(nvexecinit(&exec,&m,"b",arg,0,nil,0,nil,0)<0) fail("bin B init");
	if(nvexecsetframelimit(&exec,8)<0) fail("bin B framelimit");
	if(bintestrun(&exec)!=NvDone) fail("bin B did not finish");
	r=exec.result->root;
	if(nvtermkind(r)!=Vtuple||nvtuplelen(r)!=2) fail("bin B shape");
	if(!binbyteseq(nvtupleelem(r,0),(uchar*)"\x09\x01\x02\x03",4)) fail("bin B append");
	if(!binbyteseq(nvtupleelem(r,1),(uchar*)"\x09\x01\x02",3)) fail("bin B appbin");
	nvexecfree(&exec);
	print("ok - binary append (binappend + binappbin)\n");

	/* C. Match success: subject <<9,97,98,99>>; bintestbinary,
	 * binintget len (width 1), binbinget size len, binend;
	 * return ${len, extracted}. Check len==3, extracted==<<97,98,99>>. */
	memset(&m,0,sizeof m); memset(&f,0,sizeof f); memset(insn,0,sizeof insn); memset(konst,0,sizeof konst);
	f.name="c"; f.nreg=5; f.ninsn=17; f.insn=insn;
	konst[0].kind=Kint; konst[0].ival=3;
	konst[1].kind=Kint; konst[1].ival=97;
	konst[2].kind=Kint; konst[2].ival=98;
	konst[3].kind=Kint; konst[3].ival=99;
	m.nconst=4; m.konst=konst; m.nfunc=1; m.func=&f;
	insn[0].op=Obinalloc;      insn[0].a=1;
	insn[1].op=Obinalloc;      insn[1].a=2;
	insn[2].op=Oloadk;         insn[2].a=3; insn[2].b=0;
	insn[3].op=Obinappint;     insn[3].a=1; insn[3].b=1; insn[3].c=3; insn[3].d=(1<<2);
	insn[4].op=Oloadk;         insn[4].a=3; insn[4].b=1;
	insn[5].op=Obinappint;     insn[5].a=1; insn[5].b=1; insn[5].c=3; insn[5].d=(1<<2);
	insn[6].op=Oloadk;         insn[6].a=3; insn[6].b=2;
	insn[7].op=Obinappint;     insn[7].a=1; insn[7].b=1; insn[7].c=3; insn[7].d=(1<<2);
	insn[8].op=Oloadk;         insn[8].a=3; insn[8].b=3;
	insn[9].op=Obinappint;     insn[9].a=1; insn[9].b=1; insn[9].c=3; insn[9].d=(1<<2);
	/* every mismatch goes to 16, which returns the subject (not a tuple) */
	insn[10].op=Obintestbinary; insn[10].a=1; insn[10].b=16;
	insn[11].op=Obinintget;   insn[11].a=3; insn[11].b=16; insn[11].c=(1<<2);
	insn[12].op=Obinbinget;   insn[12].a=4; insn[12].b=3; insn[12].c=16;
	insn[13].op=Obinend;      insn[13].a=16;
	insn[14].op=Otuple;       insn[14].a=3; insn[14].b=3; insn[14].c=2;
	insn[15].op=Oreturn;      insn[15].a=3;
	insn[16].op=Oreturn;      insn[16].a=1;
	if(nvexecinit(&exec,&m,"c",arg,0,nil,0,nil,0)<0) fail("bin C init");
	if(nvexecsetframelimit(&exec,8)<0) fail("bin C framelimit");
	if(bintestrun(&exec)!=NvDone) fail("bin C did not finish");
	r=exec.result->root;
	if(nvtermkind(r)!=Vtuple||nvtuplelen(r)!=2) fail("bin C shape");
	if(nvtermkind(nvtupleelem(r,0))!=Vint||nvtermint(nvtupleelem(r,0))!=3) fail("bin C len");
	if(!binbyteseq(nvtupleelem(r,1),(uchar*)"\x61\x62\x63",3)) fail("bin C extracted");
	nvexecfree(&exec);
	print("ok - binary match (intget + binget + end)\n");

	/* D. Mismatches: each must reach the fail target (returns `mismatch),
	 * never fault. Four sub-cases in one group. */
	{
		/*
		 * Every mismatch jumps to a two-instruction tail (loadk r1 := the
		 * 'mismatch atom; return r1). Every success path returns 'matchfail
		 * or a binary instead, so reaching 'mismatch proves the fail edge.
		 */
		NvModule m2; NvFunc f2; NvInsn i2[12]; NvConst k2[3]; NvExec e2;

		/* D1: subject is an integer. */
		memset(&m2,0,sizeof m2); memset(&f2,0,sizeof f2); memset(i2,0,sizeof i2); memset(k2,0,sizeof k2);
		f2.name="d1"; f2.nreg=2; f2.ninsn=6; f2.insn=i2;
		k2[0].kind=Katom; k2[0].text="mismatch";
		k2[1].kind=Katom; k2[1].text="matchfail";
		k2[2].kind=Kint; k2[2].ival=7;
		m2.nconst=3; m2.konst=k2; m2.nfunc=1; m2.func=&f2;
		i2[0].op=Oloadk;         i2[0].a=1; i2[0].b=2;
		i2[1].op=Obintestbinary; i2[1].a=1; i2[1].b=4;
		i2[2].op=Oloadk;         i2[2].a=1; i2[2].b=1;
		i2[3].op=Oreturn;        i2[3].a=1;
		i2[4].op=Oloadk;         i2[4].a=1; i2[4].b=0;
		i2[5].op=Oreturn;        i2[5].a=1;
		if(nvexecinit(&e2,&m2,"d1",arg,0,nil,0,nil,0)<0) fail("bin D1 init");
		if(nvexecsetframelimit(&e2,8)<0) fail("bin D1 framelimit");
		if(bintestrun(&e2)!=NvDone || !binisatom(e2.result->root,"mismatch")) fail("bin D1 mismatch");
		nvexecfree(&e2);

		/* D2: binintget width 4 on a 2-byte binary. */
		memset(&m2,0,sizeof m2); memset(&f2,0,sizeof f2); memset(i2,0,sizeof i2); memset(k2,0,sizeof k2);
		f2.name="d2"; f2.nreg=2; f2.ninsn=9; f2.insn=i2;
		k2[0].kind=Katom; k2[0].text="mismatch";
		k2[1].kind=Katom; k2[1].text="matchfail";
		k2[2].kind=Kint; k2[2].ival=1;
		m2.nconst=3; m2.konst=k2; m2.nfunc=1; m2.func=&f2;
		i2[0].op=Obinalloc;      i2[0].a=1;
		i2[1].op=Oloadk;         i2[1].a=0; i2[1].b=2;
		i2[2].op=Obinappint;     i2[2].a=1; i2[2].b=1; i2[2].c=0; i2[2].d=(1<<2);
		i2[3].op=Obinappint;     i2[3].a=1; i2[3].b=1; i2[3].c=0; i2[3].d=(1<<2);
		i2[4].op=Obintestbinary; i2[4].a=1; i2[4].b=7;
		i2[5].op=Obinintget;     i2[5].a=0; i2[5].b=7; i2[5].c=(4<<2);
		i2[6].op=Oreturn;        i2[6].a=0;
		i2[7].op=Oloadk;         i2[7].a=1; i2[7].b=0;
		i2[8].op=Oreturn;        i2[8].a=1;
		if(nvexecinit(&e2,&m2,"d2",arg,0,nil,0,nil,0)<0) fail("bin D2 init");
		if(nvexecsetframelimit(&e2,8)<0) fail("bin D2 framelimit");
		if(bintestrun(&e2)!=NvDone || !binisatom(e2.result->root,"mismatch")) fail("bin D2 mismatch");
		nvexecfree(&e2);

		/* D3: binbinget size 10 on a 3-byte binary. */
		memset(&m2,0,sizeof m2); memset(&f2,0,sizeof f2); memset(i2,0,sizeof i2); memset(k2,0,sizeof k2);
		f2.name="d3"; f2.nreg=3; f2.ninsn=11; f2.insn=i2;
		k2[0].kind=Katom; k2[0].text="mismatch";
		k2[1].kind=Katom; k2[1].text="matchfail";
		k2[2].kind=Kint; k2[2].ival=10;
		m2.nconst=3; m2.konst=k2; m2.nfunc=1; m2.func=&f2;
		i2[0].op=Obinalloc;      i2[0].a=1;
		i2[1].op=Oloadk;         i2[1].a=0; i2[1].b=2;
		i2[2].op=Obinappint;     i2[2].a=1; i2[2].b=1; i2[2].c=0; i2[2].d=(1<<2);
		i2[3].op=Obinappint;     i2[3].a=1; i2[3].b=1; i2[3].c=0; i2[3].d=(1<<2);
		i2[4].op=Obinappint;     i2[4].a=1; i2[4].b=1; i2[4].c=0; i2[4].d=(1<<2);
		i2[5].op=Obintestbinary; i2[5].a=1; i2[5].b=9;
		i2[6].op=Oloadk;         i2[6].a=2; i2[6].b=2;
		i2[7].op=Obinbinget;     i2[7].a=0; i2[7].b=2; i2[7].c=9;
		i2[8].op=Oreturn;        i2[8].a=0;
		i2[9].op=Oloadk;         i2[9].a=1; i2[9].b=0;
		i2[10].op=Oreturn;       i2[10].a=1;
		if(nvexecinit(&e2,&m2,"d3",arg,0,nil,0,nil,0)<0) fail("bin D3 init");
		if(nvexecsetframelimit(&e2,8)<0) fail("bin D3 framelimit");
		if(bintestrun(&e2)!=NvDone || !binisatom(e2.result->root,"mismatch")) fail("bin D3 mismatch");
		nvexecfree(&e2);

		/* D4: binend with a trailing byte left (consumed 2 of 3). */
		memset(&m2,0,sizeof m2); memset(&f2,0,sizeof f2); memset(i2,0,sizeof i2); memset(k2,0,sizeof k2);
		f2.name="d4"; f2.nreg=3; f2.ninsn=12; f2.insn=i2;
		k2[0].kind=Katom; k2[0].text="mismatch";
		k2[1].kind=Katom; k2[1].text="matchfail";
		k2[2].kind=Kint; k2[2].ival=2;
		m2.nconst=3; m2.konst=k2; m2.nfunc=1; m2.func=&f2;
		i2[0].op=Obinalloc;      i2[0].a=1;
		i2[1].op=Oloadk;         i2[1].a=0; i2[1].b=2;
		i2[2].op=Obinappint;     i2[2].a=1; i2[2].b=1; i2[2].c=0; i2[2].d=(1<<2);
		i2[3].op=Obinappint;     i2[3].a=1; i2[3].b=1; i2[3].c=0; i2[3].d=(1<<2);
		i2[4].op=Obinappint;     i2[4].a=1; i2[4].b=1; i2[4].c=0; i2[4].d=(1<<2);
		i2[5].op=Obintestbinary; i2[5].a=1; i2[5].b=10;
		i2[6].op=Oloadk;         i2[6].a=2; i2[6].b=2;
		i2[7].op=Obinbinget;     i2[7].a=0; i2[7].b=2; i2[7].c=10;
		i2[8].op=Obinend;        i2[8].a=10;
		i2[9].op=Oreturn;        i2[9].a=0;
		i2[10].op=Oloadk;        i2[10].a=1; i2[10].b=0;
		i2[11].op=Oreturn;       i2[11].a=1;
		if(nvexecinit(&e2,&m2,"d4",arg,0,nil,0,nil,0)<0) fail("bin D4 init");
		if(nvexecsetframelimit(&e2,8)<0) fail("bin D4 framelimit");
		if(bintestrun(&e2)!=NvDone || !binisatom(e2.result->root,"mismatch")) fail("bin D4 mismatch");
		nvexecfree(&e2);

		print("ok - binary mismatch (fail target, not fault)\n");
	}

	/* E. Faults: badarith, overflow, bad_binary (x2). */
	{
		NvModule m2; NvFunc f2; NvInsn i2[4]; NvConst k2[1]; NvExec e2;
		NvTerm arg2;

		/* E1: binappint with an atom value -> badarith. */
		memset(&m2,0,sizeof m2); memset(&f2,0,sizeof f2); memset(i2,0,sizeof i2); memset(k2,0,sizeof k2);
		f2.name="e1"; f2.nreg=3; f2.ninsn=3; f2.insn=i2;
		k2[0].kind=Katom; k2[0].text="x";
		m2.nconst=1; m2.konst=k2; m2.nfunc=1; m2.func=&f2;
		i2[0].op=Obinalloc;  i2[0].a=1;
		i2[1].op=Oloadk;     i2[1].a=2; i2[1].b=0;
		i2[2].op=Obinappint; i2[2].a=1; i2[2].b=1; i2[2].c=2; i2[2].d=(1<<2);
		arg2=nvatom("x");
		if(nvexecinit(&e2,&m2,"e1",arg2,0,nil,0,nil,0)<0) fail("bin E1 init");
		if(nvexecsetframelimit(&e2,8)<0) fail("bin E1 framelimit");
		if(bintestrun(&e2)!=NvFault || strcmp(e2.fault,"badarith")!=0) fail("bin E1 fault");
		nvexecfree(&e2);

		/* E2: binappint 300 width 1 unsigned -> overflow. */
		memset(&m2,0,sizeof m2); memset(&f2,0,sizeof f2); memset(i2,0,sizeof i2); memset(k2,0,sizeof k2);
		f2.name="e2"; f2.nreg=3; f2.ninsn=3; f2.insn=i2;
		k2[0].kind=Kint; k2[0].ival=300;
		m2.nconst=1; m2.konst=k2; m2.nfunc=1; m2.func=&f2;
		i2[0].op=Obinalloc;  i2[0].a=1;
		i2[1].op=Oloadk;     i2[1].a=2; i2[1].b=0;
		i2[2].op=Obinappint; i2[2].a=1; i2[2].b=1; i2[2].c=2; i2[2].d=(1<<2);
		if(nvexecinit(&e2,&m2,"e2",arg,0,nil,0,nil,0)<0) fail("bin E2 init");
		if(nvexecsetframelimit(&e2,8)<0) fail("bin E2 framelimit");
		if(bintestrun(&e2)!=NvFault || strcmp(e2.fault,"overflow")!=0) fail("bin E2 fault");
		nvexecfree(&e2);

		/* E3: binappbin with size -1 -> bad_binary. */
		memset(&m2,0,sizeof m2); memset(&f2,0,sizeof f2); memset(i2,0,sizeof i2); memset(k2,0,sizeof k2);
		f2.name="e3"; f2.nreg=4; f2.ninsn=5; f2.insn=i2;
		k2[0].kind=Kint; k2[0].ival=-1;
		m2.nconst=1; m2.konst=k2; m2.nfunc=1; m2.func=&f2;
		i2[0].op=Obinalloc;  i2[0].a=1;
		i2[1].op=Obinalloc;  i2[1].a=2;
		i2[2].op=Oloadk;     i2[2].a=0; i2[2].b=0;
		i2[3].op=Obinappbin; i2[3].a=1; i2[3].b=1; i2[3].c=2; i2[3].d=0;
		if(nvexecinit(&e2,&m2,"e3",arg,0,nil,0,nil,0)<0) fail("bin E3 init");
		if(nvexecsetframelimit(&e2,8)<0) fail("bin E3 framelimit");
		if(bintestrun(&e2)!=NvFault || strcmp(e2.fault,"bad_binary")!=0) fail("bin E3 fault");
		nvexecfree(&e2);

		/* E4: binintget with no preceding bintestbinary -> bad_binary. */
		memset(&m2,0,sizeof m2); memset(&f2,0,sizeof f2); memset(i2,0,sizeof i2); memset(k2,0,sizeof k2);
		f2.name="e4"; f2.nreg=2; f2.ninsn=1; f2.insn=i2;
		m2.nfunc=1; m2.func=&f2;
		i2[0].op=Obinintget; i2[0].a=1; i2[0].b=0; i2[0].c=(1<<2);
		if(nvexecinit(&e2,&m2,"e4",arg,0,nil,0,nil,0)<0) fail("bin E4 init");
		if(nvexecsetframelimit(&e2,8)<0) fail("bin E4 framelimit");
		if(bintestrun(&e2)!=NvFault || strcmp(e2.fault,"bad_binary")!=0) fail("bin E4 fault");
		nvexecfree(&e2);

		print("ok - binary faults (badarith, overflow, bad_binary)\n");
	}

	/* F. Remainder: subject <<7,8,9>>; binremget -> <<8,9>> (pos was 1
	 * after a 1-byte intget), then binremget again -> <<>>; return both. */
	memset(&m,0,sizeof m); memset(&f,0,sizeof f); memset(insn,0,sizeof insn); memset(konst,0,sizeof konst);
	f.name="f"; f.nreg=4; f.ninsn=14; f.insn=insn;
	konst[0].kind=Kint; konst[0].ival=7;
	konst[1].kind=Kint; konst[1].ival=8;
	konst[2].kind=Kint; konst[2].ival=9;
	m.nconst=3; m.konst=konst; m.nfunc=1; m.func=&f;
	insn[0].op=Obinalloc;      insn[0].a=1;
	insn[1].op=Oloadk;         insn[1].a=2; insn[1].b=0;
	insn[2].op=Obinappint;     insn[2].a=1; insn[2].b=1; insn[2].c=2; insn[2].d=(1<<2);
	insn[3].op=Oloadk;         insn[3].a=2; insn[3].b=1;
	insn[4].op=Obinappint;     insn[4].a=1; insn[4].b=1; insn[4].c=2; insn[4].d=(1<<2);
	insn[5].op=Oloadk;         insn[5].a=2; insn[5].b=2;
	insn[6].op=Obinappint;     insn[6].a=1; insn[6].b=1; insn[6].c=2; insn[6].d=(1<<2);
	/* every mismatch goes to 13, which returns the subject (not a tuple) */
	insn[7].op=Obintestbinary; insn[7].a=1; insn[7].b=13;
	insn[8].op=Obinintget;     insn[8].a=2; insn[8].b=13; insn[8].c=(1<<2);
	insn[9].op=Obinremget;     insn[9].a=2;
	insn[10].op=Obinremget;    insn[10].a=3;	/* r1 stays the subject */
	insn[11].op=Otuple;        insn[11].a=0; insn[11].b=2; insn[11].c=2;	/* ${r2, r3} */
	insn[12].op=Oreturn;       insn[12].a=0;
	insn[13].op=Oreturn;       insn[13].a=1;
	if(nvexecinit(&exec,&m,"f",arg,0,nil,0,nil,0)<0) fail("bin F init");
	if(nvexecsetframelimit(&exec,8)<0) fail("bin F framelimit");
	if(bintestrun(&exec)!=NvDone) fail("bin F did not finish");
	r=exec.result->root;
	if(nvtermkind(r)!=Vtuple||nvtuplelen(r)!=2) fail("bin F shape");
	if(!binbyteseq(nvtupleelem(r,0),(uchar*)"\x08\x09",2)) fail("bin F rem0");
	if(!binisbin(nvtupleelem(r,1),&len)||len!=0) fail("bin F rem1");
	nvexecfree(&exec);
	print("ok - binary remainder (binremget)\n");

	nvheapfree(&h);
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
	bintests();
	nvheapfree(&h);
	print("all resumable execution tests passed\n");
	exits(nil);
}
