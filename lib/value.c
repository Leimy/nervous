#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../include/nvbc.h"
#include "../include/nvvm.h"

void
nvvaluefree(NvValue *v)
{
	int i;

	if(v == nil || !v->valid)
		return;
	if(v->kind == Vatom)
		free(v->atom);
	else if(v->kind == Vtuple && v->tuple != nil){
		for(i = 0; i < v->tuple->n; i++)
			nvvaluefree(&v->tuple->elem[i]);
		free(v->tuple->elem);
		free(v->tuple);
	}
	memset(v, 0, sizeof *v);
}

int
nvvalueint(NvValue *v, char *s)
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
	memset(v, 0, sizeof *v);
	v->valid = 1;
	v->kind = Vint;
	if(neg && n == (1ULL<<63))
		v->i = (vlong)(1ULL<<63);
	else
		v->i = neg ? -(vlong)n : (vlong)n;
	return 0;
}

int
nvvalueatom(NvValue *v, char *s)
{
	memset(v, 0, sizeof *v);
	v->valid = 1;
	v->kind = Vatom;
	v->atom = strdup(s);
	return v->atom == nil ? -1 : 0;
}

int
nvvaluepid(NvValue *v, ulong slot, ulong generation)
{
	memset(v, 0, sizeof *v);
	v->valid = 1;
	v->kind = Vpid;
	v->pid.slot = slot;
	v->pid.generation = generation;
	return 0;
}

int
nvvalueref(NvValue *v, uvlong incarnation, uvlong counter)
{
	memset(v, 0, sizeof *v);
	v->valid = 1;
	v->kind = Vref;
	v->ref.incarnation = incarnation;
	v->ref.counter = counter;
	return 0;
}

static int valuecopydepth(NvValue *, NvValue *, ulong);

static int
valuetupledepth(NvValue *v, NvValue *elem, int n, ulong depth)
{
	int i, rc;

	memset(v, 0, sizeof *v);
	if(depth > NvMaxtermdepth)
		return NvValuelimit;
	if(n < 0 || n != 0 && elem == nil)
		return NvValueerror;
	v->valid = 1;
	v->kind = Vtuple;
	v->tuple = mallocz(sizeof *v->tuple, 1);
	if(v->tuple == nil)
		return -1;
	v->tuple->n = n;
	v->tuple->elem = mallocz(n*sizeof *v->tuple->elem, 1);
	if(n != 0 && v->tuple->elem == nil){
		free(v->tuple);
		v->tuple = nil;
		return -1;
	}
	for(i = 0; i < n; i++){
		rc = valuecopydepth(&v->tuple->elem[i], &elem[i], depth+1);
		if(rc < 0){
			nvvaluefree(v);
			return rc;
		}
	}
	return 0;
}

int
nvvaluetuple(NvValue *v, NvValue *elem, int n)
{
	return valuetupledepth(v, elem, n, 1);
}

static int
valuecopydepth(NvValue *d, NvValue *s, ulong depth)
{
	memset(d, 0, sizeof *d);
	if(depth > NvMaxtermdepth)
		return NvValuelimit;
	if(s == nil || !s->valid)
		return NvValueerror;
	switch(s->kind){
	case Vint:
		d->valid = 1;
		d->kind = Vint;
		d->i = s->i;
		return 0;
	case Vatom:
		return s->atom == nil ? -1 : nvvalueatom(d, s->atom);
	case Vtuple:
		if(s->tuple == nil)
			return -1;
		return valuetupledepth(d, s->tuple->elem, s->tuple->n, depth);
	case Vpid:
		return nvvaluepid(d, s->pid.slot, s->pid.generation);
	case Vref:
		return nvvalueref(d, s->ref.incarnation, s->ref.counter);
	}
	return -1;
}

int
nvvaluecopy(NvValue *d, NvValue *s)
{
	return valuecopydepth(d, s, 1);
}

int
nvvalueequal(NvValue *a, NvValue *b)
{
	int i;

	if(a == nil || b == nil || !a->valid || !b->valid || a->kind != b->kind)
		return 0;
	switch(a->kind){
	case Vint: return a->i == b->i;
	case Vatom: return strcmp(a->atom, b->atom) == 0;
	case Vpid: return a->pid.slot == b->pid.slot && a->pid.generation == b->pid.generation;
	case Vref: return a->ref.incarnation == b->ref.incarnation && a->ref.counter == b->ref.counter;
	case Vtuple:
		if(a->tuple->n != b->tuple->n)
			return 0;
		for(i = 0; i < a->tuple->n; i++)
			if(!nvvalueequal(&a->tuple->elem[i], &b->tuple->elem[i]))
				return 0;
		return 1;
	}
	return 0;
}

void
nvvalueprint(Biobuf *b, NvValue *v)
{
	int i;

	if(v == nil || !v->valid){
		Bprint(b, "<uninitialized>");
		return;
	}
	switch(v->kind){
	case Vint: Bprint(b, "%lld", v->i); break;
	case Vatom: Bprint(b, "'%s", v->atom); break;
	case Vpid: Bprint(b, "<pid:%lud:%lud>", v->pid.slot, v->pid.generation); break;
	case Vref: Bprint(b, "<ref:%llux:%llud>", v->ref.incarnation, v->ref.counter); break;
	case Vtuple:
		Bprint(b, "${");
		for(i = 0; i < v->tuple->n; i++){
			if(i != 0) Bprint(b, ", ");
			nvvalueprint(b, &v->tuple->elem[i]);
		}
		Bprint(b, "}");
		break;
	default: Bprint(b, "<bad-value>"); break;
	}
}
