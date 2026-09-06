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
	fprint(2, "usage: nervous [-sG] [-H heapwords] [-a file | -A v2file | -b bytecode | -c source | -f file | -F v2file | -r source entry [args...] | -x bytecode entry [args...] | -t bytecode entry [args...]]\n");
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

void
main(int argc, char **argv)
{
	char *file, *src, *stressenv;
	int mode, gcstress;
	vlong heaplimit;
	long n;
	Parser p;
	Program *pr;
	Biobuf bout, bin, berr;
	NvModule *m;
	NvHeap h;
	NvTerm args, rootpid;
	NvFrag *result;
	NvLimits limits;
	NvScheduler sched;
	NvIO io;
	char err[256], *entry;
	int fd, rc, state, stats;
	vlong start;
	uintptr brk0;

	file = nil;
	mode = 0;
	stats = 0;
	start = 0;
	brk0 = 0;
	heaplimit = 0;
	gcstress = 0;
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
	ARGBEGIN{
	case 's': stats = 1; break;
	case 'G': gcstress = 1; break;
	case 'H':
		if(parseint(EARGF(usage()), &heaplimit) < 0 || heaplimit < 0)
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
	default: usage();
	}ARGEND
	if(file == nil){
		if(argc != 0) usage();
		print("%s\n", version);
		exits(nil);
	}
	if(mode != 'x' && mode != 't' && mode != 'r' && argc != 0) usage();
	if(mode == 'r' && argc < 1) usage();
	if(mode == 'b' || mode == 'x' || mode == 't'){
		if((mode == 'x' || mode == 't') && argc < 1)
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
			entry = argv[0];
			nvheapinit(&h, 0);
			if(makeargs(&h, argc-1, argv+1, &args, err, sizeof err) < 0){
				nvheapfree(&h);
				Bterm(&bout);
				fprint(2, "%s\n", err);
				nvmodulefree(m);
				programfree(pr);
				exits("argument");
			}
			/*
			 * D066: mailbox and message limits are now word counts of
			 * fragments including their root word, not bytes of the old
			 * recursive NvValue representation (D040 is superseded).
			 * maxheap is the enforced per-process word budget (-H);
			 * 0 is unlimited, but collection still bounds garbage.
			 * gcstress (-G) collects at every reservation. A process slot itself
			 * remains small (NvProcess plus one NvExec and its frame
			 * stack, a few hundred words at minimum), so maxprocess
			 * below is still a sanity bound against runaway spawn loops,
			 * not a capacity plan; 1000-node rings run fine. Exceeding
			 * it faults the spawning process with system_limit (D039).
			 */
			limits.maxprocess = 65536;
			limits.maxmailbox = 2*1024*1024;
			limits.maxmessage = 128*1024;
			limits.maxheap = heaplimit;
			limits.gcstress = gcstress;
			limits.maxframe = 1024;
			limits.maxtermdepth = NvMaxtermdepth;
			limits.maxduration = NvMaxduration;
			limits.maxatom = 65536;
			if(stats){
				start = nsec();
				brk0 = (uintptr)sbrk(0);
			}
			if(nvschedinit(&sched, m, &limits, 1, 1000, err, sizeof err) < 0){
				nvheapfree(&h);
				Bterm(&bout);
				fprint(2, "%s\n", err);
				nvmodulefree(m);
				programfree(pr);
				exits("run");
			}
			/*
			 * D054-D057: install real stdout/stderr as the print/eprint
			 * sinks before spawning the root, since nvschedspawn snapshots
			 * the installed NvIO at spawn time (see nvsched.h). stdout
			 * reuses the same Biobuf the CLI already uses for the final
			 * root-value report, so printed output and that report share
			 * one buffered stream in the order they actually happen.
			 */
			Binit(&berr, 2, OWRITE);
			io.out = &bout;
			io.err = &berr;
			nvschedsetio(&sched, &io);
			rc = nvschedspawnroot(&sched, entry, args, &rootpid, err, sizeof err);
			/*
			 * The scheduler copies the argument into the root process's
			 * own heap at spawn (successful or not; on failure nothing
			 * is retained either way), so the host-owned argument heap
			 * can be freed as soon as this call returns.
			 */
			nvheapfree(&h);
			if(rc < 0){
				Bterm(&bout);
				Bterm(&berr);
				fprint(2, "%s\n", err);
				nvschedfree(&sched);
				nvmodulefree(m);
				programfree(pr);
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
			/*
			 * A root fault or exit is reported before an idle scheduler
			 * is. The scheduler keeps stepping after the root dies so the
			 * remaining processes can finish; if instead they all block
			 * forever (say, waiting for a message the dead root was going
			 * to send), the run ends NvSchedIdle. That idleness is a
			 * consequence of the root's failure, not the cause, so the
			 * root diagnostic comes first and the orphaned processes are a
			 * secondary line. Checking NvSchedIdle first here used to
			 * swallow the root fault entirely and print only "deadlock".
			 */
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
				programfree(pr);
				exits("run");
			}
			if(state == NvSchedIdle){
				Bterm(&bout);
				Bterm(&berr);
				fprint(2, "deadlock: %lud live process(es), none runnable\n", sched.runtime.nlive);
				nvschedfree(&sched);
				nvmodulefree(m);
				programfree(pr);
				exits("deadlock");
			}
			printroot(&bout, sched.rootvalue);
			Bputc(&bout, '\n');
			Bterm(&berr);
			nvschedfree(&sched);
			nvmodulefree(m);
		}
	}else if(mode == 'a' || mode == 'A')
		programprint(&bout, pr);
	else
		formatprogram(&bout, pr);
	Bterm(&bout);
	programfree(pr);
	exits(nil);
}
