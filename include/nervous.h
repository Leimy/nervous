typedef struct Span Span;
typedef struct Expr Expr;
typedef struct Exprs Exprs;
typedef struct Clause Clause;
typedef struct Fn Fn;
typedef struct Program Program;
typedef struct Lexer Lexer;
typedef struct Parser Parser;
typedef struct Comment Comment;

struct Span {
	int line;
	int col;
	int endline;
	int endcol;
};

enum {
	Eint,
	Eatom,
	Evar,
	Ewild,
	Etuple,
	Ecall,
	Eblock,
	Ebind,
	Ematch,
	Ereceive,
	Eif,
	Eself,
	Emkref,
	Espawn,
	Esend,
	Eexit,
	Eunary,
	Ebinary,
};

/*
 * Expr field use by kind (D058):
 *
 * Ecall	text = function name, list = arguments.
 * Ebind	left = pattern, right = value.
 * Ematch	left = scrutinee, clauses = pattern clauses.
 * Ereceive	clauses = pattern clauses. D051: an optional trailing
 *		`after duration => body` is not a Clause; it is recorded as
 *		left = duration, right = timeout body, both nil without it.
 * Eif		left = condition, right = then block, list = at most one
 *		element holding the else branch (an Eblock or another Eif);
 *		list is nil when there is no else.
 * Espawn	text = statically named function, list = arguments.
 * Esend	left = destination pid, right = message.
 * Eexit	left = reason.
 * Eself, Emkref	no operands.
 * Eunary	text = operator, left = operand.
 * Ebinary	text = operator, left, right.
 */

struct Exprs {
	Expr *expr;
	Exprs *next;
};

struct Expr {
	int kind;
	Span span;
	vlong ival;
	char *text;
	Expr *left;
	Expr *right;
	Exprs *list;
	Clause *clauses;
};

/* D060: guard is nil when the clause has no `when`. */
struct Clause {
	Span span;
	Exprs *patterns;
	Expr *guard;
	Expr *body;
	Clause *next;
};

/*
 * Every function is a list of clauses; a source declaration
 * `fn name(patterns) { ... }` is one clause, and adjacent declarations
 * of the same name are merged in source order. A clause body is always
 * an Eblock.
 */
struct Fn {
	Span span;
	char *name;
	Clause *clauses;
	Fn *next;
};

struct Comment {
	Span span;
	char *text;
	Comment *next;
};

struct Program {
	Fn *fns;
	Comment *comments;
};

enum {
	Tbad = -1,
	Teof,
	Tident,
	Tatom,
	Tint,
	Tfn,
	Tmatch,
	Treceive,
	Tafter,
	Tif,
	Telse,
	Tspawn,
	Tself,
	Tmkref,
	Texit,
	Tand,
	Tor,
	Tnot,
	Twhen,
	Tlbrace,
	Trbrace,
	Tlparen,
	Trparen,
	Tcomma,
	Tsemi,
	Tarrow,
	Tassign,
	Tbang,
	Teq,
	Tne,
	Tlt,
	Tle,
	Tgt,
	Tge,
	Tplus,
	Tminus,
	Tstar,
	Tslash,
	Tpercent,
	Ttupleopen,
	Tmapopen,
	Tcomment,
};

typedef struct Token Token;
struct Token {
	int kind;
	Span span;
	char *text;
	vlong ival;
};

/*
 * compat selects the previous (v2) surface syntax: `fn name { ${..} =>
 * body; }` clause blocks, call-shaped process intrinsics, and none of
 * the v3 keywords. It exists only so that `nervous -F` can rewrite old
 * source into canonical current syntax.
 */
struct Lexer {
	char *name;
	char *src;
	long n;
	long p;
	int line;
	int col;
	int compat;
	char err[256];
};

enum {
	NvMaxsourcedepth = 256,
};

struct Parser {
	Lexer lex;
	Token tok;
	int compat;
	int depth;
	Comment *comments;
	char err[256];
};

Expr *exprnew(int, Span);
Exprs *exprappend(Exprs **, Expr *);
Clause *clauseappend(Clause **, Clause *);
Fn *fnappend(Fn **, Fn *);
void exprfree(Expr *);
void programfree(Program *);
void programprint(Biobuf *, Program *);
int exprblocklike(Expr *);

void lexinit(Lexer *, char *, char *, long);
Token lexnext(Lexer *);
void tokenfree(Token *);
char *tokname(int);

Program *parseprogrammode(Parser *, char *, char *, long, int);
Program *parseprogram(Parser *, char *, char *, long);
Program *parseprogramcompat(Parser *, char *, char *, long);
void formatprogram(Biobuf *, Program *);
