# Nervous

**Nervous is an experimental concurrent language and portable 64-bit virtual machine designed for 9front.** It combines Erlang-inspired lightweight processes, asynchronous message passing, selective pattern matching, process-local garbage collection, supervision, and a Plan 9-native multicore runtime built around `rfork`.

The language emphasizes single-assignment variables and exact structural matching, with distributed version skew handled by a clean separation between dispatch and field extraction rather than by loosening the matching rules themselves.

The longer-term idea is broader than a new language: Nervous is intended to explore what a modern successor to machines such as BEAM and Dis might look like—small, portable, concurrency-native, and capable of running enormous numbers of isolated processes across cores and, potentially, across machines.

## Status

WARNING: This is a historical long-form design and not the current normative specification. Several sections retain superseded syntax and semantics, including function identity/arity, tuple delimiters, comments, and implementation sequencing. For current work, read `README.md`, `docs/semantics.md`, `docs/decisions.md`, `docs/questions.md`, the active milestone, and `STATUS.md`. When this document conflicts with those compact sources, the compact sources win; do not copy a rule from here without checking its decision status.

This is a design document, not a compatibility specification.

Nervous draws heavily from Erlang/BEAM, Inferno/Dis, Plan 9, Lisp, ML-family languages, and other functional systems, but compatibility with any existing language or machine is explicitly **not** a goal.

The main design rule is:

> Keep the language small. Spend complexity in the machine and runtime only where it buys semantic power, isolation, portability, or measured performance.

---

# Part I — Language Semantics

## 1. Terms

The machine operates on **terms**.

The initial term universe is intended to remain small:

```text
Term :=
    Integer
  | Float
  | Atom
  | Tuple
  | List
  | Binary
  | Map
  | Pid
  | Ref
```

Additional term kinds should be introduced only when justified.

Strings need not be a primitive term. They may be binaries with a UTF-8 convention.

General-purpose maps and record-like values are the same underlying keyed aggregate concept. Positional data uses tuples or lists; non-positional data uses maps/records.

A tagged record-like syntax may be provided as a convenient form for protocol values:

```text
'request{
    from: self,
    ref: r,
    path: path
}
```

The precise map/record construction syntax can still change, but the semantic distinction is already clear:

- tuples/lists are positional;
- maps/records are keyed and non-positional.

---

## 2. Single-Assignment Variables

Variables bind once.

```text
x = expression;
```

binds `x`.

There is no ordinary rebinding.

Afterward, every occurrence of `x` refers to the same value for the rest of its lexical lifetime.

Inside a pattern:

- an unbound variable binds;
- an already-bound variable compares for equality;
- `_` discards and **never binds**.

Example:

```text
r = ref();

receive {
    {'reply, r, value} =>
        value;
}
```

If `r` is already bound, the second tuple field must equal that value.

No pin operator is needed.

Changing state is normally expressed by passing new state to another function invocation:

```text
fn counter {
    (n) => receive {
        'inc =>
            counter(n + 1);

        {'get, from} => {
            from <- n;
            counter(n);
        };
    };
}
```

---

## 3. One Matching Model

Pattern matching is the central semantic operation of Nervous.

The same rules apply in function clause heads, `match`, `receive`, binary decoding, and direct asserted bindings.

Matching is exact.

A pattern states the complete required shape of the value at every position-counted level: a tuple pattern's arity must equal the value's arity; a list pattern's length, up to any explicit remainder, must be satisfied exactly; a binary pattern without a remainder segment must consume the entire binary. There is no separate strict/loose toggle to remember, because there is no loose mode to opt out of.

Exactness only has meaning for position-counted aggregates. Two other kinds of pattern have no exactness dimension at all, because the question does not apply to them:

- an atom matches by identity, full stop -- there is no notion of "at least this atom";
- a map/record pattern names the keys it wants and looks them up. Whether the matched value has additional keys is not a question the pattern asks. A map is a key-value container, not a position-counted sequence, and "does it have exactly these keys and no others" is a much less natural default for a container than "does it have at least these keys" -- the same way a function reading three fields off an object in any other language does not usually insist the object has no other fields.

This gives one easy rule for the language as a whole: **fixed-shape aggregates match exactly; keyed lookups match by presence.** Nothing else needs to be memorized, and no operator is needed to switch between modes.

---

## 4. Tuple Matching

Tuple construction is exact:

```text
{'ok, value}
```

constructs exactly a two-element tuple.

Tuple matching is exact:

```text
{'ok, x}
```

matches only a two-element tuple whose first field is `'ok`. It does not match a three-element tuple.

A tuple's arity is part of its identity. Two tuples with the same tag and different arity are simply different shapes, matched by different clauses -- there is no shadowing hazard between them, and a stray extra element is a bug caught immediately rather than silently accepted.

If a message needs to grow over time, add a field to a map/record payload rather than to a bare tuple's tail; see Section 7.

---

## 5. Lists

Lists use conventional cons-oriented syntax:

```text
[]
[a, b, c]
[head | tail]
```

List matching is exact:

```text
[a, b]
```

matches only a two-element list.

To bind the remainder:

```text
[a, b | rest]
```

`rest` binds whatever list follows, including `[]`. This is the only mechanism for matching an open-ended list; there is no separate exactness operator, because `[a, b]` is already exact and `[a, b | rest]` already says precisely how much is left unconstrained.

---

## 6. Whole-Value Binding

A pattern may bind the entire original value while also destructuring it:

```text
m@{'ok, x}
```

This binds `m` to the complete value while applying the ordinary exact tuple match.

The same notation applies elsewhere:

```text
xs@[head | tail]
packet@<<1:8, kind:8>>
```

---

## 7. Maps and Records

Maps/records are the non-positional aggregate.

Example:

```text
'request{
    from: self,
    ref: r,
    path: "/tmp/a",
    trace: t
}
```

A map/record pattern names the keys it wants:

```text
'request{
    from: from,
    ref: ref,
    path: path
}
```

This matches any map tagged `'request` that has at least those three keys, regardless of what else it holds. The tag `'request` itself is matched exactly, by atom identity, the same as any other atom (Section 14) -- only the field lookup that follows is open.

Unknown fields are not rejected and are not silently discarded either: they remain part of the value. A map carries whatever keys it was built with; a pattern that names three keys out of five simply does not ask about the other two. This is not a special leniency granted to map patterns -- it is what "look up these keys in this container" already means, the same as calling a field accessor on an open record in any other language. Contrast this with Section 4: a tuple's arity is part of its identity, so an extra tuple element is a shape mismatch; a map's key set is not part of its identity in the same way, so an extra key is simply data the pattern did not ask for.

This is deliberately the only place in the language where a value may carry more than a pattern mentions. It is a property of what a map is, not an exception carved into the matching rules.

Whether general maps and tagged records use one surface syntax or two is still a syntax question, not a semantic one. The machine should have one keyed aggregate model unless implementation experience gives a reason not to.

### Decoding Is Not Dispatch

Nervous's answer to distributed version skew is not a lenient matching mode. It is a division of labor:

- **Dispatch** -- deciding which clause a value belongs to -- uses exact tag and arity matching on tuples (Section 4), or exact atom matching on a leading tag. This compiles to a clean discriminated-union switch, is exhaustiveness-checkable, and has no clause-shadowing hazard: two differently-shaped clauses are simply disjoint.
- **Field extraction** -- pulling the values a piece of code actually needs out of a payload that may have grown new fields since the code was written -- uses a map/record pattern, or an explicit decode function that fills in defaults for fields an older value never had. Maps tolerate extra keys because that is what a key-value lookup already means; nothing about dispatch needs to be lenient for this to work.

A well-formed message is therefore typically a tagged tuple carrying a map payload:

```text
{'request, RequestMap}
```

`receive` dispatches on the outer tuple's tag and arity, exactly. Whatever code does with `RequestMap` afterward tolerates the payload's evolution because it is looking fields up in a map, not because the receive clause itself was made loose.

This mirrors how systems with real production experience actually solve version skew. Protobuf decodes every message against a schema-aware unmarshaller that fills in defaults and preserves unknown fields separately, then dispatches with an exact, tag-numbered `oneof` switch -- the switch itself is never made lenient. Erlang/OTP, the closest precedent to Nervous, matches tuples by exact arity and instead relies on tagged-message conventions plus explicit `code_change/3` transformation code during a hot upgrade. Neither system loosens its dispatch operator to get tolerance; both push tolerance into a narrower layer whose only job is decoding. Nervous gets the same property from an ordinary map lookup, with no schema language needed in version 1.

---

## 8. Binary Construction and Matching

Binary syntax is a fundamental feature.

It should remain close to Erlang's bit-syntax model:

```text
<<version:8, flags:8, length:16, body:length/binary>>
```

Binary matching is exact:

```text
<<1:8, kind:8>>
```

matches only a binary that is exactly two bytes long, with those two byte values.

To bind the remainder:

```text
<<1:8, kind:8, rest/binary>>
```

`rest` binds whatever bytes follow, including none. As with lists, there is no separate exactness operator: a pattern without a trailing remainder segment already requires the binary to be fully consumed.

The following rules make binary matching deterministic:

- Segments match strictly left to right.
- A size expression may reference variables bound earlier in the same binary pattern, or variables already bound in the enclosing scope. It may not reference a variable bound later in the pattern: `<<len:16, body:len/binary>>` is well-formed; the reverse is not.
- Integer segment sizes are in bits; `/binary` segment sizes are in bytes.
- Integer segments default to unsigned and big-endian; `signed`, `unsigned`, `big`, and `little` may be written explicitly.
- There is no backtracking within a binary pattern. If a segment fails, the whole pattern fails and clause selection moves on.
- An unsized `name/binary` segment is permitted only in final position and binds the remainder.
- In the first implementation, `/binary` segments and the value as a whole must be byte-aligned. Arbitrary bit-width integer segments are permitted; full bit-string generality (unaligned binaries, odd-sized remainders) is deferred.

The initial binary syntax should support integer segments, explicit widths, signed/unsigned interpretation, endian selection, and binary remainder segments.

---

## 9. Asserted Matches

Direct pattern binding is an assertion:

```text
{'ok, value} = operation();
```

If the pattern matches, bindings are established.

If it fails, the current process exits abnormally with a runtime match failure unless some future explicit catch mechanism handles it.

Use clauses when alternatives are expected; use a direct match when failure is exceptional.

---

## 10. Clauses

Function clauses, `match`, and `receive` all consist of ordered clauses.

A clause is conceptually:

```text
pattern [if guard] => expression
```

The first clause whose pattern matches and whose guard succeeds is selected.

Function example:

```text
fn fact {
    (0) =>
        1;

    (n) =>
        n * fact(n - 1);
}
```

Value matching:

```text
match result {
    {'ok, value} =>
        use(value);

    {'error, reason} =>
        fail(reason);
}
```

Selective receive:

```text
receive {
    {'reply, ref, value} =>
        value;

    {'down, monitor, pid, reason} =>
        handle_down(pid, reason);
}
```

---

## 11. Guards

Guards are clause refinements for predicates that cannot be expressed structurally.

Example:

```text
fn classify {
    (x) if x > 0 =>
        'positive;

    (0) =>
        'zero;

    (_) =>
        'negative;
}
```

Guards are not a second matching system. They run only after the structural pattern matches.

They should remain bounded and side-effect-free: no send, spawn, I/O, process mutation, unbounded computation, or initially arbitrary user-function calls.

If ordinary matching proves sufficient for most code, guards should remain a small feature.

---

## 12. Functions and Arity

Function arity is part of function identity.

Clause heads use parentheses.

All clauses in one `fn` declaration have the same arity. Different arities require separate function declarations.

Zero-argument functions use an ordinary body:

```text
fn main {
    p = spawn server(initial);
    p
}
```

---

## 13. Expression-Valued Blocks

Nervous is expression-oriented.

The value of a block is the value of its final expression.

```text
fn answer {
    x = 40;
    x + 2
}
```

evaluates to `42`.

Semicolons separate expressions.

A trailing semicolon before a closing brace is permitted and has no effect: `x + 2` and `x + 2;` are equivalent as the final expression of a block. The block's value is the last expression either way.

---

## 14. Atoms

Atoms are symbolic identifiers:

```text
'ok
'error
'request
'connection_lost
```

Within one machine they are interned and cheaply comparable.

Atom IDs are machine-local. Distributed representations must translate atom names through the receiving node's atom table.

---

## 15. Logical and Comparison Operators

The initial language needs only:

```text
==
!=
<
<=
>
>=

and
or
not
```

`and` and `or` short-circuit.

There is no general truthiness rule.

The atoms `'true` and `'false` are ordinary atoms and serve directly as boolean values. There is no dedicated boolean term.

---

## 16. Processes

The fundamental unit of concurrency is a lightweight Nervous process.

A Nervous process is not a Plan 9 process and is not a `libthread` thread.

```text
p = spawn server(initial);
```

creates a cheap language process.

---

## 17. Asynchronous Messaging

Sending is asynchronous:

```text
pid <- message;
```

The send does not wait for the receiver.

A PID may be local or remote; language semantics do not distinguish the two.

---

## 18. Selective Receive

Every process owns a mailbox.

`receive` searches for the first message satisfying one of its clauses.

Messages that do not match remain available for later receives.

Because dispatch is exact (Section 4), two clauses with different tags or arities are simply disjoint: there is no clause-shadowing hazard, and an unexpected message shape does not match by accident. A process that must tolerate a message whose payload has grown new fields does so through the payload's map/record structure, not through leniency in the receive clause itself; see "Decoding Is Not Dispatch" in Section 7.

---

## 19. Timeouts

Duration literals:

```text
10ms
5s
2m
```

A duration literal denotes a fixed integer count of nanoseconds, decided at compile time. `10ms`, `5s`, and `2m` are surface sugar over one canonical integer representation; there is no separate duration term kind and no ambiguity of units at the machine level.

Duration literals may appear in receive timeouts:

```text
receive {
    {'reply, ref, value} =>
        value;

    after 5s =>
        {'error, 'timeout};
}
```

---

## 20. PIDs

A `Pid` is an opaque process identity.

Conceptually it must contain enough information to prevent stale identities from aliasing newer processes:

```text
Pid :=
    node incarnation
    process slot/id
    generation
```

Required properties:

- stale PIDs never identify newer processes;
- scheduler migration does not change the PID;
- local and remote PIDs obey the same semantics;
- node restart does not revive old distributed PIDs.

---

## 21. References

A `Ref` is an opaque unique identity.

Conceptually:

```text
Ref :=
    node incarnation
    unique counter/token
```

Refs are useful for RPC correlation, monitors, timers, and unique operation identity.

---

## 22. Errors, Exit, Links, and Monitors

Expected errors are ordinary values:

```text
{'ok, value}
{'error, reason}
```

Unexpected runtime faults, including asserted-match failure, cause process exit.

Exit reasons are terms.

The process model should eventually support `link`, `monitor`, and `exit`.

At the Plan 9 boundary, structured exits may naturally map to note/status strings, and notes may map back into structured machine signals.

---

# Part II — The Nervous Abstract Machine

## 23. Machine Goal

Nervous compiles to a portable 64-bit register-based abstract machine inspired by BEAM and Dis but compatible with neither.

The machine should be broad enough that Nervous is its first source language, not necessarily its only one.

---

## 24. 64-bit Model

The implementation may use a 64-bit machine word as the natural term representation, but source semantics must not depend on native pointer layout.

Possible low-bit tagging is an implementation detail.

Before fixing the layout, decide what must be immediate, small-integer width, PID/Ref boxing, Float64/Float128 representation, and GC pointer recognition.

---

## 25. Floating Point

IEEE binary64 is the minimum.

IEEE binary128 should not be designed out. It may become an optional machine capability or a distinct boxed term representation.

---

## 26. Process Machine State

A process conceptually contains:

```text
PC
X registers
Y/frame slots
process heap
mailbox
reduction counter
process identity
links/monitors
scheduler ownership state
```

Registers hold `Term`s.

---

## 27. Initial Instruction Families

Likely families include:

```text
data:
    move
    load_atom
    load_int

construction:
    make_tuple
    make_cons
    make_map
    make_binary

matching:
    test_atom
    test_int
    test_equal
    test_tuple_arity
    get_element
    test_cons
    get_head
    get_tail
    test_map_has
    get_field
    binary_match

control:
    jump
    call
    tailcall
    return

process:
    spawn
    send
    self
    make_ref
    exit

receive:
    recv_begin
    recv_peek
    recv_next
    recv_take
    recv_wait
```

Because matching is uniformly exact for tuples and binaries, one arity test suffices where a projective design would need both a minimum-arity test and a separate exact-arity test: `test_tuple_arity s, n, fail` jumps to `fail` unless `s` is a tuple of arity exactly `n`. Tuple patterns therefore partition cleanly by `(tag, arity)` -- decision trees are ordinary discriminated-union switches, with no overlapping ranges to resolve in clause order.

Binary patterns compile similarly: `binary_match` consumes segments left to right, and a pattern with no trailing remainder segment ends with an implicit check that the whole binary was consumed, rather than a separate opcode.

`test_map_has` checks for the presence of a key without asserting anything about the rest of the map's contents; it never requires an exact key set, because map matching is a lookup, not an arity check (Section 3).

---

## 28. Pattern Compilation

Patterns compile into primitive machine tests.

Multiple clauses should eventually compile into shared decision trees so common tests are performed once.

This is especially important for selective receive.

Because matching is exact, clauses that differ in tag or arity are provably disjoint; a decision tree can dispatch on `(tag, arity)` as an ordinary discriminated union rather than resolving overlapping ranges in clause order.

---

## 29. Receive Execution

Selective receive is explicit in the VM.

A process scans its mailbox using ordinary compiled pattern tests.

If no message matches, it becomes non-runnable.

The implementation should eventually avoid repeatedly rescanning known-unmatched mailbox prefixes where possible.

---

## 30. Reductions and Fairness

Processes execute with finite budgets.

A process cannot perform unbounded work without returning control to the scheduler.

Every primitive operation must either have bounded execution time or be resumable.

The target is soft-real-time behavior, not hard-real-time guarantees.

---

## 31. Portable Bytecode

Bytecode encoding must be independent of host C structs, pointer layout, `rfork`, `QLock`, Plan 9 file descriptors, and scheduler identity.

---

# Part III — 9front Runtime Strategy

## 32. Multicore Scheduling

Real parallelism comes from a small number of `rfork` scheduler processes sharing the Nervous machine's memory.

At any instant:

> Exactly one scheduler owns and may execute or mutate a Nervous process.

---

## 33. Spawn and Load Balancing

Newly spawned processes may begin on the current scheduler's run queue.

Idle schedulers may steal runnable processes.

Load balancing is not visible to the language.

---

## 34. Scheduler Migration

A running process cannot migrate.

Migration is an ownership transition at scheduler-safe points.

Because schedulers share memory, local migration does not serialize or copy the process heap.

Hazard-pointer-like or generation-based ownership may make this transition simple.

---

## 35. Scheduler Synchronization

`QLock` is acceptable for initial cross-scheduler queues.

Do not introduce lock-free structures until measurement proves the need.

The sending scheduler should not mutate the receiving process's private execution or GC state.

An idle scheduler must be able to sleep until either work arrives or its nearest timer expires, and another scheduler must be able to wake it cheaply. The chosen primitive on 9front is the semaphore family of `semacquire(2)`: schedulers sleep in `tsemacquire` (which folds in the timed sleep `after` deadlines require) and are woken with `semrelease`. `semrelease` never blocks and its count persists, so a wakeup issued before the sleeper commits to sleeping is never lost.

`rendezvous(2)` is rejected for this purpose because it is symmetric: a waker with no waiting sleeper blocks, reintroducing the lost-wakeup race the semaphore already avoids. libthread channels are the wrong layer, since schedulers are `rfork` procs, not libthread threads, and channels do not coordinate across separate procs. Notes are asynchronous and interrupt outstanding system calls rather than serving as a wakeup path, though they remain the natural mechanism at the process-exit boundary (Section 39).

---

## 36. Process-Local Garbage Collection

Ordinary heaps are process-local.

One process collecting does not require unrelated processes to stop.

A simple copying collector is a reasonable first implementation.

---

## 37. Message Memory

Shared address space does not imply arbitrary shared process-heap pointers.

The initial policy is decided:

```text
small ordinary terms:
    copy

large immutable binaries:
    specialized shared storage (future work)
```

Messages are copied on send, unconditionally, even between processes on the same scheduler. Copying preserves GC isolation (Section 36), not address-space separation: if a mailbox could hold pointers into a sender's heap, the sender's collector could no longer move or free anything reachable from any mailbox without coordinating with every recipient. The copy lands in a self-contained per-message fragment attached to the mailbox entry, not in the receiver's private heap directly -- the sending scheduler must not allocate into a process it does not own (Section 32) -- and the receiver's next collection merges the fragment into its heap.

Transferring ownership instead of copying is sound only when the transferred subgraph is reachable from nothing else, which single-assignment does not guarantee by construction. Immutable message regions and hazard-pointer-like techniques remain worth experimenting with for large, provably-exclusive payloads, but they are optimizations layered on the copying baseline, not alternatives to it, and are not language semantics.

---

## 38. I/O

Blocking OS I/O must not block an entire scheduler.

The natural 9front strategy resembles `ioproc`: a worker performs blocking OS I/O and wakes the waiting Nervous process on completion.

---

## 39. Notes and Exit Status

Inside Nervous, exit reasons remain structured terms.

At the Plan 9 boundary, they may be rendered to status strings.

Notes entering the runtime may eventually become structured machine signals.

---

# Part IV — Distribution, Analysis, and Future Work

## 40. Distribution

Local and remote messaging share the same syntax and semantics:

```text
pid <- message;
```

Wire representations must not expose local atom IDs, pointers, scheduler identity, or native struct layout.

Rolling upgrades are made practical by keeping dispatch exact (Section 4) and pushing payload evolution into map/record field extraction (Section 7); see "Decoding Is Not Dispatch" there for the full argument.

---

## 41. Process Migration Across Machines

Network process migration is not a v1 promise.

A suspended process is largely language-controlled state and may eventually be serializable.

External resources such as sockets, file descriptors, and OS handles are non-migratable capabilities unless explicitly recreated, renegotiated, delegated, or abandoned.

Stable PID identity across migration may eventually require forwarding or routing semantics.

---

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

## 46. Decisions Now Considered Settled

For the current design pass:

- braces delimit blocks;
- whitespace is not syntax;
- variables are single-assignment;
- `_` never binds;
- matching is exact for tuples, lists, and binaries; there is no strict/loose toggle;
- map/record matching names the keys it wants and never requires an exact key set, because a map's key set is not part of its identity the way a tuple's arity is;
- dispatch (tag/arity matching) and field extraction (map lookup) are deliberately separate mechanisms; version tolerance lives only in the latter -- see Section 7's "Decoding Is Not Dispatch";
- `[a, b | rest]` is the only mechanism for open-ended list matching;
- `<<..., rest/binary>>` is the only mechanism for open-ended binary matching;
- `@` binds the whole value while destructuring it;
- direct failed matches cause abnormal process exit;
- blocks are expression-valued;
- the final expression is the result;
- semicolons separate expressions, and a trailing semicolon is permitted and ignored;
- function arity is part of function identity;
- zero-argument functions use ordinary bodies;
- maps and records are one keyed aggregate concept;
- `'true` and `'false` are ordinary atoms; there is no dedicated boolean term;
- duration literals denote a fixed integer count of nanoseconds, decided at compile time;
- small-integer overflow causes a process exit with reason `{'error, 'overflow}`, never silent wraparound, leaving room for a later, fully compatible move to bignum promotion;
- messages are copied on send; the copy lands in a self-contained per-message fragment attached to the mailbox entry, merged into the receiver's heap at its next collection, so the sender never allocates into the receiver's private heap;
- scheduler wakeup uses `tsemacquire`/`semrelease`, not `rendezvous`, libthread channels, or notes;
- if the first token after a `fn` body's opening brace is `(`, the body is clause form;
- PIDs and Refs have logical node-incarnation/generation identity even though exact representation remains open;
- binary matching belongs in the early VM milestone;
- `QLock` is acceptable until measurement says otherwise.

---

## 47. Open Questions

Still deliberately unresolved:

- exact 64-bit term tagging;
- bignum representation and promotion strategy;
- exact Float64/Float128 term model;
- atom lifetime and limits;
- final map/record surface syntax;
- exact binary segment syntax beyond the minimal set;
- whether guards prove useful enough to retain and their exact primitive set;
- exception/catch semantics beyond ordinary process exits;
- exact link and monitor behavior;
- mailbox indexing and receive optimization;
- GC algorithm;
- message-region design;
- large binary ownership;
- scheduler quantum/reduction accounting;
- work-stealing policy;
- exact migration synchronization;
- portable bytecode encoding;
- code loading and replacement;
- module/name system;
- wire term encoding;
- node identity and authentication;
- static contract syntax;
- FFI design;
- I/O capability representation;
- network process migration.

These do not prevent building the first Nervous machine.

---

# Central Idea

> Computation consists largely of describing the shapes of values a process is prepared to accept.

Functions accept values through patterns.

Messages are accepted through patterns.

Binary protocols are decoded through patterns.

Errors are handled through patterns.

Patterns describe the exact shape a function or message clause requires.

Distributed version skew is handled by keeping dispatch exact and pushing tolerance into map-based field extraction and explicit decode steps -- the language expects version skew, but it does not ask the matching operator to absorb it.

Underneath that language sits a portable concurrent machine whose processes are cheap, independently collectible, asynchronously communicating, and scheduled in parallel over a small number of real execution contexts.
