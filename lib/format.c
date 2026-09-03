#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../include/nervous.h"

/*
 * Canonical layout (D021, D058). Precedence numbers mirror the parser:
 * binding 1, send 2, or 3, and 4, equality 5, ordering 6, additive 7,
 * multiplicative 8, unary 9, primary 10.
 */
enum {
	Pbind = 1,
	Psend,
	Por,
	Pand,
	Pequal,
	Porder,
	Padd,
	Pmul,
	Punary,
	Pmax,
};

static void fmtexpr(Biobuf *, Expr *, int, int);
static void indent(Biobuf *, int);
static Comment *comments;

static void
fmtcomment(Biobuf *b, Comment *c, int ind)
{
	indent(b, ind);
	Bprint(b, "//%s\n", c->text);
}

static void
commentsbefore(Biobuf *b, int line, int ind)
{
	while(comments != nil && comments->span.line < line){
		fmtcomment(b, comments, ind);
		comments = comments->next;
	}
}

static void
indent(Biobuf *b, int n)
{
	while(n-- > 0)
		Bputc(b, '\t');
}

static int
exprprec(Expr *e)
{
	switch(e->kind){
	case Ebind: return Pbind;
	case Esend: return Psend;
	case Eunary: return Punary;
	case Ebinary: break;
	default: return Pmax;
	}
	if(strcmp(e->text, "or") == 0) return Por;
	if(strcmp(e->text, "and") == 0) return Pand;
	if(strcmp(e->text, "==") == 0 || strcmp(e->text, "!=") == 0) return Pequal;
	if(strcmp(e->text, "<") == 0 || strcmp(e->text, "<=") == 0 ||
	   strcmp(e->text, ">") == 0 || strcmp(e->text, ">=") == 0) return Porder;
	if(strcmp(e->text, "+") == 0 || strcmp(e->text, "-") == 0) return Padd;
	return Pmul;
}

static void
fmtitems(Biobuf *b, Exprs *xs, int ind)
{
	int first;

	first = 1;
	for(; xs != nil; xs = xs->next){
		if(!first)
			Bprint(b, ", ");
		fmtexpr(b, xs->expr, ind, Psend);
		first = 0;
	}
}

/*
 * Block members are separated by `;` except after a block-like member,
 * which closes with its own brace. The last member never takes one.
 */
static void
fmtblock(Biobuf *b, Expr *e, int ind)
{
	Exprs *xs;

	Bprint(b, "{");
	if(e->list == nil){
		Bprint(b, "}");
		return;
	}
	Bprint(b, "\n");
	for(xs = e->list; xs != nil; xs = xs->next){
		commentsbefore(b, xs->expr->span.line, ind+1);
		indent(b, ind+1);
		fmtexpr(b, xs->expr, ind+1, 0);
		if(xs->next != nil && !exprblocklike(xs->expr))
			Bprint(b, ";");
		Bprint(b, "\n");
	}
	indent(b, ind);
	Bprint(b, "}");
}

/* `=> body` for a match, receive, or after clause; the caller has printed the head. */
static void
fmtclausebody(Biobuf *b, Expr *body, int ind)
{
	Bprint(b, " => ");
	fmtexpr(b, body, ind, 0);
	if(!exprblocklike(body))
		Bprint(b, ";");
	Bprint(b, "\n");
}

static void
fmtclauses(Biobuf *b, Clause *c, int ind)
{
	for(; c != nil; c = c->next){
		commentsbefore(b, c->span.line, ind);
		indent(b, ind);
		if(c->patterns != nil)
			fmtexpr(b, c->patterns->expr, ind, 0);
		if(c->guard != nil){
			Bprint(b, " when ");
			fmtexpr(b, c->guard, ind, 0);
		}
		fmtclausebody(b, c->body, ind);
	}
}

static void
fmtif(Biobuf *b, Expr *e, int ind)
{
	Expr *els;

	Bprint(b, "if ");
	fmtexpr(b, e->left, ind, 0);
	Bputc(b, ' ');
	fmtblock(b, e->right, ind);
	if(e->list == nil)
		return;
	els = e->list->expr;
	Bprint(b, " else ");
	if(els->kind == Eif)
		fmtif(b, els, ind);
	else
		fmtblock(b, els, ind);
}

static void
fmtexpr(Biobuf *b, Expr *e, int ind, int outer)
{
	int p, paren;

	p = exprprec(e);
	paren = p < outer;
	if(paren) Bputc(b, '(');
	switch(e->kind){
	case Eint:
		Bprint(b, "%lld", e->ival);
		break;
	case Eatom:
		Bprint(b, "'%s", e->text);
		break;
	case Evar:
		Bprint(b, "%s", e->text);
		break;
	case Ewild:
		Bputc(b, '_');
		break;
	case Etuple:
		Bprint(b, "${");
		fmtitems(b, e->list, ind);
		Bputc(b, '}');
		break;
	case Ecall:
		Bprint(b, "%s(", e->text);
		fmtitems(b, e->list, ind);
		Bputc(b, ')');
		break;
	case Eblock:
		fmtblock(b, e, ind);
		break;
	case Ebind:
		fmtexpr(b, e->left, ind, p+1);
		Bprint(b, " = ");
		fmtexpr(b, e->right, ind, p);
		break;
	case Esend:
		fmtexpr(b, e->left, ind, p+1);
		Bprint(b, " ! ");
		fmtexpr(b, e->right, ind, p);
		break;
	case Ematch:
		Bprint(b, "match ");
		fmtexpr(b, e->left, ind, 0);
		Bprint(b, " {\n");
		fmtclauses(b, e->clauses, ind+1);
		indent(b, ind);
		Bputc(b, '}');
		break;
	case Ereceive:
		Bprint(b, "receive {\n");
		fmtclauses(b, e->clauses, ind+1);
		if(e->left != nil){
			indent(b, ind+1);
			Bprint(b, "after ");
			fmtexpr(b, e->left, ind+1, 0);
			fmtclausebody(b, e->right, ind+1);
		}
		indent(b, ind);
		Bputc(b, '}');
		break;
	case Eif:
		fmtif(b, e, ind);
		break;
	case Eself:
		Bprint(b, "self");
		break;
	case Emkref:
		Bprint(b, "mkref");
		break;
	case Espawn:
		Bprint(b, "spawn %s(", e->text);
		fmtitems(b, e->list, ind);
		Bputc(b, ')');
		break;
	case Eexit:
		Bprint(b, "exit ");
		fmtexpr(b, e->left, ind, Por);
		break;
	case Eunary:
		Bprint(b, "%s", e->text);
		if(strcmp(e->text, "not") == 0)
			Bputc(b, ' ');
		fmtexpr(b, e->left, ind, p);
		break;
	case Ebinary:
		fmtexpr(b, e->left, ind, p);
		Bprint(b, " %s ", e->text);
		fmtexpr(b, e->right, ind, p+1);
		break;
	}
	if(paren) Bputc(b, ')');
}

/*
 * Each clause is its own `fn name(patterns) { ... }` declaration;
 * declarations are separated by one empty line.
 */
static void
fmtfn(Biobuf *b, Fn *f)
{
	Clause *c;
	Expr *body;

	for(c = f->clauses; c != nil; c = c->next){
		commentsbefore(b, c->span.line, 0);
		Bprint(b, "fn %s(", f->name);
		fmtitems(b, c->patterns, 0);
		Bprint(b, ")");
		if(c->guard != nil){
			Bprint(b, " when ");
			fmtexpr(b, c->guard, 0, 0);
		}
		Bputc(b, ' ');
		body = c->body;
		if(body->kind == Eblock)
			fmtblock(b, body, 0);
		else{
			Bprint(b, "{\n\t");
			fmtexpr(b, body, 1, 0);
			Bprint(b, "\n}");
		}
		Bputc(b, '\n');
		if(c->next != nil)
			Bputc(b, '\n');
	}
}

void
formatprogram(Biobuf *b, Program *p)
{
	Fn *f;

	comments = p->comments;
	for(f = p->fns; f != nil; f = f->next){
		fmtfn(b, f);
		if(f->next != nil)
			Bputc(b, '\n');
	}
	while(comments != nil){
		fmtcomment(b, comments, 0);
		comments = comments->next;
	}
}
