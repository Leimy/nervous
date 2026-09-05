#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../../include/nvbc.h"
#include "../../include/nvvm.h"
#include "../../include/nvexec.h"

static void
check(int ok, char *why)
{
	if(!ok){
		fprint(2, "FAIL: gc: %s\n", why);
		exits("test");
	}
}

static NvTerm
tuple(NvHeap *h, NvTerm *v, int n)
{
	NvTerm t;

	t = nvtuple(h, v, n);
	check(t != NvNil, "test tuple allocation");
	return t;
}

static void
kinds(void)
{
	NvHeap h;
	NvRoot r;
	NvTerm root[7], elem[4], leaf[2], old, empty, opaque;
	int i, j;

	nvheapinit(&h, 0);
	opaque = tuple(&h, nil, 0); /* Dead, even though its bits occur in a Ref. */
	leaf[0] = nvint(&h, (vlong)0x8000000000000000ULL);
	leaf[1] = nvref(&h, opaque, opaque);
	check(leaf[0] != NvNil && leaf[1] != NvNil, "boxed constructors");
	elem[0] = tuple(&h, leaf, 2);
	elem[1] = elem[0];
	empty = tuple(&h, nil, 0);
	elem[2] = empty;
	elem[3] = nvatom("gc_atom");
	check(elem[3] != NvNil, "atom constructor");
	root[0] = tuple(&h, elem, 4);
	root[1] = elem[0];
	root[2] = empty;
	root[3] = nvint(nil, -37);
	root[4] = elem[3];
	root[5] = nvpid(23, 42);
	root[6] = NvNil;
	r.word = root; r.nword = nelem(root); r.next = nil;
	for(i = 0; i < 100; i++){
		for(j = 0; j < 300; j++)
			tuple(&h, root+3, 1);
		old = root[0];
		/* Live 14, stack 13, need 1: exact fit despite abundant garbage. */
		h.maxwords = 28;
		check(nvheapcollect(&h, &r, 13, 1) == 0, "repeated exact-fit collection");
		h.maxwords = 0;
		check(root[0] != old, "root did not move");
		check(h.words == 14 && h.cur->top == 14 && h.full == nil, "dead words reclaimed");
		check(h.cur->cap >= 2*(h.words+1), "live plus need sizing");
		check(nvtupleelem(root[0], 0) == root[1] && nvtupleelem(root[0], 1) == root[1], "sharing lost");
		check(nvtupleelem(root[0], 2) == root[2] && nvtuplelen(root[2]) == 0, "empty tuple forwarding");
		check(nvtermint(nvtupleelem(root[1], 0)) == (vlong)0x8000000000000000ULL, "boxed integer payload");
		check(nvrefincarnation(nvtupleelem(root[1], 1)) == opaque &&
		      nvrefcounter(nvtupleelem(root[1], 1)) == opaque, "opaque ref payload traced");
		check(nvtermint(root[3]) == -37 && root[4] == elem[3] &&
		      nvpidslot(root[5]) == 23 && nvpidgeneration(root[5]) == 42 &&
		      root[6] == NvNil, "immediate roots changed");
	}
	nvheapfree(&h);
	print("ok - GC preserves kinds, empty tuples and sharing; reclaims garbage repeatedly\n");
}

static void
fragments(void)
{
	NvHeap source, h;
	NvFrag *adopted, *queued, *dead;
	NvTerm v[2], message, root[3], before;
	NvRoot r;

	nvheapinit(&source, 0);
	nvheapinit(&h, 0);
	v[0] = nvint(nil, 41);
	v[0] = tuple(&source, v, 1);
	v[1] = tuple(&source, nil, 0);
	message = tuple(&source, v, 2);
	check(nvfragcopy(message, ~0ULL, &adopted) == 0, "adopted copy");
	check(nvfragcopy(message, ~0ULL, &queued) == 0, "queued copy");
	check(nvfragcopy(message, ~0ULL, &dead) == 0, "dead adopted copy");
	check(nvheapadopt(&h, adopted) == 0 && nvheapadopt(&h, dead) == 0, "fragment adoption");
	nvheapfree(&source); /* Message fragments must survive sender destruction. */
	root[0] = nvtupleelem(adopted->root, 0);
	root[1] = queued->root;
	root[2] = tuple(&h, root, 2);
	before = root[0];
	queued->next = queued; /* Cyclic host metadata must not be traversed. */
	r.word = root; r.nword = nelem(root); r.next = nil;
	check(nvheapcollect(&h, &r, 0, 0) == 0, "fragment collection");
	check(root[0] != before && nvtermint(nvtupleelem(root[0], 0)) == 41, "adopted subterm root");
	check(h.adopted == nil && h.words == 5, "adopted dead bodies retained");
	check(root[1] == queued->root && queued->next == queued, "queued fragment or metadata moved");
	check(nvtupleelem(root[2], 0) == root[0] && nvtupleelem(root[2], 1) == root[1], "mixed ownership fields");
	/* Taking the formerly queued fragment makes the same root movable. */
	before = root[1];
	check(nvheapadopt(&h, queued) == 0, "take queued fragment");
	check(nvheapcollect(&h, &r, 0, 0) == 0, "collection after take");
	check(root[1] != before && nvtupleelem(root[2], 1) == root[1], "take did not change classification");
	check(h.words == 11 && h.adopted == nil, "taken fragment accounting");
	check(nvtermint(nvtupleelem(nvtupleelem(root[1], 0), 0)) == 41, "taken content");
	nvheapfree(&h);
	print("ok - GC merges adopted fragments but leaves queued storage and host metadata alone\n");
}

static void
shapes(void)
{
	NvHeap h;
	NvRoot r;
	NvTerm root, *v, t;
	int i;
	ulong cap;

	nvheapinit(&h, 0);
	root = nvint(nil, 7);
	for(i = 0; i < 10000; i++)
		root = tuple(&h, &root, 1);
	r.word = &root; r.nword = 1; r.next = nil;
	check(nvheapcollect(&h, &r, 0, 0) == 0, "deep iterative collection");
	check(h.words == 20000, "deep live count");
	t = root;
	for(i = 0; i < 10000; i++){
		check(nvtuplelen(t) == 1, "deep shape");
		t = nvtupleelem(t, 0);
	}
	check(nvtermint(t) == 7, "deep leaf");
	v = malloc(10000*sizeof(NvTerm));
	check(v != nil, "wide scratch allocation");
	for(i = 0; i < 10000; i++)
		v[i] = root;
	root = tuple(&h, v, 10000);
	free(v);
	check(nvheapcollect(&h, &r, 0, 0) == 0, "wide sharing collection");
	check(h.words == 30001, "wide graph expanded sharing");
	for(i = 1; i < 10000; i++)
		check(nvtupleelem(root, i) == nvtupleelem(root, 0), "wide alias");
	cap = h.cur->cap;
	root = NvNil;
	check(nvheapcollect(&h, &r, 0, 0) == 0 && h.words == 0, "drop entire graph");
	check(h.cur->cap == cap, "space unexpectedly shrank");
	nvheapfree(&h);
	/* Exercise a legal zero-root view and an initially empty heap. */
	nvheapinit(&h, 0);
	check(nvheapcollect(&h, nil, 0, 33) == 0 && h.words == 0 && h.cur->cap == 128, "empty heap sizing");
	nvheapfree(&h);
	print("ok - GC traverses deep and wide shared graphs iteratively and retains space capacity\n");
}

static void
failures(void)
{
	NvHeap h;
	NvRoot r;
	NvFrag *f;
	NvTerm root[2], saved[2], fields[2], headers[2];
	NvChunk *chunk;
	uvlong words;

	nvheapinit(&h, 0);
	fields[0] = nvint(nil, 9);
	root[0] = tuple(&h, fields, 1);
	fields[0] = root[0]; fields[1] = root[0];
	root[1] = tuple(&h, fields, 2);
	check(nvfragcopy(root[0], ~0ULL, &f) == 0 && nvheapadopt(&h, f) == 0, "failure adopted setup");
	r.word = root; r.nword = 2; r.next = nil;
	saved[0] = root[0]; saved[1] = root[1];
	headers[0] = NvBoxptr(root[0])[0]; headers[1] = NvBoxptr(root[1])[0];
	chunk = h.cur; words = h.words;
	/* First root copies, second exceeds the live allowance: must roll back. */
	h.maxwords = 4;
	check(nvheapcollect(&h, &r, 0, 0) == NvTermlimit && h.exhausted, "live budget refusal");
	check(root[0] == saved[0] && root[1] == saved[1], "failure changed roots");
	check(NvBoxptr(root[0])[0] == headers[0] && NvBoxptr(root[1])[0] == headers[1], "failure left forwarding headers");
	check(h.cur == chunk && h.words == words && h.adopted == f, "failure changed ownership");
	check(nvtupleelem(root[1], 1) == root[0] && nvtermint(nvtupleelem(root[0], 0)) == 9, "failure corrupted object bodies");
	check(nvheapcollect(&h, &r, 5, 0) == NvTermlimit, "stack budget refusal");
	check(nvheapcollect(&h, &r, ~0ULL, 1) == NvTermlimit, "sum overflow refusal");
	h.maxwords = 0;
	check(nvheapcollect(&h, &r, ~0ULL-2, 0) == NvTermlimit, "live plus stack overflow refusal");
	check(NvBoxptr(root[0])[0] == headers[0], "overflow rollback");
	check(nvheapcollect(&h, &r, 0, ~0ULL) == NvTermlimit, "allocation byte-size overflow refusal");
	/* Malformed owned header after a valid copied root also rolls back. */
	NvBoxptr(root[1])[0] = NvHdr(Bref, 1);
	check(nvheapcollect(&h, &r, 0, 0) == NvTermerror && !h.exhausted, "malformed owned object");
	check(NvBoxptr(root[0])[0] == headers[0] && root[0] == saved[0], "malformed rollback");
	NvBoxptr(root[1])[0] = headers[1];
	h.maxwords = 12; /* live 5 + stack 4 + need 3 */
	check(nvheapcollect(&h, &r, 4, 3) == 0 && h.words == 5 && !h.exhausted, "retry after refusal");
	check(h.adopted == nil && nvtupleelem(root[1], 0) == root[0], "retry ownership and sharing");
	nvheapfree(&h);
	print("ok - GC budget, overflow and malformed-object failures preserve roots and ownership\n");
}

static void
frames(void)
{
	NvHeap h;
	NvModule m;
	NvFunc f[2];
	NvInsn code[4];
	NvConst k;
	NvExec e;
	NvTerm arg, saved[2][NvFramehdr], old, *regs;
	ulong fp[2], i, j;
	uvlong reductions;
	char err[128];

	nvheapinit(&h, 0);
	arg = tuple(&h, nil, 0);
	memset(&m, 0, sizeof m); memset(f, 0, sizeof f);
	memset(code, 0, sizeof code); memset(&k, 0, sizeof k);
	k.kind = Kfunc; k.text = "child";
	m.nconst = 1; m.konst = &k; m.nfunc = 2; m.func = f;
	f[0].name = "main"; f[0].nreg = 3; f[0].ninsn = 3; f[0].insn = code;
	code[0].op = Otuple; code[0].a = 1; code[0].b = 0; code[0].c = 1;
	code[1].op = Ocall; code[1].a = 2; code[1].b = 0; code[1].c = 1;
	code[2].op = Oreturn; code[2].a = 2;
	f[1].name = "child"; f[1].nreg = 2; f[1].ninsn = 1; f[1].insn = code+3;
	code[3].op = Oreturn; code[3].a = 0;
	check(nvexecinit(&e, &m, "main", arg, 0, nil, 0, err, sizeof err) == 0, "frame test init");
	check(nvexecrun(&e, 2) == NvYield && e.nframe == 2, "suspend with two frames");
	fp[0] = 0; fp[1] = e.fp;
	/* The root's unused return destination is metadata, even when its
	 * bits happen to be an owned object address. It must not be traced. */
	e.stack[NvFramedst] = tuple(&e.heap, nil, 0);
	for(i = 0; i < 2; i++)
		for(j = 0; j < NvFramehdr; j++)
			saved[i][j] = e.stack[fp[i]+j];
	old = e.stack[NvFramehdr+1];
	reductions = e.reductions;
	/* A boxed-looking word beyond sp is not an active register/root. */
	check(e.sp < e.nstack, "test stack slack");
	e.stack[e.sp] = tuple(&e.heap, nil, 0);
	e.heap.maxwords = e.nstack+2;
	check(nvexeccollect(&e, 0) == NvTermlimit, "frame capacity not charged");
	check(e.stack[NvFramehdr+1] == old, "failed frame collection changed root");
	e.heap.maxwords = e.nstack+3;
	check(nvexeccollect(&e, 0) == 0 && e.heap.words == 3, "frame collection or inactive slots");
	check(e.stack[NvFramehdr+1] != old && e.stack[e.fp+NvFramehdr] == e.stack[NvFramehdr+1], "caller/callee alias not updated");
	for(i = 0; i < 2; i++)
		for(j = 0; j < NvFramehdr; j++)
			check(e.stack[fp[i]+j] == saved[i][j], "frame metadata changed");
	check(e.reductions == reductions && e.state == NvYield && e.nframe == 2, "collection executed bytecode");
	check(nvexecrun(&e, 1) == NvYield && e.nframe == 1, "child return after move");
	regs = e.stack+NvFramehdr;
	check(regs[2] == regs[1], "return alias");
	/* Popped callee registers must not retain its former roots. */
	regs[0] = NvNil; regs[1] = NvNil; regs[2] = nvint(nil, 42);
	check(nvexeccollect(&e, 0) == 0 && e.heap.words == 0, "popped frame retained roots");
	check(nvexecrun(&e, 1) == NvDone && nvtermint(e.result->root) == 42, "resume after collection");
	check(nvexeccollect(&e, 0) == NvTermerror, "terminal exec accepted for collection");
	nvexecfree(&e);
	nvheapfree(&h);
	print("ok - GC scans every active frame, skips metadata and inactive slots, and resumes unchanged\n");
}

static void
guards(void)
{
	NvHeap h;
	NvModule m;
	NvFunc f;
	NvInsn code[9];
	NvConst k[2];
	NvExec e;
	NvTerm arg;
	uvlong reductions;
	int i, state, target;
	char err[128];

	nvheapinit(&h, 0);
	arg = tuple(&h, nil, 0);
	memset(&m, 0, sizeof m); memset(&f, 0, sizeof f);
	memset(code, 0, sizeof code); memset(k, 0, sizeof k);
	m.nfunc = 1; m.func = &f; m.nconst = 2; m.konst = k;
	f.name = "guarded"; f.nreg = 5; f.ninsn = nelem(code); f.insn = code;
	k[0].kind = Kint; k[0].ival = 0x7fffffffffffffffLL;
	k[1].kind = Kint; k[1].ival = 1;
	code[0].op = Oguard; code[0].a = 7;
	code[1].op = Oloadk; code[1].a = 1; code[1].b = 0;
	code[2].op = Otuple; code[2].a = 2; code[2].b = 1; code[2].c = 1;
	code[3].op = Oloadk; code[3].a = 3; code[3].b = 1;
	code[4].op = Oadd; code[4].a = 4; code[4].b = 1; code[4].c = 3;
	code[5].op = Oguardend;
	code[6].op = Oreturn; code[6].a = 2;
	code[7].op = Otuple; code[7].a = 2; code[7].b = 0; code[7].c = 1;
	code[8].op = Oreturn; code[8].a = 2;
	check(nvexecinit(&e, &m, "guarded", arg, 0, nil, 0, err, sizeof err) == 0, "guard init");
	state = NvYield;
	for(i = 0; i < 9 && state == NvYield; i++){
		target = e.guardfail;
		reductions = e.reductions;
		check(nvexeccollect(&e, 0) == 0 && e.guardfail == target && e.reductions == reductions, "collection changed guard state");
		state = nvexecrun(&e, 1);
		if(i < 4)
			check(e.guardfail == 7, "guard lost across collection/quantum");
		if(i == 4)
			check(e.guardfail == -1 && e.stack[e.fp+NvFramepc] == 7, "guard fault target after move");
	}
	check(state == NvDone && e.reductions == 7 && e.fault[0] == 0, "guard fault escaped or accounting changed");
	check(nvtuplelen(e.result->root) == 1 && nvtuplelen(nvtupleelem(e.result->root, 0)) == 0, "guard fallback result");
	nvexecfree(&e);
	nvheapfree(&h);
	print("ok - explicit GC at every instruction preserves guard state and fault transfer\n");
}

void
main(void)
{
	kinds();
	fragments();
	shapes();
	failures();
	frames();
	guards();
	print("all memory collector tests passed\n");
	exits(nil);
}
