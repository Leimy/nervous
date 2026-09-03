#include <u.h>
#include <libc.h>
#include <bio.h>
#include "../include/nervous.h"

/*
 * Precedence levels (D016, D058). Binding and send are the two lowest
 * and both associate to the right; every other binary operator
 * associates to the left. Pmax is above every operator, so parsing
 * with min = Pmax yields exactly one primary or unary expression.
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

static char *
xstrdup(char *s)
{
	char *d;

	d = strdup(s);
	if(d == nil)
		sysfatal("out of memory");
	return d;
}

static void
next(Parser *p)
{
	Comment *c, **q;

	tokenfree(&p->tok);
	for(;;){
		p->tok = lexnext(&p->lex);
		if(p->tok.kind != Tcomment)
			break;
		c = mallocz(sizeof *c, 1);
		if(c == nil)
			sysfatal("out of memory");
		c->span = p->tok.span;
		c->text = p->tok.text;
		p->tok.text = nil;
		for(q = &p->comments; *q != nil; q = &(*q)->next)
			;
		*q = c;
		tokenfree(&p->tok);
	}
	if(p->tok.kind == Tbad && p->err[0] == 0)
		snprint(p->err, sizeof p->err, "%s:%d:%d: %s", p->lex.name,
			p->tok.span.line, p->tok.span.col, p->lex.err);
}

static void
error(Parser *p, Span s, char *fmt, ...)
{
	va_list ap;
	char b[180];

	if(p->err[0] != 0)
		return;
	va_start(ap, fmt);
	vseprint(b, b+sizeof b, fmt, ap);
	va_end(ap);
	snprint(p->err, sizeof p->err, "%s:%d:%d: %s", p->lex.name,
		s.line, s.col, b);
}

static int
take(Parser *p, int k)
{
	if(p->tok.kind != k)
		return 0;
	next(p);
	return 1;
}

static int
expect(Parser *p, int k)
{
	if(p->tok.kind != k){
		error(p, p->tok.span, "expected %s, found %s",
			tokname(k), tokname(p->tok.kind));
		return 0;
	}
	next(p);
	return 1;
}

static void
commentsfree(Comment *c)
{
	Comment *n;

	while(c != nil){
		n = c->next;
		free(c->text);
		free(c);
		c = n;
	}
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
clausefree(Clause *c)
{
	if(c == nil)
		return;
	exprsfree(c->patterns);
	exprfree(c->guard);
	exprfree(c->body);
	free(c);
}

static void
clausesfree(Clause *c)
{
	Clause *n;

	while(c != nil){
		n = c->next;
		clausefree(c);
		c = n;
	}
}

static void
fnfree(Fn *f)
{
	if(f == nil)
		return;
	free(f->name);
	clausesfree(f->clauses);
	free(f);
}

static Clause *
clausenew(Span sp)
{
	Clause *c;

	c = mallocz(sizeof *c, 1);
	if(c == nil)
		sysfatal("out of memory");
	c->span = sp;
	return c;
}

static Expr *parseexpr(Parser *, int);
static Expr *parseexpr1(Parser *, int);
static int patternok(Parser *, Expr *);

/* D060: an optional `when guard` after a clause head; nil when absent. */
static int
parseguard(Parser *p, Clause *c)
{
	if(!take(p, Twhen))
		return 0;
	c->guard = parseexpr(p, 0);
	return c->guard == nil ? -1 : 0;
}

/*
 * A block-like expression ends in its own closing brace, so a `;`
 * after it is optional both between block expressions and after a
 * clause body. Anything else must be followed by `;` unless the
 * enclosing construct closes immediately.
 */
int
exprblocklike(Expr *e)
{
	return e != nil && (e->kind == Eblock || e->kind == Ematch ||
		e->kind == Ereceive || e->kind == Eif);
}

static Expr *
parseblock(Parser *p)
{
	Expr *e, *x;
	Span sp;

	sp = p->tok.span;
	if(!expect(p, Tlbrace))
		return nil;
	e = exprnew(Eblock, sp);
	while(p->tok.kind != Trbrace && p->tok.kind != Teof && p->err[0] == 0){
		x = parseexpr(p, 0);
		if(x == nil){ exprfree(e); return nil; }
		exprappend(&e->list, x);
		if(take(p, Tsemi)){
			while(take(p, Tsemi))
				;
			continue;
		}
		if(p->tok.kind == Trbrace || exprblocklike(x))
			continue;
		error(p, p->tok.span, "expected ; or }");
		exprfree(e);
		return nil;
	}
	if(!expect(p, Trbrace)){ exprfree(e); return nil; }
	return e;
}

/* Comma-separated expressions up to, not including, the closing token. */
static int
parseitems(Parser *p, Exprs **list, int close, int patterns)
{
	Expr *x;

	if(p->tok.kind == close)
		return 0;
	for(;;){
		x = parseexpr(p, Psend);
		if(x == nil)
			return -1;
		if(patterns && !patternok(p, x)){
			exprfree(x);
			return -1;
		}
		exprappend(list, x);
		if(!take(p, Tcomma))
			break;
	}
	return 0;
}

/*
 * After a clause body: `;` is required after an expression body unless
 * the clause list closes right away, and optional after a block-like
 * body. `stop` is a second closing token (Tafter for receive) or -1.
 */
static int
clauseend(Parser *p, Expr *body, int stop)
{
	if(take(p, Tsemi))
		return 1;
	if(p->tok.kind == Trbrace || p->tok.kind == stop || exprblocklike(body))
		return 1;
	error(p, p->tok.span, "expected ; or }");
	return 0;
}

static Clause *
parseclause(Parser *p, int stop)
{
	Clause *c;
	Expr *x;

	c = clausenew(p->tok.span);
	x = parseexpr(p, 0);
	if(x == nil || !patternok(p, x)){
		exprfree(x);
		clausefree(c);
		return nil;
	}
	exprappend(&c->patterns, x);
	if(parseguard(p, c) < 0 || !expect(p, Tarrow)){ clausefree(c); return nil; }
	c->body = parseexpr(p, 0);
	if(c->body == nil || !clauseend(p, c->body, stop)){ clausefree(c); return nil; }
	return c;
}

static Expr *
parsematch(Parser *p, Span sp)
{
	Expr *e;
	Clause *c;

	e = exprnew(Ematch, sp);
	e->left = parseexpr(p, 0);
	if(e->left == nil || !expect(p, Tlbrace)){ exprfree(e); return nil; }
	while(p->tok.kind != Trbrace && p->tok.kind != Teof && p->err[0] == 0){
		c = parseclause(p, -1);
		if(c == nil){ exprfree(e); return nil; }
		clauseappend(&e->clauses, c);
	}
	if(!expect(p, Trbrace)){ exprfree(e); return nil; }
	return e;
}

static Expr *
parsereceive(Parser *p, Span sp)
{
	Expr *e;
	Clause *c;

	e = exprnew(Ereceive, sp);
	if(!expect(p, Tlbrace)){ exprfree(e); return nil; }
	while(p->tok.kind != Trbrace && p->tok.kind != Tafter && p->tok.kind != Teof && p->err[0] == 0){
		c = parseclause(p, Tafter);
		if(c == nil){ exprfree(e); return nil; }
		clauseappend(&e->clauses, c);
	}
	if(e->clauses == nil){
		error(p, sp, "receive requires at least one clause");
		exprfree(e);
		return nil;
	}
	/* D051: at most one trailing `after duration => body` clause. */
	if(take(p, Tafter)){
		e->left = parseexpr(p, 0);
		if(e->left == nil || !expect(p, Tarrow)){ exprfree(e); return nil; }
		e->right = parseexpr(p, 0);
		if(e->right == nil || !clauseend(p, e->right, -1)){ exprfree(e); return nil; }
	}
	if(!expect(p, Trbrace)){ exprfree(e); return nil; }
	return e;
}

static Expr *
parseif(Parser *p, Span sp)
{
	Expr *e, *x;

	e = exprnew(Eif, sp);
	e->left = parseexpr(p, 0);
	if(e->left == nil){ exprfree(e); return nil; }
	if(p->tok.kind != Tlbrace){
		error(p, p->tok.span, "expected { after if condition, found %s", tokname(p->tok.kind));
		exprfree(e);
		return nil;
	}
	e->right = parseblock(p);
	if(e->right == nil){ exprfree(e); return nil; }
	if(take(p, Telse)){
		if(p->tok.kind == Tif)
			x = parseexpr(p, Pmax);
		else if(p->tok.kind == Tlbrace)
			x = parseblock(p);
		else{
			error(p, p->tok.span, "expected { or if after else, found %s", tokname(p->tok.kind));
			exprfree(e);
			return nil;
		}
		if(x == nil){ exprfree(e); return nil; }
		exprappend(&e->list, x);
	}
	return e;
}

static Expr *
parsespawn(Parser *p, Span sp)
{
	Expr *e;

	e = exprnew(Espawn, sp);
	if(p->tok.kind != Tident){
		error(p, p->tok.span, "spawn requires a function name, found %s", tokname(p->tok.kind));
		exprfree(e);
		return nil;
	}
	e->text = xstrdup(p->tok.text);
	next(p);
	if(!expect(p, Tlparen) || parseitems(p, &e->list, Trparen, 0) < 0 || !expect(p, Trparen)){
		exprfree(e);
		return nil;
	}
	return e;
}

/*
 * Compat syntax spelled the process forms as calls of reserved names.
 * Rewrite them into the v3 node kinds so the formatter and compiler
 * see one AST. Anything not matching the intrinsic shape is left as
 * an ordinary call for the compiler to reject.
 */
static Expr *
compatintrinsic(Expr *e)
{
	Exprs *x;
	int n;

	for(n = 0, x = e->list; x != nil; x = x->next)
		n++;
	if(strcmp(e->text, "self") == 0 && n == 0)
		e->kind = Eself;
	else if(strcmp(e->text, "make_ref") == 0 && n == 0)
		e->kind = Emkref;
	else if(strcmp(e->text, "exit") == 0 && n == 1){
		e->kind = Eexit;
		e->left = e->list->expr;
		free(e->list);
		e->list = nil;
	}else if(strcmp(e->text, "send") == 0 && n == 2){
		e->kind = Esend;
		e->left = e->list->expr;
		e->right = e->list->next->expr;
		free(e->list->next);
		free(e->list);
		e->list = nil;
	}else if(strcmp(e->text, "spawn") == 0 && n >= 1 && e->list->expr->kind == Evar){
		e->kind = Espawn;
		x = e->list;
		e->list = x->next;
		free(e->text);
		e->text = x->expr->text;
		x->expr->text = nil;
		exprfree(x->expr);
		free(x);
	}else
		return e;
	if(e->kind != Espawn){
		free(e->text);
		e->text = nil;
	}
	return e;
}

static Expr *
parseprimary(Parser *p)
{
	Expr *e;
	Span sp;
	char *name;

	sp = p->tok.span;
	switch(p->tok.kind){
	case Tint:
		e = exprnew(Eint, sp);
		e->ival = p->tok.ival;
		next(p);
		return e;
	case Tatom:
		e = exprnew(Eatom, sp);
		e->text = xstrdup(p->tok.text);
		next(p);
		return e;
	case Tident:
		name = xstrdup(p->tok.text);
		next(p);
		if(take(p, Tlparen)){
			e = exprnew(Ecall, sp);
			e->text = name;
			if(parseitems(p, &e->list, Trparen, 0) < 0 || !expect(p, Trparen)){
				exprfree(e);
				return nil;
			}
			if(p->compat)
				e = compatintrinsic(e);
			return e;
		}
		e = exprnew(strcmp(name, "_") == 0 ? Ewild : Evar, sp);
		if(e->kind == Evar)
			e->text = name;
		else
			free(name);
		return e;
	case Tlparen:
		next(p);
		e = parseexpr(p, 0);
		if(!expect(p, Trparen)){ exprfree(e); return nil; }
		return e;
	case Tlbrace:
		return parseblock(p);
	case Ttupleopen:
		next(p);
		e = exprnew(Etuple, sp);
		if(parseitems(p, &e->list, Trbrace, 0) < 0 || !expect(p, Trbrace)){
			exprfree(e);
			return nil;
		}
		return e;
	case Tmapopen:
		error(p, sp, "maps are reserved but not implemented");
		return nil;
	case Tmatch:
		next(p);
		return parsematch(p, sp);
	case Treceive:
		next(p);
		return parsereceive(p, sp);
	case Tif:
		next(p);
		return parseif(p, sp);
	case Tspawn:
		next(p);
		return parsespawn(p, sp);
	case Tself:
		next(p);
		return exprnew(Eself, sp);
	case Tmkref:
		next(p);
		return exprnew(Emkref, sp);
	case Texit:
		next(p);
		e = exprnew(Eexit, sp);
		e->left = parseexpr(p, Por);
		if(e->left == nil){ exprfree(e); return nil; }
		return e;
	default:
		error(p, sp, "expected expression, found %s", tokname(p->tok.kind));
		return nil;
	}
}

static int
prec(int k)
{
	switch(k){
	case Tassign: return Pbind;
	case Tbang: return Psend;
	case Tor: return Por;
	case Tand: return Pand;
	case Teq: case Tne: return Pequal;
	case Tlt: case Tle: case Tgt: case Tge: return Porder;
	case Tplus: case Tminus: return Padd;
	case Tstar: case Tslash: case Tpercent: return Pmul;
	}
	return 0;
}

static Expr *
parseexpr(Parser *p, int min)
{
	Expr *e;

	if(p->depth >= NvMaxsourcedepth){
		error(p, p->tok.span, "nesting too deep");
		return nil;
	}
	p->depth++;
	e = parseexpr1(p, min);
	p->depth--;
	return e;
}

static Expr *
parseexpr1(Parser *p, int min)
{
	Expr *e, *r, *n;
	Span sp;
	int k, q, links, rightassoc;
	char *op;

	sp = p->tok.span;
	if(p->tok.kind == Tnot || p->tok.kind == Tminus || p->tok.kind == Tplus){
		op = xstrdup(tokname(p->tok.kind));
		next(p);
		e = exprnew(Eunary, sp);
		e->text = op;
		e->left = parseexpr(p, Punary);
		if(e->left == nil){ exprfree(e); return nil; }
	}else
		e = parseprimary(p);
	if(e == nil)
		return nil;
	/* Each left-associative link is one more AST level, built iteratively. */
	links = 0;
	while((q = prec(p->tok.kind)) >= min && q != 0){
		if(p->depth + ++links >= NvMaxsourcedepth){
			error(p, p->tok.span, "nesting too deep");
			exprfree(e);
			return nil;
		}
		k = p->tok.kind;
		sp = p->tok.span;
		rightassoc = k == Tassign || k == Tbang;
		next(p);
		r = parseexpr(p, rightassoc ? q : q + 1);
		if(r == nil){ exprfree(e); return nil; }
		if(k == Tassign && !patternok(p, e)){
			exprfree(e);
			exprfree(r);
			return nil;
		}
		if(k == Tassign)
			n = exprnew(Ebind, sp);
		else if(k == Tbang)
			n = exprnew(Esend, sp);
		else{
			n = exprnew(Ebinary, sp);
			n->text = xstrdup(tokname(k));
		}
		n->left = e;
		n->right = r;
		e = n;
	}
	return e;
}

static int
patternok(Parser *p, Expr *e)
{
	Exprs *x;

	if(e == nil)
		return 0;
	switch(e->kind){
	case Eint:
	case Eatom:
	case Evar:
	case Ewild:
		return 1;
	case Eunary:
		/* D060: a negative integer literal is a pattern. */
		if(strcmp(e->text, "-") == 0 && e->left->kind == Eint)
			return 1;
		break;
	case Etuple:
		for(x = e->list; x != nil; x = x->next)
			if(!patternok(p, x->expr))
				return 0;
		return 1;
	}
	error(p, e->span, "expression is not a pattern");
	return 0;
}

/* `fn name(patterns) { ... }`: one clause with a block body. */
static Clause *
parsefnclause(Parser *p, Span sp)
{
	Clause *c;

	c = clausenew(sp);
	if(!expect(p, Tlparen) || parseitems(p, &c->patterns, Trparen, 1) < 0 || !expect(p, Trparen) ||
	   parseguard(p, c) < 0){
		clausefree(c);
		return nil;
	}
	if(p->tok.kind != Tlbrace){
		error(p, p->tok.span, "expected { after function head, found %s", tokname(p->tok.kind));
		clausefree(c);
		return nil;
	}
	c->body = parseblock(p);
	if(c->body == nil){ clausefree(c); return nil; }
	return c;
}

/* Wrap a non-block clause body so every function clause body is an Eblock. */
static Expr *
blockify(Expr *e)
{
	Expr *b;

	if(e == nil || e->kind == Eblock)
		return e;
	b = exprnew(Eblock, e->span);
	exprappend(&b->list, e);
	return b;
}

/*
 * Compat function body: `fn name { ${..} => body; ... }` is clause
 * form when the body starts with a tuple followed by `=>`; otherwise
 * the whole brace is one zero-argument clause body.
 */
static int
parsecompatfn(Parser *p, Fn *f, Span sp)
{
	Clause *c;
	Expr *e, *body;

	if(!expect(p, Tlbrace))
		return -1;
	body = exprnew(Eblock, sp);
	if(p->tok.kind == Ttupleopen){
		e = parseexpr(p, 0);
		if(e == nil){ exprfree(body); return -1; }
		if(p->tok.kind == Tarrow){
			exprfree(body);
			for(;;){
				c = clausenew(e->span);
				if(e->kind != Etuple || !patternok(p, e)){
					if(e->kind != Etuple)
						error(p, e->span, "expected tuple clause head");
					exprfree(e);
					clausefree(c);
					return -1;
				}
				c->patterns = e->list;
				e->list = nil;
				exprfree(e);
				if(!expect(p, Tarrow)){ clausefree(c); return -1; }
				c->body = blockify(parseexpr(p, 0));
				if(c->body == nil || !clauseend(p, c->body, -1)){ clausefree(c); return -1; }
				clauseappend(&f->clauses, c);
				if(p->tok.kind == Trbrace || p->tok.kind == Teof || p->err[0] != 0)
					break;
				e = parseexpr(p, 0);
				if(e == nil)
					return -1;
			}
			return expect(p, Trbrace) ? 0 : -1;
		}
		exprappend(&body->list, e);
		if(!take(p, Tsemi) && p->tok.kind != Trbrace && !exprblocklike(e)){
			error(p, p->tok.span, "expected ; or }");
			exprfree(body);
			return -1;
		}
		while(take(p, Tsemi))
			;
	}
	while(p->tok.kind != Trbrace && p->tok.kind != Teof && p->err[0] == 0){
		e = parseexpr(p, 0);
		if(e == nil){ exprfree(body); return -1; }
		exprappend(&body->list, e);
		if(take(p, Tsemi)){
			while(take(p, Tsemi))
				;
			continue;
		}
		if(p->tok.kind == Trbrace || exprblocklike(e))
			continue;
		error(p, p->tok.span, "expected ; or }");
		exprfree(body);
		return -1;
	}
	if(!expect(p, Trbrace)){ exprfree(body); return -1; }
	c = clausenew(sp);
	c->body = body;
	clauseappend(&f->clauses, c);
	return 0;
}

Program *
parseprogrammode(Parser *p, char *name, char *src, long n, int compat)
{
	Program *pr;
	Fn *f, *last;
	Clause *c;
	Span sp;

	memset(p, 0, sizeof *p);
	p->compat = compat;
	lexinit(&p->lex, name, src, n);
	p->lex.compat = compat;
	p->tok.kind = Teof;
	next(p);
	pr = mallocz(sizeof *pr, 1);
	if(pr == nil)
		sysfatal("out of memory");
	last = nil;
	while(p->tok.kind != Teof && p->err[0] == 0){
		sp = p->tok.span;
		if(!expect(p, Tfn))
			break;
		if(p->tok.kind != Tident){
			error(p, p->tok.span, "function name expected, found %s", tokname(p->tok.kind));
			break;
		}
		f = mallocz(sizeof *f, 1);
		if(f == nil)
			sysfatal("out of memory");
		f->span = sp;
		f->name = xstrdup(p->tok.text);
		next(p);
		if(compat){
			if(parsecompatfn(p, f, sp) < 0){
				fnfree(f);
				break;
			}
		}else{
			c = parsefnclause(p, sp);
			if(c == nil){
				fnfree(f);
				break;
			}
			clauseappend(&f->clauses, c);
		}
		/* Adjacent declarations of one name are that function's clauses. */
		if(last != nil && strcmp(last->name, f->name) == 0){
			clauseappend(&last->clauses, f->clauses);
			f->clauses = nil;
			fnfree(f);
		}else{
			fnappend(&pr->fns, f);
			last = f;
		}
	}
	tokenfree(&p->tok);
	if(p->err[0] != 0){
		commentsfree(p->comments);
		p->comments = nil;
		programfree(pr);
		return nil;
	}
	pr->comments = p->comments;
	p->comments = nil;
	return pr;
}

Program *
parseprogram(Parser *p, char *name, char *src, long n)
{
	return parseprogrammode(p, name, src, n, 0);
}

Program *
parseprogramcompat(Parser *p, char *name, char *src, long n)
{
	return parseprogrammode(p, name, src, n, 1);
}
