# Milestone 01 - Frontend

## Goal

Parse the smallest useful Nervous source subset into a stable, inspectable AST.

## Read first

- `../docs/semantics.md`
- `../docs/decisions.md`

Consult parsing and syntax sections of `../nervous_design.md` only when the compact documents do not answer a question.

## Initial source subset

- integer literals and atoms;
- variables and `_` in patterns;
- exact tuple construction and patterns;
- named function declarations and calls;
- blocks, asserted binding, `match`, and basic arithmetic;
- semicolon separation and optional trailing semicolon;
- unambiguous current delimiters: blocks `{}`, tuples `${}`, and grouping/calls `()`;
- a canonical AST-to-current-source formatter;
- a v1 compatibility input mode for the temporary brace-tuple syntax.

Process syntax may be parsed now if cheap, but need not be accepted by later stages until milestone 05. Lists and binaries may be deferred to their owning implementation milestones.

## Work

1. Settle the milestone 01 semantic questions and record answers in `docs/decisions.md`.
2. Write a lexer with source spans and useful errors.
3. Write recursive-descent declaration/statement parsing and Pratt expression parsing.
4. Represent patterns distinctly from arbitrary expressions where that simplifies validation.
5. Validate pattern contexts and binding rules; mixed function-clause tuple arities are permitted.
6. Add a deterministic AST printer for tests and debugging.

## Deliverables

- Lexer and parser library.
- AST definitions with source locations.
- AST-print mode in the command.
- Canonical stdout-only format mode and v1-to-current rewriting.
- Positive, negative, formatting, comment-preservation, migration, and round-trip golden tests.

## Required tests

- Empty and minimal functions.
- Clause versus ordinary zero-argument function body disambiguation.
- Nested blocks and trailing semicolons.
- Tuple literals and exact tuple patterns.
- Repeated variables, `_`, and duplicate illegal bindings.
- Operator precedence and left-to-right source preservation.
- Invalid token, unterminated construct, and useful line/column diagnostics.

## Exit criterion

Every accepted construct has a deterministic AST representation, malformed input fails without crashes or cascades of misleading diagnostics, and formatting preserves comment text/order while producing current syntax.

## Status

Complete. See `../STATUS.md` for the accepted implementation summary.

## Not in scope

Name resolution across modules, type analysis, optimization, bytecode emission, closures, maps, or final binary grammar.
