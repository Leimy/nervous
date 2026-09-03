#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ctype.h>
#include "../include/nervous.h"

static Span
span(Lexer *l, int line, int col)
{
	Span s;

	s.line = line;
	s.col = col;
	s.endline = l->line;
	s.endcol = l->col;
	return s;
}

static int
peek(Lexer *l)
{
	if(l->p >= l->n)
		return -1;
	return (uchar)l->src[l->p];
}

static int
get(Lexer *l)
{
	int c;

	c = peek(l);
	if(c < 0)
		return c;
	l->p++;
	if(c == '\n'){
		l->line++;
		l->col = 1;
	}else
		l->col++;
	return c;
}

static char *
copytext(Lexer *l, long p)
{
	char *s;
	long n;

	n = l->p - p;
	s = malloc(n+1);
	if(s == nil)
		sysfatal("out of memory");
	memmove(s, l->src+p, n);
	s[n] = 0;
	return s;
}

void
lexinit(Lexer *l, char *name, char *src, long n)
{
	memset(l, 0, sizeof *l);
	l->name = name;
	l->src = src;
	l->n = n;
	l->line = 1;
	l->col = 1;
}

void
tokenfree(Token *t)
{
	free(t->text);
	t->text = nil;
}

static Token
tok(Lexer *l, int kind, int line, int col)
{
	Token t;

	memset(&t, 0, sizeof t);
	t.kind = kind;
	t.span = span(l, line, col);
	return t;
}

static int
idstart(int c)
{
	return c == '_' || isalpha(c);
}

static int
idchar(int c)
{
	return c == '_' || isalnum(c);
}

/*
 * Keywords shared by both syntaxes come first; the v3 process and
 * conditional forms are ordinary identifiers under the compat lexer,
 * where `self()` and `exit(reason)` were call-shaped intrinsics.
 */
static struct {
	char *name;
	int kind;
	int v3only;
} keywords[] = {
	{ "fn", Tfn, 0 },
	{ "match", Tmatch, 0 },
	{ "receive", Treceive, 0 },
	{ "after", Tafter, 0 },
	{ "and", Tand, 0 },
	{ "or", Tor, 0 },
	{ "not", Tnot, 0 },
	{ "when", Twhen, 1 },
	{ "if", Tif, 1 },
	{ "else", Telse, 1 },
	{ "spawn", Tspawn, 1 },
	{ "self", Tself, 1 },
	{ "mkref", Tmkref, 1 },
	{ "exit", Texit, 1 },
};

Token
lexnext(Lexer *l)
{
	Token t;
	long p;
	int c, i, line, col;
	uvlong v;

	for(;;){
		c = peek(l);
		if(c == ' ' || c == '\t' || c == '\r' || c == '\n'){
			get(l);
			continue;
		}
		break;
	}
	line = l->line;
	col = l->col;
	p = l->p;
	c = get(l);
	if(c < 0)
		return tok(l, Teof, line, col);
	if(c == '/' && peek(l) == '/'){
		get(l);
		p = l->p;
		while(peek(l) >= 0 && peek(l) != '\n')
			get(l);
		t = tok(l, Tcomment, line, col);
		t.text = copytext(l, p);
		return t;
	}
	if(isdigit(c)){
		v = c-'0';
		while(isdigit(peek(l))){
			c = get(l);
			if(v > ((uvlong)0x7FFFFFFFFFFFFFFFULL - (c-'0'))/10){
				snprint(l->err, sizeof l->err, "integer literal overflow");
				return tok(l, Tbad, line, col);
			}
			v = v*10 + c-'0';
		}
		t = tok(l, Tint, line, col);
		t.ival = v;
		return t;
	}
	if(idstart(c)){
		while(idchar(peek(l))) get(l);
		t = tok(l, Tident, line, col);
		t.text = copytext(l, p);
		for(i = 0; i < nelem(keywords); i++)
			if((!keywords[i].v3only || !l->compat) && strcmp(t.text, keywords[i].name) == 0){
				t.kind = keywords[i].kind;
				break;
			}
		return t;
	}
	if(c == '\''){
		p = l->p;
		if(!idstart(peek(l))){
			snprint(l->err, sizeof l->err, "atom name expected after quote");
			return tok(l, Tbad, line, col);
		}
		get(l);
		while(idchar(peek(l))) get(l);
		t = tok(l, Tatom, line, col);
		t.text = copytext(l, p);
		return t;
	}
	switch(c){
	case '$':
		if(peek(l) == '{'){ get(l); return tok(l, Ttupleopen, line, col); }
		break;
	case '#':
		if(peek(l) == '{'){ get(l); return tok(l, Tmapopen, line, col); }
		break;
	case '{': return tok(l, Tlbrace, line, col);
	case '}': return tok(l, Trbrace, line, col);
	case '(': return tok(l, Tlparen, line, col);
	case ')': return tok(l, Trparen, line, col);
	case ',': return tok(l, Tcomma, line, col);
	case ';': return tok(l, Tsemi, line, col);
	case '+': return tok(l, Tplus, line, col);
	case '-': return tok(l, Tminus, line, col);
	case '*': return tok(l, Tstar, line, col);
	case '/': return tok(l, Tslash, line, col);
	case '%': return tok(l, Tpercent, line, col);
	case '=':
		if(peek(l) == '>'){ get(l); return tok(l, Tarrow, line, col); }
		if(peek(l) == '='){ get(l); return tok(l, Teq, line, col); }
		return tok(l, Tassign, line, col);
	case '!':
		/* `!=` is not-equal; a lone `!` is send. `a !=b` is therefore a comparison. */
		if(peek(l) == '='){ get(l); return tok(l, Tne, line, col); }
		return tok(l, Tbang, line, col);
	case '<':
		if(peek(l) == '='){ get(l); return tok(l, Tle, line, col); }
		return tok(l, Tlt, line, col);
	case '>':
		if(peek(l) == '='){ get(l); return tok(l, Tge, line, col); }
		return tok(l, Tgt, line, col);
	}
	snprint(l->err, sizeof l->err, "invalid character 0x%02x", c);
	return tok(l, Tbad, line, col);
}

char *
tokname(int k)
{
	static char *n[] = {
		"end of file", "identifier", "atom", "integer",
		"fn", "match", "receive", "after", "if", "else", "spawn", "self", "mkref", "exit",
		"and", "or", "not", "when",
		"{", "}", "(", ")", ",", ";", "=>", "=", "!", "==", "!=", "<", "<=", ">", ">=",
		"+", "-", "*", "/", "%", "${", "#{", "comment",
	};
	if(k == Tbad) return "invalid token";
	if(k < 0 || k >= nelem(n)) return "token";
	return n[k];
}
