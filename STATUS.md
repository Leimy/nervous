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
state: active milestone 08; T04p accepted on user-confirmed passing tests and recorded benchmarks
prerequisites: R2, 05, 06 and PR2-T01 accepted
latest build: mk -a tests benchmarks rebuilt every command/library/test/benchmark object and
  linked all targets without diagnostics after final T04p edits. Earlier benchmark build's
  unreachable-return warning was fixed before this clean-diagnostic forced rebuild.
latest accepted runtime evidence: user explicitly confirms all T04p tests passed after the
  benchmark review, including the requested normal and CLI-stress suites. T04p accepted;
  earlier T04b/T04a/PR2-T01 results remain historical evidence.
pending runtime evidence: none for this save point; future changes need fresh verification.
benchmark: first T04p rc bench/run.rc and seven rc bench/perf.rc cases received via /dev/snarf,
  normalized in bench/README.md. Whole-program waiters-10000: 725 ns/message and 17975288
  high-water bytes, versus T04b 1136 and 89391824. No new sizing/offload default selected.
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
| M08-T04b automatic inline collection | coordinator, sole implementer | done | User reports all requested suites passing; correctness accepted; write set released. First benchmark recorded, not final performance acceptance. |
| M08-T04p inline performance investigation | coordinator, sole implementer | done | Geometric table growth/free hint, small-GC scratch optimization, optional timing/snapshots and phase benchmark built. mk -a tests benchmarks passed; first original/phase benchmarks received and recorded. User explicitly confirms all tests passed; accepted and write set released. No sizing/default-trigger change or off-process work. |

### M08-T04p exclusive assignment

Canonical root `/usr/dave/work/nervous/`; exclusive paths relative to it: `lib/process.c`, `lib/exec.c`, `lib/gc.c`, `lib/sched.c`, `include/nvproc.h`, `include/nvsched.h`, `tests/memory/autotest.c`, `tests/memory/gctest.c`, `tests/memory/README.md`, `bench/perftest.c`, `bench/perf.rc`, `bench/README.md`, `mkfile`, `.gitignore`, `STATUS.md`, `docs/decisions.md`. No sub-agents or overlapping writes. Preserve lowest-slot reuse, PID retirement, FIFO order, exact budgets, guard/retry semantics and failure rollback. Build all tests and benchmark binaries with mk; user runs tests/stress/benchmarks. Timing must be opt-in and snapshots explicit, never a per-dispatch process-table scan. Prior T04b passes are baseline evidence only; no new speed/memory improvement is claimed before measurements.

### M08-T04p handoff and pending evidence

Source findings: append-only creation previously did N*(N-1)/2 live-slot examinations and one process-table realloc per new slot. A small collection did four allocator pairs: frame-root views, destination descriptor, to-space and rollback source pointers. These counts follow from source; their contribution to the 89 MB host high-water and slowdown has not been measured separately.

Implemented:

- `NvRuntime.nalloc` separates allocation capacity from initialized nslot. Growth is geometric (initially up to 16, or the smaller initial live limit), byte-size/PID-domain checked, and spare entries are initialized only when becoming actual slots. Retired slots may require capacity beyond maxprocess, a live-count limit.
- `freehint` skips the known non-reusable prefix during spawn and is lowered on exit. Lowest-slot reuse, retirement and FIFO order are unchanged. Counters expose growths, moved grows/old requested bytes, and slot probes. Append-only spawn no longer rescans all existing live slots; arbitrary reuse can still scan gaps.
- `nvexeccollect` uses eight frame views on the C stack and allocates only for deeper frame chains. `nvheapcollect` uses 64 source-pointer slots on the C stack for small spaces, promotes to dynamic scratch after restored growth trials, and reuses the current chunk descriptor without changing it before commit. The common small collection now allocates only to-space. No per-process scratch cache, new minimum heap, or GC trigger change.
- `NvScheduler.profile` optionally uses real monotonic elapsed timing for execution, collection and spawn. Default is off and ordinary CLI runs do not read profiling clocks. Spawn time overlaps execution when called from bytecode. Collection work counters distinguish demand/idle attempts and input/output words; startup compaction is excluded from those counters.
- `nvschedmemory` provides explicit quiescent snapshots of requested table, exec, heap-capacity, stack-capacity, adopted, mailbox and reporting storage. Used byte counts are subsets. It is never invoked automatically in dispatch; snapshots exclude allocator/module/atom storage and transient GC scratch.
- New `bench/perftest.c` and `bench/perf.rc` separate waiter construction, blocking, busy setup, traffic and draining. The script compares waiter populations, explicit busy-heap pre-sizing requests and profiling off/on in fresh host processes. This host-driven zero-argument waiter control is NOT byte-for-byte `bench/waiters.nv`; compare original `bench/run.rc` for whole-program before/after numbers.
- `mk benchmarks` compiles/links perftest only; ordinary clean metadata and .gitignore include its outputs. No script, clean or install target was run by tools.
- Added regression coverage: geometric growth counts/zero append-prefix probes; lowest-free reuse; stale PIDs; retirement beyond tiny live limits; queue/mailbox preservation; snapshot components; default-off timing and invariant counts with timing on; small-to-large scratch promotion; descriptor reuse; dynamic root views above eight frames on failure/success/resumption.
- Recorded D073 and diagnostic instructions in bench/README.md and tests/memory/README.md. The full suite's memory runner discovers the added C test groups through the existing binaries.

Build: `mk tests` passed, then `mk tests benchmarks` found one unreachable-return warning in the new benchmark driver. Removed the unreachable return after sysfatal. Final `mk -a tests benchmarks` rebuilt all objects and linked all targets without diagnostics. No behavior/performance claims derive from that build.

User checks, from `/usr/dave/work/nervous`:

```rc
rc tests/run.rc
nervous_gcstress=1 rc tests/run.rc
rc bench/run.rc
rc bench/perf.rc
```

The full suite includes memory; `rc tests/memory/run.rc` isolates it. The user has now supplied both benchmark scripts' output via /dev/snarf, but no T04p regression/stress report yet. Normalized measurements and limitations are in bench/README.md. This recording turn changed documentation only; no source, compilation or runtime commands were executed.

First T04p measurements: original rings improve 5.1-8.5%; waiters-10000 improves 36.2% (1136 to 725 ns/message), and its high-water drops 79.9% (89.39 to 17.98 MB). Original-shape reduction/dispatch/message/GC counts are identical to T04b. Sieve high-water increases 2.8% in this single sample, so improvements are not universal.

The default phase control's traffic is 674/672 ns/message at 1000/10000 waiters. The 10000-waiter host break grows to 17958744 bytes during spawning and stays flat through traffic/drain/free. Requested waiter storage is 15041792 bytes: table 1441792, execs 3120000, heap capacity/headers 5360000, stacks 5120000. Used heap and stack bytes (80000/720000) are subsets. After draining only table/result remain; after runtime free tracked storage is zero. These distinguish retained capacity and startup from accumulation; they do not reconstruct the old 89 MB allocation history or prove absence of every allocator leak.

Pre-sizing only the two busy heaps to 128/512 words changes traffic 672 -> 645 -> 622 ns/message and collections 22618 -> 10666 -> 2567 at identical 5000012 reductions. An 88.7% collection reduction gives only 7.4% faster traffic; 512-word sizing is not a new global default (10000 idle heaps would cost 35.84 MB extra). Profiled traffic rises to 1248/1091 ns/message at 64/512 words, roughly 86%/75% overhead, so timed region ratios must not be read as unperturbed costs. Next optimization candidate is duplicated arithmetic/call-target work between preflight and execution, preserving reservation/guard/retry safety. No such implementation is assigned yet.

State: done. After the benchmark review the user explicitly confirmed all tests passed and requested a save-point commit message. T04p is accepted and its write set released. Earlier pending-test statements in the measurement narrative describe the evidence available then, superseded by this confirmation. No commits or pushes by the coordinator. Off-process collection stays unassigned.

### M08-T04b assignment / exclusive ownership

Depends on accepted T04a. Objective: reservation/retry, automatic inline servicing, startup/frame/adoption accounting, stress/limits interfaces, regression coverage and commit-message handoff. Off-process collection is not assigned.

Exclusive canonical paths (all listed relative paths are under `/usr/dave/work/nervous/`): `lib/exec.c`, `lib/value.c`, `lib/gc.c`, `lib/process.c`, `lib/sched.c`, `lib/vm.c`, `include/nvvm.h`, `include/nvexec.h`, `include/nvproc.h`, `include/nvsched.h`, `cmd/nervous/main.c`, `tests/memory/`, `tests/process/exectest.c`, `tests/process/schedtest.c`, `tests/process/iotest.c`, `tests/process/r2test.c`, `tests/process/ptest.c`, `tests/run.rc`, `mkfile`, `.gitignore`, `README.md`, `STATUS.md`, `docs/decisions.md`, `milestones/08-memory.md`. Shared integration surfaces remain coordinator-owned under `COORDINATION.md`. No parallel edits or sub-agent assignments.

Acceptance satisfied: forced compilation and user-reported passing memory/full suites and CLI inline-stress suite. The user supplied the first non-stress benchmark through /dev/snarf. T04b is done for correctness; its write set is released. Performance findings are tracked separately under planned T04p, and milestone 08 remains active.

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

Accepted user-run checks on the rebuilt T04b working tree:

```rc
cd /usr/dave/work/nervous
rc tests/memory/run.rc
rc tests/run.rc
nervous_gcstress=1 rc tests/run.rc
```

Expected memory endings: `all memory collector tests passed` and `all automatic inline collector tests passed`. The memory suite's C integration test always exercises both stress settings. The environment run stresses all command-driven execution paths while preserving C fixtures' explicit scheduler configurations.

User also supplied the normal (non-stress) run:

```rc
rc bench/run.rc
```

Results read from /dev/snarf are recorded in `bench/README.md` and `bench/inline-gc-first.txt`. All six benchmarks report zero GC failures and process faults. T04b correctness is accepted on the user's report that all requested tests pass; its write set is released. The prior forced build remains compilation evidence only. That earlier recording turn changed documentation/evidence only. T04p has since changed source and been rebuilt; its first timings have arrived, while normal/stress regression confirmation remains pending.

Performance is not finalized: rings are 2.19-2.40x stage-2 cost and collect once per roughly 6.3-6.5 messages; waiters-10000 reports 89.39 MB host high-water and a 72% per-message penalty versus waiters-1000. These are separate metrics from small collected live-heap samples. Investigate fixed collection costs and allocation/retained-capacity breakdown before choosing offload policy. The combined commit message already supplied is suitable for a correctness checkpoint, not a claim of completed milestone-08 performance work. No commit or push has been performed.

## Source-control context

The prior coordinator recorded uncommitted D063/D067-D070 design changes and PR2-T01 verifier/fixture/doc changes. T04a and T04b now overlap those documentation paths. Review the actual diff and stage deliberately rather than treating the tree as one docs-only change. This coordinator has not inspected git status, and cannot infer staged/committed state from successful builds. The user owns commit/push.

## Next implementation after inline acceptance

T04p's first measurements are recorded and the user explicitly confirms all tests passed. The save point is accepted and its write set released; the user owns commit/push. The next bounded optimization candidate is eliminating duplicate arithmetic/call-target computations across preflight and execution without changing reservation boundaries. Keep the default heap minimum unchanged: the busy-only sizing experiment recovers about 7%, while raising every idle process's capacity would materially increase footprint. Repeat short measurements when evaluating small gains. Profiling does not isolate preflight alone and materially perturbs time. Tiny-live-set measurements still do not select an offload threshold.

After that, the off-process work remains:

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

User-reported stage-2 performance: about 300-350 ns/ring hop (~12.5 ns/reduction), versus 1.06-1.31 us after atom interning and 1.5-2.0 us at baseline. Its `-s` heap figure measured uncollected garbage, not live footprint. Waiters-10000 measured 513 ns/message versus 310 at 1000; the first automatic-GC run is now recorded in bench/README.md (1136 versus 659), with its cause still unresolved. The large stage-2 ring was not run because accumulated garbage would not fit.

The first stage-2 test run found nvtermequal's identity fast path preceding the depth check. Fixed, user rerun passed, committed with stage 2. T04b now has its own later user-reported passing results; the earlier stage-2/PR2-T01/T04a passes were not reused as its evidence.

Tools: mk is compilation-only. Runtime scripts, benchmarks, cleaning, installation and source-control commands must be run by the user, not smuggled through mk.
