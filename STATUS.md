# Nervous Project Status

Operational source of truth. Settled design is in `docs/decisions.md`; benchmark evidence is in `bench/README.md`; regression coverage is in `tests/memory/README.md`. Older milestone history lives in `STATUS-archive.md` and the review archives.

## Checkpoint and authorization

```text
milestone: 08 - Memory, active but not complete
checkpoint: PR2-T01, M08-T04a, M08-T04b and M08-T04p accepted
implementation: future feature work remains PAUSED; narrow CLI/environment defect repair active
coordinator: nervous-memory-coordinator (role label, not a discovered session identity)
active source assignment: M08-T04e below, sole implementer; no sub-agents
next tasks: planned and unassigned; require user go-ahead and exclusive assignment before edits
source control: user requested a combined commit message; message supplied.
  No commit/push/staging performed or git status independently inspected by the coordinator.
  Do not assume the user has committed/pushed merely because this is an accepted save point.
```

Evidence correction (M08-T04e): the user confirmed the requested commands passed, but source inspection now shows the test runners used `rfork E` (RFCENVG), discarding inherited `nervous_gcstress=1`. That command was NOT proof of full CLI stress coverage. Normal-suite and explicitly configured C stress evidence remain valid; full CLI-stress coverage needs a repaired harness and rerun. The user also reproduced bare ring invocation printing usage while an explicit `nervous_gcstress=0` succeeds, exposing empty-environment handling in the CLI. Future feature work remains paused; only this bounded defect repair is active.

Milestone 08 remains open because off-process collection, its ownership/wakeup/teardown tests, both-mode stress acceptance, and measured policy choices remain outstanding. No formal review gate is active. R3 has not opened and is not complete.

## Accepted evidence

- Build: `mk -a tests benchmarks` rebuilt every command/library/test/benchmark object and linked all targets without diagnostics after final T04p source edits. An earlier unreachable-return warning in the new benchmark driver was fixed before that rebuild. Build tools executed compilation/linking only.
- Behavior: user confirmed both requested T04p suite commands passed. Correction: `rfork E` discarded inherited CLI stress, so these establish normal-suite and explicit C stress coverage, NOT full CLI-stress coverage. T04e repairs inheritance and adds a collection-count canary. Fresh normal/CLI-stress runs on the repaired tree are pending.
- Measurement: user supplied the original six `rc bench/run.rc` shapes and all seven `rc bench/perf.rc` cases through `/dev/snarf`. Normalized T04p results and limitations are in `bench/README.md`; the earlier inline baseline is also preserved in `bench/inline-gc-first.txt`.
- Latest build: `mk tests` compiled/linked the T04e CLI fix and all test targets without diagnostics. New rc regressions require user execution; mk does not validate or run scripts. No runtime tests, benchmarks, commit or push executed by the coordinator.

## Milestone ledger

00-07 and R1-R2 are complete; their historical rows are archived.

| Milestone | State | Dependencies | Remaining scope |
|---|---|---|---|
| 08 Memory | active; implementation paused | R2, 05, 06 | Off-process collector and acceptance/policy work |
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
| M08-T04e CLI/environment defect repair | built; user tests pending | T04p | Empty-as-unset, named diagnostic, environment-preserving runners and direct CLI regressions |
| M08-T04q duplicate interpreter work | planned, optional | T04p | Time-boxed preflight/execution reuse; no safety or sizing-policy change |
| M08-T04r large-live-set latency baseline | planned | T04p; use T04q result if pursued | Required measurement fixture for choosing offload policy; inline baseline first |
| M08-T04c off-process collection | planned | T04p, T04r | D068 ownership, collector procs, wakeup, failure and teardown correctness |
| M08-T04d policy and milestone acceptance | planned | T04c, T04r | Both-mode stress, representative latency/footprint measurements, recorded constants |

T04q is not a new milestone dependency gate. It may be skipped if the user prefers forward feature work. Do not turn optional throughput optimization into an indefinite prerequisite for off-process correctness.

## M08-T04e CLI/environment defect repair

Latest follow-up: user progressed to the CLI print-order golden, reporting `a,a,root,b,b,ok` rather than `root,a,a,b,b,ok`. This matches GC-stress scheduling: the reservation for the second spawn's argument tuple yields the root behind child a. The fixture has no message synchronization imposing root-before-child order. Retain the original normal golden and add a stress-only golden selected by the runner's expected CLI mode; preserve byte-for-byte checking. Sole coordinator write set additionally includes `tests/process/cli/print-order.stress.out`. Full normal/stress suite completion remains pending user verification; no scheduler/runtime changes.

Latest user verification: bare `./nervous` invocation now works. Both suite runs passed the inherited-mode and absent-variable canaries, then stopped at a false failure in the new rc ring helper. Preserved ring stdout was `20000` and stderr empty. Fixed the helper's final false `if` leaving a nonzero status by ending successful validation with an `echo`, as the value helper already does. `mk all` linked without diagnostics; no C/runtime changes in this follow-up. Both suite reruns remain pending; neither earlier run reached the existing regression groups.

State: implemented; `mk tests` passed without diagnostics. User runtime checks pending; write set retained until acceptance. Coordinator is sole implementer. Exclusive canonical root `/usr/dave/work/nervous/`; paths: `cmd/nervous/main.c`, `tests/run.rc`, `tests/frontend/run.rc`, `tests/bytecode/run.rc`, `tests/vm/run.rc`, `tests/pattern/run.rc`, `tests/process/run.rc`, `tests/memory/run.rc`, `tests/cli/`, `STATUS.md`, `README.md`, `docs/decisions.md`, `docs/review-findings.md`, `milestones/08-memory.md`, `tests/memory/README.md`, `bench/README.md`. No VM/GC/runtime algorithm or benchmark source change assigned.

Implemented empty-as-unset parsing with an environment-specific invalid-value diagnostic; changed all seven existing test runners from `rfork E` to `rfork e`. Added `tests/cli/run.rc` and `value.nv`: absent/zero-length/quoted-empty/0/1/-G/invalid cases, bare ring under absent/empty settings, and a one-tuple allocation canary checking zero versus one collection. The parent passes its expected inherited mode separately so lost inheritance fails the canary. Full runner includes it. Benchmark scripts and runtime algorithms are unchanged.

User checks from the repository root:

```rc
./nervous -r examples/ring.nv main
rc tests/run.rc
nervous_gcstress=1 rc tests/run.rc
```

Expected bare ring output: `20000`. Both suites must include `all CLI environment tests passed` and end with `all nervous regression suites passed`. `rc tests/cli/run.rc` isolates the new regressions. Prior passing commands do not close this defect; new scripts have not been run by the coordinator.

## Recommended next sequence (not started)

### 1. Optional bounded interpreter pass: T04q

Source inspection found arithmetic results and call-target resolution computed during reservation preflight and then computed again for execution. A narrow pass can remove this duplication before adding another execution owner.

- Read `lib/exec.c`, `include/nvexec.h`, D063/D067/D071-D073 and the memory test coverage first.
- Reuse scalar arithmetic results or function indices within the SAME instruction attempt where safe. Discard/recompute the plan after NvCollect; never retain an unrooted heap pointer across movement. Avoid persistent module caches or public ABI changes unless separately justified.
- Preserve exact operand errors, overflow/divide behavior, guard fault transfer, call/tailcall accounting, unchanged pc/reductions on collection requests, and one-time side effects.
- Candidate implementation surface: `lib/exec.c` plus focused memory tests. These are proposed surfaces, not an exclusive assignment yet; the coordinator must assign exact paths before work starts.
- Acceptance: forced build, normal/stress regressions, quanta 1/1000, and repeated unprofiled original/control benchmarks. Keep measured wins or a justified simplification; do not weaken checks to chase the stage-2 number.
- Stopping rule: one bounded attempt, then proceed or defer. Further profiler, heap-layout or scheduling redesign is outside this task.

### 2. Establish the actual offload use case: T04r

The existing benchmarks have tiny collected live sets. Off-process GC is intended to protect unrelated processes from a LARGE collection pause, not make a tiny collection faster.

- Build a controlled large-live-set owner alongside small-message/heartbeat peers; vary retained live words independently of waiter count and allocation rate.
- Record inline peer latency distributions/tails (not only total ns/message), throughput, collection frequency and memory. Include a control for measurement overhead and repeat short runs.
- Keep the fixture reusable for forced inline, forced off-process and threshold candidates later. Do not fabricate an off-process baseline before it exists.
- Read `bench/README.md`, `bench/perftest.c`, `lib/gc.c`, D068-D069 and the milestone's tests. Bench/test files and any necessary instrumentation need an exact future write assignment.

### 3. Implement off-process ownership and lifecycle: T04c

Follow the settled D068-D070 design; this step adds real shared-memory concurrency and deserves its own implementation/review checkpoint.

- Orthogonal heap owner states idle/running/collecting and a lock; execution only with idle ownership acquired for the mutator.
- Collector procs using rfork(RFPROC|RFMEM|RFNOWAIT), completion signalling, and `_exits`; no mailbox metadata read/write by collectors.
- Skip/requeue collecting runnable slots without duplicate execution; if all work is collecting, wait on a semaphore bounded by the nearest deadline rather than spin.
- Sends and deadline wakeups remain ordinary scheduler events; process-table growth must not invalidate collector-held state. Collectors must not retain pointers into the relocatable slot table.
- Handle launch/allocation failures, completion publication and error delivery, outstanding collections during teardown, and snapshot access to collecting heaps. Preserve D072's retry-time guard-aware fault path.
- Use deterministic lifecycle tests where possible, plus forced-offload stress. Validate sends/wakes during collection, queue progress, no lost completion, no use-after-free, and clean teardown. The exact tests/interfaces are to be designed and assigned before implementation, not assumed settled by this list.

### 4. Measure policy and close 08: T04d

- Run full stress with inline-only and forced off-process collection. Rerun original and large-live-set benchmarks; choose gcoffload and any justified sizing/idle constants from results.
- Current accepted defaults remain unchanged unless a separate measured decision replaces them. Record the chosen constants and evidence in decisions/questions/bench records.
- Confirm bounded long-running memory and explain heap/message ownership without cross-heap dependencies. Reconcile all required tests in `milestones/08-memory.md` before marking it complete.
- Then milestone 09 and the mandatory R3 review, in order. No new language features or multicore schedulers are part of this handoff.

## Current implementation map

- Representation/accounting: `include/nvvm.h`, `lib/value.c`. Tagged words, interned atoms, immutable sharing, independent fragments. Managed execution heaps are contiguous; host/startup construction still uses non-moving chunks.
- Collector: `lib/gc.c`. Cheney copy; address classification includes adopted fragments and excludes stable external fragments. Source headers can be restored on failed trials; roots commit only after all fallible work. Small rollback scratch uses the C stack; the heap descriptor is reused only on successful commit.
- Roots/reservations: `include/nvexec.h`, `lib/exec.c`. Active registers only, retained stack capacity charged, small root-view scratch on the C stack, NvCollect request/retry and guard-aware failure. Standalone servicing in `lib/vm.c` preserves the reduction budget.
- Processes: `include/nvproc.h`, `lib/process.c`. Fragment mailboxes, non-consuming recvneed before take, geometric slot capacity and lowest-free hint. Retired slots can outnumber the configured live-process limit; FIFO links use indices.
- Scheduler: `include/nvsched.h`, `lib/sched.c`. Inline demand/idle collection, explicit storage snapshots and opt-in profiling. No off-process owner state/lock protocol exists yet. Default dispatch has neither snapshot scans nor profiling clock reads.
- CLI: `-H` word budget, `-G` stress, `-s` statistics. `nervous_gcstress=1` sets CLI stress default. No gcoffload option exists yet.
- Tests: `tests/memory/gctest.c` (seven groups) and `autotest.c` (six groups), included by `tests/run.rc`. Build-only benchmark target: `mk benchmarks` produces `bench/perftest`.

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

1. Obtain user go-ahead for the chosen next task. This documentation update does not supply it.
2. Confirm actual source-control state with the user; preserve the accepted checkpoint before new edits. A commit message is not evidence of a commit.
3. Read README, this status, milestone 08, D063-D073 and the task's adjacent source/tests. Read COORDINATION before assigning workers.
4. Assign exact exclusive canonical paths; keep shared headers/build/docs coordinator-owned unless explicitly transferred. T04e retains its bounded write set pending user verification; feature tasks remain unassigned.
5. Build with mk after edits. User runs behavioral tests/benchmarks; never execute scripts, cleaning, installation or source-control commands through the compilation-only mk tool.
