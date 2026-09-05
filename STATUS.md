# Nervous Project Status

Operational source of truth. Historical work is in `STATUS-archive.md`; normative design is in `docs/decisions.md`; review concerns persist in the finding ledgers.

## Coordinator

```text
name/session: nervous-memory-coordinator (current user-authorized session)
scope: sole implementer; no sub-agent write assignments
identity: role label, not a discovered claude9fs session name
```

## Current milestone and evidence

```text
milestone: 08 - Memory
state: active; automatic INLINE collection implemented, awaiting user runtime verification
prerequisites: R2, 05, 06 and PR2-T01 accepted
latest build: mk -a tests rebuilt every command/library/test object and linked all targets
  without diagnostics after the final M08-T04b source/test edits; incremental mk tests also passed
latest accepted runtime evidence: user reports both rc tests/memory/run.rc and rc tests/run.rc
  passing on M08-T04a, the explicit collector core. Earlier PR2-T01 suites also passed.
pending runtime evidence: all tests on M08-T04b, including new autotest and CLI inline-stress runs
benchmark: historical stage-2 data only; automatic-GC benchmark has not been run
source control: no commit/push/staging or independent git status inspection by this coordinator
```

The ordinary runtime now collects automatically. This is not milestone-08 completion: off-process collection, its ownership/wakeup/teardown tests, both-mode stress acceptance and measured policy constants remain outstanding. No formal review gate is active; R3 is neither open nor complete.

## Milestone ledger

00-07 and R1-R2 are complete; historical rows are archived.

| Milestone | State | Dependencies | Summary |
|---|---|---|---|
| 08 Memory | active | R2, 05, 06 | Representation, fragments, roots, collector, accounting and policy |
| 09 Binaries | not-started | 04, 08 | Binary construction and matching |
| R3 Memory review | not-started | 08, 09 | Root/fragment/representation/binary ownership gate |
| 10 Multicore | not-started | R3, 05, 06, 08, 09 | Parallel schedulers and work movement |
| R4 Multicore review | not-started | 10 | Mandatory part of multicore acceptance |

## Task ledger

| Task | Owner | State | Evidence / next step |
|---|---|---|---|
| M08-T01 representation design | prior coordinator | done | D061-D066 recorded |
| M08-T02 interned atoms | prior coordinator | done | User-accepted and committed before stage 2 |
| M08-T03 tagged terms, frame stack, fragment mailboxes | prior coordinator | done | Tests/bench user-confirmed; committed as "milestone 08 stage 2" |
| PR2-T01 guard verifier boundary | prior/resumed coordinator | done | D071; user reports bytecode/full suites passing; post-R2-F01 closed |
| M08-T04 complete collector/accounting/policy stage | coordinator | active | Inline first, off-process after inline validation |
| M08-T04a explicit collector and frame-root adapter | coordinator | done | User reports memory/full suites passing; write set released |
| M08-T04b automatic inline collection | coordinator, sole implementer | integrating | Forced build passed; new runtime and stress suites pending |

### M08-T04b assignment / exclusive ownership

Depends on accepted T04a. Objective: reservation/retry, automatic inline servicing, startup/frame/adoption accounting, stress/limits interfaces, regression coverage and commit-message handoff. Off-process collection is not assigned.

Exclusive canonical paths (all listed relative paths are under `/usr/dave/work/nervous/`): `lib/exec.c`, `lib/value.c`, `lib/gc.c`, `lib/process.c`, `lib/sched.c`, `lib/vm.c`, `include/nvvm.h`, `include/nvexec.h`, `include/nvproc.h`, `include/nvsched.h`, `cmd/nervous/main.c`, `tests/memory/`, `tests/process/exectest.c`, `tests/process/schedtest.c`, `tests/process/iotest.c`, `tests/process/r2test.c`, `tests/process/ptest.c`, `tests/run.rc`, `mkfile`, `.gitignore`, `README.md`, `STATUS.md`, `docs/decisions.md`, `milestones/08-memory.md`. Shared integration surfaces remain coordinator-owned under `COORDINATION.md`. No parallel edits or sub-agent assignments.

Acceptance: forced compilation, user-run memory/full suites and CLI inline-stress suite. Compilation is not behavioral acceptance. Write set remains reserved pending those results.

## Review findings

post-R2-F01 is closed: structural guard verification, 5 valid and 21 invalid fixtures, and compiler-produced guard coverage at quanta 1/1000 passed the user-run suites. D071 and `docs/review-findings.md` retain evidence. R2-F16 (advisory NvLimits.maxduration) stays deferred to milestone 10. R3-F01 is closed. `REVIEW-impressions.md` is not declared resolved by the collector work.

## What is built

### Accepted explicit core (T04a)

`lib/gc.c` performs Cheney copying over root ranges with no exec, scheduler or mailbox dependency. Owned objects are in current/full chunks or adopted fragments; external boxed pointers are left untouched without dereferencing them. The caller guarantees stable self-contained external storage. Only tuples contain traceable fields; Ref and integer bodies are opaque.

Forwarding uses a one-word offset header, including for empty tuples. Source addresses recorded alongside to-space permit restoring headers on a failed copy or growth trial. Roots are committed only after all fallible work. Successful collection frees old chunks/all adopted fragments, produces one contiguous chunk and sets heap.words to live object words.

`nvexeccollect` validates the active frame chain, supplies register-only ranges, skips frame metadata/inactive slots, and charges retained nstack capacity. It changes no pc, guard target or reductions. Core tests include deep/wide/shared graphs, adopted versus queued fragments, cyclic host metadata, partial-copy failure rollback, frame roots and guarded execution.

### New automatic inline integration (T04b, D072)

- `prepare` in `lib/exec.c` determines allocation before instruction side effects, tracing or reduction charging. Tuple, boxed loadk/arithmetic, mkref, call/tailcall stack growth and recvtake all participate. Space demand is distinct from budget-only charge; adopted fragments contribute to pressure even with maxheap=0.
- A failed reservation returns NvCollect with the unchanged pending instruction and charge. Durable exec state remains NvYield. Repeated execution calls cannot run a pending request.
- `nvexecgc` services the request while the owner is stopped and records success/limit/allocator failure for exactly one retry. It does not execute or fault bytecode. The interpreter charges the retried failing instruction once and uses its normal fault path, preserving D060 guard-fault-as-clause-failure semantics.
- Scheduler returns the owner to the D059 FIFO tail exactly once before collecting inline. A queued peer runs before its retry. Standalone `nvexecruninline` instead services requests within the same quantum, preserving exact reduction limits and trace lines.
- Initial retained stack capacity is charged before argument copying. The argument is rooted and compacted before execution. Managed heaps refuse chunk growth; host construction/startup copying retain non-moving chunks. Budget/byte-size arithmetic is checked before allocation.
- `nvprocrecvneed` and the host recvneed callback validate/size the queued candidate without taking it. Only successfully reserved recvtake unlinks/adopts it. Core collectors still never inspect mailbox metadata.
- Idle scheduler steps may collect waiting processes with used/adopted words above half their space and above their last live watermark, without executing bytecode. An unchanged live set is not repeatedly collected; failed optional collection leaves ownership intact.
- `NvLimits.gcstress` is explicitly initialized in existing tests and validated as 0/1. The scheduler copies it into each exec. New callers must initialize it.
- CLI `-H words` enforces the process budget (0 = unlimited); `-G` forces one collection per allocating-instruction reservation. Both apply to -r/-x/-t. `nervous_gcstress=1` supplies the CLI stress default for regression scripts; it does not override C tests' explicit runtime limits.
- Statistics now label host allocator high-water bytes separately from successful per-process live-heap samples. Collection counters exclude startup compaction; sampled heap words exclude frame capacity and are labeled per-process, not aggregate footprint.

### Tests and build changes

New `tests/memory/autotest.c` has five groups: atomic requests and call/tailcall capacity/FIFO peer progress; guard exhaustion; receive reservation and one-time adoption; idle reclamation; and a source-compiled 1000-message tuple/Ref/boxed-arithmetic loop. The loop runs at quanta 1/1000 with stress off/on, asserting bounded managed space, actual collections, identical reductions and no duplicate messages or Refs.

The memory runner executes both gctest and autotest; the full runner already includes memory. `tests/process/exectest.c` uses the inline convenience wrapper because its assertions measure bytecode quanta, not collector dispatches. Other process tests initialize the added limit field. The mkfile builds/links the new test and the ordinary clean rule names its outputs; `.gitignore` ignores both memory binaries. No clean target was run.

`tests/memory/README.md` documents coverage and its limits. README, milestone 08, headers and D072 describe the new API and scope. D063/D067-D070's off-process design is recorded but not implemented by this slice.

## Verification handoff

User runs against the rebuilt working tree:

```rc
cd /usr/dave/work/nervous
rc tests/memory/run.rc
rc tests/run.rc
nervous_gcstress=1 rc tests/run.rc
```

Expected memory endings: `all memory collector tests passed` and `all automatic inline collector tests passed`. The memory suite's C integration test always exercises both stress settings. The environment run stresses all command-driven execution paths while preserving C fixtures' explicit scheduler configurations.

Then measure normally (not under stress):

```rc
rc bench/run.rc
```

Record numbers in `bench/README.md` only after the user supplies them. No new performance or footprint claim has been made. The latest `mk -a tests` rebuilt and linked every target without diagnostics; that is build-only evidence, not a runtime pass or clean-from-empty claim.

On success, accept T04b, release its write set and review/stage the combined changes. On failure, fix the reported regression before off-process work or acceptance. User has requested a combined commit message for the safety/collector work; the coordinator supplies it in the response, without asserting that pending runtime checks passed. No commit or push has been performed.

## Source-control context

The prior coordinator recorded uncommitted D063/D067-D070 design changes and PR2-T01 verifier/fixture/doc changes. T04a and T04b now overlap those documentation paths. Review the actual diff and stage deliberately rather than treating the tree as one docs-only change. This coordinator has not inspected git status, and cannot infer staged/committed state from successful builds. The user owns commit/push.

## Next implementation after inline acceptance

Read D063-D072, `milestones/08-memory.md`, `tests/memory/README.md`, the heap/exec/process/scheduler headers and their source, and `bench/README.md`.

1. D068 heap owner states (idle/running/collecting), lock and dispatch ownership. Current collection is synchronous and relies on exclusive stopped-owner access; there is no owner lock yet.
2. Off-process policy: gcoffload threshold, rfork(RFPROC|RFMEM|RFNOWAIT), `_exits`, completion semaphore, skip/requeue collecting runnable slots, deadline-bounded waits, teardown waiting for every collector. Never let a collector read mailbox metadata.
3. Demand/idle collection tests under off-process ownership, sends/deadlines during collection, teardown, and full stress in both configurations. Preserve retry-time guard fault delivery from D072.
4. Benchmark large-live-set latency and choose gcoffload and sizing/idle thresholds from evidence; update questions and decision notes. Remeasure the stage-2 waiters regression before attributing it conclusively.
5. Finish milestone acceptance criteria, then binaries and R3 in order. No later review/milestone is implicitly accepted by T04b.

## Known limitations / measurements still needed

- Linear chunk/adopted address lookup and scratch source-address storage proportional to space capacity are deliberately simple. Growth trials may recopy. A collection's transient memory includes old/new space and scratch, not merely the maxheap live-data budget.
- The collector conservatively sizes space using the added charge even for adoption/stack growth. This may retain extra slack; it is recorded on D072, not hidden as optimal sizing.
- Startup compaction and preflight checks add overhead; arithmetic/call validation is currently repeated at execution after preflight. Measure before optimizing.
- Host malloc failure paths have source review but no deterministic injection test. Owned-header checks do not make arbitrary C pointers/root descriptors safe. Custom host recvneed/recvtake callbacks must report a stable candidate charge and must not collect internally.
- Off-process collection, gcoffload, lock/owner protocol, off-process stress and policy measurements remain undone.
- The global atom table remains unlocked under one scheduler; milestone 10 owns synchronization. Its configured limit cannot fall below the already interned count. Calls still resolve target names and initialization rescans constants.

## Historical baseline

Stage 2 used tagged 64-bit terms (62-bit small integers, atom indices, PID 30-slot/32-generation bits) and copied ordinary messages once into fragments. Register copies were word copies, frame stacks contiguous, and root results/exit reasons independent fragments. Its ordinary heaps accumulated garbage until exit.

User-reported stage-2 performance: about 300-350 ns/ring hop (~12.5 ns/reduction), versus 1.06-1.31 us after atom interning and 1.5-2.0 us at baseline. Its `-s` heap figure measured uncollected garbage, not live footprint. Waiters-10000 measured 513 ns/message versus 310 at 1000; automatic-GC results are still needed. The large stage-2 ring was not run because accumulated garbage would not fit.

The first stage-2 test run found nvtermequal's identity fast path preceding the depth check. Fixed, user rerun passed, committed with stage 2. Do not confuse that historical result, PR2-T01 acceptance, or T04a acceptance with verification of T04b.

Tools: mk is compilation-only. Runtime scripts, benchmarks, cleaning, installation and source-control commands must be run by the user, not smuggled through mk.
