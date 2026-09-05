# Milestone 08 - Process-Local Heap and GC

## Dependency gate

Milestones 05 and 06 and Review R2 must complete before process-local heap integration begins. (Milestone 07, Basic I/O, does not gate this: it needs nothing from heap representation and this milestone needs nothing from it.)

## Current safety preflight

PR2-T01 is accepted: the user reports the bytecode and full regression suites passing with D071's guard-boundary verification. post-R2-F01 is closed and M08-T04 may resume. See `../docs/review-findings.md` and `../STATUS.md`. This bounded prerequisite does not change the collector design or declare R3 open or complete.

M08-T04a (collector core and frame-root adapter) is user-accepted: memory and full regression suites passed. M08-T04b adds automatic inline collection (D072): pre-instruction reservation/retry, startup/stack/fragment accounting, idle reclamation, CLI limits and stress, and integration tests. Build evidence and pending user-run acceptance are in STATUS. Off-process collection (D068), both-mode stress acceptance, and benchmark-based policy tuning remain outstanding; this is not milestone-08 completion.

## Goal

Replace temporary allocation with independently collectible process heaps while preserving mailbox isolation.

## Read first

- `../docs/semantics.md`
- `../docs/decisions.md`
- `../docs/questions.md`, section "Milestone 08"
- `05-processes.md`

## Scope

- Initial 64-bit term representation.
- Process-local heap.
- Simple copying collector, runnable on a Plan 9 proc other than the scheduler's while the scheduler runs other processes (D067-D069).
- Complete root enumeration from registers, frames, process metadata, and active receive state.
- Safe handling or merging of message fragments.
- Controlled allocation failure.

## Core invariants

- One process collecting never scans or mutates another process's heap.
- No ordinary mailbox message depends on sender-heap lifetime.
- Every pointer-bearing term is recognized unambiguously by the collector.
- All live roots are described at every allocation or GC safe point.

## Required tests

- Live tuples survive repeated collections.
- Dead structures are reclaimed.
- Cyclic host metadata does not confuse term traversal.
- A queued message survives sender collection and exit.
- A received fragment becomes normal receiver-owned data according to the chosen policy.
- Deep and wide values either collect or fail with a controlled resource reason.
- No unrelated process pauses semantically, even though the first scheduler executes one process at a time.

## Exit criterion

Long-running allocation tests have bounded memory proportional to live data, and process/message ownership can be explained without cross-heap pointers.

## Not in scope

Generational GC, concurrent GC (meaning collection interleaved with the *owning* process running: barriers, incremental copying), large shared binaries, ownership transfer, lock-free reclamation, or final bignums. Collecting a stopped process's heap on another proc while the scheduler runs other processes (D068) is in scope and is not concurrent GC in that sense.
