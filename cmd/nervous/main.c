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
	fprint(2, "usage: nervous [-a file | -A v2file | -b bytecode | -c source | -f file | -F v2file | -r source entry [args...] | -x bytecode entry [args...] | -t bytecode entry [args...]]\n");
	exits("usage");
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

static int
makeargs(int argc, char **argv, NvValue *args, char *err, int nerr)
{
	NvValue *av;
	int i, ok;

	av = mallocz(argc*sizeof *av, 1);
	if(argc != 0 && av == nil){ snprint(err,nerr,"out of memory"); return -1; }
	ok = -1;
	for(i = 0; i < argc; i++){
		if(argv[i][0] == '\'' && argv[i][1] != 0){
			if(nvvalueatom(&av[i], argv[i]+1) < 0){ snprint(err,nerr,"out of memory"); goto Out; }
		}else if(nvvalueint(&av[i], argv[i]) < 0){
			snprint(err,nerr,"bad argument %s: expected integer or atom",argv[i]);
			goto Out;
		}
	}
	if(nvvaluetuple(args, av, argc) < 0){ snprint(err,nerr,"out of memory"); goto Out; }
	ok = 0;
Out:
	for(i = 0; i < argc; i++)
		nvvaluefree(&av[i]);
	free(av);
	return ok;
}

void
main(int argc, char **argv)
{
	char *file, *src;
	int mode;
	long n;
	Parser p;
	Program *pr;
	Biobuf bout, bin, berr;
	NvModule *m;
	NvValue *av, args, result, rootpid;
	NvLimits limits;
	NvScheduler sched;
	NvIO io;
	char err[256], *entry;
	int fd, i, state;

	file = nil;
	mode = 0;
	ARGBEGIN{
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
		av = mallocz(argc*sizeof *av, 1);
		if(argc != 0 && av == nil)
			sysfatal("out of memory");
		for(i = 0; i < argc; i++){
			if(argv[i][0] == '\'' && argv[i][1] != 0){
				if(nvvalueatom(&av[i], argv[i]+1) < 0)
					sysfatal("out of memory");
			}else if(nvvalueint(&av[i], argv[i]) < 0){
				fprint(2, "bad argument %s: expected integer or atom\n", argv[i]);
				while(i-- > 0)
					nvvaluefree(&av[i]);
				free(av);
				Bterm(&bout);
				nvmodulefree(m);
				exits("argument");
			}
		}
		if(nvvaluetuple(&args, av, argc) < 0)
			sysfatal("out of memory");
		for(i = 0; i < argc; i++)
			nvvaluefree(&av[i]);
		free(av);
		if(nvexecute(&bout, m, entry, &args, mode == 't', 1000000, &result, err, sizeof err) < 0){
			Bterm(&bout);
			fprint(2, "fault %s\n", err);
			nvvaluefree(&args);
			nvmodulefree(m);
			exits("fault");
		}
		if(mode == 't')
			Bprint(&bout, "value ");
		nvvalueprint(&bout, &result);
		Bputc(&bout, '\n');
		Bterm(&bout);
		nvvaluefree(&result);
		nvvaluefree(&args);
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
			if(makeargs(argc-1, argv+1, &args, err, sizeof err) < 0){
				Bterm(&bout);
				fprint(2, "%s\n", err);
				nvmodulefree(m);
				programfree(pr);
				exits("argument");
			}
			/*
			 * A process slot is small (NvProcess plus one NvExec and its
			 * first frame, a few hundred bytes), so this is a sanity bound
			 * against runaway spawn loops, not a capacity plan; 1000-node
			 * rings run fine, and per-process memory is milestone 08's to
			 * make exact. Exceeding it faults the spawning process with
			 * system_limit (D039).
			 */
			limits.maxprocess = 65536;
			limits.maxmailbox = 16*1024*1024;
			limits.maxmessage = 1024*1024;
			limits.maxframe = 1024;
			limits.maxtermdepth = NvMaxtermdepth;
			limits.maxduration = NvMaxduration;
			if(nvschedinit(&sched, m, &limits, 1, 1000, err, sizeof err) < 0){
				Bterm(&bout);
				fprint(2, "%s\n", err);
				nvvaluefree(&args);
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
			if(nvschedspawnroot(&sched, entry, &args, &rootpid, err, sizeof err) < 0){
				Bterm(&bout);
				Bterm(&berr);
				fprint(2, "%s\n", err);
				nvschedfree(&sched);
				nvvaluefree(&args);
				nvmodulefree(m);
				programfree(pr);
				exits("run");
			}
			for(;;){
				state = nvschedstep(&sched, err, sizeof err);
				if(state != NvSchedProgress)
					break;
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
					nvvalueprint(&bout, &sched.rootvalue);
					Bputc(&bout, '\n');
					Bterm(&bout);
				}
				if(state == NvSchedIdle)
					fprint(2, "deadlock: %lud live process(es) orphaned by the root, none runnable\n", sched.runtime.nlive);
				nvschedfree(&sched);
				nvvaluefree(&rootpid);
				nvvaluefree(&args);
				nvmodulefree(m);
				programfree(pr);
				exits("run");
			}
			if(state == NvSchedIdle){
				Bterm(&bout);
				Bterm(&berr);
				fprint(2, "deadlock: %lud live process(es), none runnable\n", sched.runtime.nlive);
				nvschedfree(&sched);
				nvvaluefree(&rootpid);
				nvvaluefree(&args);
				nvmodulefree(m);
				programfree(pr);
				exits("deadlock");
			}
			nvvalueprint(&bout, &sched.rootvalue);
			Bputc(&bout, '\n');
			Bterm(&berr);
			nvschedfree(&sched);
			nvvaluefree(&rootpid);
			nvvaluefree(&args);
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
