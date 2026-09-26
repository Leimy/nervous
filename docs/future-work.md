# Nervous Future Work

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

## Decisions Now Considered Settled

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
