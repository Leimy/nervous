# Early Semantic Contract

This is the compact normative contract for the initial implementation sequence. The long-form rationale remains in `../nervous_design.md`.

## Values in the early subset

The first subset contains:

- checked small integers;
- interned atoms;
- exact tuples;
- later, lists and binaries;
- opaque PIDs and Refs once processes are introduced.

Maps, floats, closures, distribution, links, monitors, and catch semantics are deferred.

## Variables and scope

Variables are single-assignment. `_` never binds. An already-bound variable in a pattern compares for equality.

Pattern bindings are transactional: no new binding becomes visible unless the complete pattern and guard succeed.

A function clause is one lexical scope. Nested blocks do not permit shadowing or rebinding. Bindings introduced by a `match` or receive clause, or inside an `if` branch, are local to that clause body or branch and do not escape it.

## Matching and clauses

Tuple and list patterns match exact shapes. A list tail pattern explicitly accepts a remainder. Binary patterns consume the complete binary unless they contain a final remainder segment.

Clauses are ordered. Exact arity makes clauses of different tuple arities disjoint, but patterns of the same arity may overlap and retain source-order behavior.

A function, `match`, or `receive` clause may carry a guard: `fn f(x) when x > 0 { ... }`, `pattern when guard => body`. The guard is evaluated after the pattern has matched and bound its variables and before the clause is selected; the clause is selected only if the guard yields exactly `'true`. A guard that yields `'false` or any non-boolean, or that faults for any reason (`badarith` on a non-integer, `divide_by_zero`, `overflow`, ...), makes the clause fail like a pattern mismatch: the next clause is tried, and in a `receive` the candidate stays in the mailbox. A guard can never terminate the process. Guard expressions are restricted to literals, variables, tuple construction, the operators, and the type tests `is_int`, `is_atom`, `is_tuple`, `is_pid`, `is_ref`; calls, bindings, blocks, `match`, `receive`, `if`, and every process or I/O form are compile errors in a guard (D060). The type tests are also ordinary expressions usable anywhere. A negative integer literal is a pattern.

A direct failed match exits the current process abnormally.

## Functions

The initial implementation has statically named functions only. Source-level function identity is its name. Every function receives one semantic argument tuple, so clauses of different tuple arities may coexist. A conventional call `f(a, b)` supplies `${a, b}`; the compiler may avoid materializing that tuple when unobservable. A module component will be added when the module system is designed. There are no anonymous functions or closures in the early subset.

A function is written as one or more adjacent declarations `fn name(pattern, ...) { ... }`; each declaration is one clause and the clauses are tried in source order (D058). A non-adjacent redeclaration is a duplicate-function error. `fn main() { ... }` is an ordinary clause with an empty head, so it faults `function_clause` when called with arguments.

`spawn worker(args...)` names a statically resolved function and evaluates its arguments left to right; they form the child's semantic argument tuple.

A call in tail position is a proper tail call: it replaces the caller's frame rather than pushing a new one, so it does not count against the frame limit and self-recursion in tail position runs in constant frame depth. Tail position is the final expression of a function clause body, the final expression of a block that is itself in tail position, and every branch of an `if`, every clause body of a `match` or `receive`, and the `after` body of a `receive`, when that construct is itself in tail position. Anything that still needs the call's value afterwards -- a binding `x = f()`, a send `pid ! f()`, an operator operand, a tuple element, an `exit f()` -- is not a tail call. This is what makes an Erlang-style message loop (`fn loop(s) { receive { m => loop(s2); } }`) and a generator that recurses once per element viable under the bounded frame limit.

## Expressions

Blocks are expression-valued. Semicolons separate expressions; a trailing semicolon does not change the block value, and a semicolon is optional after a block-like expression (`{...}`, `match`, `receive`, `if`), which is terminated by its own closing brace. `match` and `receive` clauses follow the same rule: `pattern => expr;` or `pattern => { ... }`.

There is no general truthiness. The ordinary atoms `'true` and `'false` are boolean results. `if cond { a } else { b }` requires `cond` to be exactly `'true` or `'false` and faults `match_fail` otherwise; `else if` chains, and an `if` with no `else` yields `'ok`. Branch bindings are local to the branch. `==` and `!=` are D028 structural equality.

`and`, `or`, and `not` are the boolean operators and follow the same rule as `if`: every operand must be exactly `'true` or `'false`, and anything else faults `match_fail`. `and` and `or` short-circuit -- the right operand is evaluated only when the left one does not decide the result -- so a binding made inside a right operand exists on only one path and is local to that operand, as an `if` branch's bindings are. Unary `-` is checked subtraction from zero (negating the minimum integer faults `overflow`), and a negative integer literal is a compile-time constant; unary `+` is the identity on an integer and faults `badarith` on anything else. Negative literals are expressions, not yet patterns.

Calls, aggregate elements, and operator operands evaluate left to right. Binding `=` and send `!` are the two lowest-precedence operators and associate to the right; everything else associates to the left.

## Processes and messages

Nervous processes are lightweight VM processes, not Plan 9 processes or libthread threads.

Send is asynchronous. Ordinary message terms are copied into a self-contained mailbox fragment; no mailbox points into the sender's private heap.

Current mailbox byte limits use the provisional host-representation estimate in D040. That estimate is an implementation budget only, not a language-visible term size; R3 will replace it when the final fragment/heap representation exists.

Send is written `pid ! message` and returns the sent value, so `a ! b ! m` delivers `m` to `b` and then to `a`. Sending to a dead or stale PID drops the message and still returns it. `self` is the current PID, `mkref` is a fresh opaque unique Ref, and `exit reason` terminates the current process with an arbitrary reason term. These are keywords, not calls (D058).

Selective receive is written `receive { pattern => body; ... }`. It scans messages oldest-first and clauses in source order for each candidate, consumes the first selected candidate before evaluating its body, and preserves all unmatched messages in order. If no candidate matches, the process waits; after wakeup, scanning restarts at the oldest retained message. Receive-clause bindings are transactional and local to the selected clause. Timeout behavior remains milestone 06 work.

## Host output

`print(value)` and `eprint(value)` write a term to host stdout and stderr respectively, rendered exactly as `nvvalueprint` already renders it elsewhere (so an atom keeps its leading quote, e.g. `'hello_world`). Both are reserved intrinsics and are the only names a program may not define as functions; the process forms are keywords instead (D058). Each blocks the calling process until the write completes and returns the atom `'ok` on success; a write failure faults the process with `io_error`. Both count as one ordinary reduction (D030) and never change process lifecycle state. Because one scheduler dispatches one process per quantum, output from different processes is already totally ordered by dispatch order, with no message or buffering guarantee beyond that. See D053 through D057 for the full rationale, including why this is not and will never be a general FFI.

## Failure

Expected errors are ordinary values. In the early VM, runtime faults cause abnormal process exit with a bounded symbolic reason rendered as `fault <reason>`; the implementation stores that reason as a diagnostic string corresponding to the D029 reason atom. Explicit `exit(reason)` carries an arbitrary term. Converting all VM faults into first-class reason terms is required before links, monitors, or catch semantics can observe them.

Resource exhaustion must be detected and reported rather than causing memory corruption. Source expression nesting is limited to 256 and excessive nesting is an ordinary parse error. The milestone 05 runtime limits process count, mailbox/message bytes, non-tail call frames, and term depth. Call-depth and tuple-construction depth exhaustion fault the offending process with `system_limit`; an over-depth send reports `mailbox_full`. The portable provisional term-depth ceiling is 256, and a runtime may configure a smaller send ceiling. Exact process-heap and aggregate-memory accounting remain milestone 07 work.

## Machine boundary

Source semantics do not depend on pointer layout, host C structs, scheduler ownership, or Plan 9 file descriptors. Bytecode and terms must preserve that separation even when the first implementation uses convenient in-memory representations.
