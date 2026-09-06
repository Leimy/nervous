# Nervous Project Status

Operational source of truth. Settled design is in `docs/decisions.md`; benchmark evidence is in `bench/README.md`; regression coverage is in `tests/memory/README.md`. Older milestone history lives in `STATUS-archive.md` and the review archives.

## Checkpoint and authorization

```text
milestone: 08 - Memory, active but not complete
checkpoint: PR2-T01, M08-T04a, M08-T04b, M08-T04p, M08-T04e and M08-T04c accepted
implementation: PAUSED at user's request (session budget); no task is
  in-flight, no write set is held by anyone
coordinator: nervous-memory-coordinator (role label, not a discovered session identity)
active source assignment: none
next tasks: T04r and T04d planned and unassigned; require explicit
  user go-ahead and exclusive assignment before any edits, per the
  Resumption checklist below
source control: user requested a combined commit message; message supplied.
  No commit/push/staging performed or git status independently inspected by the coordinator.
  Do not assume the user has committed/pushed merely because this is an accepted save point.
```

M08-T04e and M08-T04c are both accepted: the user confirmed `rc tests/run.rc` and `nervous_gcstress=1 rc tests/run.rc` pass on the current tree, the latter now also exercising `tests/memory/offloadtest`'s seven off-process lifecycle groups. This closes the last open acceptance gate for T04c (see the M08-T04c section below for the coordinator review and five fixes made before this confirmation).

Milestone 08 remains open: T04r (large-live-set latency baseline) and T04d (policy measurement and milestone acceptance) are still outstanding, both planned but unassigned. No formal review gate is active. R3 has not opened and is not complete. D074 (`docs/decisions.md`) settles the off-process ownership/wakeup/teardown protocol implemented and accepted in T04c.

**Session paused here at the user's request** (out of budget for now). This is a clean stopping point: no task is assigned, no write set is held, the build is clean, and both regression suites pass. Resume by reading the Resumption checklist at the end of this file.

## Accepted evidence

- Build: `mk -a tests benchmarks` rebuilt every command/library/test/benchmark object and linked all targets without diagnostics after final T04p source edits. An earlier unreachable-return warning in the new benchmark driver was fixed before that rebuild. Build tools executed compilation/linking only.
- Behavior: user confirmed both requested T04p suite commands passed. Correction: `rfork E` discarded inherited CLI stress, so those runs established normal-suite and explicit C stress coverage, NOT full CLI-stress coverage at the time. T04e repaired inheritance and added a collection-count canary; the user has since confirmed fresh normal and CLI-stress runs both pass on the repaired tree (see the T04e closure bullet below).
- Measurement: user supplied the original six `rc bench/run.rc` shapes and all seven `rc bench/perf.rc` cases through `/dev/snarf`. Normalized T04p results and limitations are in `bench/README.md`; the earlier inline baseline is also preserved in `bench/inline-gc-first.txt`.
- Latest build: `mk tests` compiled/linked the T04e CLI fix and all test targets without diagnostics. New rc regressions require user execution; mk does not validate or run scripts. No runtime tests, benchmarks, commit or push executed by the coordinator.
- T04e closure: user confirmed both `rc tests/run.rc` and `nervous_gcstress=1 rc tests/run.rc` pass on the repaired tree, including the CLI environment suite and the inherited-mode canary. This is the fresh normal/CLI-stress evidence the earlier `rfork E` correction called for.

## Milestone ledger

00-07 and R1-R2 are complete; their historical rows are archived.

| Milestone | State | Dependencies | Remaining scope |
|---|---|---|---|
| 08 Memory | active; T04r/T04d unassigned, paused | R2, 05, 06 | Latency baseline measurement and final policy/acceptance |
| 09 Binaries | not-started | 04, 08 | Binary construction and matching |
| R3 Memory review | not-started | 08, 09 | Root/fragment/representation/binary ownership gate |
| 10 Multicore | not-started | R3, 05, 06, 08, 09 | Parallel schedulers and work movement |
| R4 Multicore review | not-started | 10 | Mandatory multicore acceptance review |

## Task ledger

Every completed write set below is released. Planned tasks have no assignee or reserved files.

| Task | State | Dependencies | Scope / evidence |
|---|---|---|---|
| M08-T01 representation design | done | prior milestones | D061-D066 |
| M08-T02 interned atoms | done | T01 | User-accepted; previously recorded committed before stage 2 |
| M08-T03 tagged terms, frames, fragments | done | T02 | Tests/bench user-confirmed; previously recorded committed as "milestone 08 stage 2" |
| PR2-T01 guard verifier hardening | done | post-R2 safety finding | D071; bytecode/full suites user-confirmed; post-R2-F01 closed |
| M08-T04a explicit collector/root adapter | done | PR2-T01 | Core suites user-confirmed |
| M08-T04b automatic inline collection | done | T04a | D072; normal/stress suites and first inline benchmark accepted |
| M08-T04p bounded performance/diagnostic pass | done | T04b | D073; final build, tests and original/phase benchmarks accepted |
| M08-T04e CLI/environment defect repair | done | T04p | Empty-as-unset, named diagnostic, environment-preserving runners and direct CLI regressions; user-confirmed passing both suites |
| M08-T04q duplicate interpreter work | planned, optional, deferred | T04p | Time-boxed preflight/execution reuse; no safety or sizing-policy change; not a dependency of T04c |
| M08-T04c off-process collection | done | T04e | D074 ownership protocol, collector procs, wakeup, failure and teardown correctness; five coordinator-applied correctness fixes during review (see M08-T04c section); `mk -a tests benchmarks` clean; `tests/memory/offloadtest.c` lifecycle suite; user confirmed both `rc tests/run.rc` and `nervous_gcstress=1 rc tests/run.rc` pass |
| M08-T04r large-live-set latency baseline | planned | T04c | Required measurement fixture for choosing offload policy; deferred behind T04c since no evidence-based threshold can exist before the mechanism does |
| M08-T04d policy and milestone acceptance | planned | T04c, T04r | Both-mode stress, representative latency/footprint measurements, recorded constants |

T04q is not a new milestone dependency gate and is not required before T04c; the user chose to proceed directly to off-process correctness. T04r was reordered after T04c: a threshold measurement needs the off-process mechanism to measure against, and current tiny-live-set benchmarks already show no existing workload would select a finite gcoffload value (see D074, "gcoffload default and every existing call site").

## M08-T04e CLI/environment defect repair (done)

Accepted: user confirmed both `rc tests/run.rc` and `nervous_gcstress=1 rc tests/run.rc` pass, each reporting `all CLI environment tests passed` and ending with `all nervous regression suites passed`. Write set released. Implementation summary retained for the record: empty-as-unset environment parsing with a named diagnostic, `rfork E` to `rfork e` in all seven test runners so inherited `nervous_gcstress` survives, and `tests/cli/run.rc`/`value.nv` covering absent/zero-length/quoted-empty/0/1/-G/invalid cases plus a one-tuple allocation canary. No VM/GC/runtime algorithm change.

## M08-T04c off-process collection (done)

Accepted: user confirmed both `rc tests/run.rc` and `nervous_gcstress=1 rc tests/run.rc` pass, the latter now exercising `tests/memory/offloadtest`'s seven lifecycle groups (`holdbasic`, `holddeadline`, `holdteardown`, `holdall`, `holdsweepcap`, `holddeadlock` -- this last one added during coordinator review -- and `nooffloaddefault`). Write set released. Implements D074 in full. Summary for the record:

- `include/nvvm.h`: `NvHeap` gains `owner` (`NvHeapIdle`/`NvHeapRunning`/`NvHeapCollecting`) and a `Lock`.
- `include/nvexec.h`: `NvExec` gains `offlaunched` (scheduler-only bookkeeping; the collector child never touches it). `nvexecgc`/`nvexeccollect`/`nvexecrun` signatures unchanged, confirmed sufficient as pure, exec-only functions callable from a forked proc.
- `include/nvproc.h`: `NvLimits.gcoffload` (word threshold; 0 = never off-process, the corrected D074 default polarity). `nvprocrequeue` declared.
- `include/nvsched.h`: `NvScheduler` gains `gcoutstanding`, `gcofffallback`, malloc'd `gcsem`/`gchold` pointers; `NvMemstats.ncollecting`; `nvschedgchold` (test hold-point hook) declared.
- `lib/gc.c`, `lib/exec.c`: unchanged, confirmed (not just assumed) sufficient by reading `nvexecgc`/`nvexeccollect`/`nvheapcollect` closely -- they are already scheduler/exec-agnostic pure functions over one stopped process's own heap and stack.
- `lib/process.c`: added `nvprocrequeue` (dequeue-and-reenqueue-at-tail for a specific runnable slot), needed by the dispatch-skip logic; preserves the D059 FIFO invariant.
- `lib/sched.c`: launch (`collect`, off-process branch), the collector child (`collectorchild`, restricted to `NvExec*`/`gcsem`/`gchold` arguments only), the locked completion fold (`gcfold`), dispatch-time discovery (`finddispatchable`), the never-idle/never-spin wait logic in `nvschedstep`, the bounded teardown drain (`gcdrain` in `nvschedfree`), the idle-sweep launch cap, and `nvschedmemory`'s skip-and-count of collecting heaps.
- `cmd/nervous/main.c`, `tests/process/ptest.c` (3 sites), `tests/process/schedtest.c` (1), `tests/process/iotest.c` (1), `tests/process/r2test.c` (10, not the ballpark "about nine" in D074 -- see handoff), `tests/memory/autotest.c` (1, its shared `limits()` helper), `bench/perftest.c` (1): explicit `.gcoffload = 0;` added at every confirmed `NvLimits` construction site. `tests/memory/gctest.c` and `tests/process/exectest.c` confirmed to construct no `NvLimits`, unchanged.
- `tests/memory/offloadtest.c` (new) and `tests/memory/README.md`: six lifecycle-test groups using a new deterministic test hold point (`nvschedgchold`).
- Also touched, **outside the originally assigned write set**, because the task cannot otherwise build or run: `mkfile` (new `offloadtest`/`tests/memory/offloadtest.$O` rules, added to the `tests` meta-target and `clean`) and `tests/memory/run.rc` (invoke the new binary). Flagged as a blocker in the mid-task handoff; the coordinator (continuing this same session) explicitly instructed touching them, so they are included here rather than left undone, but the exception is recorded for the record.

`mk -a tests benchmarks` rebuilt every target from clean with no diagnostics. `rc tests/run.rc` and `nervous_gcstress=1 rc tests/run.rc` are still required user-run checks (neither the implementer nor the coordinator has a shell; this is the same evidentiary discipline every prior task in this file has followed). See the implementer's full handoff report for semantic decisions proposed (test hold-point shape, idle-sweep cap as a private constant, gcoffload/gcofffallback/gcoutstanding on `NvScheduler`, the force-all-off-process spelling as `gcoffload=1`) -- all ratified as reasonable on review, none reopened.

### Coordinator review (this session)

Read every changed file directly rather than trusting the handoff summary, per `COORDINATION.md`. Verified: the shared-memory placement requirement (`gcsem`/`gchold` are genuinely malloc'd pointers on `NvScheduler`, not embedded fields -- confirmed by reading `nvschedinit`/`nvschedfree`); the `gcoffload` polarity and its explicit presence at all 18 pre-existing `NvLimits` construction sites across 7 files (spot-checked `cmd/nervous/main.c` and `tests/process/r2test.c`'s all 10 sites in full, both correct; `tests/process/ptest.c`'s 3 sites read in full and correct); `lib/process.c`'s `nvprocrequeue`; the full new `tests/memory/offloadtest.c` suite; `mkfile`/`tests/memory/run.rc`.

**Four correctness defects found and fixed directly** (small, precisely scoped each; fixed in place rather than round-tripped back to the implementer, given the cost of a full review cycle -- a second advisor review pass and one more self-check while writing a new regression test caught three of these four after the first pass looked clean):

1. **Ordering gap in the completion fold.** `gcfold` correctly took the heap `Lock` before reading the collector-written `gcretry`/`livewords` fields, but the caller (`finddispatchable`) then did a *second*, unlocked read of `e->heap.owner` immediately afterward to decide dispatch. If `gcfold`'s locked read observed "still collecting" but the collector child finished microseconds later, that second unlocked read could observe `NvHeapIdle` without ever having synchronized via the lock -- meaning the interpreter's later unlocked read of `e->gcretry` in `prepare()` could, on a weak memory model (this project targets 7c/arm64), be the *first* witness of the collector's writes, with no ordering guarantee. Fixed by making `gcfold`'s own locked determination authoritative: it now returns whether the exec is safe to dispatch (1) or still collecting (0), and `finddispatchable` uses that return value instead of re-reading `owner` unlocked.
2. **Livelock in deadlock detection.** `gcfold` was reachable only through `finddispatchable`, which examines only `Prrunnable` slots. A collection launched by the opportunistic idle sweep (D067) targets a `Prwaiting` process, which can stay waiting forever in a genuinely deadlocked program -- its completion would never be folded, `gcoutstanding` would never reach zero, and D046 deadlock detection would be silently disabled from that point on: every future `nvschedstep` call would loop on `tsemacquire` instead of ever reporting `NvSchedIdle` again. Fixed with a new `gcfoldall(s)` that walks every process slot (runnable or waiting) and folds any `offlaunched` exec whose collector has completed; called once per idle-branch pass before the `gcoutstanding == 0` decision.
3. **Use-after-free risk in teardown.** `gcdrain` decremented `gcoutstanding` once per successful `tsemacquire`, treating the semaphore's count as a 1:1 proxy for "one more completion is foldable". That is false: `gcfold` (via ordinary dispatch) decrements `gcoutstanding` independent of whether anyone consumed that exec's `semrelease`, leaving a stale credit in the semaphore. A later `gcdrain` `tsemacquire` could consume that stale credit and report drain-complete while a *different* collector was still genuinely running, freeing memory out from under it. Fixed: the semaphore is now used purely as a sleep/wakeup signal in `gcdrain`; every iteration calls `gcfoldall` to re-derive the true count from the locked owner check, never from the semaphore value.
4. **Double-launch risk in the idle sweep.** The sweep's launch-eligibility check read `e->heap.owner != NvHeapCollecting` unlocked. If a collector had already published `NvHeapIdle` for an exec whose completion had not yet been folded (fix 2 had not run since), the sweep could launch a *second* collector for the same exec -- double-incrementing `gcoutstanding`, discarding the first collection's results, and risking two collector procs genuinely racing on one heap. Fixed by gating on `!e->offlaunched` instead: `offlaunched` is touched only by the scheduler proc itself, so it needs no lock and is authoritative for "a launch/fold cycle is already in flight."
5. **Stale semaphore credits degrade the wait into a spin.** A direct consequence of fix 2/3's design (the dispatch-path fold decrements `gcoutstanding` without ever consuming the matching `semrelease`): once any completion has ever been folded that way, the leftover credit makes every later bounded `tsemacquire` in the "collector outstanding" wait return immediately instead of actually sleeping until the next real completion. Not a correctness bug -- `gcfoldall` keeps the count accurate regardless -- but it defeats the entire point of "never spin" for the rest of that scheduler's life. Fixed: drain the semaphore to zero (non-blocking) and re-fold immediately before the bounded wait, rechecking `gcoutstanding == 0` (this recheck doubles as the lost-wakeup guard for a completion landing in the narrow window since the earlier fold).

All five are recorded as amendments in D074 (`docs/decisions.md`) so the design record stays normative, not just the code. A regression test (`holddeadlock` in `tests/memory/offloadtest.c`) specifically exercises fix 2/3's scenario: a waiting, unreachable-by-message process whose off-process collection completes with nothing else outstanding must still result in `NvSchedIdle` being reported. Rebuilt clean (`mk tests benchmarks`) after every fix; no file outside `lib/sched.c`, `tests/memory/offloadtest.c`, and `docs/decisions.md` needed a change for any of the five.

Also independently re-verified (not merely assumed from the earlier summary) by directly re-reading both files in full: `tests/process/schedtest.c` and `tests/process/iotest.c` each correctly carry their one `.gcoffload = 0;` addition. Combined with the direct re-reads of `cmd/nervous/main.c` (1 site) and `tests/process/r2test.c` (all 10 sites) and `tests/process/ptest.c` (all 3 sites) earlier in this review, every one of the 18 sites across the 7 files this task was assigned to fix has now actually been read and confirmed, not just claimed.

Two minor, non-blocking notes for T04d rather than fixes made now:
- `gcfoldall` runs after the idle sweep, not before, so an exec whose waiting-process collection completed in an earlier pass becomes eligible for a fresh sweep decision only on the *next* pass, not the same one. Correct (one pass of latency, no staleness beyond that), just not maximally prompt; moving the call before the sweep would let the sweep see freshly-folded `livewords` in the same pass. Optional.
- The new `holddeadlock` test relies on `nvexecinit`'s initial heap space being exactly 64 words so that a 41-word tuple exceeds `cap/2` and triggers the idle sweep's eligibility check. If a future change alters that initial sizing, this test will fail loudly at the "waiting process's collector was not launched off-process" assertion rather than silently passing -- the right failure mode, but worth knowing what it means if the user's `rc tests/memory/run.rc` run reports it.

Accepted: user confirmed both `rc tests/run.rc` and `nervous_gcstress=1 rc tests/run.rc` pass. Not reopening any of the implementer's flagged decisions; T04d inherits the sweep cap (`NvGcsweepcap = 8`) and the `gcoffload` default (0) as provisional, exactly as flagged.

## Recommended next sequence

T04c is done. The session paused here (budget) before starting either of the two remaining items below; both are planned but unassigned, and neither has a write set reserved.

### T04r large-live-set latency baseline (planned, next)

The existing benchmarks have tiny collected live sets. Off-process GC is intended to protect unrelated processes from a LARGE collection pause, not make a tiny collection faster; no evidence-based `gcoffload` threshold can be chosen before the off-process mechanism exists to measure against.

- Build a controlled large-live-set owner alongside small-message/heartbeat peers; vary retained live words independently of waiter count and allocation rate.
- Record inline peer latency distributions/tails (not only total ns/message), throughput, collection frequency and memory. Include a control for measurement overhead and repeat short runs.
- Keep the fixture reusable for forced inline, forced off-process and threshold candidates. Do not fabricate an off-process baseline before it exists.
- Read `bench/README.md`, `bench/perftest.c`, `lib/gc.c`, D068-D069, D074 and the milestone's tests. Bench/test files and any necessary instrumentation need an exact future write assignment.

### T04q duplicate interpreter work (planned, optional, deferred; not a T04c dependency)

Source inspection found arithmetic results and call-target resolution computed during reservation preflight and then computed again for execution. A narrow pass can remove this duplication. Not required before or after T04c; pursue only if the user wants the throughput work specifically.

- Read `lib/exec.c`, `include/nvexec.h`, D063/D067/D071-D073 and the memory test coverage first.
- Reuse scalar arithmetic results or function indices within the SAME instruction attempt where safe. Discard/recompute the plan after NvCollect; never retain an unrooted heap pointer across movement. Avoid persistent module caches or public ABI changes unless separately justified.
- Preserve exact operand errors, overflow/divide behavior, guard fault transfer, call/tailcall accounting, unchanged pc/reductions on collection requests, and one-time side effects.
- Acceptance: forced build, normal/stress regressions, quanta 1/1000, and repeated unprofiled original/control benchmarks. Keep measured wins or a justified simplification; do not weaken checks to chase the stage-2 number.
- Stopping rule: one bounded attempt, then proceed or defer.

### Measure policy and close 08: T04d

- Run full stress with inline-only and forced off-process collection. Rerun original and large-live-set benchmarks; choose gcoffload and any justified sizing/idle constants from results.
- Current accepted defaults remain unchanged unless a separate measured decision replaces them. Record the chosen constants and evidence in decisions/questions/bench records.
- Confirm bounded long-running memory and explain heap/message ownership without cross-heap dependencies. Reconcile all required tests in `milestones/08-memory.md` before marking it complete.
- Then milestone 09 and the mandatory R3 review, in order. No new language features or multicore schedulers are part of this handoff.

## Current implementation map

- Representation/accounting: `include/nvvm.h`, `lib/value.c`. Tagged words, interned atoms, immutable sharing, independent fragments. Managed execution heaps are contiguous; host/startup construction still uses non-moving chunks.
- Collector: `lib/gc.c`. Cheney copy; address classification includes adopted fragments and excludes stable external fragments. Source headers can be restored on failed trials; roots commit only after all fallible work. Small rollback scratch uses the C stack; the heap descriptor is reused only on successful commit.
- Roots/reservations: `include/nvexec.h`, `lib/exec.c`. Active registers only, retained stack capacity charged, small root-view scratch on the C stack, NvCollect request/retry and guard-aware failure. Standalone servicing in `lib/vm.c` preserves the reduction budget.
- Processes: `include/nvproc.h`, `lib/process.c`. Fragment mailboxes, non-consuming recvneed before take, geometric slot capacity and lowest-free hint. Retired slots can outnumber the configured live-process limit; FIFO links use indices.
- Scheduler: `include/nvsched.h`, `lib/sched.c`. Inline demand/idle collection, explicit storage snapshots and opt-in profiling, plus (M08-T04c, D074) off-process collection: a heap owner state and `Lock`, `rfork(RFPROC|RFMEM|RFNOWAIT)` collector procs restricted to a bare `NvExec*`/semaphore argument list, a locked completion fold, never-spin/never-false-idle scheduler waiting on a malloc'd completion semaphore bounded by the nearest deadline, a capped idle-sweep launch burst, and a bounded teardown drain. Default dispatch still has neither snapshot scans nor profiling clock reads on the no-collector path.
- CLI: `-H` word budget, `-G` stress, `-s` statistics. `nervous_gcstress=1` sets CLI stress default. `NvLimits.gcoffload` exists (M08-T04c); the CLI itself still hardcodes 0 (never off-process), matching D074's required safe default -- no CLI flag exposes it yet, deferred to T04d alongside the real threshold value.
- Tests: `tests/memory/gctest.c` (seven groups), `autotest.c` (six groups), and `offloadtest.c` (seven off-process lifecycle groups, M08-T04c), included by `tests/run.rc`. Build-only benchmark target: `mk benchmarks` produces `bench/perftest`.

## What the accepted measurements do and do not establish

- T04p whole-program ring cost is 672-731 ns/message, 5.1-8.5% below first-inline measurements. Waiters-10000 falls from 1136 to 725 ns/message and 89.39 to 17.98 MB host high-water. Counts of bytecode work, messages, dispatches and collections are unchanged. These are single-run comparisons, not confidence intervals.
- The phase control gives 674/672 ns/message at 1000/10000 waiters. At 10000, the host break grows during startup and is flat through traffic; requested waiter storage is 15.04 MB, mostly heap/stack capacity and exec structs. This is not an allocator-leak proof or an exact reconstruction of the former 89 MB allocation history.
- Pre-sizing only two busy heaps from 64 to 512 words cuts collections 88.7% but traffic cost only 7.4%. Do not raise every idle heap's minimum: 10000 such increases would add 35.84 MB. Keep minimum 64 and current policy for now; adaptive hot-process sizing is optional later research, not an accepted design change.
- Opt-in profiling raises traffic elapsed time roughly 75-86% in this control. Prefer repeated unprofiled comparisons; the timed execution bucket includes host calls and does not isolate preflight alone.
- The table/search and GC allocator changes were measured together. Do not assign their independent contributions more precisely than the evidence allows.
- Stage 2's roughly 300 ns/message came from heaps that never collected. It is a historical throughput reference, not a safe allocation policy or a required performance threshold for milestone 08.

## Remaining cautions

- No offload default, adaptive sizing or heap shrinking has been implemented. D069 defers shrinking; `docs/questions.md` records the open policy measurements.
- Collector cost includes classification/freeing of adopted fragments and scratch/trial work, not just copying live words. Transient old/new/scratch space is not the retained-data maxheap budget.
- Host malloc failure paths have source review but no deterministic injection coverage. Root descriptors, C pointers and custom host callback contracts are trusted; verifier guarantees apply to bytecode, not arbitrary host metadata.
- Global atom synchronization and multi-scheduler ownership remain milestone-10 obligations; do not quietly expand T04q into that work.
- post-R2-F01 is closed with D071 evidence. R2-F16 remains deferred to milestone 10. `REVIEW-impressions.md` is not declared wholly resolved; R3 remains a mandatory future gate.

## Resumption checklist

1. Obtain user go-ahead for the chosen next task (T04r or T04d; see "Recommended next sequence" above). This documentation update does not supply it.
2. Confirm actual source-control state with the user; preserve the accepted checkpoint before new edits. A commit message is not evidence of a commit. Nothing in this session committed or pushed anything.
3. Read README, this status, milestone 08, D061-D074 (D074 especially, including its five coordinator-review amendments) and the task's adjacent source/tests. Read COORDINATION before assigning workers.
4. Assign exact exclusive canonical paths; keep shared headers/build/docs coordinator-owned unless explicitly transferred. T04e and T04c are both done and their write sets released; T04r and T04d remain unassigned and need fresh task rows plus exact write sets before any edit begins.
5. Build with mk after edits. User runs behavioral tests/benchmarks; never execute scripts, cleaning, installation or source-control commands through the compilation-only mk tool.
6. If working with a sub-agent again: raise `maxrounds`/`autocontinue` with two SEPARATE ctl writes, not combined with a `model` write in the same call -- combining them was observed this session to silently reset both back to their defaults (20/0), costing a wasted round-capped exchange before it was caught. Verify by reading `ctl` back before sending the task prompt.
