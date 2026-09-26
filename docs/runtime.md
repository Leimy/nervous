# Nervous Runtime Strategy (design rationale)

> **Not normative.** This file is Part III of the deprecated `nervous_design.md`, split out for readability; section numbers are the original ones, and "Section N" references below 32 point into `language-semantics.md`. It records the intended 9front runtime direction. What is actually built and settled is in `docs/decisions.md` (D059 run queue, D061-D075 memory and collection) and `STATUS.md`'s implementation map; those win on any conflict. As of milestone 09 there is still exactly one scheduler proc (multicore is milestone 10), so Sections 32-35 describe future work; the collector (Section 36) is a per-process Cheney copier with optional off-process collection (D063, D074, D075).

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

Mailbox and message limits are word counts, not byte counts (D066): `NvLimits.maxmailbox` and `maxmessage` bound the words of queued fragments, root word included. `nvprocsend()` checks them, together with the term-depth limit, all-or-nothing during the copy, so an over-limit send reports `mailbox_full` and enqueues nothing.

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
