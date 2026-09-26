# Nervous Language Philosophy and Architecture

## 42. Static Analysis Without Closing the World

The runtime language is dynamically typed.

Static analysis may still reason about sets of terms, unions, intersections, refinements, reachable clauses, overlapping clauses, impossible clauses, and possible match failures.

The central analysis question may be:

> What set of terms can reach this point?

Exact matching (Section 3) makes this more tractable than a projective-by-default rule would have: clauses that differ in tag or arity are provably disjoint, and ordinary exhaustiveness/overlap analysis applies without needing an interval or subsumption analysis over open-ended shapes.

Static analysis should remain optional initially and must not force distributed protocols into closed algebraic data types.

---

## 43. Parsing

The parser should be intentionally boring.

A hand-written recursive-descent parser plus Pratt parsing for operators is sufficient.

Whitespace has no syntactic significance.

Braces delimit blocks.

One disambiguation rule is required for `fn` bodies. A body is either entirely clause form or a plain statement body (Section 12), and the parser decides with a single token of lookahead: if the first token after the opening brace is `(`, the body is clause form. A plain body therefore may not begin with a parenthesized expression statement, which costs nothing in practice and keeps the parser free of unbounded lookahead and backtracking.

---

## 44. First Implementation Milestone

The first implementation should support:

- atoms;
- integers;
- tuples;
- basic binaries;
- single-assignment variables;
- `_`;
- exact matching (tuples, lists, binaries);
- function calls;
- tail calls;
- asserted match failure;
- PIDs;
- Refs;
- `spawn`;
- asynchronous send;
- selective receive;
- timeout;
- process exit.

Basic binary matching belongs in the early milestone so the pattern compiler is tested against more than tuple destructuring.

Start with one scheduler, a simple process-local heap, simple GC, a simple mailbox, and a simple bytecode interpreter.

Then add multiple `rfork` schedulers, `QLock`-protected cross-scheduler queues, migration/work stealing, and then distribution.

---

## 45. Guiding Principles

1. **Patterns are fundamental.**
2. **Variables bind once.**
3. **`_` never binds.**
4. **Patterns are exact; there is no separate strict/loose mode to remember.**
5. **A direct failed match is a process failure.**
6. **Messages are asynchronous.**
7. **Processes are extremely cheap.**
8. **Process heaps are independently collectible.**
9. **Distributed version skew is handled by separating dispatch from field extraction, never by loosening matching.**
10. **Use positional aggregates for positional meaning and maps/records for keyed meaning.**
11. **The source language remains small.**
12. **The machine owns concurrency semantics.**
13. **The bytecode format is portable.**
14. **Simple synchronization comes before clever synchronization.**
15. **Static analysis may prove strong properties without closing the dynamic term universe.**
16. **External resources are capabilities, not magical migratable state.**
17. **Implementation complexity must justify itself through semantic value or measurement.**

---

## 46. Central Idea

> Computation consists largely of describing the shapes of values a process is prepared to accept.

Functions accept values through patterns.

Messages are accepted through patterns.

Binary protocols are decoded through patterns.

Errors are handled through patterns.

Patterns describe the exact shape a function or message clause requires.

Distributed version skew is handled by keeping dispatch exact and pushing tolerance into map-based field extraction and explicit decode steps -- the language expects version skew, but it does not ask the matching operator to absorb it.

Underneath that language sits a portable concurrent machine whose processes are cheap, independently collectible, asynchronously communicating, and scheduled in parallel over a small number of real execution contexts.
