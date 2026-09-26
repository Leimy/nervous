# Nervous Language Semantics (design rationale)

> **Not normative.** This file is Part I of the deprecated `nervous_design.md`, split out verbatim for readability. It explains *why* the language is shaped the way it is, but its examples use superseded surface syntax and some rules have since been narrowed. The normative contract is `docs/semantics.md`; settled decisions are in `docs/decisions.md`. Both win over this file on any conflict. Section numbers are the original `nervous_design.md` numbers; "Section N" references may point into the sibling split files (`runtime.md` 32-39, `distribution.md` 40-41, `language-philosophy.md` 42-46, `future-work.md` 47).
>
> Known differences from the implemented language:
>
> - Tuples are written `${a, b}`, not `{a, b}` (braces are blocks only).
> - Send is `pid ! message`, not `pid <- message` (D058).
> - Functions are adjacent single-clause declarations `fn f(pattern, ...) { body }`, not clause-form `fn f { (p) => body; }`; `fn main() { ... }`, not `fn main { ... }` (D058).
> - Guards use `when`, not `if` (D060); `if` is the conditional expression.
> - A fresh Ref is the keyword `mkref`, not `ref()`; `self`, `spawn`, and `exit` are keywords too (D058).
> - Every function takes one semantic argument tuple, so clauses of different arities may coexist under one name (`docs/semantics.md`, "Functions"); Section 12's "all clauses have the same arity" does not hold.
> - Binary integer segments take only the widths 8, 16, 32, and 64 (D078), not arbitrary bit widths; `/binary` sizes are bytes; the rest segment takes no modifiers and must be last.
> - Duration literals (`5s`, `10ms`, Section 19) are not implemented. `after` takes an integer nanosecond count or `'infinity` (D049).
> - Lists, maps/records, floats, `@` whole-value binding, links, and monitors are not implemented; Sections 1, 5, 6, 7, and 22 describe intended design.

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

A message is copied once into a self-contained fragment owned by the receiver's mailbox. The matching phase places a term pointing into that fragment's root (not a copy), and only the take operation adopts the fragment into the receiver's process heap.

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
