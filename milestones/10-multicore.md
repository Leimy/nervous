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

## Current lock-split experiment (M10-T04a)

After the user-run scaling and rejected retry captures, the user authorized
trying sharded blocking locks. The FIRST SLICE is user-observed correctness-
green and retained after the complete CPU/GC scaling capture; see
`../docs/sharding.md`, `../bench/shards-results.md` and STATUS.md. Local dispatch/yield/receive/GC use
one per-scheduler QLock. Structural/cross-owner operations take the machine
gate followed by ALL shards in ascending order, reverse release. No held
local shard may request the machine gate. This is a deliberately smaller
landing than the full process-lock/queue-lock stage-2 proposal below: it
preserves atomic queue publication, table routing and exact termination,
while leaving send/ref/steal/idle serialized. No libthread conversion or
spinlock swap. T02, full T04 and R4 are not declared complete by this slice.

New scheduler tests cover progress with unrelated gates held and both modes
at N=1/2/4; bench/shards.rc compares same-build single-global and sharded
controls. User reports the requested tests pass; the full same-build capture
ends with all cases passed. Four balanced workers at N=4 improve global ->
sharded from 1.398 -> 0.311 s (64-word heaps) and 0.812 -> 0.226 s (4096).
This closes the first-slice run checkpoint, not R4 or the full mailbox split.
The interactive interrupt/no-orphans check remains unconfirmed.

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

Landed on a green suite (all three invocations). The only assertion values that changed are `autotest.c:tablegrowth`'s capacity facts and `receivetake`'s `tablebytes` formula, as the spec allowed. `bench/run.rc` rerun (`bench/runs/T00b-small.txt`): counts identical to T04p.

### T00b -- done

Accepted on a green suite; `bench/runs/T00b-small.txt` is the N=1 baseline every later `benchcmp` compares against. All six items above are implemented (see STATUS.md's checkpoint block for the file-by-file account). Implementation choices worth knowing when reading the code, all within D083/D087/D088:

- The per-scheduler run queue is a small `NvRunq` struct. The queues are malloc'd and owned by `NvRuntime` (`runq[]`, indexed by `NvProcess.owner`), and each `NvSched` points at its own; `process.c` reaches a slot's queue through its owner, so `nvprocsend`'s wake and a deadline wake both enqueue on the owner's queue by construction (D083's "wake to home"). `nvruntimesetnrunq` grows the set; T01 calls it with N before spawning the root.
- `nvprocspawnon(r, owner, ...)` is the real spawn; `nvprocspawn` is the owner-0 wrapper, kept so the bare-runtime fixtures did not churn.
- `NvExec.sched` is a `void *` (nvexec.h does not know `NvSched`), valid exactly for one quantum. Host callbacks take the per-proc state from it and the machine from `host->aux`.
- The D088 counters that have no event until T01 are declared and printed as zeros now, so the `-s` row shape is fixed before they can move. Own-queue arrivals are counted at the queue (`NvRunq.enqueues`); remote enqueues, steals, wakes, sleeps and lock figures are enqueuer-side and belong to T01. The totals lines are unchanged; the rows follow them.
- `nvschedstep` is a facade over `step(s, sc, ...)`, and every table scan in `step`/`gcfoldall` is already filtered by `p->owner == sc->index` (a no-op at N=1) so T01's proc loop is `step(s, sched[i], ...)` with the lock around the runtime mutations, not a second dispatch path.

### M10-T01 -- N procs under the global lock (spec; cut when T00b is green)

Implements D081 (staged lock), D083 (owner/wake-to-home, steal), D085 (idle/wake protocol, output lock), D086 (termination), plus the Finding-7 atom decision below. Coordinator-implemented; the write set is nearly the whole runtime and the work is one interlocking mechanism, so it does not partition. Sub-agents, if any, do read-only audits of the finished lock discipline (template: the R3-F02 audit).

Write set: `include/nvsched.h`, `include/nvproc.h`, `include/nvexec.h` (only if `NvExec` needs a field), `lib/sched.c`, `lib/process.c`, `lib/exec.c` (fixedatom/Oloadk/Otestatom only), `cmd/nervous/main.c`, new `tests/multicore/` fixtures + `tests/run.rc` lines, `bench/run.rc` (`-p` rows), `docs/decisions.md` (D090), `docs/semantics.md` (D085's restated output-order guarantee).

Steps, in landing order (each builds clean; the N=1 suites must pass after every one, since the N=1 facade is the same code):

1. **Configuration.** `nvschedinit` keeps its signature as the N=1 facade; new `int nvschedsetnsched(NvScheduler*, int n, char*, int)`, legal only before the root spawn, allocates `sched[1..n-1]` (each with its own `gcsem`/`gchold`, `index`, `runq = runtime.runq[i]` after `nvruntimesetnrunq(&runtime, n)`). `runscheduled` calls it with `-p N` in place of today's refusal.
2. **Global lock.** `QLock lock` on `NvScheduler` (malloc'd is unnecessary: `NvScheduler` is the main proc's stack and `RFMEM` shares data but not stacks -- so **`NvScheduler` itself must become malloc'd by `runscheduled`** or the lock must be a malloc'd pointer; choose the pointer, `QLock *lock`, so the fixtures' stack-allocated `NvScheduler` keep working). Held for every runtime mutation and every table scan: in `step`, around `finddispatchable` + `nvprocdispatch` (pop), around `nvprocyield`/`nvprocexit`/`collect`'s bookkeeping, around the whole idle branch; in the host callbacks around `nvprocsend` (whole send, copy included -- see hazard; the copy-outside-lock split is a T04 candidate gated on T03's `lockwaitns`), `nvprocref`, `nvprocspawnon`+exec setup, and every `nvprocrecv*`/`nvprocarmdeadline`. **Never held across `nvexecrun`**, `nvfragcopy` in the root-exit path, `Bflush`, or `rfork`. `lockacq`/`lockwaitns` are incremented by a `lockrt(s, sc)` wrapper (`nsec()` around `qlock`; the profile flag gates the clock read as today's `profile` does). Off-process collector children take no lock (D074 unchanged; they touch only the exec).
3. **Wake-to-home and remote enqueue.** `runenq` is already owner-directed. Add to `process.c` an optional enqueuer identity: `nvprocsendfrom(r, from, pid, value, ...)`/`nvprocwakefrom(r, from, slot)` (or a `r->enqhook(from, owner)` callback set by the scheduler) so the scheduler learns "slot X was pushed on owner Y's queue by scheduler `from`". On `from != owner`: `sched[from]->remoteenq++`, `semrelease(sched[owner]->sem)`, `sched[from]->wakessent++`. On `from == owner` with `idlemask != 0` and `runq->nrunnable > 1`: `semrelease` the lowest idle scheduler (D085's "there is work to steal" hint). At N=1 nothing fires.
4. **Idle protocol and steal.** `step` returns `NvSchedIdle` exactly as now but no longer calls `clock.wait` when `s->nsched > 1`; instead it records `sc->wakeat = earliest` (0 = none) and returns. The proc loop (`schedproc`, below), on `NvSchedIdle`: under the lock, set its bit in `s->idlemask`, `sc->sleeps++`; try `nvprocsteal(r, slot, sc->index)` on the head of the longest other queue whose exec is `!offlaunched` (`stealstaken++`, victim's `stealsgiven++`; steal = `runrm` from the old owner's queue, `owner = new`, `runenq`; PID/heap/mailbox untouched -- required test 3); if stolen, clear the bit and continue. Else check D086 termination (below); else release the lock and `tsemacquire(sc->sem, ms)` with `ms` = `deadlinems(wakeat)` if `wakeat`, else the D074 GC bound if a collector is outstanding, else a long bounded wait (`NvGcwaitmaxms`); on return, clear the bit under the lock; a return of 1 is `wakesrecv++`; `sleepns` accumulates. The set-bit / re-check-queues / sleep order is what makes required test 4 true. Expired deadlines after a semaphore wake: `step`'s existing "now >= earliest" wake loop (currently only in the collector-outstanding branch) is hoisted so any idle entry with an expired owned deadline wakes it, N=1 included (same observable behavior: at N=1 `clock.wait` has already advanced the clock).
5. **Termination (D086).** Under the lock: `nidle` (schedulers with idle bit set) and the sum of `runq[i]->nrunnable`; the scheduler whose idle transition makes `nidle == nsched` with total runnable 0, `gcoutstanding` 0 on every `sched[i]`, and no owned armed deadline anywhere sets `s->stopping = NvStopIdle`; root completion/fault/exit with `nlive == 0` (today's `NvSchedDone`) sets `NvStopDone`. `stopping` is checked at the top of the loop and every sleeper is `semrelease`d when it is set. Each proc runs `gcdrain(s, sc)` then `semrelease(s->finished)`; the main proc (which runs `sched[0]`'s loop itself, so N=1 forks nothing) waits `finished` N-1 times, then reports and `nvschedfree`s. A note to the main proc sets `stopping = NvStopInterrupt` and wakes everyone.
6. **Proc loop.** `int nvmachinerun(NvScheduler*, char*, int)`: `rfork(RFPROC|RFMEM|RFNOWAIT)` for `sched[1..n-1]` (a failed rfork after some succeeded: set `stopping`, wait for the started ones, report "system_limit"), then run `schedproc(s, sched[0])` inline, wait, and return the machine outcome (`NvSchedDone`/`NvSchedIdle`/`NvSchedError` with the first error string captured under the lock). `runscheduled` uses it for every N (N=1 too: same code, no fork), replacing the `for(;;) nvschedstep` loop; the deterministic fixtures keep `nvschedstep`.
7. **Output (D085/P8).** `QLock *iolock` around each `print`/`eprint` line (print + `\n` + `Bflush`), and the D057 restatement in `docs/semantics.md`: per-process order is program order; across processes it is dispatch order on one scheduler and unordered across schedulers; lines are never interleaved.
8. **Atoms (Finding 7 -> D090, proposed).** `nvschedinit` pre-interns `true`/`false`/`ok`/`undefined` so `lib/exec.c`'s `fixedatom`/`boolatom` never allocate on an interpreter path, and `Oloadk`/`Otestatom` fault `bad_atom` instead of interning when `k->atom == NvNil` (unreachable after `nvexecinit`'s pre-intern; the fault replaces a silent shared-table write). Then `nvatom` needs no lock on any bytecode path; D070's lock remains a host/test obligation. **Needs the user's approval before it is written.**
9. **Statistics.** All D088 counters live; `-s` rows populate. `bench/run.rc` gains `-p 1/2/4` rows for ring, sieve and `cpubound.nv` (already written; run it once at N=1 first: `nervous -r bench/cpubound.nv main` should print 5600000).

Tests (`tests/multicore/`, run by `tests/run.rc` at `-p 2` and `-p 4`, plus one deterministic C fixture):
- `cpubound.nv` completes with the same value at every `-p` (required test 1), and its `-s` shows dispatches on more than one row at `-p 4`.
- `order.nv`: k senders each send 1..n to one receiver; the receiver asserts strictly increasing sequence per sender pid, returning `'ok` or the first violation (required test 2).
- `migrate.nv`: a process that records `self` before and after many yields/receives while siblings churn; equality of the recorded pids is the PID-stable-across-steal check (required test 3), with `stealstaken > 0` in `-s` confirming steals happened.
- `exitstorm.nv`: senders flood a receiver that exits mid-stream; asserts completion, and `-s` `dropped` + `nvschedmemory` after stop (via a C fixture) show no leaked fragment (required test 6).
- C fixture `tests/multicore/mctest.c`: `nvschedsetnsched(2)`, install a hold hook that parks a scheduler between setting its idle bit and `tsemacquire` (`nvschedidlehold`, `gchold`-style malloc'd word), inject a send to a process it owns from the main proc, release, assert dispatch (required test 4). Required test 5 (double enqueue) has no window under the global lock -- both halves are one critical section -- so it is a T04 test; say so in `tests/multicore/README.md`.
- Required test 7 is the unchanged N=1 suites.

Hazards specific to this task, beyond the list above: `qlock` uses `rendezvous`, so no `RFREND`; `tsemacquire` can return -1 on a note -- treat as a timeout; `semrelease` on a freed word is the R3-F06 bug class, so every `sem` is freed only after `finished` has been collected N-1 times; `nvschedmemory` is called only after `nvmachinerun` returns.

### M10-T01 -- landed (three rounds; core accepted, fixtures and measurement rows open)

Steps 1-6 and 8 of the spec above are in the tree and accepted: all three suite invocations pass end to end at N=1, and `examples/sieve.nv main 5000`, `examples/ring.nv main 1000 2000` and `bench/cpubound.nv main` produce their N=1 values at `-p 2` and `-p 4`. `bench/benchcmp bench/runs/T00b-small.txt bench/runs/T01-small.txt`: 0 count changes, 0 cost flags, every shape within +-2.6%. STATUS.md's checkpoint block has the round-by-round account; this section records what a reader of the code should know differs from the spec, and what each round found.

Departures from the spec, all deliberate:

- **Locks are embedded, not pointers** (`QLock lock`, `QLock iolock` in `NvScheduler`). `nvmachinerun` needs the whole `NvScheduler` malloc'd anyway -- it holds the runtime the forked procs share -- so `runscheduled` in `main.c` now `mallocz`es it and both Biobufs (`malloc` + `Binit`, not `Bfdopen`: round 1 found that `Bterm` on a `Bfdopen`'d stream closes fd 2, which silently ate every `fprint(2, ...)` after it, including the fault line the `binfault-overflow` fixture looks for). Fixtures keep their stack `NvScheduler` because at N=1 no proc is ever forked.
- **The lock is off at N=1.** `NvScheduler.locking` is set by `nvschedsetnsched` (N>1) or by a fixture through `nvschedsetlocking`; `lockrt`/`unlockrt` are no-ops otherwise, and it can never be turned off at N>1. Round 1 took it unconditionally to make a nested take deadlock visibly in the N=1 suites -- which it did not, and the +13-17% wall on every message-bound shape was the price. The lock-discipline audit below is the replacement for that safety net.
- **`oncpu`/`pendingwake` (not in the spec).** `nvprocrecvwait` marks a process `Prwaiting` a few instructions before the interpreter yields; at N>1 a remote sender saw `Prwaiting` in that window and enqueued a process that was still running (round 1: the sieve at `-p 4` stopped after 13 processes with "yielded process has bad lifecycle state"; a third scheduler could in principle have stolen and run it concurrently). `nvprocdispatch` now sets `NvProcess.oncpu`; `nvprocwakefrom` on an `oncpu` process sets `pendingwake` and returns; `nvprocoffcpu`, called by `step` under the lock right after `nvexecrun`, clears `oncpu` and performs the deferred enqueue. Any fixture that drives `nvprocdispatch` by hand must call `nvprocoffcpu` before expecting a send to make the process `Prrunnable` (`ptest.c`'s lifecycle block is the reference).
- **Steal threshold `NvStealmin = 2`** (D083 amendment): stealing at >= 1 made the ring's single token migrate on every hop (120K steals per 2M hops, 2x slower at `-p 2`). `kickidle` likewise only fires when a backlog of 2 remains after the pop.
- **`idle()` re-checks its own queue** with `finddispatchable` before stealing or setting its bit (D085 amendment). Round 2 exposed the window between `idlestep` releasing the lock and `idle()` retaking it: a remote enqueue landing there saw no idle bit, sent no kick, and the owner slept the full 60 s bound with a runnable on its queue. Round 1 had been hiding it behind the steal churn.
- **Step 8 faults `bad_constant`, not `bad_atom`** (D090): no new reason was needed.
- **The idle branch wakes an expired owned deadline itself** at N>1 (the spec's hoisting), and at N>1 never blocks: `idlestep` returns `NvSchedIdle` with `wakeat`/`wakeatvalid` for the proc loop to sleep on. At N=1 it blocks exactly where it always did, lock released.
- **`nvmachineinterrupt` is wired** through `atnotify` in `main.c`'s `runscheduled`, for the `interrupt` note only, active only across `nvmachinerun`. Forked scheduler procs share the note group and run the same handler; it is idempotent. `nvmachinerun`'s wait on `finished` retries a `-1` (note) return rather than treating it as a lost scheduler.

Round summary: round 1 (built from the spec) found the `Bfdopen`/`Bterm` fd loss, the on-CPU wake race, and the steal churn; round 2 fixed those and found the own-queue lost wakeup (a ring hang) plus the `ptest` contract change; round 3 fixed both and was accepted. Numbers at round 3: ring `-p 2` 1.30 s (N=1 0.83 s; 16.3M lock acquisitions, ~8 per hop, plus ~34K idle/kick/sleep cycles because sched 1 owns the 18 nodes it stole during build); sieve `-p 4` 0.69 s (N=1 0.13 s); cpubound `-p 4` 0.89 s, no speedup because the fixture collects on nearly every dispatch (11 live words, 3-word tuple per iteration against D069's 64-word minimum space) -- it measures dispatch+collect, not CPU, and needs a bigger live set before T03 can use it.

Closed after round 3 (coordinator, same session): step 7's `docs/semantics.md` restatement; step 9's `bench/run.rc` `-p` rows as the `multi` set (`benchp`; not yet run -- T03 runs it); the note handler (above); and the lock-discipline audit of `lib/sched.c`, done by reading the file end to end against the five questions -- `lockrt`/`unlockrt` paired on every return path (11 host callbacks, `collect`, `idlestep`'s 14 returns, `step`'s 12, `idle`, `stopmachine`, `gcdrain`); `iolock` reached only from inside a quantum; nothing held across `nvexecrun`, `clock.wait`, any blocking `tsemacquire`, `rfork`, or `doinline`; every table scan under the lock; the heap `Lock` only taken inside the QLock or alone and only for a one-word transition. All clean. One observation, now a comment in `step()`: the root-exit `nvfragcopy` runs under the lock, once per machine, accepted. `nvprocsteal`'s `offlaunched` refusal confirmed in `process.c`.

`tests/multicore/` is written (`order.nv`, `migrate.nv`, `exitstorm.nv`, `README.md`; `tests/run.rc` runs them at `-p 1/2/4`) but has not yet been run -- see STATUS for the owed run. Required tests 1 and 7 are met by the runs above and the suites; 2, 3 and 6 are covered by the new fixtures once they pass; 4 needs the `mctest.c` hold-point fixture (a new `NvSched` idle hook, deferred until it can be validated by a run); 5 has no window under the global lock and belongs to T04.
