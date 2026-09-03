typedef struct NvValue NvValue;
typedef struct NvTuple NvTuple;
typedef struct NvPid NvPid;
typedef struct NvRef NvRef;

enum {
	NvMaxtermdepth = 256,
	NvValueerror = -1,
	NvValuelimit = -2,
};

/*
 * D049: the accepted receive-timeout duration domain, in nanoseconds.
 * Chosen so that any monotonic clock reading plus any accepted duration
 * stays inside the signed 64-bit nanosecond domain, which is what makes
 * deadline arithmetic total. It is not a policy about useful timeouts.
 */
#define NvMaxduration 1000000000000000000LL

enum {
	Vint,
	Vatom,
	Vtuple,
	Vpid,
	Vref,
};

struct NvPid {
	ulong slot;
	ulong generation;
};

struct NvRef {
	uvlong incarnation;
	uvlong counter;
};

struct NvTuple {
	int n;
	NvValue *elem;
};

struct NvValue {
	int valid;
	int kind;
	union {
		vlong i;
		char *atom;
		NvTuple *tuple;
		NvPid pid;
		NvRef ref;
	};
};

int nvexecute(Biobuf *, NvModule *, char *, NvValue *, int, vlong, NvValue *, char *, int);
void nvvalueprint(Biobuf *, NvValue *);
void nvvaluefree(NvValue *);
int nvvalueint(NvValue *, char *);
int nvvalueatom(NvValue *, char *);
int nvvaluetuple(NvValue *, NvValue *, int);
int nvvaluepid(NvValue *, ulong, ulong);
int nvvalueref(NvValue *, uvlong, uvlong);
int nvvaluecopy(NvValue *, NvValue *);
int nvvalueequal(NvValue *, NvValue *);
