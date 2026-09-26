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
	Ebinagg,
	Ebinseg,
};

/* D076-D078: binary aggregate and segment classes (Ebinseg ival, high bits). */
enum {
	Binsegint = 0,
	Binsegsized,
	Binsegrest,
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
 * Ebinagg	list = segments (Ebinseg); no other fields.
 * Ebinseg	D076: ival = (class << 8) | (width << 2) | (signed<<1 |
 *		little), class one of Binsegint/Binsegsized/Binsegrest, width
 *		the byte count 1, 2, 4, 8 for Binsegint (0 otherwise);
 *		left = the segment operand (pattern position: an Evar, Ewild,
 *		or Eint for Binsegint, an Evar/Ewild for the binary forms;
 *		construction position: any expression); right = the size
 *		expression for Binsegsized, nil otherwise.
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
	Tbinopen,
	Tbinclose,
	Tlbrace,
	Trbrace,
	Tlparen,
	Trparen,
	Tcomma,
	Tsemi,
	Tcolon,
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
	Token peek;		/* one-token lookahead (D076 segment disambiguation) */
	int havepeek;
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
