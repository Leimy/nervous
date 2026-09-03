#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../include/nervous.h"

Expr *
exprnew(int kind, Span sp)
{
	Expr *e;

	e = mallocz(sizeof *e, 1);
	if(e == nil)
		sysfatal("out of memory");
	e->kind = kind;
	e->span = sp;
	return e;
}

Exprs *
exprappend(Exprs **head, Expr *e)
{
	Exprs *n, **p;

	n = mallocz(sizeof *n, 1);
	if(n == nil)
		sysfatal("out of memory");
	n->expr = e;
	for(p = head; *p != nil; p = &(*p)->next)
		;
	*p = n;
	return n;
}

Clause *
clauseappend(Clause **head, Clause *c)
{
	Clause **p;

	for(p = head; *p != nil; p = &(*p)->next)
		;
	*p = c;
	return c;
}

Fn *
fnappend(Fn **head, Fn *f)
{
	Fn **p;

	for(p = head; *p != nil; p = &(*p)->next)
		;
	*p = f;
	return f;
}

static void
exprsfree(Exprs *xs)
{
	Exprs *n;

	while(xs != nil){
		n = xs->next;
		exprfree(xs->expr);
		free(xs);
		xs = n;
	}
}

static void
clausesfree(Clause *c)
{
	Clause *n;

	while(c != nil){
		n = c->next;
		exprsfree(c->patterns);
		exprfree(c->body);
		free(c);
		c = n;
	}
}

void
exprfree(Expr *e)
{
	if(e == nil)
		return;
	free(e->text);
	exprfree(e->left);
	exprfree(e->right);
	exprsfree(e->list);
	clausesfree(e->clauses);
	free(e);
}

void
programfree(Program *p)
{
	Fn *f, *n;
	Comment *c, *cn;

	if(p == nil)
		return;
	for(f = p->fns; f != nil; f = n){
		n = f->next;
		free(f->name);
		clausesfree(f->clauses);
		free(f);
	}
	for(c = p->comments; c != nil; c = cn){
		cn = c->next;
		free(c->text);
		free(c);
	}
	free(p);
}

static void printexpr(Biobuf *, Expr *);

static void
printquoted(Biobuf *b, char *s)
{
	uchar c;

	Bputc(b, '"');
	while((c = *s++) != 0){
		switch(c){
		case '\\': Bprint(b, "\\\\"); break;
		case '"': Bprint(b, "\\\""); break;
		case '\n': Bprint(b, "\\n"); break;
		case '\r': Bprint(b, "\\r"); break;
		case '\t': Bprint(b, "\\t"); break;
		default:
			if(c < 0x20 || c == 0x7f)
				Bprint(b, "\\x%02ux", c);
			else
				Bputc(b, c);
			break;
		}
	}
	Bputc(b, '"');
}

static void
printlist(Biobuf *b, Exprs *xs)
{
	for(; xs != nil; xs = xs->next){
		Bprint(b, " ");
		printexpr(b, xs->expr);
	}
}

static void
printclauses(Biobuf *b, Clause *c)
{
	for(; c != nil; c = c->next){
		Bprint(b, " (clause (patterns");
		printlist(b, c->patterns);
		Bprint(b, ") ");
		printexpr(b, c->body);
		Bprint(b, ")");
	}
}

static void
printexpr(Biobuf *b, Expr *e)
{
	if(e == nil){
		Bprint(b, "nil");
		return;
	}
	switch(e->kind){
	case Eint: Bprint(b, "(int %lld)", e->ival); break;
	case Eatom: Bprint(b, "(atom %s)", e->text); break;
	case Evar: Bprint(b, "(var %s)", e->text); break;
	case Ewild: Bprint(b, "wild"); break;
	case Etuple:
		Bprint(b, "(tuple"); printlist(b, e->list); Bprint(b, ")"); break;
	case Ecall:
		Bprint(b, "(call %s", e->text); printlist(b, e->list); Bprint(b, ")"); break;
	case Eblock:
		Bprint(b, "(block"); printlist(b, e->list); Bprint(b, ")"); break;
	case Ebind:
		Bprint(b, "(bind "); printexpr(b, e->left); Bprint(b, " "); printexpr(b, e->right); Bprint(b, ")"); break;
	case Ematch:
		Bprint(b, "(match "); printexpr(b, e->left); printclauses(b, e->clauses); Bprint(b, ")"); break;
	case Ereceive:
		Bprint(b, "(receive"); printclauses(b, e->clauses);
		if(e->left != nil){
			Bprint(b, " (after "); printexpr(b, e->left); Bprint(b, " "); printexpr(b, e->right); Bprint(b, ")");
		}
		Bprint(b, ")"); break;
	case Eif:
		Bprint(b, "(if "); printexpr(b, e->left); Bprint(b, " "); printexpr(b, e->right);
		Bprint(b, " "); printexpr(b, e->list != nil ? e->list->expr : nil); Bprint(b, ")"); break;
	case Eself: Bprint(b, "self"); break;
	case Emkref: Bprint(b, "mkref"); break;
	case Espawn:
		Bprint(b, "(spawn %s", e->text); printlist(b, e->list); Bprint(b, ")"); break;
	case Esend:
		Bprint(b, "(send "); printexpr(b, e->left); Bprint(b, " "); printexpr(b, e->right); Bprint(b, ")"); break;
	case Eexit:
		Bprint(b, "(exit "); printexpr(b, e->left); Bprint(b, ")"); break;
	case Eunary:
		Bprint(b, "(unary %s ", e->text); printexpr(b, e->left); Bprint(b, ")"); break;
	case Ebinary:
		Bprint(b, "(binary %s ", e->text); printexpr(b, e->left); Bprint(b, " "); printexpr(b, e->right); Bprint(b, ")"); break;
	default: Bprint(b, "(bad-expr %d)", e->kind); break;
	}
}

void
programprint(Biobuf *b, Program *p)
{
	Fn *f;
	Comment *c;

	Bprint(b, "(program");
	for(c = p->comments; c != nil; c = c->next){
		Bprint(b, "\n  (comment %d %d ", c->span.line, c->span.col);
		printquoted(b, c->text);
		Bputc(b, ')');
	}
	for(f = p->fns; f != nil; f = f->next){
		Bprint(b, "\n  (fn %s", f->name);
		printclauses(b, f->clauses);
		Bprint(b, ")");
	}
	Bprint(b, "\n)\n");
}
