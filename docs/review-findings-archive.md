# Closed Review Findings Archive

This file holds findings from review gates that are fully closed and will not
reopen. Nothing here is deleted, only relocated out of `review-findings.md` to
keep that ledger short for agents doing ongoing work. States and severity
rules are defined in `review-findings.md` and `COORDINATION.md`. Do not copy
entries back into `review-findings.md`; if a genuinely new defect touches the
same area, give it a fresh ID in the ledger that owns the gate that is
currently active.

## R1 - Foundation consolidation (gate complete; `R1` milestone state is `complete` in STATUS.md)

| ID | Severity | State | Owner | Finding | Required evidence |
|---|---|---|---|---|---|
| R1-F01 | high | closed | R1 coordinator | Instruction dispatch was duplicated in `lib/vm.c` and `lib/exec.c`, allowing opcode semantic drift | `lib/exec.c` is now the sole dispatch; `nvexecute` delegates; aggregate VM and process suites pass |
| R1-F02 | high | closed | R1 coordinator | Compiler and pattern-lowering partial-allocation/error paths lacked a dedicated ownership audit | Audit fixed partial-function cleanup, ignored emission failure, match error leaks, transactional transfer, diagnostics, and tuple cleanup ordering; deterministic allocation-failure sweep and aggregate suite pass |
| R1-F03 | medium | closed | R1 coordinator | Accepted suites required several manual commands, allowing old milestones to regress unnoticed | `mk tests` builds all artifacts; `rc tests/run.rc` passes frontend, bytecode, VM, pattern/compiler, process, and resumable execution suites |
| R1-F04 | medium | closed | R1 coordinator | Compiler validation was incomplete for duplicate functions, unresolved calls, register pressure, unsupported constructs, and malformed generated control flow | Duplicate/unresolved, register-pressure, and unsupported-construct tests pass; `nvcompile` verifies every generated module before returning it |
| R1-F05 | medium | closed | R1 coordinator | `nervous_design.md` contains superseded syntax/function statements that can mislead contributors | Prominent historical/superseded warning links to compact normative documents and states that compact sources win |
| R1-F06 | medium | closed | R1 coordinator | Mailbox byte accounting estimates host allocation rather than a final fragment/heap representation | D040 documents the provisional estimate and assigns replacement to R3; boundary, subtraction-underflow, and malformed-value tests pass in the aggregate suite |
| R1-F07 | low | closed | M05/R2 coordinator | Source compile/execute lacked CLI integration and scheduler lifecycle reporting | D046 defines root outcome, fault, exit, and deadlock behavior; `nervous -r` compiles and schedules source; aggregate arithmetic and concurrent ping/pong command checks pass |
| R1-F08 | low | closed | R1 coordinator | `nvvm.h` use depended implicitly on `NvModule` being declared first | `value.c` and mk dependencies now include `nvbc.h` explicitly; aggregate compilation succeeds |
| R1-F09 | high | closed | R1 coordinator | Compiled patterns treated variables already bound in the lexical environment as duplicate-binding errors rather than equality tests | Pattern lowering accepts known bindings and emits `testeq`; focused test and aggregate compiler suite pass |
| R1-F10 | medium | closed | R1 coordinator | A zero-argument current-syntax function body whose first expression is a tuple was misclassified as clause-form | Parser now distinguishes tuple-leading bodies from `${...} =>` after parsing the tuple; body and clause regressions pass |
| R1-F11 | high | closed | M05 coordinator | Signed overflow in `tuple` operand-window verification could admit out-of-bounds verifier and executor access | Overflow-free subtraction check added; aggregate suite rejects both out-of-range and signed-overflow fixtures |
| R1-F12 | low | closed | M05 coordinator | D007's original overflow reason contradicted D029 and the implementation | D007 now explicitly defers reason representation to D029's `overflow` reason |
| R1-F13 | low | closed | M05 coordinator | Definite-initialization dataflow silently omitted control-flow edges to pc 0, relying on an unstated monotonic-entry invariant | The pc-0 special case was removed so backedges participate in the normal merge/fixpoint; loop-entry and aggregate verifier suites pass |
| R1-F14 | low | closed | M05 coordinator | A clause-form parse failure at the closing brace leaked the partial clause list | Parser now walks and frees the owned partial clause list before freeing the function; parse-negative and aggregate suites pass (closure is code/ownership review, not external leak instrumentation) |

## R2 - Concurrency and lifecycle (gate complete; `R2` milestone state is `complete` in STATUS.md)

Pre-gate findings R2-F01 through R2-F14 were found and closed during milestones 05/06; the formal gate (task R2-T01: four parallel read-only investigators over lifecycle/state, mailbox/messages, timers/deadlines, fairness/limits) added R2-F15 through R2-F20; R2-F21 through R2-F23 are post-gate defects found by the first programs to exercise the runtime at scale (`examples/sieve.nv`, `examples/ring.nv`). Every row except R2-F16 is `closed` with a user-confirmed `rc tests/run.rc` pass; R2-F16 is `deferred` to milestone 10 with an owner (`docs/questions.md`, "Milestone 10"). Evidence columns are condensed here; the tests named are all still in the suite.

| ID | Severity | State | Finding | Evidence |
|---|---|---|---|---|
| R2-F01 | high | closed | Slot reuse after `ulong` generation wrap could alias an ancient stale PID (D037) | Exhausted generations retire the slot; lookup accepts only live states |
| R2-F02 | medium | closed | Process-table growth during `spawn` after the cursor wrapped gave the parent an extra quantum | Scheduler reacquires the slot after execution (superseded entirely by D059's run queue) |
| R2-F03 | high | closed | Receive wait/take/advance could not distinguish an exhausted scan from no scan | Explicit scan-phase state guards every receive host call |
| R2-F04 | high | closed | Clause patch loops rewrote pattern-test targets emitted by clause bodies | Patch windows end at the outer pattern's end |
| R2-F05 | high | closed | A message appended after scan exhaustion but before `recvwait` could sleep with a match queued | `nvprocrecvwait` detects the append and stays runnable; quantum-one regression |
| R2-F06 | medium | closed | No frame-depth limit; D039 overstated aggregate accounting | D047 `maxframe` and `system_limit` |
| R2-F07 | low | closed | Semantics called every fault a term | Semantics distinguishes D029 diagnostics from `exit` terms |
| R2-F08 | medium | closed | Recursive parsing had no source-depth bound | `NvMaxsourcedepth` 256 (D052); residue fixed as R2-F14 |
| R2-F09 | low | closed | Term-depth exhaustion reported as `out_of_memory` | `NvValuelimit` -> `system_limit` |
| R2-F10 | low | closed | `nvschedspawn` could read a stale caller error buffer | Cleared on entry |
| R2-F11 | evidence | closed | No executable coverage of a clause-form receive blocking | `tests/process/cli/deadlock.nv` |
| R2-F12 | evidence | closed | Receive pinned only at quantum one | `schedtest` sweeps quanta 1-4 |
| R2-F13 | evidence | closed | Formatter idempotence tested with one pass | Frontend harness reformats every golden |
| R2-F14 | medium | closed | Iteratively built operator chains escaped the R2-F08 bound; deep left spines exhausted the C stack in free/format/print/compile | `parseexpr1` charges every chain link; 64-link accepted, 256-link rejected |
| R2-F15 | medium | closed | A timed receive matching on its first scan never consumed its deadline; `hasdeadline`/`deadline` survived exit and slot reuse (latent: the only reader also checked `Prwaiting`) | `nvprocexit` and `nvprocspawn`'s reuse branch clear both fields; `r2test.c` `h1regression` |
| R2-F16 | low | deferred (milestone 10) | `NvLimits.maxduration` is declared but never validated or read | Documented advisory-only in `nvproc.h`; owner named in `questions.md` |
| R2-F17 | evidence | closed | No test appended a message while a scan cursor sat on an earlier candidate | `r2test.c` `midscanappend` |
| R2-F18 | evidence | closed | No test reaped a sender immediately after send-then-return and checked the copy | `r2test.c` `sendthenexit` |
| R2-F19 | evidence | closed | No test showed a stale timed-receive deadline cleared before a later plain receive blocks | `r2test.c` `crossreceiveleak` |
| R2-F20 | evidence | closed | Fairness for a process spawned into a low slot was only incidentally covered | `r2test.c` `belowcursorfairness` (rewritten against the D059 queue) |
| R2-F21 | medium | closed | The compiler never emitted `tailcall`; every recursive message loop grew a frame per message and faulted `system_limit` at `maxframe` (D047's "tail calls retain their depth" was vacuous) | `compileexpr` tail flag lowers every tail-position call to `tailcall`; tail position defined in `semantics.md`; `compiletest.c` 3000-deep loop, non-tail twin still faults; `run.rc` runs `sieve.nv main 1023` |
| R2-F22 | low | closed | CLI checked `NvSchedIdle` before the root outcome, so a root fault whose orphans then blocked printed only "deadlock" (D046 violated) | Root outcome first, orphan count second; `tests/process/cli/orphan.nv` |
| R2-F23 | medium | closed | D042's rotating slot scan made dispatch O(live processes) when the next runnable slot lay behind the cursor: 65535-node ring 5826 s, 44 us/hop vs 2 us at 1000 nodes | D059 intrusive FIFO run queue; 32768 nodes 114.75 s, 1.75 us/hop; `bench/` |

Not carried forward from the formal gate (named so the gap stays visible): the fuller "tiny limits" plan (T5-03 message exactly at `maxmessage`, T5-06 tiny `maxduration` -- blocked on R2-F16, T5-07 whole suite under tiny limits) and a dedicated spawn-storm reduction-floor test. State-transition invariants distilled from the lifecycle lens: `NvProcess.state` is a plain int written only by guarded transition functions; `scanning` is 1 iff `state == Prrunning`; `hasdeadline` is cleared at every slot-death/reuse point and otherwise read only where `Prwaiting` is also checked.
