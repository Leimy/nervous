# Memory collector and automatic inline GC tests

Build from the repository root with `mk tests`, then run:

```rc
rc tests/memory/run.rc
rc tests/run.rc
nervous_gcstress=1 rc tests/run.rc
```

The full runner includes this suite. It uses working-tree binaries, not
installed commands. The agent's mk tool verifies compilation only; the
user must execute the tests. `nervous_gcstress=1` makes CLI invocations
(including -r/-x/-t) force collection at every allocating reservation.
C tests retain their explicitly configured limits; autotest exercises
both stress settings independently.

## gctest: the explicit core (M08-T04a)

Six groups, ending with `all memory collector tests passed`:

- 100 rounds of garbage allocation and exact-live-budget collection;
  every tagged/boxed kind, Ref payload bits resembling owned pointers,
  empty tuples, NvNil, aliases and exact live-word counts.
- Adopted versus queued fragments: sender heap can be destroyed; only
  live adopted subterms remain; queued storage and cyclic host metadata
  stay untouched. Taking a queued fragment changes its classification.
- A 10000-level chain followed by 10000 aliases to it, traversed by the
  test iteratively too. Trial-space growth/rollback, sharing, dropping
  roots, no shrinking, and initially empty heap sizing.
- Partial-copy budget failure, stack/need arithmetic overflow and an
  invalid owned header. Checks restored source headers, roots, bodies,
  accounting and adopted ownership; successful retry afterward.
- Caller/callee aliases, pointer-looking frame metadata, inactive stack
  slots and popped frames; retained capacity is charged but not traced.
- Explicit collection before every instruction of a guard program,
  preserving its fault target, result and reductions across movement.

## autotest: reservation and hosts (M08-T04b)

Five groups, ending with `all automatic inline collector tests passed`:

- Atomic NvCollect requests: unchanged pc/root/reductions, repeat calls
  cannot execute a pending request, exact startup and retained-stack
  limits, tail-call and non-tail-call growth, FIFO peer progress before
  the owner's retry, and no duplicate run-queue insertion.
- A guard whose tuple allocation cannot fit maxheap still returns from
  its fallback clause. Normal and stress runs charge the failed
  instruction once and never convert it to a scheduler-level exit.
- Receive reservation leaves the candidate queued and unadopted across
  collection (including failure). A successful retry takes/adopts once,
  while an insufficient budget faults without taking it first.
- An idle waiting process with garbage above the provisional threshold
  is collected without executing bytecode. A second idle step does not
  recollect its unchanged live set.
- A source-compiled 1000-message loop combines tuple allocation, mkref,
  full-range boxed arithmetic, guards, receive adoption and tail calls.
  Runs at quanta 1/1000 with stress off/on. Checks bounded managed heap
  space, real collections, identical reduction totals and exactly 1000
  messages/Refs, detecting duplicated side effects on retries.

## Limits of the evidence

The earlier core suite was user-confirmed passing before automatic
integration. The new build needs fresh runtime verification: do not reuse
that historical result as evidence for autotest or the integrated runtime.

These are inline tests. No gcoffload option, collector proc, owner-lock
protocol, multicore behavior or milestone-08 acceptance is claimed.
Run `rc bench/run.rc` separately to measure the working-tree binary;
benchmarks are not pass/fail regressions.

Host malloc failure paths are reviewed but not deterministically injected.
Malformed-object checks cover owned headers, not arbitrary untrusted C
pointers or root descriptors. External roots must refer to stable,
self-contained fragments. Caller metadata is trusted and collection
requires exclusive stopped-owner access.
