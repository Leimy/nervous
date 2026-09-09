# Milestone 08 - Process-Local Heap and GC

## Dependency gate

Milestones 05 and 06 and Review R2 must complete before process-local heap integration begins. (Milestone 07, Basic I/O, does not gate this: it needs nothing from heap representation and this milestone needs nothing from it.)

## Milestone complete (M08-T04d)

All planned tasks (T01-T03, PR2-T01, T04a/b/p/e/c/r/d) are accepted; T04q remains optional/deferred and is not a dependency of anything. `mk -a tests benchmarks` builds clean; the user has confirmed, across this milestone's history, both normal and CLI-stress regressions passing, most recently including CLI-forced-all-off-process stress (`nervous_gcstress=1 nervous_gcoffload=1 rc tests/run.rc`) and three repeated `bench/largelive.rc` runs per shape/threshold. D061-D075 (`../docs/decisions.md`) settle representation, per-process collection, off-process collection's full protocol and its false-idle fix, and the final `gcoffload`/idle-sweep-cap policy. Latest measurements are in `../bench/README.md`.

Required tests and exit criterion (below) are reconciled against the accepted suites, named specifically rather than by generic coverage claim: "live tuples survive repeated collections" and "dead structures are reclaimed" by gctest's "GC preserves kinds, empty tuples and sharing; reclaims garbage repeatedly"; "cyclic host metadata does not confuse term traversal" by gctest's "GC scans every active frame, skips metadata and inactive slots, and resumes unchanged"; "a queued message survives sender collection and exit" by gctest's "GC merges adopted fragments but leaves queued storage and host metadata alone" (the classification rule that excludes queued mailbox fragments from any collection applies uniformly regardless of whose heap is collecting, so this generic property test covers the sender-collects case specifically) together with autotest's full-stress "1000-message loop" (messages/Refs arrive exactly correct under continuous gcstress-forced collection across participating processes' heaps) -- exit-only survival (not collection) is the separate, already-cited R2 test "a copied message survives immediate sender exit and reaping with no aliasing"; "a received fragment becomes normal receiver-owned data" by gctest's "GC merges adopted fragments..." (adoption half) and autotest's "Receive reservation leaves the candidate queued and unadopted across collection... successful retry takes/adopts once"; "deep and wide values either collect or fail with a controlled resource reason" by gctest's "GC traverses deep and wide shared graphs iteratively and retains space capacity" and "GC budget, overflow and malformed-object failures preserve roots and ownership"; "no unrelated process pauses semantically" by `offloadtest.c`'s eight lifecycle groups (D068/D074's off-process mechanism exists precisely for this) and measured directly by M08-T04r/T04d. The exit criterion (bounded memory proportional to live data; ownership explainable without cross-heap pointers) is demonstrated by T04p's bounded-memory results and by D064's fragment-ownership design throughout.

**`gcoffload` default: 0 (never off-process), D075.** M08-T04d found a real, repeatable crossover -- off-process collection hurts the tail at a 50000-word live set and helps at 500000 words -- bracketed, not located, so no positive default is supported by current evidence; every workload this tree exercises today has a far smaller live set than even the "hurts" bracket. `NvGcsweepcap = 8` (idle-sweep launch cap) stays unmeasured and explicitly provisional; nothing measured so far exercised the sweep path under real load. `-o words`/`$nervous_gcoffload` (`cmd/nervous/main.c`) expose the threshold to anyone who needs it, without changing this default.

Milestone 09 (Binaries) and the mandatory R3 review are next, in that order; see `../STATUS.md`'s Resumption checklist. Neither is started or assigned.

## Planned continuation (historical; all items below are done)

1. Optional, bounded T04q: remove duplicated arithmetic/call-target computations between preflight and execution. Preserve reservation, guard, root and reduction semantics; discard/recompute any per-attempt plan across NvCollect. Do not weaken checks or enlarge global heap defaults. Test and measure, then stop or defer rather than indefinitely pursuing stage-2 throughput. **Still optional and deferred; not done, but not required.**
2. T04r (done): established an inline large-live-set latency baseline with unrelated small-message peers, measuring latency tails as well as throughput and memory, with controls for observation overhead. Found and fixed a real scheduler false-idle bug along the way (D074 amendment).
3. T04c (done): D068/D074 off-process ownership, completion/wakeup and teardown, exactly as designed, plus five coordinator-review correctness fixes recorded as D074 amendments.
4. T04d (done): ran full inline-only and CLI-forced-offload stress, repeated representative benchmarks (three runs per shape/threshold), selected policy constants from evidence (`gcoffload` stays 0, D075; idle-sweep cap stays 8, unmeasured/provisional), and reconciled milestone acceptance criteria above.

T04q was never a dependency gate; the user chose the large-live-set/off-process track directly, which is why T04r/T04c/T04d proceeded before it. D069/D072's minimum/sizing/idle policies remain in place, unchanged by any measured decision in T04d.

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
