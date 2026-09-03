# Canonical Formatting

Nervous has one source layout. `nervous -f file.nv` parses current syntax and writes canonical current syntax to stdout. `nervous -F old.nv` parses the previous (v2) syntax and writes canonical current syntax to stdout; this is the migration path for source written before D058.

There are deliberately no style flags. Indentation uses tabs, operators have one surrounding space, blocks place expressions on separate lines, and declarations are separated by one empty line.

Formatting must be:

- semantics-preserving;
- deterministic;
- idempotent;
- parseable by the current parser.

The formatter never overwrites its input. Redirect output to a new file and inspect it before replacing source.

## Layout

Each function clause is its own declaration, `fn name(patterns) {` on one line, body indented, `}` alone. Clauses of one function are consecutive declarations separated by an empty line like any other declarations.

Inside a block, expressions are separated by `;` except after a block-like expression (`{...}`, `match`, `receive`, `if`), which closes with its own brace and takes no `;`. The final expression never takes one.

`match` and `receive` clauses are one per line at one extra indent. An expression body stays on the clause line and ends with `;`: `${'ok, v} => v + 1;`. A block body opens on the clause line and takes no `;`:

```
match x {
	${'ok, v} => {
		print(v);
		v
	}
	_ => 0;
}
```

The `after` clause of a receive follows the same rule. `if` chains place `else if` and `else` on the closing brace of the preceding block.

Process operations are keyword forms: `self`, `mkref`, `spawn worker(args...)`, `pid ! message`, `exit reason`. `!` is written with one space either side (which also keeps it visually distinct from `!=`). Binding and send bind loosest and associate right, so `x = pid ! msg` and `a ! b ! m` print without parentheses while `(a ! b) ! m` keeps them.

`tests/frontend/*.fmt` are the canonical fixtures; `compat.nv`/`compat.fmt` show the previous syntax and its conversion.

## Comments

Line comments are retained as ordered AST trivia and emitted in canonical `//` form. Comments keep their text and order, but formatting may relocate an end-of-line comment to its own line before the next syntactic node. This avoids binding documentation to unreliable whitespace while preserving it losslessly.

An in-place mode remains deferred until automated parse-format-parse and comment-preservation tests run as part of the normal test suite.
