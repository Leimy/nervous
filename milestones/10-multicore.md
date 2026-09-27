# Milestone 10 - Multicore Runtime

## Dependency gates

Milestones 05, 06, 08, and 09 and Review R3 must complete before multicore integration begins. (Milestone 07, Basic I/O, is not a dependency; it does not touch process ownership, scheduling, or heap representation.) Review R4 is part of milestone 10 acceptance and must complete before milestone 10 can be marked complete.

## Goal

Run the already-correct process machine in parallel across a small number of `rfork` scheduler processes without changing language semantics.

## Read first

- `../docs/decisions.md`
- `../docs/questions.md`, section "Milestone 10"
- `05-processes.md`
- `06-timeouts.md`
- `08-memory.md`

Consult the runtime sections of `../nervous_design.md` for the original synchronization rationale.

## Scope

- Shared-memory `rfork` scheduler processes.
- Per-scheduler run queues.
- `QLock`-protected cross-scheduler operations initially.
- Work stealing or explicit work transfer.
- Safe ownership transitions only at scheduler safe points.
- Idle sleep with `tsemacquire`; wakeup with `semrelease`.
- Timer ownership compatible with scheduler sleep deadlines.

## Central invariant

At every instant exactly one scheduler owns and may execute or mutate a Nervous process. A running process never migrates; only a suspended process changes ownership.

## Required tests

- Parallel CPU-bound processes execute on multiple schedulers.
- Heavy cross-scheduler messaging preserves the milestone 05 ordering contract.
- Repeated migration does not change a PID or corrupt heap state.
- A wakeup issued just before sleep is not lost.
- Timer and message wakeups do not enqueue a process twice.
- Process exit during cross-scheduler traffic is safe.
- Single-scheduler mode retains identical language-visible results.

## Measurement requirement

Collect basic queue, steal, wakeup, migration, and reduction counts before considering lock-free structures. Complexity requires measured justification.

## Exit criterion

Stress tests run with several schedulers without duplicate ownership, lost wakeups, cross-heap mutation, or language-visible scheduler dependence.

## Not in scope

Distribution, remote process migration, lock-free queues, hard-real-time guarantees, or scheduler-aware source semantics.

## Coordinator design review (pre-implementation)

Written after R3 closed, before any M10 task was cut, from `lib/sched.c`, `lib/process.c`, `include/nvproc.h`, `include/nvsched.h`, D011/D036/D042/D046/D059/D068/D070/D074/D080, `docs/runtime.md` 32-35 and `docs/questions.md` "Milestone 10". **Accepted by the user in full; recorded as D081-D089** (P1 -> D081, P2 -> D082, P3+P4 -> D083, P5 -> D084, P6+P7 -> D085, P8 -> D086, P10 -> D087, P9 -> D088, P11 -> D089). The decisions are authoritative where this section and they differ.

### What the current implementation commits to

- One `NvRuntime`: a `realloc`-grown process table (every `NvProcess*` dangles across a spawn; the code copes by re-fetching by slot), one FIFO run queue threaded through slot indices (D059), `nvprocsend` = lookup, copy, append, wake with no lock anywhere.
- `NvScheduler` is runtime + module + host table + clock + io + GC bookkeeping + root tracking, stack-allocated by every caller. Host callbacks recover "the scheduler" through `e->host->aux`, a single shared table; under N procs there is no single scheduler to recover that way.
- Deadlines are examined only in `nvschedstep`'s idle branch (see finding 1).
- Off-process GC bookkeeping (`gcoutstanding`, `gclaunched`, `gccredits`, `gcsem`, the fold) is per scheduler; a completion must be folded by the scheduler that launched it.
- Statistics (`nvschedmemory`, the `-s` counters) assume an explicit quiescent snapshot.

### Findings on the existing design

1. **Timers starve under load (pre-existing, not multicore-specific).** `receive ... after N` is only observed to expire when the run queue is empty; while any process is runnable, every timeout in the system is effectively infinite. D050 is satisfied to the letter ("with nothing runnable, sleep until the nearest deadline") but this is almost certainly not the intended contract, and under N schedulers it gets worse (each scheduler's queue must empty). Proposal 5 below fixes it as part of the deadline-heap work; it changes observable timing in single-scheduler mode, so it is a user decision, not a drive-by.
2. **The process table's realloc discipline cannot survive a second proc.** D070 offered "a lock, or fixed size at `maxprocess`". A segmented table (proposal 2) is strictly better than either: stable addresses without preallocating `maxprocess` slots.
3. **D046 deadlock detection becomes termination detection.** "Idle" must mean: no runnable slot on any queue, no collector outstanding on any scheduler, no armed deadline on any waiting process, and no wake in flight. That is exact only if every transition into `runnable` happens under one lock or bumps one global epoch. Proposal 1 (global lock first) makes it exact for free at stage 1; the epoch is the stage-2 replacement.
4. **`docs/runtime.md` 34's "hazard-pointer-like or generation-based ownership"** is unnecessary under either stage: an `owner` field in `NvProcess` written under the process lock (or the global lock) is sufficient, because a running process is never touched by anyone but its owner and a queued process is only touched under its queue's lock.
5. **D057's cross-process output order** is defined by dispatch order on one scheduler; with N schedulers it is undefined across schedulers and must be restated as per-process order plus line atomicity (proposal 8). The shared `Biobuf` needs a lock regardless.
6. **Plan 9 `malloc` has one lock.** Every send allocates a fragment; N message-heavy schedulers will contend there before they contend on any Nervous lock. Not a design change now, but it must be in the measurement (proposal 9) so the first contention seen is not misattributed.
7. **Atom interning at run time** is claimed by D070 to need a lock; whether any bytecode path interns at run time (as opposed to compile/load time) must be audited before T01. If none does, the lock is only for host code (tests).

### Proposed decisions (awaiting approval)

1. **Staged locking: one global runtime lock first.** Stage 1 runs N scheduler procs with a single `QLock` (D011 allows it) held for every runtime mutation -- spawn, exit, send/enqueue, wake, queue push/pop/steal, ref allocation, deadline arm -- and *released for the whole of a quantum's interpretation*, which touches only the process's private heap and stack (D068). This alone gives the parallel CPU-bound speedup and makes cross-scheduler messaging, migration, and termination detection trivially correct. Stage 2 splits it (per-process lock for state/mailbox/deadline, per-queue spinlock) *only* where the stage-1 counters show contention, per the milestone's own measurement requirement. Lock order for stage 2, fixed now so stage-1 code is written to it: process lock and queue lock are never nested (set state under the process lock, release, then enqueue under the queue lock; dispatch pops under the queue lock, releases, then marks running under the process lock). The transient "runnable but not yet enqueued" window is documented and is invisible to every other party (a sender sees runnable and does not enqueue; a stealer cannot see an unenqueued slot; a timer owner sees not-waiting and skips).
2. **Segmented process table.** Slots live in fixed-size chunks (e.g. 1024) reached through a chunk pointer array; growth appends a chunk and never moves a slot, so `NvProcess*` is stable for the process's lifetime. Slot-index links (D059) still work. One extra indirection per lookup. Replaces `realloc`; the "re-fetch after spawn" discipline becomes unnecessary but harmless.
3. **Ownership and wake target.** `NvProcess.owner` is the scheduler index, always defined: set at spawn (the spawning scheduler's queue -- `runtime.md` 33), changed only by a steal. A waiting process keeps its owner (its "home"), which owns its deadline. A message wake from scheduler A to a process owned by B marks it runnable, pushes it on **B's** queue, and `semrelease`s B; it does not migrate to A. Migration is only by steal. This keeps "exactly one scheduler owns a process at every instant" literally true and testable (`owner` is never undefined), and keeps timer ownership trivial. The alternative (wake onto the waker's queue) was considered and rejected for now: it makes a waiting process owner-less and the deadline's owner a separate concept; revisit only if measurement shows message locality beats heap locality.
4. **Work movement: steal one from the head of the longest queue.** An idle scheduler, before sleeping, takes the head (runnable longest -- preserves D059's fairness intent globally) of the most-loaded queue, if that process is stealable: `Prrunnable`, in a queue, and `!offlaunched` (a heap under off-process collection must be folded by its launching scheduler; unmovable until then -- the D068 `owner` state plus the D074 flag are the whole safe-point test). Steal changes `owner` and nothing else; PID, heap, stack, mailbox untouched (required test 3). No steal-half, no push, until measured.
5. **Per-scheduler indexed deadline heap; deadlines checked once per dispatch.** Each scheduler keeps a binary min-heap of (deadline, slot, generation) for the waiting processes it owns, with a per-process heap index so re-arming or clearing is O(log n) and the heap holds at most one entry per waiting process (no stale accumulation for `after` loops). Firing validates state == waiting, generation, and `hasdeadline` (D050's guarantees preserved). The top is peeked once per dispatch as well as at idle, fixing finding 1: a timeout fires within one quantum of expiry regardless of load. Resolves the "indexed deadlines" question. R2-F16 is resolved in the same task: `NvLimits.maxduration` becomes authoritative (validated `1..NvMaxduration` by `nvruntimeinit`, checked by `nvprocarmdeadline` instead of the compile-time ceiling).
6. **Idle sleep and wake protocol.** Each scheduler has a malloc'd semaphore. Before sleeping it: sets its bit in a shared idle mask (under the global lock at stage 1), re-checks every queue for stealable work, drains stale credits, then `tsemacquire`s bounded by its own deadline-heap top (or the D074 GC wait). A waker `semrelease`s the target scheduler whenever it pushes onto that scheduler's queue; a scheduler pushing onto its *own* queue while the idle mask is non-empty and its queue length exceeds 1 `semrelease`s the lowest idle scheduler (so idle schedulers learn there is work to steal). Set-bit / recheck / sleep is what makes "a wakeup issued just before sleep is not lost" (required test 4) true; the semaphore's persistent count is what makes it cheap.
7. **Termination and shutdown.** A machine-level count of idle schedulers plus total runnable, under the global lock: the scheduler whose idle transition makes idle == N with total runnable 0, no GC outstanding anywhere, and no deadline heap non-empty declares D046 Idle and sets `stopping`; root completion or fault sets `stopping` the same way once `nlive == 0`. `stopping` is checked at the top of every scheduler loop; the main proc waits on a `finished` semaphore N times, then aggregates statistics and tears down (each scheduler's own `gcdrain` runs before it signals). A note (interrupt) to the main proc sets `stopping` and `semrelease`s every scheduler.
8. **Output.** One lock around each `print`/`eprint` line (write + flush). D057's guarantee is restated: per-process output order is program order; across processes it is dispatch order *on one scheduler* and unordered across schedulers; lines are never interleaved.
9. **Measurement first.** Per-scheduler counters from the first task: dispatches, reductions, own enqueues, remote enqueues, steals taken/given, wakes sent/received, idle sleeps and total sleep ns, deadline fires, lock acquisitions and (stage 1) lock wait ns on the global lock, and malloc count per send path. `-s` prints one row per scheduler plus totals. `bench/run.rc` gains `-p N` rows for ring, sieve, and a new CPU-bound fixture. Stage 2 (lock splitting) is gated on these numbers, not on intuition.
10. **One code path; N=1 is the same code.** `NvScheduler` is split into `NvMachine` (shared: runtime, module, host table, clock, io, limits, global lock, idle mask, root tracking, scheduler array) and `NvSched` (per proc: run queue, semaphore, deadline heap, GC bookkeeping, counters, current pid). Both are malloc'd (the D074 lesson: `RFMEM` does not share a caller's stack). `nvschedinit`/`nvschedspawnroot`/`nvschedstep`/`nvschedfree` remain as the N=1 facade over the same code, so every existing deterministic test keeps driving the *real* dispatch path step by step; N>1 adds `nvmachinerun`. "Single-scheduler mode retains identical language-visible results" is then enforced by the existing suites, not by a frozen copy of old code. The host callbacks reach their scheduler through a new `NvExec.sched` pointer set at dispatch (private to the running scheduler) rather than through the shared host table.
11. **GC stance under N schedulers.** Inline collection stays the default and is per process, so N schedulers collect N heaps in parallel with no coordination and each pause affects only that scheduler's queue -- the stance `docs/questions.md` records as BEAM's. Off-process collection remains available per scheduler with the D074 protocol unchanged; the only new rule is proposal 4's "unmovable while `offlaunched`". `nvschedmemory` is taken only after all schedulers have stopped.

### Test strategy

Deterministic tests keep the N=1 step API. N>1 correctness is tested with (a) stress fixtures run to completion and checked for invariants after the fact (every process exited exactly once; owner never observed changing while `Prrunning`; PID stable across counted migrations; message order per sender intact -- a receiver checks strictly increasing sequence numbers per sender pid), and (b) two hold points in the style of `nvschedgchold`: one that parks a scheduler between setting its idle bit and `tsemacquire` (lost-wakeup test: inject a send in the window, release, assert dispatch), and one that parks a scheduler between marking a process runnable and pushing it (double-enqueue test: fire its deadline in the window, assert one queue entry). Exit during traffic: a sender loop against a receiver that exits mid-stream; assert `ndropped` accounts for every post-exit send and no fragment leaks (`nvschedmemory` after stop).

### Staging (tasks to cut after approval)

- **T00 -- refactor with no new behavior.** `NvMachine`/`NvSched` split (10), segmented table (2), `NvExec.sched`, `owner` field, counters and `-s` rows (9), `-p N` parsed but N=1 only. Acceptance: all three suite invocations pass unchanged; `bench/run.rc` numbers within noise of `bench/README.md`.
- **T01 -- N procs under the global lock.** Proposals 1, 3, 4, 6, 7, 8. Acceptance: required tests 1, 2, 3, 4, 5, 6, 7 (7 via the unchanged suites).
- **T02 -- deadline heap and timer semantics.** Proposal 5 and R2-F16. Acceptance: `after` fires within one quantum under load (new fixture), existing timeout suites unchanged, `holddeadline` adjusted only if the hold-point interaction requires it.
- **T03 -- measurement.** `-p 1/2/4` on ring, sieve, and the CPU-bound fixture; record in `bench/README.md`; decide whether stage 2 is needed and where.
- **T04 (conditional) -- split the global lock** per T03's evidence, in the order proposal 1 fixes.
- **R4** inside acceptance: ownership, wakeup, migration, shutdown review (mandatory per `R4-multicore-review.md`).

### Tasks

T00 is split in two so each half lands on a green suite before the next begins; both are pure refactors with no new behavior, and the acceptance for each is identical: the three suite invocations pass unchanged and `bench/run.rc` is within noise of `bench/README.md`.

**M10-T00a -- segmented process table (D082).** `include/nvproc.h`, `lib/process.c`, and every file that indexes `runtime.process[...]` directly.
- `NvRuntime` replaces `NvProcess *process; ulong nalloc;` with a chunk-pointer array (`NvProcess **chunk; ulong nchunk;`), `NvProcchunk = 1024` slots per chunk, and an accessor `NvProcess *nvprocat(NvRuntime*, ulong slot)` (inline-cheap: `chunk[slot>>10] + (slot&1023)`), plus a bounds-checked variant if the existing code relies on `slot >= nslot` checks (it does, in `lookup`).
- Growth appends one chunk under the existing `NvMaxslot`/`maxprocess` rules; `tablegrows` keeps counting chunk appends; `tablemoves`/`tablemovebytes` become permanently zero (keep the fields, document them as retired so `-s` output shape is unchanged).
- `nvruntimefree` frees each chunk then the array. `memset` of a fresh slot stays.
- Every direct `r->process[i]` / `s.runtime.process[i]` in `lib/`, `cmd/`, `tests/`, `bench/` becomes `nvprocat(...)`. Tests that assert `nalloc` values (`autotest.c:tablegrowth` asserts `nalloc == 256`, `tablegrows == 5`, and the one-process-limit growth) are rewritten to assert the equivalent chunk facts (`nchunk`, `tablegrows == 1` for 200 slots) with the old numbers noted in a comment; no other test assertion changes.
- Write-set partition for sub-agents: one owns `nvproc.h` + `process.c` + `sched.c` + `main.c`; the fixture churn is split by file among others *after* the header lands, since every fixture depends on the accessor's name.

**M10-T00b -- machine/scheduler split, ownership field, counters (D083 field only, D087, D088 counters, `-p` parsing).** `include/nvsched.h`, `include/nvexec.h`, `lib/sched.c`, `lib/exec.c` (host-callback lookup only), `cmd/nervous/main.c`, and every fixture that reads scheduler fields.
- `NvSched` (per scheduler: run queue head/tail/nrunnable moved here from `NvRuntime`, `sem`, D074 GC bookkeeping and `gchold`/`gcidlestep`, counters, `currentslot/generation/valid`, `profile` timers, `index`) malloc'd by `nvschedinit`; `NvScheduler` keeps the shared state (runtime, module, host, clock, io, root tracking) and gains `NvSched **sched; int nsched;`. At N=1 `nvschedstep` operates on `sched[0]`.
- `NvProcess.owner` added, set at spawn to the spawning scheduler's index (0 at N=1), read by nothing yet.
- `NvExec.sched` set at dispatch, cleared after; host callbacks use it instead of `e->host->aux` for per-scheduler state (the machine is still reached through `host->aux`).
- Counters per D088 on `NvSched`; `-s` prints one row per scheduler plus totals (one row at N=1, totals line identical in content to today's).
- `-p N` parsed in `main.c`, validated `1..NvMaxsched` (a new constant, 64), and rejected with a clear diagnostic for N>1 until T01 ("multicore not yet enabled").
- Fixtures: `s.collections`, `s.gcoutstanding`, `s.reductions`, etc. move to `s.sched[0]->...`; to keep the churn mechanical, provide no aliases -- a single sub-agent per fixture file does the rename against the new header, and the coordinator reviews that no assertion's *value* changed.

T01-T04 and R4 as staged above; cut after T00b lands.

### Hazards to carry into the tasks

- `RFMEM` shares data/bss/heap, not stacks: every structure another proc touches must be malloc'd. `QLock` sleeps via `rendezvous`, which works across procs only within one rendezvous group -- do not `RFREND`.
- `Lock` is a spinlock; never hold one across `malloc`, a `Bflush`, or a quantum.
- The interpreter runs a quantum with no lock: every host callback must re-validate the pid it acts on (it already does, via `lookup`) and must not cache an `NvProcess*` across the callback.
- `nvfragcopy` in `nvprocsend` runs *outside* the lock (it allocates and can be long); only the append/wake is inside. The budget check (`mailboxwords`) is therefore racy across senders and must be re-checked under the lock, refusing with `mailbox_full` and freeing the fragment if it no longer fits -- all-or-nothing is preserved.
- Statistics counters written by many procs: per-scheduler, aggregated at stop; never a shared counter incremented without a lock.
- `nvprocat` (D082) is a macro that evaluates its slot argument twice; never pass it a side-effecting expression. T00a's first build did (`r->nslot++`) and the root spawned into one slot while being queued under another.

### T00a -- done

Landed on a green suite (all three invocations). The only assertion values that changed are `autotest.c:tablegrowth`'s capacity facts and `receivetake`'s `tablebytes` formula, as the spec allowed. `bench/run.rc` still to be rerun to confirm the extra indirection is within noise.
