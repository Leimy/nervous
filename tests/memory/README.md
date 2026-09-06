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

Seven groups, ending with `all memory collector tests passed`:

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

- T04p scratch paths: a small trial must roll back and promote to dynamic
  scratch for a wide adopted root; the heap descriptor is reused on
  success. A ten-frame exec exercises malloc-backed root views on
  failure, success and resumption.

## autotest: reservation and hosts (M08-T04b/T04p)

Six groups, ending with `all automatic inline collector tests passed`:

- T04p table growth: 200 appends require five capacity allocations and
  zero live-prefix slot probes; mailboxes and FIFO links survive growth,
  spare capacity does not bypass maxprocess, reuse is lowest-slot-first,
  and retirement can require new slots beyond the live-process limit.
- Atomic NvCollect requests: unchanged pc/root/reductions, repeat calls
  cannot execute a pending request, exact startup and retained-stack
  limits, tail-call and non-tail-call growth, FIFO peer progress before
  the owner's retry, and no duplicate run-queue insertion.
- A guard whose tuple allocation cannot fit maxheap still returns from
  its fallback clause. Normal and stress runs charge the failed
  instruction once and never convert it to a scheduler-level exit.
- Receive reservation leaves the candidate queued and unadopted across
  collection (including failure). A successful retry takes/adopts once,
  while an insufficient budget faults without taking it first. Explicit
  snapshots check table/exec/heap/stack capacity, movement of bytes from
  queued to adopted ownership, reporting fragments, and cleanup. Timing
  remains zero when profiling is disabled.
- An idle waiting process with garbage above the provisional threshold
  is collected without executing bytecode. A second idle step does not
  recollect its unchanged live set.
- A source-compiled 1000-message loop combines tuple allocation, mkref,
  full-range boxed arithmetic, guards, receive adoption and tail calls.
  Runs at quanta 1/1000 with stress off/on. Checks bounded managed heap
  space, real collections, identical reduction totals and exactly 1000
  messages/Refs, detecting duplicated side effects on retries. Timing is
  enabled for the 1000-quantum cases and must not affect these assertions.

## Limits of the evidence

T04p's reported passes establish normal-suite and explicit C stress evidence.
Correction: the old runners used `rfork E`, discarding inherited CLI stress;
the environment-prefixed run did not establish full CLI-stress coverage.
T04e now uses `rfork e`, fixes empty-environment CLI startup, and adds a
collection-count canary in `tests/cli/run.rc`. `mk tests` passed; fresh normal
and CLI-stress runtime checks are pending. Feature work remains paused.
See STATUS for the bounded repair and verification handoff.

These are inline tests. No gcoffload option, collector proc, owner-lock
protocol, multicore behavior or milestone-08 acceptance is claimed.
Run `rc bench/run.rc` separately to measure the working-tree binary;
benchmarks are not pass/fail regressions.

Host malloc failure paths are reviewed but not deterministically injected.
Malformed-object checks cover owned headers, not arbitrary untrusted C
pointers or root descriptors. External roots must refer to stable,
self-contained fragments. Caller metadata is trusted and collection
requires exclusive stopped-owner access.
