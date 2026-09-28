# Early Semantic Contract

This is the compact normative contract for the initial implementation sequence; `decisions.md` records the reasoning behind each rule and wins on any conflict. Long-form rationale is in the non-normative design files split from `../nervous_design.md` (`language-semantics.md` and its siblings; see `README.md` in this directory), whose examples use superseded syntax.

## Values in the early subset

The implemented subset contains:

- checked 64-bit integers (D007, D061);
- interned atoms (D062);
- exact tuples, written `${a, b}`;
- binaries, written `<<...>>` (milestone 09, D076-D079; see "Binaries" below);
- opaque PIDs and Refs (D038).

Lists, maps, floats, closures, `@` whole-value binding, distribution, links, monitors, and catch semantics are deferred.

## Variables and scope

Variables are single-assignment. `_` never binds. An already-bound variable in a pattern compares for equality.

Pattern bindings are transactional: no new binding becomes visible unless the complete pattern and guard succeed.

A function clause is one lexical scope. Nested blocks do not permit shadowing or rebinding. Bindings introduced by a `match` or receive clause, or inside an `if` branch, are local to that clause body or branch and do not escape it.

## Matching and clauses

Tuple patterns match exact shapes, as list patterns will once lists exist; a list tail pattern will be the explicit way to accept a remainder (D001). Binary patterns consume the complete binary unless they contain a final remainder segment.

Clauses are ordered. Exact arity makes clauses of different tuple arities disjoint, but patterns of the same arity may overlap and retain source-order behavior.

A function, `match`, or `receive` clause may carry a guard: `fn f(x) when x > 0 { ... }`, `pattern when guard => body`. The guard is evaluated after the pattern has matched and bound its variables and before the clause is selected; the clause is selected only if the guard yields exactly `'true`. A guard that yields `'false` or any non-boolean, or that faults for any reason (`badarith` on a non-integer, `divide_by_zero`, `overflow`, ...), makes the clause fail like a pattern mismatch: the next clause is tried, and in a `receive` the candidate stays in the mailbox. A guard can never terminate the process. Guard expressions are restricted to literals, variables, tuple construction, the operators, and the type tests `is_int`, `is_atom`, `is_tuple`, `is_pid`, `is_ref`, `is_binary`. A guard may not contain calls, bindings, blocks, `match`, `receive`, `if`, binary aggregates, or any process or I/O form; each is a compile error (D060, D079). The type tests are also ordinary expressions usable anywhere. A negative integer literal is a pattern.

A direct failed match exits the current process abnormally.

## Functions

The initial implementation has statically named functions only. Source-level function identity is its name. Every function receives one semantic argument tuple, so clauses of different tuple arities may coexist. A conventional call `f(a, b)` supplies `${a, b}`; the compiler may avoid materializing that tuple when unobservable. A module component will be added when the module system is designed. There are no anonymous functions or closures in the early subset.

A function is written as one or more adjacent declarations `fn name(pattern, ...) { ... }`; each declaration is one clause and the clauses are tried in source order (D058). A non-adjacent redeclaration is a duplicate-function error. `fn main() { ... }` is an ordinary clause with an empty head, so it faults `function_clause` when called with arguments.

`spawn worker(args...)` names a statically resolved function and evaluates its arguments left to right; they form the child's semantic argument tuple.

A call in tail position is a proper tail call: it replaces the caller's frame rather than pushing a new one, so it does not count against the frame limit and self-recursion in tail position runs in constant frame depth. Tail position is the final expression of a function clause body, the final expression of a block that is itself in tail position, and every branch of an `if`, every clause body of a `match` or `receive`, and the `after` body of a `receive`, when that construct is itself in tail position. Anything that still needs the call's value afterwards -- a binding `x = f()`, a send `pid ! f()`, an operator operand, a tuple element, an `exit f()` -- is not a tail call. This is what makes an Erlang-style message loop (`fn loop(s) { receive { m => loop(s2); } }`) and a generator that recurses once per element viable under the bounded frame limit.

## Expressions

Blocks are expression-valued. Semicolons separate expressions; a trailing semicolon does not change the block value, and a semicolon is optional after a block-like expression (`{...}`, `match`, `receive`, `if`), which is terminated by its own closing brace. `match` and `receive` clauses follow the same rule: `pattern => expr;` or `pattern => { ... }`.

There is no general truthiness. The ordinary atoms `'true` and `'false` are boolean results. `if cond { a } else { b }` requires `cond` to be exactly `'true` or `'false` and faults `match_fail` otherwise; `else if` chains, and an `if` with no `else` yields `'ok`. Branch bindings are local to the branch. `==` and `!=` are D028 structural equality.

`and`, `or`, and `not` are the boolean operators and follow the same rule as `if`: every operand must be exactly `'true` or `'false`, and anything else faults `match_fail`. `and` and `or` short-circuit -- the right operand is evaluated only when the left one does not decide the result -- so a binding made inside a right operand exists on only one path and is local to that operand, as an `if` branch's bindings are. Unary `-` is checked subtraction from zero (negating the minimum integer faults `overflow`), and a negative integer literal is a compile-time constant; unary `+` is the identity on an integer and faults `badarith` on anything else. A negative integer literal is also a pattern (D060).

Calls, aggregate elements, and operator operands evaluate left to right. Binding `=` and send `!` are the two lowest-precedence operators and associate to the right; everything else associates to the left.

## Processes and messages

Nervous processes are lightweight VM processes, not Plan 9 processes or libthread threads. They are executed by one or more schedulers, each a Plan 9 proc sharing the runtime's memory (`nervous -p N`, D070, D081-D089). A program cannot observe which scheduler runs it, how many there are, or when a process moves between them: every rule in this section holds identically at N=1 and at N>1, and single-scheduler execution is the same code with N=1, not a separate mode (D087). At every instant exactly one scheduler owns a process; a running process never moves, and a waiting process stays with the scheduler that owns it until it is woken there (D083).

Send is asynchronous. Ordinary message terms are copied into a self-contained mailbox fragment; no mailbox points into the sender's private heap.

Mailbox and message limits are word counts of the copied fragment, checked all-or-nothing during the copy (D064, D066). They are implementation budgets, not language-visible term sizes.

Messages from one sender to one receiver arrive in the sender's program order; messages from different senders have no relative order (D036). This holds across schedulers: a send appends to the receiver's mailbox under one runtime lock, and one sender is one process, so its sends are serialized by its own execution (D081).

A system in which no process is runnable, no receive deadline is armed, and no collection is in flight is deadlocked, and the `-r` command reports it as such (D046). With several schedulers this is decided exactly, never guessed: the last scheduler to go idle declares it under the same lock every wake takes (D085).

Send is written `pid ! message` and returns the sent value, so `a ! b ! m` delivers `m` to `b` and then to `a`. Sending to a dead or stale PID drops the message and still returns it. `self` is the current PID, `mkref` is a fresh opaque unique Ref, and `exit reason` terminates the current process with an arbitrary reason term. These are keywords, not calls (D058).

Selective receive is written `receive { pattern => body; ... }`. It scans messages oldest-first and clauses in source order for each candidate, consumes the first selected candidate before evaluating its body, and preserves all unmatched messages in order. If no candidate matches, the process waits; after wakeup, scanning restarts at the oldest retained message. Receive-clause bindings are transactional and local to the selected clause.

A receive may end with one `after duration => body` clause (D048-D051). The duration is an integer count of nanoseconds (0 through `NvMaxduration`) or `'infinity`. Any other value faults `bad_timeout`, and an out-of-range literal is a compile error. Duration literals such as `5s` are not implemented yet. The deadline is computed once, on entry. The timeout body runs only after a complete scan finds nothing, so a timeout is a lower bound on waiting and never fires while an acceptable message is queued. `after 0` is a single nonblocking scan.

## Binaries

A binary is an immutable, self-contained byte string (D076). It is never a view into another binary. Equality compares bytes exactly, and a binary prints as `<<b0, b1, ...>>`. The aggregate `<< segment, ... >>` has three segment forms:

- `v:W` with optional `/signed` or `/unsigned` and `/big` or `/little`: an integer segment. `W` must be a literal 8, 16, 32, or 64, and the default is unsigned big-endian (D078).
- `v:size/binary`: a sized binary segment, where `size` is a byte count.
- `v/binary`: an unsized binary segment.

`<<>>` is the empty binary. A segment value or size is one primary or unary expression, so parenthesize anything looser. The modifier names are ordinary identifiers outside modifier position.

Construction evaluates every segment value and size left to right, then builds the result (D077). It faults `badarith` on a non-integer integer value or size, and `overflow` on a value outside the segment's range. It faults `bad_binary` on a negative size or a non-binary `/binary` operand, and `system_limit` when the heap budget is exhausted (D079).

Matching proceeds left to right with no backtracking, and its bindings are transactional like every other pattern. Integer segments in a pattern are a variable, `_`, or an integer literal. A size may name a variable already bound outside the pattern or by an earlier segment of the same pattern. A size bound by the same or a later segment is a compile error. An unsized `/binary` segment is allowed only last in a pattern and binds the remaining bytes, possibly none. Without one, the pattern must consume the whole binary, so a trailing byte is a mismatch. A subject that is not a binary, is too short, or has a bad size is a mismatch, never a fault. A binding of a sub-binary copies its bytes.

## Host output

`print(value)` and `eprint(value)` write a term to host stdout and stderr respectively, rendered exactly as `nvvalueprint` already renders it elsewhere (so an atom keeps its leading quote, e.g. `'hello_world`). Both are reserved intrinsics and are the only names a program may not define as functions; the process forms are keywords instead (D058). Each blocks the calling process until the write completes and returns the atom `'ok` on success; a write failure faults the process with `io_error`. Both count as one ordinary reduction (D030) and never change process lifecycle state.

Ordering (D057 as restated by D086): output from one process appears in that process's program order. Output from different processes appears in dispatch order when both are dispatched by the same scheduler, and in no defined order across schedulers. A line -- the rendered value and its newline -- is never interleaved with another process's line, on any scheduler. With one scheduler (the default) every process shares that scheduler, so the guarantee is exactly D057's total dispatch order. There is no message or buffering guarantee beyond this. See D053 through D057 for the full rationale, including why this is not and will never be a general FFI.

## Failure

Expected errors are ordinary values. In the early VM, runtime faults cause abnormal process exit with a bounded symbolic reason rendered as `fault <reason>`; the implementation stores that reason as a diagnostic string corresponding to the D029 reason atom. Explicit `exit reason` carries an arbitrary term. Converting all VM faults into first-class reason terms is required before links, monitors, or catch semantics can observe them.

Resource exhaustion must be detected and reported rather than causing memory corruption. Source expression nesting is limited to 256 and excessive nesting is an ordinary parse error. The runtime limits process count, mailbox and message words, non-tail call frames, term depth, and each process's heap words (`maxheap`: live heap, adopted fragments, and frame-stack capacity; D066). Exhausting call depth or the heap budget faults the offending process with `system_limit`. An over-depth or over-budget send reports `mailbox_full`. Equality and printing past the term-depth ceiling fault `system_limit` (D061). The portable provisional term-depth ceiling is 256, and a runtime may configure a smaller send ceiling.

## Machine boundary

Source semantics do not depend on pointer layout, host C structs, scheduler ownership, or Plan 9 file descriptors. Bytecode and terms must preserve that separation even when the first implementation uses convenient in-memory representations.
