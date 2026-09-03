typedef struct NvExec NvExec;
typedef struct NvExecFrame NvExecFrame;
typedef struct NvExecHost NvExecHost;

enum {
	NvYield,
	NvDone,
	NvFault,
	NvExit,
};

struct NvExecHost {
	void *aux;
	int (*self)(void *, NvValue *, char *, int);
	int (*makeref)(void *, NvValue *, char *, int);
	int (*send)(void *, NvValue *, NvValue *, char *, int);
	int (*spawn)(void *, char *, NvValue *, NvValue *, char *, int);
	int (*recvbegin)(void *, NvValue *, char *, int);
	int (*recvnext)(void *, NvValue *, char *, int);
	int (*recvtake)(void *, char *, int);
	int (*recvwait)(void *, char *, int);
	/*
	 * D051 timeout boundary. recvdeadline arms one absolute deadline from
	 * a duration term and returns 0, or -1 with a reason for a duration
	 * outside the D049 domain; an `'infinity` duration arms nothing and
	 * still returns 0. recvwaitdeadline returns 0 when the process blocked
	 * and must resume at the retry target, 1 when the armed deadline had
	 * already expired and execution must fall through to the timeout body,
	 * and -1 with a reason on an illegal scan phase.
	 */
	int (*recvdeadline)(void *, NvValue *, char *, int);
	int (*recvwaitdeadline)(void *, char *, int);
	/*
	 * D053-D057 I/O boundary. print/eprint write one value to host
	 * stdout/stderr respectively and return 0 on success or -1 with a
	 * reason (io_error on a host write failure). Unlike self/send, the
	 * callback does not produce the destination value: nvexecrun writes
	 * the fixed atom 'ok into the destination register itself on
	 * success, the same way arithmetic instructions build their own
	 * result. A nil slot (no host, or a host that did not wire this
	 * operation) faults bad_process_context exactly like every other
	 * process instruction.
	 */
	int (*print)(void *, NvValue *, char *, int);
	int (*eprint)(void *, NvValue *, char *, int);
};

struct NvExecFrame {
	NvFunc *func;
	int pc;
	NvValue *reg;
	int dst;
	NvExecFrame *caller;
};

struct NvExec {
	NvModule *module;
	NvExecFrame *frame;
	NvValue result;
	NvValue exitreason;
	uvlong reductions;
	ulong nframe;
	ulong maxframe;
	Biobuf *trace;
	NvExecHost host;
	int traceon;
	int state;
	char fault[128];
};

int nvexecinit(NvExec *, NvModule *, char *, NvValue *, Biobuf *, int, char *, int);
void nvexecsethost(NvExec *, NvExecHost *);
int nvexecsetframelimit(NvExec *, ulong);
int nvexecrun(NvExec *, uvlong);
void nvexecfree(NvExec *);
