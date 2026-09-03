#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../include/nvbc.h"

typedef struct Reader Reader;
struct Reader {
	Biobuf *b;
	char *name;
	int line;
	char *err;
	int nerr;
};

static NvModule *readerr(Reader *, NvModule *, char *);

static NvModule *
readerr(Reader *r, NvModule *m, char *s)
{
	snprint(r->err, r->nerr, "%s:%d: %s", r->name, r->line, s);
	nvmodulefree(m);
	return nil;
}

static char *
line(Reader *r)
{
	char *s;
	int n;

	s = Brdstr(r->b, '\n', 1);
	if(s == nil)
		return nil;
	r->line++;
	n = strlen(s);
	if(n > 0 && s[n-1] == '\n')
		s[--n] = 0;
	if(n > 0 && s[n-1] == '\r')
		s[--n] = 0;
	return s;
}

static int
words(char *s, char **v, int nv)
{
	int n;

	n = 0;
	while(*s != 0){
		while(*s == ' ' || *s == '\t')
			s++;
		if(*s == 0)
			break;
		if(n == nv)
			return nv+1;
		v[n++] = s;
		while(*s != 0 && *s != ' ' && *s != '\t')
			s++;
		if(*s != 0)
			*s++ = 0;
	}
	return n;
}

static int
ident(char *s)
{
	uchar c;

	c = *s++;
	if(!(c == '_' || c >= 'A' && c <= 'Z' || c >= 'a' && c <= 'z'))
		return 0;
	while((c = *s++) != 0)
		if(!(c == '_' || c >= 'A' && c <= 'Z' || c >= 'a' && c <= 'z' || c >= '0' && c <= '9'))
			return 0;
	return 1;
}

static int
parseuint(char *s, int *vp)
{
	uvlong v;
	uchar c;

	if(*s == 0)
		return -1;
	v = 0;
	while((c = *s++) != 0){
		if(c < '0' || c > '9')
			return -1;
		v = v*10 + c-'0';
		if(v > 0x7fffffffULL)
			return -1;
	}
	*vp = v;
	return 0;
}

static int
sint(char *s, vlong *vp)
{
	uvlong v, lim;
	int neg;
	uchar c;

	neg = *s == '-';
	if(neg)
		s++;
	if(*s == 0 || *s == '+')
		return -1;
	lim = neg ? (1ULL<<63) : (1ULL<<63)-1;
	v = 0;
	while((c = *s++) != 0){
		if(c < '0' || c > '9' || v > (lim-(c-'0'))/10)
			return -1;
		v = v*10 + c-'0';
	}
	if(neg && v == (1ULL<<63))
		*vp = (vlong)(1ULL<<63);
	else
		*vp = neg ? -(vlong)v : (vlong)v;
	return 0;
}

static int
addconst(NvModule *m, NvConst *k)
{
	NvConst *p;

	if(m->nconst == NvMaxitem)
		return -1;
	p = realloc(m->konst, (m->nconst+1)*sizeof *p);
	if(p == nil)
		return -1;
	m->konst = p;
	m->konst[m->nconst++] = *k;
	return 0;
}

static int
addfunc(NvModule *m, NvFunc *f)
{
	NvFunc *p;

	if(m->nfunc == NvMaxitem)
		return -1;
	p = realloc(m->func, (m->nfunc+1)*sizeof *p);
	if(p == nil)
		return -1;
	m->func = p;
	m->func[m->nfunc++] = *f;
	return 0;
}

static int
addinsn(NvFunc *f, NvInsn *i)
{
	NvInsn *p;

	if(f->ninsn == NvMaxitem)
		return -1;
	p = realloc(f->insn, (f->ninsn+1)*sizeof *p);
	if(p == nil)
		return -1;
	f->insn = p;
	f->insn[f->ninsn++] = *i;
	return 0;
}

static int
opcode(char *s)
{
	int i;

	for(i = 0; i < Nopcode; i++)
		if(strcmp(s, nvopname(i)) == 0)
			return i;
	return -1;
}

/* Operand counts are part of the canonical symbolic format. */
static int
noperand(int op)
{
	switch(op){
	case Onop: case Orecvtake: return 0;
	case Ojump: case Oreturn: case Ofail: case Oself: case Omakeref:
	case Orecvwait: case Oexit:
	case Orecvdeadline: case Orecvwaitdeadline: return 1;
	case Oloadk: case Omove: case Otailcall: case Orecvbegin: case Orecvnext:
	case Oprint: case Oeprint: return 2;
	case Otuple: case Ocall: case Otestatom: case Otestint:
	case Otesteq: case Otestarity: case Ogetelem:
	case Oadd: case Osub: case Omul: case Odiv: case Orem:
	case Olt: case Ole: case Ogt: case Oge:
	case Osend: case Ospawn: return 3;
	}
	return -1;
}

NvModule *
nvread(Biobuf *b, char *name, char *err, int nerr)
{
	Reader r;
	NvModule *m;
	NvConst k;
	NvFunc f;
	NvInsn in;
	char *s, *v[8], *colon;
	int n, op, no, pc, i, x;

	r.b = b;
	r.name = name;
	r.line = 0;
	r.err = err;
	r.nerr = nerr;
	m = mallocz(sizeof *m, 1);
	if(m == nil){
		snprint(err, nerr, "%s: out of memory", name);
		return nil;
	}
	s = line(&r);
	if(s == nil)
		return readerr(&r, m, "expected module");
	if(strcmp(s, "module") != 0){
		free(s);
		return readerr(&r, m, "expected module");
	}
	free(s);
	for(;;){
		s = line(&r);
		if(s == nil)
			return readerr(&r, m, "expected function or end of file");
		n = words(s, v, nelem(v));
		if(n == 0){
			free(s);
			return readerr(&r, m, "blank lines are not canonical");
		}
		if(strcmp(v[0], "const") != 0)
			break;
		if(n != 3){
			free(s);
			return readerr(&r, m, "bad constant declaration");
		}
		memset(&k, 0, sizeof k);
		if(strcmp(v[1], "int") == 0){
			k.kind = Kint;
			if(sint(v[2], &k.ival) < 0){
				free(s);
				return readerr(&r, m, "bad integer constant");
			}
		}else if(strcmp(v[1], "atom") == 0 || strcmp(v[1], "func") == 0){
			if(!ident(v[2])){
				free(s);
				return readerr(&r, m, "bad constant name");
			}
			k.kind = strcmp(v[1], "atom") == 0 ? Katom : Kfunc;
			k.text = strdup(v[2]);
			if(k.text == nil){
				free(s);
				return readerr(&r, m, "out of memory");
			}
		}else{
			free(s);
			return readerr(&r, m, "bad constant kind");
		}
		if(addconst(m, &k) < 0){
			free(k.text);
			free(s);
			return readerr(&r, m, "constant limit or out of memory");
		}
		free(s);
	}
	for(;;){
		if(n != 3 || strcmp(v[0], "func") != 0 || !ident(v[1]) || parseuint(v[2], &x) < 0){
			free(s);
			return readerr(&r, m, "bad function declaration");
		}
		memset(&f, 0, sizeof f);
		f.name = strdup(v[1]);
		f.nreg = x;
		free(s);
		if(f.name == nil)
			return readerr(&r, m, "out of memory");
		pc = 0;
		for(;;){
			s = line(&r);
			if(s == nil){
				free(f.name);
				free(f.insn);
				return readerr(&r, m, "expected end");
			}
			if(strcmp(s, "end") == 0){
				free(s);
				break;
			}
			n = words(s, v, nelem(v));
			if(n < 2 || (colon = strchr(v[0], ':')) == nil || colon[1] != 0){
				free(s); free(f.name); free(f.insn);
				return readerr(&r, m, "bad instruction label");
			}
			*colon = 0;
			if(parseuint(v[0], &x) < 0 || x != pc){
				free(s); free(f.name); free(f.insn);
				return readerr(&r, m, "instruction labels must be contiguous");
			}
			op = opcode(v[1]);
			if(op < 0){
				free(s); free(f.name); free(f.insn);
				return readerr(&r, m, "unknown opcode");
			}
			no = noperand(op);
			if(n != no+2){
				free(s); free(f.name); free(f.insn);
				return readerr(&r, m, "wrong operand count");
			}
			memset(&in, 0, sizeof in);
			in.op = op;
			for(i = 0; i < no; i++){
				if(parseuint(v[i+2], &x) < 0){
					free(s); free(f.name); free(f.insn);
					return readerr(&r, m, "bad instruction operand");
				}
				if(i == 0) in.a = x;
				else if(i == 1) in.b = x;
				else if(i == 2) in.c = x;
				else in.d = x;
			}
			if(addinsn(&f, &in) < 0){
				free(s); free(f.name); free(f.insn);
				return readerr(&r, m, "instruction limit or out of memory");
			}
			pc++;
			free(s);
		}
		if(addfunc(m, &f) < 0){
			free(f.name); free(f.insn);
			return readerr(&r, m, "function limit or out of memory");
		}
		s = line(&r);
		if(s == nil)
			return m;
		n = words(s, v, nelem(v));
		if(n == 0){
			free(s);
			return readerr(&r, m, "blank lines are not canonical");
		}
	}
}
