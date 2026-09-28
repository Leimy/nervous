#include <u.h>
#include <libc.h>
#include <bio.h>

/*
 * benchcmp: parse captures of `rc bench/run.rc` output and print a table.
 * With one file: table of that run.
 * With two files: table of new.txt, each row followed by a delta line against old.txt.
 *
 * Counts (uvlong): spawned, peaklive, completed, faulted, exited, dispatches, reductions,
 *                  sent, dropped, collections, gcfailed, atoms.
 * Costs (double): wall, nspermsg, highwater, bytesperproc.
 *
 * A field absent in a run is marked "-" in output; costs absent in either run skip delta.
 */

enum {
	Maxbench = 32,
	Fwall = 1<<0,
	Fspawned = 1<<1,
	Fpeaklive = 1<<2,
	Fcompleted = 1<<3,
	Ffaulted = 1<<4,
	Fexited = 1<<5,
	Fdispatches = 1<<6,
	Freductions = 1<<7,
	Fsent = 1<<8,
	Fdropped = 1<<9,
	Fcollections = 1<<10,
	Fgcfailed = 1<<11,
	Fatoms = 1<<12,
	Fnspermsg = 1<<13,
	Fhighwater = 1<<14,
	Fbytesperproc = 1<<15,
};

typedef struct Bench Bench;

struct Bench {
	char shape[64];           /* "ring 10 200000", "ring-up 1000 2000", "sieve 5000" */
	int have;                 /* bitmask of fields present */
	uvlong spawned, peaklive, completed, faulted, exited, dispatches, reductions,
	       sent, dropped, collections, gcfailed, atoms;
	double wall, nspermsg, highwater, bytesperproc;
};

static int nextnum(char **pp, uvlong *v)
{
	char *p = *pp;
	char *end;

	while(*p && (*p < '0' || *p > '9'))
		p++;
	if(*p == 0)
		return 0;
	*v = strtoull(p, &end, 10);
	*pp = end;
	return 1;
}

static int nextdouble(char **pp, double *v)
{
	char *p = *pp;

	while(*p && (*p < '0' || *p > '9') && *p != '.' && *p != '-' && *p != '+')
		p++;
	if(*p == 0)
		return 0;
	*v = strtod(p, &p);
	*pp = p;
	return 1;
}

static void setshape(Bench *b, char *rest)
{
	char *p, *q;
	int i;

	/* rest is like "/usr/dave/work/nervous/examples/ring.nv main 10 200000" */
	p = strrchr(rest, '/');
	if(p == nil)
		p = rest;
	else
		p++;
	/* p now points to "ring.nv main 10 200000" */
	q = strchr(p, ' ');
	if(q == nil)
		q = p + strlen(p);
	/* copy basename up to first space or end, strip .nv */
	for(i = 0; i < 63 && p+i < q; i++){
		if(p[i] == '.' && p+i+2 < q && p[i+1] == 'n' && p[i+2] == 'v')
			break;
		b->shape[i] = p[i];
	}
	b->shape[i] = 0;
	/* append " " + args after "main" */
	if(q < p + strlen(p)){
		p = strstr(q, "main ");
		if(p != nil){
			p += 5;  /* skip "main " */
			while(*p == ' ')
				p++;
			q = b->shape + strlen(b->shape);
			if(q < b->shape + 63)
				*q++ = ' ';
			for(i = 0; i < 63 - (q - b->shape) && p[i]; i++)
				q[i] = p[i];
			q[i] = 0;
		}
	}
}

static void parsestat(Bench *b, char *s)
{
	char *p;
	uvlong tmp;

	p = s;

	if(strncmp(p, "wall ", 5) == 0){
		p += 5;
		if(nextdouble(&p, &b->wall))
			b->have |= Fwall;
		return;
	}
	if(strncmp(p, "processes ", 10) == 0){
		p += 10;
		if(nextnum(&p, &tmp)){ b->spawned = tmp; b->have |= Fspawned; }
		if(nextnum(&p, &tmp)){ b->peaklive = tmp; b->have |= Fpeaklive; }
		if(nextnum(&p, &tmp)){ b->completed = tmp; b->have |= Fcompleted; }
		if(nextnum(&p, &tmp)){ b->faulted = tmp; b->have |= Ffaulted; }
		if(nextnum(&p, &tmp)){ b->exited = tmp; b->have |= Fexited; }
		return;
	}
	if(strncmp(p, "dispatches ", 11) == 0){
		p += 11;
		if(nextnum(&p, &tmp)){ b->dispatches = tmp; b->have |= Fdispatches; }
		if(nextnum(&p, &tmp)){ b->reductions = tmp; b->have |= Freductions; }
		return;
	}
	if(strncmp(p, "messages ", 9) == 0){
		p += 9;
		if(nextnum(&p, &tmp)){ b->sent = tmp; b->have |= Fsent; }
		if(nextnum(&p, &tmp)){ b->dropped = tmp; b->have |= Fdropped; }
		if(nextnum(&p, &tmp)){ b->nspermsg = tmp; b->have |= Fnspermsg; }
		return;
	}
	if(strncmp(p, "host allocation ", 16) == 0){
		p += 16;
		if(nextnum(&p, &tmp)){ b->highwater = tmp; b->have |= Fhighwater; }
		if(nextnum(&p, &tmp)){ b->bytesperproc = tmp; b->have |= Fbytesperproc; }
		return;
	}
	if(strncmp(p, "GC ", 3) == 0){
		p += 3;
		if(nextnum(&p, &tmp)){ b->collections = tmp; b->have |= Fcollections; }
		if(nextnum(&p, &tmp)){ b->gcfailed = tmp; b->have |= Fgcfailed; }
		return;
	}
	if(strncmp(p, "atoms ", 6) == 0){
		p += 6;
		if(nextnum(&p, &tmp)){ b->atoms = tmp; b->have |= Fatoms; }
		return;
	}
}

static int
readrun(char *file, Bench *b, int max)
{
	Biobuf *bp;
	char *line;
	int n;

	bp = Bopen(file, OREAD);
	if(bp == nil)
		sysfatal("open %s: %r", file);
	n = 0;
	while((line = Brdstr(bp, '\n', 1)) != nil){
		if(strncmp(line, "== ", 3) == 0){
			if(n < max){
				memset(&b[n], 0, sizeof b[n]);
				setshape(&b[n], line+3);
				n++;
			}
		}else if(n > 0 && strncmp(line, "stats: ", 7) == 0)
			parsestat(&b[n-1], line+7);
		free(line);
	}
	Bterm(bp);
	return n;
}

static void usage(void)
{
	fprint(2, "usage: benchcmp [-t pct] new.txt\n");
	fprint(2, "       benchcmp [-t pct] old.txt new.txt\n");
	exits("usage");
}

static void
printrow(Bench *b)
{
	uvlong redperdisp;

	print("%-22s ", b->shape);
	if(b->have & Fsent)
		print("%9llud ", b->sent);
	else
		print("%9s ", "-");
	if(b->have & Fwall)
		print("%8.3f ", b->wall);
	else
		print("%8s ", "-");
	if(b->have & Fnspermsg)
		print("%7.0f ", b->nspermsg);
	else
		print("%7s ", "-");
	if((b->have & (Fdispatches|Freductions)) == (Fdispatches|Freductions) && b->dispatches > 0){
		redperdisp = b->reductions / b->dispatches;
		print("%9llud ", redperdisp);
	}else{
		print("%9s ", "-");
	}
	if(b->have & Fcollections)
		print("%11llud ", b->collections);
	else
		print("%11s ", "-");
	if(b->have & Fhighwater)
		print("%10.0f ", b->highwater);
	else
		print("%10s ", "-");
	if(b->have & Fbytesperproc)
		print("%10.0f", b->bytesperproc);
	else
		print("%10s", "-");
	print("\n");
}

/*
 * One cost item: "name old->new (+N.N%)", with " !" when the change
 * reaches the threshold.  prec is the %f precision (3 for wall, 0 for
 * the byte and nanosecond figures).  Returns 1 if flagged.
 */
static int
cost(char *name, double old, double new, int prec, double pctthresh)
{
	double pct;
	int flag;

	if(old == 0)
		return 0;
	pct = (new - old) / old * 100.0;
	flag = pct >= pctthresh || pct <= -pctthresh;
	print("%s %.*f->%.*f (%s%.1f%%)%s  ", name, prec, old, prec, new,
		pct >= 0 ? "+" : "", pct, flag ? " !" : "");
	return flag;
}

static void
deltarow(Bench *old, Bench *new, double pctthresh, int *pcountchanged, int *pflagged)
{
	int flag, countsdiff;
	int both;

	both = old->have & new->have;
	flag = 0;
	print("    ");
	if(both & Fwall)
		flag += cost("wall", old->wall, new->wall, 3, pctthresh);
	if(both & Fnspermsg)
		flag += cost("ns/msg", old->nspermsg, new->nspermsg, 0, pctthresh);
	if(both & Fhighwater)
		flag += cost("highwater", old->highwater, new->highwater, 0, pctthresh);
	if(both & Fbytesperproc)
		flag += cost("bytes/proc", old->bytesperproc, new->bytesperproc, 0, pctthresh);

	/* check all 12 counts that are present in both */
	countsdiff = 0;
	if((old->have & new->have & Fspawned) && old->spawned != new->spawned) countsdiff = 1;
	if((old->have & new->have & Fpeaklive) && old->peaklive != new->peaklive) countsdiff = 1;
	if((old->have & new->have & Fcompleted) && old->completed != new->completed) countsdiff = 1;
	if((old->have & new->have & Ffaulted) && old->faulted != new->faulted) countsdiff = 1;
	if((old->have & new->have & Fexited) && old->exited != new->exited) countsdiff = 1;
	if((old->have & new->have & Fdispatches) && old->dispatches != new->dispatches) countsdiff = 1;
	if((old->have & new->have & Freductions) && old->reductions != new->reductions) countsdiff = 1;
	if((old->have & new->have & Fsent) && old->sent != new->sent) countsdiff = 1;
	if((old->have & new->have & Fdropped) && old->dropped != new->dropped) countsdiff = 1;
	if((old->have & new->have & Fcollections) && old->collections != new->collections) countsdiff = 1;
	if((old->have & new->have & Fgcfailed) && old->gcfailed != new->gcfailed) countsdiff = 1;
	if((old->have & new->have & Fatoms) && old->atoms != new->atoms) countsdiff = 1;

	if(countsdiff){
		print("counts: CHANGED");
		(*pcountchanged)++;
		if((old->have & new->have & Fspawned) && old->spawned != new->spawned)
			print(" spawned %llud->%llud", old->spawned, new->spawned);
		if((old->have & new->have & Fpeaklive) && old->peaklive != new->peaklive)
			print(" peaklive %llud->%llud", old->peaklive, new->peaklive);
		if((old->have & new->have & Fcompleted) && old->completed != new->completed)
			print(" completed %llud->%llud", old->completed, new->completed);
		if((old->have & new->have & Ffaulted) && old->faulted != new->faulted)
			print(" faulted %llud->%llud", old->faulted, new->faulted);
		if((old->have & new->have & Fexited) && old->exited != new->exited)
			print(" exited %llud->%llud", old->exited, new->exited);
		if((old->have & new->have & Fdispatches) && old->dispatches != new->dispatches)
			print(" dispatches %llud->%llud", old->dispatches, new->dispatches);
		if((old->have & new->have & Freductions) && old->reductions != new->reductions)
			print(" reductions %llud->%llud", old->reductions, new->reductions);
		if((old->have & new->have & Fsent) && old->sent != new->sent)
			print(" sent %llud->%llud", old->sent, new->sent);
		if((old->have & new->have & Fdropped) && old->dropped != new->dropped)
			print(" dropped %llud->%llud", old->dropped, new->dropped);
		if((old->have & new->have & Fcollections) && old->collections != new->collections)
			print(" collections %llud->%llud", old->collections, new->collections);
		if((old->have & new->have & Fgcfailed) && old->gcfailed != new->gcfailed)
			print(" gcfailed %llud->%llud", old->gcfailed, new->gcfailed);
		if((old->have & new->have & Fatoms) && old->atoms != new->atoms)
			print(" atoms %llud->%llud", old->atoms, new->atoms);
		print("\n");
	}else{
		print("counts: same\n");
	}

	*pflagged += flag;
}

static int
findshape(Bench *b, int n, char *shape)
{
	int i;

	for(i = 0; i < n; i++)
		if(strcmp(b[i].shape, shape) == 0)
			return i;
	return -1;
}

void
main(int argc, char **argv)
{
	double pctthresh;
	int i, nold, nnew, ncomp, ncountch, nflagged;
	int found;
	char *oldfile, *newfile;
	static Bench oldrun[Maxbench], newrun[Maxbench];

	pctthresh = 10.0;
	ARGBEGIN{
	case 't':
		pctthresh = atof(EARGF(usage()));
		if(pctthresh < 0)
			pctthresh = -pctthresh;
		break;
	default:
		usage();
	}ARGEND;

	if(argc != 1 && argc != 2)
		usage();

	if(argc == 1){
		newfile = argv[0];
		nnew = readrun(newfile, newrun, Maxbench);
		print("%-22s %9s %8s %7s %9s %11s %10s %10s\n",
			"shape", "messages", "wall(s)", "ns/msg", "red/disp", "collections", "highwater", "bytes/proc");
		for(i = 0; i < nnew; i++)
			printrow(&newrun[i]);
	}else{
		oldfile = argv[0];
		newfile = argv[1];
		nold = readrun(oldfile, oldrun, Maxbench);
		nnew = readrun(newfile, newrun, Maxbench);

		print("%-22s %9s %8s %7s %9s %11s %10s %10s\n",
			"shape", "messages", "wall(s)", "ns/msg", "red/disp", "collections", "highwater", "bytes/proc");
		ncomp = 0;
		ncountch = 0;
		nflagged = 0;
		for(i = 0; i < nnew; i++){
			printrow(&newrun[i]);
			found = findshape(oldrun, nold, newrun[i].shape);
			if(found >= 0){
				deltarow(&oldrun[found], &newrun[i], pctthresh, &ncountch, &nflagged);
				ncomp++;
			}else{
				print("    (no match in old)\n");
			}
		}
		/* shapes only in old */
		for(i = 0; i < nold; i++){
			found = findshape(newrun, nnew, oldrun[i].shape);
			if(found < 0){
				print("    (only in old) %s\n", oldrun[i].shape);
			}
		}
		print("%d shapes compared, %d count changes, %d cost flags\n", ncomp, ncountch, nflagged);
		if(ncountch > 0)
			exits("counts");
	}
	exits(nil);
}
