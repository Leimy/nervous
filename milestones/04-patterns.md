# Milestone 04 - Patterns and Clauses

## Goal

Implement one transactional exact-matching mechanism and use it for asserted bindings, function clauses, and `match`.

## Read first

- `../docs/semantics.md`
- `../docs/decisions.md`
- `../docs/questions.md`, section "Milestone 04"
- `03-interpreter.md`

## Semantics that must hold

- Tuple arity is exact.
- `_` never binds.
- An unbound variable tentatively binds.
- An already-bound variable tests equality.
- Failed patterns expose none of their tentative bindings.
- Clauses are tried in source order wherever patterns overlap.
- Direct match failure exits abnormally.

Exact arity does not eliminate overlap between patterns of the same shape.

## Scope

- Literal, variable, wildcard, tuple, and whole-value patterns if `@` is retained now.
- Function-clause selection.
- `match` expressions.
- Pattern compiler lowering to VM tests and failure edges.
- Guards only if the milestone question is resolved in favor of including them.

## Required tests

- Exact tuple arity rejection.
- Nested tuple destructuring.
- Repeated-variable equality.
- Rollback after a late nested failure.
- Overlapping clauses preserving source order.
- Different arities dispatching independently.
- Asserted-match failure reason.
- No-clause function failure.

## Exit criterion

All three matching contexts share the same observable rules, and failed attempts never leak bindings.

## Current implementation state

Complete. One transactional matcher serves asserted bindings, function clauses, and `match`; literal, variable, wildcard, and exact nested tuple patterns lower to primitive VM tests. Repeated variables use structural equality, failed attempts do not expose tentative bindings, clauses preserve source order, and failure reasons are stable. Runtime, parsed-pattern, primitive-bytecode, and compiled-source harnesses all passed on 2026-08-18. Guards and whole-value `@` remain deferred by D034.

## Not in scope

Selective receive scanning, lists, binaries, maps, decision-tree optimization, or exhaustiveness analysis.
