# Nervous Project Status

Operational source of truth. Settled design is in `docs/decisions.md`; benchmark evidence is in `bench/README.md`; regression coverage is in `tests/memory/README.md`. Older milestone history lives in `STATUS-archive.md` and the review archives.

## Checkpoint and authorization

```text
milestone: 08 - Memory, COMPLETE; 09 - Binaries, COMPLETE (T05 results
  recorded in bench/README.md "First run"); R3 - Memory review, OPEN
checkpoint: M09 closed on the user's `rc bench/latency.rc` output.
  R3-F02 closed: user confirmed `rc tests/run.rc` and
  `nervous_gcstress=1 rc tests/run.rc` pass with the lib/value.c guard.
implementation: none active.  Coordinator is Claude.  Sub-agent policy
  revised (see /usr/dave/local_models.md, "Economic Review"): cloud
  Sonnet for bounded read-only audits, coordinator for edits; local
  models only for independent second-opinion review or doc summaries.
active source assignment: none (coordinator implemented D080 directly;
  write set released on build).
next: M10-T00a (segmented process table, D082) is DONE: all three suite
  invocations pass (user-run).  Uncommitted; suggested message
  "M10-T00a: segmented process table (D082)".  bench/run.rc has not been
  rerun on it yet -- do that before or alongside T00b so any indirection
  cost is attributed to the right change.  Next: cut M10-T00b (machine/
  scheduler split, owner field, counters, -p parsing; spec in
  milestones/10-multicore.md "Tasks").
  T00a lesson: nvprocat is a macro that evaluates its slot argument
  twice; the first build passed `r->nslot++` and the root spawned into
  slot 1 while queued/pid'd as slot 0 (`scheduler: bad_pid`, 0
  dispatches).  Header now warns.  Build with `mk nervous tests
  benchmarks`; `mk tests benchmarks` alone does not relink the CLI
  binary the suite runs first.
  Changed: include/nvproc.h (chunk/nchunk, NvProcchunk=1024, nvprocat
  macro; nalloc/process gone; tablemoves/tablemovebytes retired at 0),
  lib/process.c (growth appends a chunk; nvruntimefree frees chunks),
  lib/sched.c (nvprocat everywhere; tablebytes = nchunk*NvProcchunk*
  sizeof), and mechanical nvprocat conversion in tests/memory/autotest.c
  + offloadtest.c, tests/process/ptest.c + schedtest.c + r2test.c,
  bench/perftest.c + largelive.c + latency.c.  The ONLY assertion values
  that changed are in autotest.c:tablegrowth (nalloc 256/1/2 -> nchunk 1;
  tablegrows 5 -> 1; each with a "was:" comment) and receivetake's
  tablebytes formula.  Acceptance: the three suite invocations pass and
  bench/run.rc within noise.  Then T00b (spec in milestones/10-multicore.md
  "Tasks").  Suggested commit: "M10-T00a: segmented process table (D082)".
  Note the design docs (D081-D089 etc.) are also uncommitted.

M10 opening -- the coordinator's full pre-implementation design review
  is in milestones/10-multicore.md ("Coordinator design review"): 7
  findings on the existing design, 11 proposed decisions, a test
  strategy, staging T00-T04 + R4, and hazards.  It supersedes the short
  list below, which is kept as the original question set.  Nothing in
  it is decided until the user approves and the accepted proposals are
  written as D081+ in docs/decisions.md; then T00 (refactor, no new
  behavior) is cut first.  Decisions that most need the user's call:
  staged global lock first (P1); segmented table (P2); wake-to-home
  ownership (P3); deadline check per dispatch, which changes observable
  timing under load in single-scheduler mode (P5, finding 1); one code
  path with N=1 as the same code (P10).
  Original question list:
  a. Process table and PID ownership: one shared NvRuntime table under a
     QLock, or per-scheduler tables with a global slot->scheduler map.
     PIDs must not change on migration (required test).
  b. Mailbox locking: a sender on scheduler A appending to a process
     owned by scheduler B.  D068 already says the mailbox is
     scheduler/sender territory; M10 must make that a per-process lock
     or a per-owner-scheduler lock, and must preserve the M05
     per-sender FIFO contract across the lock.
  c. Wakeup: a receive-wait on B, a send from A -- how A makes the
     process runnable on B (or steals it) without a lost wakeup or a
     double enqueue (two required tests).  The D074 semaphore pattern
     (tsemacquire idle / semrelease wake) is the intended primitive.
  d. Work movement: steal vs. push, and the safe points (a process is
     only movable while suspended and not NvHeapCollecting -- the D068
     `owner` field already encodes the second condition).
  e. Timers: which scheduler owns a deadline and how a sleeping
     scheduler's tsemacquire bound is computed when the earliest
     deadline belongs to a process it does not own.
  f. Off-process GC under multiple schedulers: gcoutstanding /
     gclaunched / gccredits are per-scheduler today; a migrated process
     with a collector outstanding must fold on its new owner or be
     unmovable until folded (simplest: unmovable while offlaunched).
  g. Statistics: the "measurement requirement" wants queue, steal,
     wakeup, migration and reduction counts before any lock-free work;
     decide the counters and the -s output shape up front.
  h. Single-scheduler mode must be bit-identical to today (required
     test): keep the current nvschedstep path as the N=1 case rather
     than a special case of the new one.
source control: M09-T01..T04 and the docs split are committed and pushed
  by the user.  UNCOMMITTED as of this checkpoint (D080 files added
  below): the R3-F02 fix
  (lib/value.c, include/nvvm.h), the M09-T05 results and R3 opening
  (bench/README.md, STATUS.md, milestones/09-binaries.md,
  docs/review-findings.md); and the D080 work charging (include/nvvm.h,
  include/nvexec.h, include/nvsched.h, lib/value.c, lib/exec.c,
  lib/sched.c, cmd/nervous/main.c, tests/process/exectest.c,
  bench/latency.c, bench/latency.rc, docs/decisions.md,
  docs/bytecode.md, docs/review-findings.md, README.md, STATUS.md);
  and the R3 closing set (lib/verify.c R3-F03, tests/memory/autotest.c,
  tests/process/ptest.c, tests/process/exectest.c,
  milestones/R3-memory-review.md, docs/review-findings.md, README.md,
  STATUS.md); and the R3-F05/F06 fix (lib/sched.c, include/nvexec.h,
  include/nvsched.h, tests/memory/offloadtest.c, docs/decisions.md D074
  amendments, docs/review-findings.md, milestones/R3-memory-review.md).  Suggested messages: "milestone 09 complete (T05 latency
  results); open R3; R3-F02 nvfragcopy size guard"; "D080: charge
  traversal work as reductions; -w work ceiling (R3-F04)"; "R3:
  coverage fixtures for ownership transitions and live-data memory;
  R3-F03; coverage map and exit statement".  The
  coordinator commits nothing; ask the user at a checkpoint.
source control: user committed and pushed M09-T01..T04 plus the
  docs/ split ("milestone 09 binaries (T01-T04); split design doc into
  docs/ (WIP)").  A follow-up docs-only commit marks the split files as
  non-normative rationale and restores docs/semantics.md as the
  contract.  The coordinator commits nothing; ask the user at a
  checkpoint.
```

Milestone 08 (Memory) and CLI-T01 are complete and accepted; their full narrative, evidence, task ledger and review notes are in `STATUS-archive.md` ("Milestone 08 detail"). Settled policy from 08 lives in `docs/decisions.md` (D061-D075); benchmark evidence in `bench/README.md`.

## Milestone ledger

00-07 and R1-R2 are complete; their historical rows are archived.

| Milestone | State | Dependencies | Remaining scope |
|---|---|---|---|
| 08 Memory | **complete** | R2, 05, 06 | none -- all tasks (T04a/b/p/e/c/r/d) accepted |
| 09 Binaries | **complete** | 04, 08 | none -- T01-T05 done; T05 results in `bench/README.md` |
| R3 Memory review | **open** | 08, 09 | Root/fragment/representation/binary ownership gate, plus the latency-isolation decision T05's numbers force (see below) |
| 10 Multicore | not-started | R3, 05, 06, 08, 09 | Parallel schedulers and work movement |
| R4 Multicore review | not-started | 10 | Mandatory multicore acceptance review |

## Task ledger

The milestone-08 task ledger is archived in `STATUS-archive.md`. Still open from 08: M08-T04q (optional, deferred; see its section below). Milestone-09 tasks are under "M09-Tasks (in progress)".

## M09-Tasks (in progress)

### Progress (current)

- **M09-T01 Bbin runtime: done.** A first draft came from a long Qwen run. Claude's review fixed: word-vs-byte arithmetic in `nvbinapp` (heap corruption); no D067 reservation for the new opcodes in `lib/exec.c`; verifier dataflow and guard edges for them in `lib/verify.c`; 32-bit `1L` shifts; the signed range check; zero padding; `lib/pattern.c` length types; D079 fault names. Nine opcodes: `binalloc`, `binappint`, `binappbin`, `binappend`, `bintestbinary`, `binintget`, `binbinget`, `binremget`, `binend`. Tests: construction/encoding checks in `tests/process/ptest.c`; VM opcode cases A-F in `tests/process/exectest.c` (construction, append, match, mismatch-to-fail-target, faults, remainder). `docs/bytecode.md` documents the opcodes. User-confirmed passing.
- **M09-T02 frontend: done.**
  - Lexer tokens `<<`/`>>`/`:`, and parser (`lib/parse.c`) producing `Ebinagg` with one `Ebinseg` per segment. Encoding of a segment is `ival = (kind<<8) | (width<<2) | (signed<<1) | little`, kinds `Binsegint`/`Binsegsized`/`Binsegrest` (header `include/nervous.h`). Decisions made while implementing (within D076): `<<>>` is the empty binary; segment values and sizes are one primary or unary expression (parenthesize anything looser); `/binary` excludes int modifiers; a rest segment takes no modifiers and must be last; `patternok` accepts binaries with pattern-form values.
  - AST printer (`lib/ast.c`), fixtures `tests/frontend/binary.nv/.ast`, `bad-binwidth`, `bad-binrest`.
  - Formatter (`lib/format.c`, rules in `docs/format.md`), fixture `binary-fmt.nv/.fmt`.
  - AST->`Pbin` conversion (`lib/patcompile.c`, `convertbin`), rejecting a size variable bound by the same or a later segment of that binary. Tests: nine cases in `tests/pattern/parsetest.c` (`binpatterns`), covering most of the milestone's required matching tests at the `nvpatternmatch` level.
  - `lib/patbc.c` `compilebin`: `Pbin` -> `bintestbinary`, one get per segment, `binend` unless a rest segment ends it (a discarded rest emits nothing). A size variable must already be bound when its segment is lowered, which also rejects `${<<p:n/binary>>, n}`. Repeated names test equality like `Pvar`.
  - `lib/compile.c` `patchtests` now repatches the binary ops' fail targets (field `b` for `bintestbinary`/`binintget`, `c` for `binbinget`, `a` for `binend`); without it every binary mismatch jumped to pc 0.
- **M09-T03: done.** `compilebinagg`: evaluates every segment value and size left to right, then `binalloc` plus one append per segment into fresh registers. `is_binary` type test (usable in guards); `guardok` already rejects construction in guards (D079).
- **Also fixed:** `bintestbinary` was written and read with one operand instead of two (`lib/bytecode.c`, `lib/bcread.c`), so saved bytecode lost the fail target and a failing test looped at pc 0 under `-X`.
- **End-to-end test:** `tests/frontend/binary-run.nv/.out`, run by the new `ran` helper in `tests/frontend/run.rc` both via `-r` and via `-c` + `-X`. `-r` output user-confirmed to match all 14 expected values.
- **M09-T04: done, accepted (user-confirmed full suite).** Fault fixtures `tests/frontend/binfault-*.nv` (eight, via the new `faulted` helper), compile rejections `binreject-*.nv/.err` (three, via `rejected -c`), and the exit-criterion example `examples/protocol.nv` (via `ran`, expected output `tests/frontend/protocol.out`). All outputs were first produced by a user run and checked against predictions before being saved. Head-pattern compile errors now report the first parameter's position instead of 0:0 (`clausetuple`). Required tests reconciled in `milestones/09-binaries.md`, "Implementation state".
- **M09-T05: done.** User ran `rc bench/latency.rc`; results and their reading are in `bench/README.md` ("First run"). Headline: equality/print/copy on a shared `${x,x}` chain cost ~16x per four levels of depth (116ms/653ms/188ms at depth 24); a hog running `==` on depth-16 chains stalls peer round trips 27ms median / 58ms max against a 0.5us baseline; a hog dispatch is min(quantum, heap headroom) because each tail call allocates a 3-word argument tuple, and the reduction quantum bounds nothing inside one instruction. Original build description follows. `bench/latency.c`, `bench/latency.rc`, a `latency` mkfile target (in `benchmarks` and `clean`), and the "M09-T05" section of `bench/README.md`. Part 1 times host-side `nvtermequal`/`nvtermprint`/`nvfragcopy` on independently built shared `${x,x}` chains (depth 1-24; the traversals do not detect sharing, so cost doubles per level), plus a linear-chain probe of the depth-256 limit. Part 2 (`latency hop depth peers rounds`) times peer round trips before and while a hog evaluates `a == b` on two depth-d chains; one equality is one reduction regardless of cost (`run()` in `lib/exec.c`), which is the isolation question for R3. Provenance: part 1 drafted by local Qwen (analysis correct; Claude fixed a 32-bit `1UL<<255` shift and a check applied to the wrong chain shape); part 2 drafted by local GLM after Qwen stalled, then largely rewritten by Claude (the draft omitted the equality and used an unallocated pid array). `mk benchmarks` builds clean. The three pre-run risks the README section originally listed (word-count formulas, a possible `Idle` abort while the hog waits on `after 0`, and whether `a == b;` parses) were each checked against `lib/value.c`, `lib/process.c` and `lib/parse.c` and hold; see the README section. Only the run itself remains.
- **Remaining:** user runs `rc bench/latency.rc`; record results; then milestone 09 can close and R3 opens.
- **R3 pre-gate audit (while T05 awaits its run):** a Sonnet read-only audit of the nine `bin*` opcodes across `value.c`/`exec.c`/`gc.c`/`verify.c`/`pattern.c` (D063 pointer discipline, 32-bit widths, reservation exactness, bounds, encoding, GC sizing, verifier rules) found one real defect and one cosmetic one, both recorded in `docs/review-findings.md`: **R3-F02** (medium, fixed-pending-verification) -- `nvfragcopy` passed an unguarded 64-bit byte size to `malloc(ulong)` and truncated `count` into `NvFrag.nword`; a guard now returns `NvTermlimit` (`lib/value.c`, contract comment in `include/nvvm.h`); `mk tests benchmarks` clean. **R3-F03** (low, open) -- vacuous `(x&3) > 3` flag check in `verify.c`. Questions A, C-G of the audit came back clean with quoted evidence. R3-F02 needs the user to rerun `rc tests/run.rc` and `nervous_gcstress=1 rc tests/run.rc` before it closes.

The original plan follows. Design settled in D076-D079 (representation, grammar/evaluation, width/alignment, sizing reservation and fault reasons). Write sets are reserved for the named task. T01 and T02 are disjoint behind the settled interfaces (the D076-D079 opcode operand meanings and the `Ebinagg`/`Ebinseg` AST shapes) and may run in parallel; T03 depends on both; T04 depends on T03. T05 is independent of T01-T04 (it measures the pre-existing term machinery, not binaries) and may run in parallel with any of them.

| Task | State | Dependencies | Write set (canonical, exclusive) | Objective |
|---|---|---|---|---|
| M09-T01 Bbin runtime | done | 08 | `include/nvvm.h`, `include/nvexec.h`, `lib/value.c`, `lib/gc.c`, `include/nvpat.h`, `lib/pattern.c`, `lib/exec.c`, `include/nvbc.h`, `lib/verify.c`, `lib/bytecode.c`, `docs/bytecode.md` | `Bbin` boxed kind and `Vbin`/`is_binary`; `nvbin`/`nvbinlen`/`nvbinbytes`; `nvbinbuild`/`nvbinbinget`/`nvbinremget` (self-contained byte copies, budget-aware); equality/print/heapcopy/fragcopy support; new opcodes `binalloc`, `binbinget`, `binremget` (D079 sizing reservation: allocate, retry from clean state, no mid-copy collection); verifier operand + guard-region rules (binaries excluded from guards, D060/D071); disassembly text for the new opcodes. |
| M09-T02 Binary frontend | done | 08 (independent of T01; builds on the coordinator-settled `include/nvpat.h` `Pbin` interface) | `include/nervous.h`, `lib/parse.c`, `lib/lex.c`, `lib/ast.c`, `lib/patcompile.c` (AST->`NvPattern` conversion, `nvpatternfromexpr`), `lib/patbc.c` (pattern->bytecode lowering, `nvpatterncode`), `lib/format.c`, `docs/format.md`, `tests/frontend/*` (new fixtures only) | `<< ... >>` aggregate with the D076 segment grammar (integer segments with literal widths {8,16,32,64} and `/signed`/`/unsigned`/`/big`/`/little` modifiers; `:size / binary` and `/binary` segments; final unsized remainder); `Ebinagg`/`Ebinseg` AST nodes (parser does not validate forward/backward references or pattern-mode legality; the compiler and pattern checker do); `patternok` accepts binaries; `nvpatternfromexpr` converts `Ebinagg` to the `Pbin` `NvPattern`; `nvpatterncode` lowers binary patterns to `binalloc`/`binbinget`/`binremget` with a byte-position register; guard check rejects binaries (D079); canonical formatting for the aggregate (D021, `docs/format.md`); lexer/AST plumbing (a `Tbinopen`/`Tbinclose` token pair is expected -- the exact token names are an implementer detail to report, as with any new token). |
| M09-T03 Binary compiler lowering | done | T01, T02 | `lib/compile.c`, `docs/decisions.md` (only if an implementation-level refinement is needed; report it) | Lower `Ebinagg` construction to `binalloc` (D077: evaluate operands left to right, compute total, single allocation, fill left to right) and the D079 construction faults (`bad_binary`, `overflow`, `badarith`); wire `is_binary` into `typetest` and the D060 guard/type-test surface; forward-reference size validation in patterns is already the pattern checker's (T02) -- this task is construction side and the intrinsic wiring only. |
| M09-T04 Tests + exit-criterion example | done | T03 | `tests/bytecode/*`, `tests/pattern/*`, `tests/vm/*`, `tests/process/*`, `tests/run.rc`, `examples/README.md`, new `examples/protocol.nv` (the milestone exit-criterion example: a small length-prefixed protocol), `milestones/09-binaries.md` (implementation-state section only) | Every "Required tests" item from `milestones/09-binaries.md`: exact match + trailing-byte rejection, empty/nonempty remainders, length-prefixed payload, endian + signed decoding, invalid forward size reference rejected by the frontend, late failure rolling back earlier segment bindings, allocation/size-limit failures controlled. Golden tests in the existing suites; the example demonstrates the exit criterion (length-prefixed protocol over process-heap-owned binaries). |
| M09-T05 Latency-isolation measurement (R3 prep) | built; awaiting user run of `rc bench/latency.rc` | 08 | `bench/latency.c` (new), `bench/latency.rc` (new), `bench/README.md`, `mkfile` (only the new target + `benchmarks`/`clean` entries, as T04r did) | First numbers for REVIEW-impressions.md's third isolation leg (now in R3's scope): structural equality and print cost on independently built shared graphs (`${x,x}` chains at increasing depth), copy of shared deep terms, and small-message hop latency measured with an expensive-term peer running concurrently on the same scheduler. Measurement only -- no mechanism changes (work-sensitive charging, resumable traversals, bounded exports, or I/O offloading are named there as possible later mechanisms; this task measures, it does not choose). |

Notes on the write sets. The three pattern files do different things and are split to keep T01/T02 disjoint: `lib/patcompile.c` is the AST-to-`NvPattern` conversion (`nvpatternfromexpr`, T02), `lib/patbc.c` is the pattern-to-bytecode lowering (`nvpatterncode`, T02), and `lib/pattern.c` is the runtime matcher (`nvpatternmatch`, T01) that both the pattern harness and the source path use, where a `Pbin` pattern matches a `Bbin` term. The one genuinely shared surface, `include/nvpat.h` (the `Pbin` kind and `NvBinseg` struct), is settled centrally in `milestones/09-binaries.md`'s "Settled interfaces" section before either task starts, so T01 owns it and T02 builds to it without editing it -- this is the coordinator-settles-the-interface rule from COORDINATION.md's parallelism policy, not an exception. If a task finds it needs a file outside its set, it reports a blocker, it does not expand scope. `include/nvproc.h` is in no M09 set (no `NvLimits` field is added: D078/D079 deliberately reuse the heap word budget rather than a new binary byte limit, so there is no `.gcoffload`-style field to thread through the construction sites). The `mkfile` change in T05 follows the T04r precedent (new build target only, no behavior change to existing targets).

## Recommended next sequence

**Milestones 08 and 09 are complete. R3 is open** and gates milestone 10. Done so far in R3: R3-F02 closed (user-tested); audit legs A1 (bin* opcodes) and A2 (roots, ownership transitions, mid-scan collection, idle sweep, off-process collector, GC rollback, table realloc) clean and logged; D080 decided and implemented for the latency-isolation leg (R3-F04, fixed-pending-verification: traversal visits charged as reductions at 8 per, optional `-w` ceiling). **R3 is complete.** D080 verified by measurement; coverage leg R3-A3 closed gaps 3a/3e/3f/4 with fixtures; the newly mandatory `nervous_gcstress=1 nervous_gcoffload=1` suite run exposed R3-F05 (off-process idle collections were no-ops since M08-T04c) and led to R3-F06 (teardown semrelease use-after-free); both fixed with regressions, and all three suite invocations pass on the final build. **Milestone 10 (Multicore) is open**: design accepted and recorded as D081-D089; M10-T00a (segmented process table) is cut and ready; T00b, T01-T04 and R4 staged in `milestones/10-multicore.md`.

## T04q duplicate interpreter work (planned, optional, deferred; not a T04c dependency)

Source inspection found arithmetic results and call-target resolution computed during reservation preflight and then computed again for execution. A narrow pass can remove this duplication. Not required before or after T04c; pursue only if the user wants the throughput work specifically.

- Read `lib/exec.c`, `include/nvexec.h`, D063/D067/D071-D073 and the memory test coverage first.
- Reuse scalar arithmetic results or function indices within the SAME instruction attempt where safe. Discard/recompute the plan after NvCollect; never retain an unrooted heap pointer across movement. Avoid persistent module caches or public ABI changes unless separately justified.
- Preserve exact operand errors, overflow/divide behavior, guard fault transfer, call/tailcall accounting, unchanged pc/reductions on collection requests, and one-time side effects.
- Acceptance: forced build, normal/stress regressions, quanta 1/1000, and repeated unprofiled original/control benchmarks. Keep measured wins or a justified simplification; do not weaken checks to chase the stage-2 number.
- Stopping rule: one bounded attempt, then proceed or defer.

## Current implementation map

- Representation/accounting: `include/nvvm.h`, `lib/value.c`. Tagged words, interned atoms, immutable sharing, independent fragments. Managed execution heaps are contiguous; host/startup construction still uses non-moving chunks.
- Collector: `lib/gc.c`. Cheney copy; address classification includes adopted fragments and excludes stable external fragments. Source headers can be restored on failed trials; roots commit only after all fallible work. Small rollback scratch uses the C stack; the heap descriptor is reused only on successful commit.
- Roots/reservations: `include/nvexec.h`, `lib/exec.c`. Active registers only, retained stack capacity charged, small root-view scratch on the C stack, NvCollect request/retry and guard-aware failure. Standalone servicing in `lib/vm.c` preserves the reduction budget.
- Processes: `include/nvproc.h`, `lib/process.c`. Fragment mailboxes, non-consuming recvneed before take, geometric slot capacity and lowest-free hint. Retired slots can outnumber the configured live-process limit; FIFO links use indices.
- Scheduler: `include/nvsched.h`, `lib/sched.c`. Inline demand/idle collection, explicit storage snapshots and opt-in profiling, plus (M08-T04c, D074) off-process collection: a heap owner state and `Lock`, `rfork(RFPROC|RFMEM|RFNOWAIT)` collector procs restricted to a bare `NvExec*`/semaphore argument list, a locked completion fold, never-spin/never-false-idle scheduler waiting on a malloc'd completion semaphore bounded by the nearest deadline, a capped idle-sweep launch burst, and a bounded teardown drain. Default dispatch still has neither snapshot scans nor profiling clock reads on the no-collector path.
- Binaries (M09, D076-D079): `Bbin` terms in `include/nvvm.h`/`lib/value.c` (`nvbin`, `nvbinapp`); runtime matching in `lib/pattern.c`; nine `bin*` opcodes in `lib/exec.c`, verified in `lib/verify.c`; frontend in `lib/lex.c`, `lib/parse.c`, `lib/patcompile.c`, `lib/patbc.c`, `lib/compile.c`, `lib/format.c`.
- CLI: `-H` word budget, `-G` stress, `-o words` off-process threshold (M08-T04d), `-s` statistics. `nervous_gcstress=1`/`nervous_gcoffload=words` set the matching CLI defaults; `-o`/`$nervous_gcoffload` apply only to `-r`/`-X` (the standalone `-x`/`-t` executor has no scheduler). Default is 0 (never off-process, D075, measurement-confirmed) -- unaffected by every test/benchmark's own independent `NvLimits.gcoffload = 0` construction.
- Tests: `tests/memory/gctest.c` (seven groups), `autotest.c` (six groups), and `offloadtest.c` (eight off-process lifecycle groups, M08-T04c/T04d), included by `tests/run.rc`. Build-only benchmark target: `mk benchmarks` produces `bench/perftest`, `bench/largelive` and `bench/latency` (M09-T05).

## Remaining cautions

- `gcoffload` default (0, D075) and the idle-sweep cap (`NvGcsweepcap=8`, still unmeasured/provisional) are recorded; adaptive sizing and heap shrinking remain unimplemented. D069 defers shrinking; `docs/questions.md` records the remaining open policy questions (what would reopen the `gcoffload` default, the persistent-collector-pool idea).
- Collector cost includes classification/freeing of adopted fragments and scratch/trial work, not just copying live words. Transient old/new/scratch space is not the retained-data maxheap budget.
- Host malloc failure paths have source review but no deterministic injection coverage. Root descriptors, C pointers and custom host callback contracts are trusted; verifier guarantees apply to bytecode, not arbitrary host metadata.
- Global atom synchronization and multi-scheduler ownership remain milestone-10 obligations; do not quietly expand T04q into that work.
- post-R2-F01 is closed with D071 evidence. R2-F16 remains deferred to milestone 10. `REVIEW-impressions.md` is not declared wholly resolved; R3 remains a mandatory future gate.

## Resumption checklist

1. R3 is open: read the checkpoint block's `next:` line and "Recommended next sequence", then `docs/review-findings.md` (R3 table) and `bench/README.md` ("M09-T05" -> "First run" -> "The R3 finding"). Milestone 09's task detail under "M09-Tasks" is history now. If instead resuming T04q (still optional, still deferred) or anything not scoped above, fall back to the fuller read list in step 3.
2. Confirm actual source-control state with the user; preserve the accepted checkpoint before new edits. A commit message is not evidence of a commit. As of the M09-T04 checkpoint, the user reports committing and pushing (see "source control" in the checkpoint block).
3. (Only if step 1's fast path doesn't apply) Read README, this status, `docs/semantics.md` (the normative contract; the `docs/` design-rationale files split from `nervous_design.md` are not), `milestones/09-binaries.md` and D076-D079 for milestone 09; for R3 preparation, `milestones/R3-memory-review.md` and D061-D075 (D074 especially, including its coordinator-review amendments) and the task's adjacent source/tests. Read COORDINATION before assigning workers.
4. Assign exact exclusive canonical paths; keep shared headers/build/docs coordinator-owned unless explicitly transferred. Every milestone-08 task (T04a-T04d), CLI-T01, and M09-T01..T04 are done, with their write sets released. M09-T05's row in "M09-Tasks (in progress)" has its exact write set; assign it and start.
5. Build with mk after edits. User runs behavioral tests/benchmarks; never execute scripts, cleaning, installation or source-control commands through the compilation-only mk tool.
6. Sub-agents: read `/usr/dave/local_models.md` "Economic Review" first -- the current policy is cloud Sonnet (`claude-sonnet-5`) for bounded read-only audits with a numbered deliverable and mandatory source quotes (the R3-F02 audit is the template: seven files in order, seven questions, ~35 calls, `maxrounds 40`), the coordinator for every edit, and local models only for second-opinion review or doc summaries. If working with a sub-agent again: raise `maxrounds`/`autocontinue` with two SEPARATE ctl writes, not combined with a `model` write in the same call -- combining them was observed this session to silently reset both back to their defaults (20/0), costing a wasted round-capped exchange before it was caught. Verify by reading `ctl` back before sending the task prompt.
