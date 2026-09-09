#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../../include/nervous.h"
#include "../../include/nvbc.h"
#include "../../include/nvvm.h"
#include "../../include/nvcompile.h"
#include "../../include/nvexec.h"
#include "../../include/nvproc.h"
#include "../../include/nvsched.h"

static char *version = "nervous frontend 4";

static void
usage(void)
{
	fprint(2, "usage: nervous [-sG] [-H heapwords] [-o words] [-a file | -A v2file | -b bytecode | -c source | -f file | -F v2file | -r source entry [args...] | -x bytecode entry [args...] | -t bytecode entry [args...] | -X bytecode entry [args...]]\n");
	exits("usage");
}

/*
 * -s: scheduler statistics on stderr after a -r run, whatever its outcome.
 * Wall time is measured from just before scheduler creation to the report;
 * the heap figure is the break's growth over the same interval, which is a
 * high-water mark because Plan 9 malloc never lowers the break. Bytes per
 * peak live process is a rough per-process footprint under this load, not
 * an exact accounting (D066 gives exact word accounting per process, but
 * this line still reports host bytes across the whole run, not per-process
 * heap words); it includes mailbox traffic in flight at the peak.
 */
static void
printstats(NvScheduler *s, vlong start, uintptr brk0)
{
	vlong ns;
	uintptr heap;
	uvlong hops;

	ns = nsec() - start;
	heap = (uintptr)sbrk(0) - brk0;
	hops = s->runtime.nsent;
	fprint(2, "stats: wall %lld.%03llds\n", ns/1000000000LL, (ns%1000000000LL)/1000000LL);
	fprint(2, "stats: processes %llud spawned, %lud peak live, %llud completed, %llud faulted, %llud exited\n",
		s->runtime.nspawned, s->runtime.maxlive, s->completed, s->faulted, s->exited);
	fprint(2, "stats: dispatches %llud, reductions %llud (%llud per dispatch), timer wakes %llud\n",
		s->dispatches, s->reductions, s->dispatches ? s->reductions/s->dispatches : 0, s->timerwakes);
	fprint(2, "stats: messages %llud sent, %llud dropped to dead pids", hops, s->runtime.ndropped);
	if(hops != 0 && ns > 0)
		fprint(2, ", %llud ns per message", (uvlong)ns/hops);
	fprint(2, "\n");
	fprint(2, "stats: host allocation %llud bytes high-water", (uvlong)heap);
	if(s->runtime.maxlive != 0)
		fprint(2, ", %llud per peak live process", (uvlong)heap/s->runtime.maxlive);
	fprint(2, "\n");
	fprint(2, "stats: GC %llud collections, %llud failed; per-process live heap %llud words last, %llud words largest collected\n",
		s->collections, s->gcfailed, s->lastlivewords, s->maxlivewords);
	fprint(2, "stats: atoms %lud interned\n", nvatomcount());
}

static char *
readall(char *name, long *np)
{
	int fd;
	Dir *d;
	char *s;
	long n, m;

	fd = open(name, OREAD);
	if(fd < 0)
		sysfatal("open %s: %r", name);
	d = dirfstat(fd);
	if(d == nil)
		sysfatal("stat %s: %r", name);
	n = d->length;
	free(d);
	s = malloc(n+1);
	if(s == nil)
		sysfatal("out of memory");
	m = readn(fd, s, n);
	close(fd);
	if(m != n)
		sysfatal("read %s: %r", name);
	s[n] = 0;
	*np = n;
	return s;
}

/*
 * Strict decimal parsing with overflow detection over the full signed
 * 64-bit range, including the minimum. This reimplements what the old
 * NvValue-era nvvalueint did as string parsing; nvint (nvvm.h) now owns
 * deciding whether the parsed value fits a small term or must be boxed.
 */
static int
parseint(char *s, vlong *out)
{
	uvlong n, lim;
	int neg;
	uchar c;

	neg = *s == '-';
	if(neg)
		s++;
	if(*s == 0 || *s == '+')
		return -1;
	lim = neg ? (1ULL<<63) : (1ULL<<63)-1;
	n = 0;
	while((c = *s++) != 0){
		if(c < '0' || c > '9' || n > (lim-(c-'0'))/10)
			return -1;
		n = n*10 + c-'0';
	}
	if(neg && n == (1ULL<<63))
		*out = (vlong)(1ULL<<63);
	else
		*out = neg ? -(vlong)n : (vlong)n;
	return 0;
}

/*
 * Builds the CLI argument tuple in the host-owned heap h: each argv item
 * is nvatom(name) (quoted-atom convention unchanged) or a parsed integer
 * boxed by nvint. The tuple itself, and any boxed integers among its
 * elements, live in h; the caller frees h once the callee has copied the
 * tuple out (nvexecinit / spawn), or at exit.
 */
static int
makeargs(NvHeap *h, int argc, char **argv, NvTerm *args, char *err, int nerr)
{
	NvTerm *av;
	vlong v;
	int i;

	av = nil;
	if(argc != 0){
		av = mallocz(argc*sizeof *av, 1);
		if(av == nil){
			snprint(err, nerr, "out of memory");
			return -1;
		}
	}
	for(i = 0; i < argc; i++){
		if(argv[i][0] == '\'' && argv[i][1] != 0){
			av[i] = nvatom(argv[i]+1);
			if(av[i] == NvNil){
				snprint(err, nerr, "out of memory");
				free(av);
				return -1;
			}
		}else if(parseint(argv[i], &v) == 0){
			av[i] = nvint(h, v);
			if(av[i] == NvNil){
				snprint(err, nerr, "out of memory");
				free(av);
				return -1;
			}
		}else{
			snprint(err, nerr, "bad argument %s: expected integer or atom", argv[i]);
			free(av);
			return -1;
		}
	}
	*args = nvtuple(h, av, argc);
	free(av);
	if(*args == NvNil){
		snprint(err, nerr, "out of memory");
		return -1;
	}
	return 0;
}

/* Prints a root/execute result fragment's root term, or "<none>" if there is none. */
static void
printroot(Biobuf *b, NvFrag *f)
{
	if(f == nil)
		Bprint(b, "<none>");
	else
		nvtermprint(b, f->root);
}

/*
 * Runs an already-verified module under the full process scheduler,
 * exactly as -r does once nvcompile has produced a module: argv[0] is
 * the entry function, argv[1:] its argument tuple, matching -r's own
 * convention. Shared by -r (module built from source by nvcompile) and
 * -X (module loaded from a saved bytecode file by nvread/nvverify), so
 * a bytecode artifact produced by -c and executed by -X gets the same
 * host callback table, IO streams and statistics as -r, unlike -x/-t
 * (lib/vm.c's nvexecutelimits), which install no host at all and so
 * cannot run any program that spawns, sends, receives or does I/O.
 * Always exits(); takes ownership of m (frees it on every path) and
 * never returns.
 */
static void
runscheduled(NvModule *m, int argc, char **argv, uvlong heaplimit, int gcstress, uvlong gcoffload, int stats)
{
	Biobuf bout, berr;
	NvHeap h;
	NvTerm args, rootpid;
	NvLimits limits;
	NvScheduler sched;
	NvIO io;
	char err[256], *entry;
	int rc, state;
	vlong start;
	uintptr brk0;

	start = 0;
	brk0 = 0;
	entry = argv[0];
	nvheapinit(&h, 0);
	Binit(&bout, 1, OWRITE);
	if(makeargs(&h, argc-1, argv+1, &args, err, sizeof err) < 0){
		nvheapfree(&h);
		Bterm(&bout);
		fprint(2, "%s\n", err);
		nvmodulefree(m);
		exits("argument");
	}
	/* See D066/D074 commentary in main() for these constants. */
	limits.maxprocess = 65536;
	limits.maxmailbox = 2*1024*1024;
	limits.maxmessage = 128*1024;
	limits.maxheap = heaplimit;
	limits.gcstress = gcstress;
	limits.maxframe = 1024;
	limits.maxtermdepth = NvMaxtermdepth;
	limits.maxduration = NvMaxduration;
	limits.maxatom = 65536;
	limits.gcoffload = gcoffload;
	if(stats){
		start = nsec();
		brk0 = (uintptr)sbrk(0);
	}
	if(nvschedinit(&sched, m, &limits, 1, 1000, err, sizeof err) < 0){
		nvheapfree(&h);
		Bterm(&bout);
		fprint(2, "%s\n", err);
		nvmodulefree(m);
		exits("run");
	}
	Binit(&berr, 2, OWRITE);
	io.out = &bout;
	io.err = &berr;
	nvschedsetio(&sched, &io);
	rc = nvschedspawnroot(&sched, entry, args, &rootpid, err, sizeof err);
	nvheapfree(&h);
	if(rc < 0){
		Bterm(&bout);
		Bterm(&berr);
		fprint(2, "%s\n", err);
		nvschedfree(&sched);
		nvmodulefree(m);
		exits("run");
	}
	for(;;){
		state = nvschedstep(&sched, err, sizeof err);
		if(state != NvSchedProgress)
			break;
	}
	if(stats){
		Bflush(&bout);
		Bflush(&berr);
		printstats(&sched, start, brk0);
	}
	if(state == NvSchedError || sched.rootstate == NvRootFault || sched.rootstate == NvRootExit){
		Bterm(&bout);
		Bterm(&berr);
		if(state == NvSchedError)
			fprint(2, "scheduler: %s\n", err);
		else if(sched.rootstate == NvRootFault)
			fprint(2, "fault %s\n", sched.rootfault);
		else{
			fprint(2, "exit ");
			Binit(&bout, 2, OWRITE);
			printroot(&bout, sched.rootvalue);
			Bputc(&bout, '\n');
			Bterm(&bout);
		}
		if(state == NvSchedIdle)
			fprint(2, "deadlock: %lud live process(es) orphaned by the root, none runnable\n", sched.runtime.nlive);
		nvschedfree(&sched);
		nvmodulefree(m);
		exits("run");
	}
	if(state == NvSchedIdle){
		Bterm(&bout);
		Bterm(&berr);
		fprint(2, "deadlock: %lud live process(es), none runnable\n", sched.runtime.nlive);
		nvschedfree(&sched);
		nvmodulefree(m);
		exits("deadlock");
	}
	printroot(&bout, sched.rootvalue);
	Bputc(&bout, '\n');
	Bterm(&bout);
	Bterm(&berr);
	nvschedfree(&sched);
	nvmodulefree(m);
	exits(nil);
}

void
main(int argc, char **argv)
{
	char *file, *src, *stressenv, *offloadenv;
	int mode, gcstress;
	vlong heaplimit, gcoffload;
	long n;
	Parser p;
	Program *pr;
	Biobuf bout, bin;
	NvModule *m;
	NvHeap h;
	NvTerm args;
	NvFrag *result;
	char err[256], *entry;
	int fd, rc, stats;

	file = nil;
	mode = 0;
	stats = 0;
	heaplimit = 0;
	gcstress = 0;
	gcoffload = 0;
	/* rc may export an unset/restored variable as an empty /env file. */
	stressenv = getenv("nervous_gcstress");
	if(stressenv != nil){
		if(strcmp(stressenv, "1") == 0)
			gcstress = 1;
		else if(stressenv[0] != 0 && strcmp(stressenv, "0") != 0){
			fprint(2, "nervous: invalid nervous_gcstress (expected empty, 0 or 1)\n");
			free(stressenv);
			exits("environment");
		}
		free(stressenv);
	}
	/*
	 * D074/M08-T04d: $nervous_gcoffload sets the default off-process
	 * collection threshold (same word units and 0-means-never polarity
	 * as NvLimits.gcoffload) the same way $nervous_gcstress sets the
	 * gcstress default -- so test runners that already `rfork e` to
	 * preserve nervous_gcstress (T04e) inherit this one too, letting a
	 * whole rc script force off-process stress without touching every
	 * individual invocation.
	 */
	offloadenv = getenv("nervous_gcoffload");
	if(offloadenv != nil){
		if(offloadenv[0] != 0 && (parseint(offloadenv, &gcoffload) < 0 || gcoffload < 0)){
			fprint(2, "nervous: invalid nervous_gcoffload (expected empty or a non-negative decimal word count)\n");
			free(offloadenv);
			exits("environment");
		}
		free(offloadenv);
	}
	ARGBEGIN{
	case 's': stats = 1; break;
	case 'G': gcstress = 1; break;
	case 'H':
		if(parseint(EARGF(usage()), &heaplimit) < 0 || heaplimit < 0)
			usage();
		break;
	case 'o':
		if(parseint(EARGF(usage()), &gcoffload) < 0 || gcoffload < 0)
			usage();
		break;
	case 'a': mode = 'a'; file = EARGF(usage()); break;
	case 'A': mode = 'A'; file = EARGF(usage()); break;
	case 'b': mode = 'b'; file = EARGF(usage()); break;
	case 'c': mode = 'c'; file = EARGF(usage()); break;
	case 'f': mode = 'f'; file = EARGF(usage()); break;
	case 'F': mode = 'F'; file = EARGF(usage()); break;
	case 'r': mode = 'r'; file = EARGF(usage()); break;
	case 'x': mode = 'x'; file = EARGF(usage()); break;
	case 't': mode = 't'; file = EARGF(usage()); break;
	case 'X': mode = 'X'; file = EARGF(usage()); break;
	default: usage();
	}ARGEND
	if(file == nil){
		if(argc != 0) usage();
		print("%s\n", version);
		exits(nil);
	}
	if(mode != 'x' && mode != 't' && mode != 'r' && mode != 'X' && argc != 0) usage();
	if(mode == 'r' && argc < 1) usage();
	if(mode == 'b' || mode == 'x' || mode == 't' || mode == 'X'){
		if((mode == 'x' || mode == 't' || mode == 'X') && argc < 1)
			usage();
		fd = open(file, OREAD);
		if(fd < 0)
			sysfatal("open %s: %r", file);
		Binit(&bin, fd, OREAD);
		m = nvread(&bin, file, err, sizeof err);
		Bterm(&bin);
		close(fd);
		if(m == nil){
			fprint(2, "%s\n", err);
			exits("bytecode");
		}
		if(nvverify(m, err, sizeof err) < 0){
			fprint(2, "%s\n", err);
			nvmodulefree(m);
			exits("verify");
		}
		if(mode == 'X')
			runscheduled(m, argc, argv, heaplimit, gcstress, gcoffload, stats);
		Binit(&bout, 1, OWRITE);
		if(mode == 'b'){
			nvdisasm(&bout, m);
			Bterm(&bout);
			nvmodulefree(m);
			exits(nil);
		}
		entry = argv[0];
		argc--;
		argv++;
		nvheapinit(&h, 0);
		if(makeargs(&h, argc, argv, &args, err, sizeof err) < 0){
			nvheapfree(&h);
			Bterm(&bout);
			fprint(2, "%s\n", err);
			nvmodulefree(m);
			exits("argument");
		}
		rc = nvexecutelimits(&bout, m, entry, args, mode == 't', 1000000, heaplimit, gcstress, &result, err, sizeof err);
		nvheapfree(&h);
		if(rc < 0){
			Bterm(&bout);
			fprint(2, "fault %s\n", err);
			nvmodulefree(m);
			exits("fault");
		}
		if(mode == 't')
			Bprint(&bout, "value ");
		printroot(&bout, result);
		Bputc(&bout, '\n');
		Bterm(&bout);
		nvfragfree(result);
		nvmodulefree(m);
		exits(nil);
	}
	src = readall(file, &n);
	if(mode == 'A' || mode == 'F')
		pr = parseprogramcompat(&p, file, src, n);
	else
		pr = parseprogram(&p, file, src, n);
	free(src);
	if(pr == nil){
		fprint(2, "%s\n", p.err);
		exits("parse");
	}
	Binit(&bout, 1, OWRITE);
	if(mode == 'c' || mode == 'r'){
		m = nvcompile(pr, err, sizeof err);
		if(m == nil){
			Bterm(&bout);
			fprint(2, "%s\n", err);
			programfree(pr);
			exits("compile");
		}
		if(mode == 'c'){
			nvdisasm(&bout, m);
			nvmodulefree(m);
		}else{
			/*
			 * runscheduled always exits(); the AST is fully consumed by
			 * nvcompile already, so free it before handing m off, since
			 * control never returns here to reach the shared tail below.
			 */
			programfree(pr);
			runscheduled(m, argc, argv, heaplimit, gcstress, gcoffload, stats);
		}
	}else if(mode == 'a' || mode == 'A')
		programprint(&bout, pr);
	else
		formatprogram(&bout, pr);
	Bterm(&bout);
	programfree(pr);
	exits(nil);
}
