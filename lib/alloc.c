#include <u.h>
#include <libc.h>
#include "../include/nvalloc.h"

static vlong failafter = -1;

void
nvallocfail(vlong n)
{
	failafter = n;
}

static int
shouldfail(void)
{
	if(failafter < 0)
		return 0;
	if(failafter == 0)
		return 1;
	failafter--;
	return 0;
}

void *
nvmalloc(ulong n)
{
	if(shouldfail())
		return nil;
	return malloc(n);
}

void *
nvmallocz(ulong n, int clear)
{
	if(shouldfail())
		return nil;
	return mallocz(n, clear);
}

void *
nvrealloc(void *p, ulong n)
{
	if(shouldfail())
		return nil;
	return realloc(p, n);
}

char *
nvstrdup(char *s)
{
	if(shouldfail())
		return nil;
	return strdup(s);
}
