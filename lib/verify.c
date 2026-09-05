#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../include/nvbc.h"
#include "../include/nvvm.h"

enum {
	Nword = (NvMaxreg+31)/32,
};

/* D060: istype's immediate kind operand must name a real term kind. */
static int
kindok(int k)
{
	return k == Vint || k == Vatom || k == Vtuple || k == Vpid || k == Vref;
}

static int
bad(char *err, int nerr, NvFunc *f, int pc, char *s)
{
	snprint(err, nerr, "%s:%d: %s", f->name, pc, s);
	return -1;
}

static int
regok(NvFunc *f, int r)
{
	return r >= 0 && r < f->nreg;
}

static int
constok(NvModule *m, int k, int kind)
{
	return k >= 0 && k < m->nconst && m->konst[k].kind == kind;
}

static int
targetok(NvFunc *f, int pc)
{
	return pc >= 0 && pc < f->ninsn;
}

/* Verify portable operands before execution reaches host operations. */
static int
verifyinsn(NvModule *m, NvFunc *f, int pc, NvInsn *i, char *err, int nerr)
{
	if(i->op < 0 || i->op >= Nopcode)
		return bad(err, nerr, f, pc, "invalid opcode");
	switch(i->op){
	case Oloadk:
		if(!regok(f, i->a) || i->b < 0 || i->b >= m->nconst) return bad(err,nerr,f,pc,"bad loadk operand");
		break;
	case Omove:
		if(!regok(f, i->a) || !regok(f, i->b)) return bad(err,nerr,f,pc,"bad move register");
		break;
	case Otuple:
		if(!regok(f, i->a) || i->b < 0 || i->b > f->nreg ||
		   i->c < 0 || i->c > f->nreg-i->b)
			return bad(err,nerr,f,pc,"bad tuple range");
		break;
	case Ojump:
		if(!targetok(f, i->a)) return bad(err,nerr,f,pc,"bad jump target");
		break;
	case Ocall:
		if(!regok(f,i->a) || !constok(m,i->b,Kfunc) || !regok(f,i->c)) return bad(err,nerr,f,pc,"bad call operand");
		break;
	case Otailcall:
		if(!constok(m,i->a,Kfunc) || !regok(f,i->b)) return bad(err,nerr,f,pc,"bad tailcall operand");
		break;
	case Oreturn:
		if(!regok(f,i->a)) return bad(err,nerr,f,pc,"bad return register");
		break;
	case Otestatom:
		if(!regok(f,i->a) || !constok(m,i->b,Katom) || !targetok(f,i->c)) return bad(err,nerr,f,pc,"bad testatom operand");
		break;
	case Otestint:
		if(!regok(f,i->a) || !constok(m,i->b,Kint) || !targetok(f,i->c)) return bad(err,nerr,f,pc,"bad testint operand");
		break;
	case Otesteq:
		if(!regok(f,i->a) || !regok(f,i->b) || !targetok(f,i->c)) return bad(err,nerr,f,pc,"bad testeq operand");
		break;
	case Otestarity:
		if(!regok(f,i->a) || i->b < 0 || !targetok(f,i->c)) return bad(err,nerr,f,pc,"bad testarity operand");
		break;
	case Ogetelem:
		if(!regok(f,i->a) || !regok(f,i->b) || i->c < 0) return bad(err,nerr,f,pc,"bad getelem operand");
		break;
	case Oadd: case Osub: case Omul: case Odiv: case Orem:
	case Olt: case Ole: case Ogt: case Oge:
		if(!regok(f,i->a) || !regok(f,i->b) || !regok(f,i->c)) return bad(err,nerr,f,pc,"bad integer operation register");
		break;
	case Ofail:
		if(i->a < 0 || i->a >= m->nconst) return bad(err,nerr,f,pc,"bad fail constant");
		break;
	case Oself: case Omakeref:
		if(!regok(f,i->a)) return bad(err,nerr,f,pc,"bad process destination");
		break;
	case Osend:
		if(!regok(f,i->a) || !regok(f,i->b) || !regok(f,i->c)) return bad(err,nerr,f,pc,"bad send register");
		break;
	case Ospawn:
		if(!regok(f,i->a) || !constok(m,i->b,Kfunc) || !regok(f,i->c)) return bad(err,nerr,f,pc,"bad spawn operand");
		break;
	case Orecvbegin: case Orecvnext:
		if(!regok(f,i->a) || !regok(f,i->b) || i->a == i->b) return bad(err,nerr,f,pc,"bad receive destinations");
		break;
	case Orecvtake:
		break;
	case Orecvwait:
		if(!targetok(f,i->a)) return bad(err,nerr,f,pc,"bad receive retry target");
		break;
	case Orecvdeadline:
		if(!regok(f,i->a)) return bad(err,nerr,f,pc,"bad recvdeadline duration register");
		break;
	case Orecvwaitdeadline:
		if(!targetok(f,i->a)) return bad(err,nerr,f,pc,"bad receive retry target");
		break;
	case Oexit:
		if(!regok(f,i->a)) return bad(err,nerr,f,pc,"bad exit register");
		break;
	case Oprint: case Oeprint:
		if(!regok(f,i->a) || !regok(f,i->b))
			return bad(err,nerr,f,pc,i->op==Oprint?"bad print register":"bad eprint register");
		break;
	case Oguard:
		if(!targetok(f,i->a)) return bad(err,nerr,f,pc,"bad guard fail target");
		break;
	case Oguardend:
		break;
	case Oistype:
		if(!regok(f,i->a) || !regok(f,i->b) || !kindok(i->c)) return bad(err,nerr,f,pc,"bad istype operand");
		break;
	case Onop:
		break;
	}
	return 0;
}

/* D071: guards cannot call, leave the frame, or perform host operations. */
static int
guardop(int op)
{
	switch(op){
	case Oloadk: case Omove: case Otuple: case Ojump:
	case Otestatom: case Otestint: case Otesteq: case Otestarity:
	case Ogetelem: case Oadd: case Osub: case Omul: case Odiv: case Orem:
	case Olt: case Ole: case Ogt: case Oge:
	case Ofail: case Onop: case Oistype: case Oguardend:
		return 1;
	}
	return 0;
}

static int
guardedge(NvFunc *f, int pc, int to, int active, int *region, char *err, int nerr)
{
	/* verifyinsn has already checked every explicit target's range. */
	if(region[to] != active)
		return bad(err, nerr, f, pc, "control flow crosses guard boundary");
	return 0;
}

/*
 * D071 / post-R2-F01: guardfail is execution-wide, not a frame field.
 * Establish that every entry to an instruction has the guard state its
 * lexical region describes, and that no frame or host operation runs
 * with guard mode active. The map records state BEFORE the instruction:
 * -1 outside, otherwise the pc of the owning guard. Thus the guard itself
 * is outside, while its guardend still belongs to the guarded region.
 *
 * Check all instructions, not only reachable ones, just as verifyinsn
 * does. A normal edge must preserve the region, except the fallthroughs
 * of guard and guardend. A fault clears guard mode, so a guard's failure
 * edge must lead outside. Entry pc 0 is always outside. Together these
 * rules prove guard state by induction over execution, including loops.
 */
static int
verifyguards(NvFunc *f, char *err, int nerr)
{
	int *region;
	int pc, active, out, fall;
	NvInsn *i;
	char msg[80];

	region = malloc(f->ninsn*sizeof *region);
	if(region == nil){
		snprint(err, nerr, "%s: out of memory", f->name);
		return -1;
	}
	active = -1;
	for(pc = 0; pc < f->ninsn; pc++){
		i = &f->insn[pc];
		region[pc] = active;
		if(i->op == Oguard){
			if(active >= 0){
				bad(err, nerr, f, pc, "nested guard");
				goto fail;
			}
			active = pc;
		}else if(i->op == Oguardend){
			if(active < 0){
				bad(err, nerr, f, pc, "guardend without guard");
				goto fail;
			}
			active = -1;
		}else if(active >= 0 && !guardop(i->op)){
			snprint(msg, sizeof msg, "%s not allowed in guard", nvopname(i->op));
			bad(err, nerr, f, pc, msg);
			goto fail;
		}
	}
	if(active >= 0){
		bad(err, nerr, f, active, "guard without guardend");
		goto fail;
	}
	for(pc = 0; pc < f->ninsn; pc++){
		i = &f->insn[pc];
		out = region[pc];
		fall = 1;
		switch(i->op){
		case Oguard:
			if(region[i->a] != -1){
				bad(err, nerr, f, pc, "guard failure target inside guard");
				goto fail;
			}
			out = pc;
			break;
		case Oguardend:
			out = -1;
			break;
		case Ojump: case Orecvwait: case Orecvwaitdeadline:
			if(guardedge(f, pc, i->a, out, region, err, nerr) < 0)
				goto fail;
			fall = i->op == Orecvwaitdeadline;
			break;
		case Otestatom: case Otestint: case Otesteq: case Otestarity:
			if(guardedge(f, pc, i->c, out, region, err, nerr) < 0)
				goto fail;
			break;
		case Otailcall: case Oreturn: case Ofail: case Oexit:
			fall = 0;
			break;
		}
		if(fall && pc+1 < f->ninsn &&
		   guardedge(f, pc, pc+1, out, region, err, nerr) < 0)
			goto fail;
	}
	free(region);
	return 0;
fail:
	free(region);
	return -1;
}

static void
setreg(ulong *s, int r)
{
	s[r/32] |= 1UL<<(r%32);
}

static int
hasreg(ulong *s, int r)
{
	return (s[r/32] & 1UL<<(r%32)) != 0;
}

static int
readreg(NvFunc *f, int pc, ulong *s, int r, char *err, int nerr)
{
	char b[64];

	if(hasreg(s, r))
		return 0;
	snprint(b, sizeof b, "register %d may be uninitialized", r);
	return bad(err, nerr, f, pc, b);
}

static int
checkreads(NvFunc *f, int pc, NvInsn *i, ulong *s, char *err, int nerr)
{
	int j;

	switch(i->op){
	case Omove:
		return readreg(f, pc, s, i->b, err, nerr);
	case Otuple:
		for(j = 0; j < i->c; j++)
			if(readreg(f, pc, s, i->b+j, err, nerr) < 0)
				return -1;
		break;
	case Ocall:
		return readreg(f, pc, s, i->c, err, nerr);
	case Otailcall:
		return readreg(f, pc, s, i->b, err, nerr);
	case Oreturn: case Otestatom: case Otestint: case Otestarity: case Oexit:
		return readreg(f, pc, s, i->a, err, nerr);
	case Otesteq:
		if(readreg(f, pc, s, i->a, err, nerr) < 0)
			return -1;
		return readreg(f, pc, s, i->b, err, nerr);
	case Ogetelem: case Oistype:
		return readreg(f, pc, s, i->b, err, nerr);
	case Oadd: case Osub: case Omul: case Odiv: case Orem:
	case Olt: case Ole: case Ogt: case Oge:
	case Osend:
		if(readreg(f, pc, s, i->b, err, nerr) < 0)
			return -1;
		return readreg(f, pc, s, i->c, err, nerr);
	case Ospawn:
		return readreg(f, pc, s, i->c, err, nerr);
	case Orecvdeadline:
		return readreg(f, pc, s, i->a, err, nerr);
	case Oprint: case Oeprint:
		return readreg(f, pc, s, i->b, err, nerr);
	}
	return 0;
}

static void
transfer(NvInsn *i, ulong *s)
{
	switch(i->op){
	case Oloadk: case Omove: case Otuple: case Ocall: case Ogetelem:
	case Oadd: case Osub: case Omul: case Odiv: case Orem:
	case Olt: case Ole: case Ogt: case Oge:
	case Oself: case Omakeref: case Osend: case Ospawn: case Oprint: case Oeprint:
	case Oistype:
		setreg(s, i->a);
		break;
	case Orecvbegin: case Orecvnext:
		setreg(s, i->a);
		setreg(s, i->b);
		break;
	}
}

static int
merge(ulong *dst, ulong *src, int first)
{
	ulong x;
	int i, changed;

	changed = 0;
	for(i = 0; i < Nword; i++){
		x = first ? src[i] : dst[i] & src[i];
		if(dst[i] != x){
			dst[i] = x;
			changed = 1;
		}
	}
	return changed;
}

static void
edge(NvFunc *f, int to, ulong *out, ulong *in, uchar *seen, int *queued, int *queue, int *qt, int *nq)
{
	int changed;

	changed = merge(in+to*Nword, out, !seen[to]);
	if(!seen[to] || changed){
		seen[to] = 1;
		if(!queued[to]){
			queue[*qt] = to;
			*qt = (*qt+1) % f->ninsn;
			(*nq)++;
			queued[to] = 1;
		}
	}
}

static int
verifyflow(NvFunc *f, char *err, int nerr)
{
	ulong *in, out[Nword];
	uchar *seen;
	int *queue, *queued;
	NvInsn *i;
	int qh, qt, nq, pc, j, fall;

	in = mallocz(f->ninsn*Nword*sizeof *in, 1);
	seen = mallocz(f->ninsn, 1);
	queue = malloc(f->ninsn*sizeof *queue);
	queued = mallocz(f->ninsn*sizeof *queued, 1);
	if(in == nil || seen == nil || queue == nil || queued == nil){
		free(in); free(seen); free(queue); free(queued);
		snprint(err, nerr, "%s: out of memory", f->name);
		return -1;
	}
	qh = 0;
	qt = f->ninsn == 1 ? 0 : 1;
	nq = 1;
	queue[0] = 0;
	queued[0] = 1;
	seen[0] = 1;
	setreg(in, 0);
	while(nq > 0){
		pc = queue[qh];
		qh = (qh+1) % f->ninsn;
		nq--;
		queued[pc] = 0;
		memmove(out, in+pc*Nword, sizeof out);
		i = &f->insn[pc];
		transfer(i, out);
		fall = 1;
		switch(i->op){
		case Ojump:
			edge(f, i->a, out, in, seen, queued, queue, &qt, &nq);
			fall = 0;
			break;
		case Otestatom: case Otestint: case Otesteq: case Otestarity:
			edge(f, i->c, out, in, seen, queued, queue, &qt, &nq);
			break;
		case Oguard:
			/*
			 * D060: any instruction up to the matching guardend may
			 * transfer to the fail target on a fault. One edge from here
			 * suffices for definite initialization: registers are only
			 * ever set, so the set live at this guard is a subset of the
			 * set live at every instruction the fault could come from,
			 * and the merge at the target is an intersection.
			 */
			edge(f, i->a, out, in, seen, queued, queue, &qt, &nq);
			break;
		case Orecvwait:
			edge(f, i->a, out, in, seen, queued, queue, &qt, &nq);
			fall = 0;
			break;
		case Orecvwaitdeadline:
			/*
			 * D051: two successors, unlike recvwait. Blocking (or an
			 * exhausted-scan race rescan) resumes at the retry target;
			 * an expired deadline falls through into the timeout body.
			 * Leaving fall at 1 lets the ordinary fallthrough edge below
			 * cover that second successor, so the reachable-fallthrough
			 * check and definite-initialization merge both see it.
			 */
			edge(f, i->a, out, in, seen, queued, queue, &qt, &nq);
			break;
		case Otailcall: case Oreturn: case Ofail: case Oexit:
			fall = 0;
			break;
		}
		if(fall){
			j = pc+1;
			if(j == f->ninsn){
				free(in); free(seen); free(queue); free(queued);
				return bad(err, nerr, f, pc, "reachable fallthrough at end");
			}
			edge(f, j, out, in, seen, queued, queue, &qt, &nq);
		}
	}
	for(pc = 0; pc < f->ninsn; pc++)
		if(seen[pc] && checkreads(f, pc, &f->insn[pc], in+pc*Nword, err, nerr) < 0){
			free(in); free(seen); free(queue); free(queued);
			return -1;
		}
	free(in); free(seen); free(queue); free(queued);
	return 0;
}

int
nvverify(NvModule *m, char *err, int nerr)
{
	NvFunc *f;
	int i, j;

	if(m == nil){ snprint(err,nerr,"nil module"); return -1; }
	if(m->nconst < 0 || m->nconst > NvMaxitem || m->nfunc < 0 || m->nfunc > NvMaxitem){ snprint(err,nerr,"module limit exceeded"); return -1; }
	for(i = 0; i < m->nfunc; i++){
		f = &m->func[i];
		if(f->name == nil || f->nreg < 1 || f->nreg > NvMaxreg || f->ninsn < 1 || f->ninsn > NvMaxitem){ snprint(err,nerr,"bad function header %d",i); return -1; }
		for(j = 0; j < f->ninsn; j++)
			if(verifyinsn(m, f, j, &f->insn[j], err, nerr) < 0)
				return -1;
		if(verifyguards(f, err, nerr) < 0 || verifyflow(f, err, nerr) < 0)
			return -1;
	}
	return 0;
}
