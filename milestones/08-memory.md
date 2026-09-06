# Milestone 08 - Process-Local Heap and GC

## Dependency gate

Milestones 05 and 06 and Review R2 must complete before process-local heap integration begins. (Milestone 07, Basic I/O, does not gate this: it needs nothing from heap representation and this milestone needs nothing from it.)

## Accepted checkpoint; implementation paused

T04e correction: a bare CLI invocation could fail on an empty environment entry, and `rfork E` in test runners discarded inherited CLI stress. The repair compiles; fresh normal/CLI-stress checks are pending. Historical acceptance below retains normal and explicit C stress evidence, not full CLI-stress coverage. Only the bounded defect repair is active; feature tasks remain paused.

PR2-T01 (D071 guard verification), M08-T04a (collector/root adapter), M08-T04b (automatic inline collection, D072), and M08-T04p (amortized table storage, small-GC allocation reduction and diagnostics, D073) are accepted. The user confirmed normal and CLI-stress regressions passing after the T04p benchmark review. `mk -a tests benchmarks` rebuilt all targets without diagnostics. Latest measurements are in `../bench/README.md`; post-R2-F01 is closed in the finding ledger.

The user has paused implementation at this save point. No source write assignment is active; upcoming tasks are planned/unassigned in `../STATUS.md`. This documentation handoff is not permission to start them. Milestone 08 is still active, not complete; R3 has not opened.

## Planned continuation

1. Optional, bounded T04q: remove duplicated arithmetic/call-target computations between preflight and execution. Preserve reservation, guard, root and reduction semantics; discard/recompute any per-attempt plan across NvCollect. Do not weaken checks or enlarge global heap defaults. Test and measure, then stop or defer rather than indefinitely pursuing stage-2 throughput.
2. T04r: establish an inline large-live-set latency baseline with unrelated small-message peers. Measure latency tails as well as throughput and memory, with controls for observation overhead. Existing tiny-live-set benchmarks cannot select an offload threshold.
3. T04c (implemented, handed off; coordinator build/behavioral verification pending): D068/D074 off-process ownership, completion/wakeup and teardown. `lib/sched.c` launches a collector off-process from both the demand and opportunistic-idle trigger sites (word-threshold policy via `NvLimits.gcoffload`, 0 = never, matching every other zero-default limit), discovers completion lazily and only at dispatch time (a locked read of `gcretry`/`livewords`, required for correctness on the project's arm64/7c target), never spins or reports idle/deadlock while a collector is outstanding, caps opportunistic-sweep forks per pass, and drains outstanding collectors with a bounded loop before `nvschedfree` frees anything. A new deterministic test hold point (`nvschedgchold`) lets `tests/memory/offloadtest.c` exercise send-during-collection, a deadline firing during collection, teardown with a collector in flight, and every runnable process collecting at once, without racing real timing. `nvexecgc`/`nvexeccollect`/`nvheapcollect` needed no change, confirmed by reading them, not just assumed. Exact `gcoffload` default value, a CLI flag for it, and measured policy constants remain T04d work.
4. T04d: run full inline-only and forced-offload stress, repeat representative benchmarks, select policy constants (including the real `gcoffload` default and the idle-sweep launch cap) from evidence, and reconcile all milestone acceptance criteria. Only then proceed to binaries and R3.

T04q is a recommendation, not a new dependency gate; the user may choose the large-live-set/off-process track directly. Exact interfaces and exclusive write sets must be assigned before implementation. D069/D072's current minimum/sizing/idle policies remain in place until a measured decision changes them.

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
