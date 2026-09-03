# Nervous Project Status

This file is maintained by the active coordinator. Workers report changes to the coordinator rather than editing it concurrently.

## Coordinator

```text
name/session: claude-coordinator-session-3
since: claimed at the R2/milestone-07 boundary (no clock access in this environment; event-labeled
  rather than dated, see claude-coordinator-session-2's final STATUS.md for the prior state)
```

## Current milestone

```text
milestone: R2 - Concurrency and lifecycle review
state: complete
acceptance owner: claude-coordinator-session-3
last verified build: mk clean && mk tests passes from empty, including tests/process/r2test.c
last verified test run: rc tests/run.rc passed completely (user-confirmed via /tmp/results), including
  every R2 regression (tests/process/r2test.c's five "ok - R2..." lines and four "ok - tiny: ..."
  lines) alongside the complete unchanged existing aggregate suite
```

R2's formal adversarial review is complete and accepted. Four parallel sub-agent investigators
(lifecycle/state, mailbox/messages, timers/deadlines, fairness/limits) covered every adversarial
question in `milestones/R2-concurrency-review.md`. One real defect was found and fixed (R2-F15,
closing the milestone-06 deferred gap with an actual fix, not just a re-confirmed inspection
argument), one dead-field finding was deferred to milestone 10 with a named owner (R2-F16), and four
NEEDS-TEST gaps were closed with new regressions in `tests/process/r2test.c` (R2-F17 through R2-F20).
The user ran `mk tests` and `rc tests/run.rc` and confirmed a complete pass. Every finding above
`R2-F15`/`R2-F17`-`R2-F20` is now `closed` in `docs/review-findings.md`; `R2-F16` remains correctly
`deferred`. Milestone 08 (Memory) is the next available forward-feature track (see "Next coordinator
actions" below); no milestone is currently claimed as active by this coordinator.

Milestone states are:

```text
not-started
ready
active
blocked
acceptance
complete
```

Review milestones are mandatory dependency gates. Forward feature work pauses until the active review exit criterion is met.

## Milestone ledger

Milestones 00 through 05 and review R1 are complete; their full ledger rows, acceptance summaries, and released task rows are relocated to `STATUS-archive.md` to keep this file short. Ordinary forward work never needs that archive: every dependency below that names an archived milestone or task ID is satisfied.

| Milestone | State | Dependencies | Acceptance summary |
|---|---|---|---|
| 06 Timeouts | complete | 05 | Receive deadlines (D048-D052) implemented and passing; one low-risk gap deferred to R2 (see below) |
| R2 Concurrency review | complete | 05, 06 | Lifecycle, fairness, ordering, message/timer ownership gate; review complete (R2-T01), one fix (R2-F15), one deferral (R2-F16), four new regressions (R2-F17-F20); user-confirmed via `rc tests/run.rc` |
| 07 Basic I/O | complete | 05, 06 | `print`/`eprint` through the existing host-callback boundary (D053-D057); implemented and passing, user-confirmed via `rc tests/run.rc` |
| 08 Memory | not-started | R2, 05, 06 | Process-local GC and message ownership |
| 09 Binaries | not-started | 04, 08 | Binary construction and exact matching |
| R3 Memory review | not-started | 08, 09 | GC roots, fragments, representation, and binary ownership gate |
| 10 Multicore | not-started | R3, 05, 06, 08, 09 | Parallel schedulers and work movement |
| R4 Multicore review | not-started | 10 implementation | Mandatory part of milestone 10 acceptance |

Milestone 07 (I/O) is deliberately independent of R2: it touches none of R2's concurrency/lifecycle/timer surface, so it can be planned and implemented before, during, or after R2 without waiting. It is placed before Memory/Binaries because it needs nothing from either (D053).

## Active task ledger

Completed/released rows through R1 (M00-T01 through M05-T02, R1-T01) are relocated to `STATUS-archive.md`.

| ID | Milestone | Assignee | Role | State | Depends on | Exclusive write set | Objective |
|---|---|---|---|---|---|---|---|
| M06-T02 | 06 | claude-coordinator-session-2 (self-executed) | worker | done | M06-T01 | released | `after duration => body`: token, AST, parser, formatter, printer, and compiler lowering to `recvdeadline`/`recvwaitdeadline` |
| M06-T03 | 06 | claude-coordinator-session-2 (self-executed) | worker | done | M06-T01 | released | Deadline arming/expiry in `process.c`, dispatch in `exec.c`, verifier rules, and clock/idle-wake in `sched.c` |
| M06-T01 | 06 | claude-coordinator-session-2 | coordinator | done | M05-T01; M05-T02 | released | Settled timeout semantics/interfaces as D048-D052 |
| M07-T01 | 07 | claude-coordinator-session-3 (self-executed) | coordinator | done | none | released | Settled I/O interface as D054-D057; updated `milestones/07-io.md`, `docs/questions.md`, `docs/semantics.md`, `docs/bytecode.md` |
| M07-T02 | 07 | claude-coordinator-session-3 (self-executed) | worker | done | M07-T01 | released | `print`/`eprint` opcodes, host callbacks, CLI wiring, and every required test from `milestones/07-io.md` |
| R2-T01 | R2 | claude-coordinator-session-3 (self-executed, with delegated read-only sub-agent investigators) | coordinator | done | M06-T01 through M06-T03, R2-F01 through R2-F14 (all closed) | released | Ran R2's formal adversarial review (four parallel read-only sub-agent investigators covering every adversarial question); fixed R2-F15 (`lib/process.c`: `nvprocexit` and `nvprocspawn`'s reuse branch now clear `hasdeadline`/`deadline`), deferred R2-F16 (`NvLimits.maxduration` dead field) to milestone 10 with a doc comment and a `docs/questions.md` bullet, and closed R2-F17 through R2-F20 with new regressions in `tests/process/r2test.c`. `mk clean && mk tests` passes from empty; user ran `rc tests/run.rc` and confirmed a complete pass (`/tmp/results`) |

M06-T02/M06-T03 were implemented directly by the coordinator in one pass rather than dispatched to separate workers, since the settled interface made splitting them lower-value than doing them coherently together; recorded here as a deviation from the usual flow, not hidden. `rc tests/run.rc` has passed completely against this implementation on repeated runs, including deliberately-added coverage for `'infinity`, zero timeout, differing (non-tied) deadlines, message-vs-expiry racing, and a runtime `bad_timeout` fault through the full execute path. One narrow gap remains and is deferred to R2 rather than closed here: no dedicated regression exercises deadline cancellation specifically via process exit/fault/scheduler teardown (argued safe by inspection -- `nvprocexit` frees the mailbox/exec unconditionally regardless of `hasdeadline`, and D037/D041 already prevent a reused slot from inheriting a stale wait state -- but not exercised as an executable test). This falls squarely inside R2's existing "Required work" item 3 (`milestones/R2-concurrency-review.md`: "inspect every mailbox and timer ownership path for leaks, duplicate ownership, and dangling process references"), so no separate tracking is needed beyond this note. R2-T01 subsequently found this inspection argument half wrong: `nvprocexit` does free the mailbox/exec unconditionally as claimed, but D037/D041 do not actually zero the deadline fields -- an incidental scheduler guard was doing that work instead, until R2-T01 fixed it directly. See R2-F15 in `docs/review-findings.md`.

M07-T01/M07-T02 follow the same self-executed pattern as M06-T02/T03: one coordinator settling and then implementing a small, already-independent milestone coherently rather than splitting it across workers. Both are now `done`: the user ran `rc tests/run.rc` (this coordinator's tools can compile but not execute programs, so that confirmation had to come from the user) and reported a full pass, recorded in `/tmp/results`, covering every fixture named in `milestones/07-io.md`'s "Required tests" and exercising `examples/hello.nv` directly via the `hello`/`print-kinds`/`print-order` loop. Milestone 07 is accordingly `complete` in the ledger above.

R2-T01 used a different split from M06/M07: R2's review is read-only investigation across the same small set of core files viewed through several adversarial lenses (lifecycle/state, mailbox/messages, timers/deadlines, fairness/limits), which COORDINATION.md permits to overlap freely since none of it edits files. Four sub-agent investigators ran those four lenses in parallel against `milestones/R2-concurrency-review.md`'s adversarial questions and required work, each writing a report to its own scratch path (`/tmp/r2-<lens>.md`, not durable) and citing exact functions/code paths for every claim, including "no finding." This coordinator independently re-verified every claim against current source before entering anything in `docs/review-findings.md` (the same discipline the incoming coordinator already applied to R2-F09 through R2-F13), reconciled two investigators' differing framings of the same fact into one finding (R2-F15), then implemented the fix, the deferral, and four new regressions itself, keeping `lib/*.c` edits single-session. All four sub-agent sessions were hung up after their reports were read and synthesized. See `docs/review-findings.md` for the resulting entries and `milestones/R2-concurrency-review.md`'s state note for the full exit-criterion disposition per adversarial question.

## Reserved integration surfaces

Coordinator-owned by default:

```text
/usr/dave/work/nervous/README.md
/usr/dave/work/nervous/STATUS.md
/usr/dave/work/nervous/STATUS-archive.md
/usr/dave/work/nervous/COORDINATION.md
/usr/dave/work/nervous/mkfile
/usr/dave/work/nervous/docs/decisions.md
/usr/dave/work/nervous/docs/questions.md
/usr/dave/work/nervous/docs/review-findings.md
/usr/dave/work/nervous/docs/review-findings-archive.md
shared mkfiles
shared public headers
```

## Review findings

The persistent ledger is `docs/review-findings.md`. The final independent re-review verified every first-review closure and recommended acceptance. R2-F08 through R2-F13 are closed: aggregate tests pass bounded source nesting, distinct term-limit reporting, deterministic spawn diagnostics, clause-form blocking receive, quantum 1-4 receive outcomes, and formatter idempotence.

The incoming coordinator independently re-verified those closures against source rather than against the ledger. R2-F09 (`Otuple` maps `NvValuelimit` to `system_limit`), R2-F10 (`nvschedspawn` clears the caller error buffer on entry), R2-F11 (`tests/process/cli/deadlock.nv` is clause-form), R2-F12 (quanta 1-4 sweep), and R2-F13 (frontend harness reformats every golden) all hold. R2-F08 did not: its bound covered only recursive descent, leaving iteratively built operator chains unbounded. That residue was R2-F14, fixed under M05-T02 and closed with a passing aggregate suite.

The formal R2 gate (R2-T01) added R2-F15 through R2-F20 (superseding the "No finding is open" state that held before it): one fix (R2-F15, the deferred M06 deadline gap), one deferral (R2-F16, `NvLimits.maxduration`, to milestone 10), and four evidence-class NEEDS-TEST closures (R2-F17 through R2-F20). The user ran `mk tests` and `rc tests/run.rc` and confirmed a complete pass; every one of these except R2-F16 is now `closed`, and R2-F16 is correctly `deferred`. No finding is open.

## Integration queue

```text
handoff state: claimed by claude-coordinator-session-3; no milestone currently actively claimed
gate: milestones 00-07 and R2 are all complete. Milestone 08 (Memory) is the next forward-feature
  track; its dependencies (R2, 05, 06) are now all satisfied. No review gate is currently active.
baseline: mk clean && mk tests pass from empty, including tests/process/r2test.c. rc tests/run.rc
  passes completely (user-confirmed, /tmp/results), including every R2 regression and the complete
  unchanged existing aggregate suite.
open findings: none open; R2-F16 deferred to milestone 10 (see docs/review-findings.md)
active task: none claimed yet for milestone 08
required first reads for milestone 08: README.md, STATUS.md, milestones/08-memory.md,
  docs/questions.md's "Milestone 08 - Memory" section, docs/decisions.md D039/D040/D047 (the
  provisional accounting milestone 08 is expected to replace with exact heap/GC accounting)
adjacent source map for milestone 08: lib/value.c, lib/process.c (mailbox byte accounting to be
  replaced), include/nvvm.h, include/nvproc.h; likely new files for heap/GC representation
constraint: none currently active. A later review gate (R3, before milestone 10) will apply once
  milestone 08/09 are far enough along; see README.md's milestone order.
```

## Recently completed

Two defect fixes found by running `examples/sieve.nv` at `generate(2, 1024, 'nil)`, recorded as
R2-F21 and R2-F22 in `docs/review-findings.md` (state `fixed-pending-verification`). R2-F21: the
compiler never emitted `tailcall`, so D047's "tail calls retain their current depth" was vacuous and
every recursive message loop grew one frame per message until `system_limit` at `maxframe`;
`lib/compile.c` now lowers every tail-position call (clause body, last expression of a tail block,
every arm of a tail `if`/`match`/`receive` including `after`) to `tailcall`, with the definition made
normative in `docs/semantics.md` and noted on D047. R2-F22: `cmd/nervous/main.c` checked
`NvSchedIdle` before the root outcome, so a root fault whose orphaned children then blocked forever
was reported only as "deadlock"; the root diagnostic now comes first with an orphan count as a second
line (noted on D046). New regressions: tail-loop depth/lowering checks in
`tests/pattern/compiletest.c`, `tests/process/cli/orphan.nv`/`.err`, and a sieve line-count check in
`tests/run.rc`. `mk tests` builds clean. **Not yet user-confirmed:** `rc tests/run.rc` must be run;
the hand-written `orphan.err` golden is the most likely thing to need adjusting.

Three new runtime-goal examples, also **not yet user-confirmed** (written and reviewed against the
parser and printer, but not executed): `examples/ring.nv` (message passing; root value 20000),
`examples/isolation.nv` (fault isolation; a `divide_by_zero` child, siblings unaffected, lost reply
detected by `after`; golden `tests/process/cli/isolation.out`), and `examples/ioserver.nv` (I/O as a
Ref-correlated message exchange; golden `tests/process/cli/ioserver.out`). All three are wired into
`tests/run.rc`. `examples/README.md` now lists every example with its goal. The I/O-as-messages
direction the ioserver example sketches is recorded in `docs/questions.md` ("Later") with the
questions it forces, to be settled as a decision before file/network I/O is built.

Surface syntax v3 (D058) landed as a self-contained frontend pass, outside the milestone sequence.
Functions are `fn name(patterns) { ... }` with adjacent same-name declarations forming one function's
clauses; `match`/`receive` clauses need `;` only after expression bodies; `if`/`else if`/`else` is
compiler sugar over `'true`/`'false` (`'ok` when no `else`); `==`/`!=` lower onto `testeq`; and the
process forms are keywords with their own AST nodes (`self`, `mkref`, `spawn f(args)`, `pid ! msg`,
`exit reason`). `print`/`eprint` stay call-shaped and are now the only compiler-reserved names.
Syntax v1 is gone; the previous (v2) syntax is the `-A`/`-F` compat input, exercised by
`tests/frontend/compat.nv`. Files touched: `include/nervous.h`, `lib/{lex,parse,ast,format,compile}.c`
(`lib/parsev1.c` removed), `cmd/nervous/main.c`, `mkfile`, every `.nv` example/fixture and golden,
the embedded sources in `tests/pattern/{parsetest,compiletest}.c` and `tests/process/r2test.c`, and
`docs/{decisions,semantics,format}.md`. `mk tests` builds clean. **Not yet user-confirmed:** the
agent that made this change can compile but not execute, so the new goldens under `tests/frontend/`
were written by hand from the formatter's rules; `rc tests/run.rc` must be run to confirm them (any
`.fmt`/`.ast` mismatch is most likely a golden transcription error rather than a parser defect, but
check the parser first if the `-f` idempotence step fails).

R2's formal adversarial review (task R2-T01) is complete and accepted; R2 is `complete` in the milestone ledger. Four parallel
sub-agent investigators covered every adversarial question in `milestones/R2-concurrency-review.md`
across four lenses (lifecycle/state, mailbox/messages, timers/deadlines, fairness/limits). One real
defect was found and fixed: R2-F15, a stale `hasdeadline`/`deadline` surviving process exit and slot
reuse whenever a timed receive matched its message on the very first scan (never consuming the
armed deadline through `recvwait`/`recvwaitdeadline`). This is also the exact gap milestone 06
deferred to R2 -- its "safe by inspection" argument turned out to be half right (`nvprocexit` does
free unconditionally) and half wrong (D037/D041 do not actually zero this field; an incidental
`Prwaiting` guard was doing that work instead). `lib/process.c` now clears both fields at every
slot-death and slot-reuse point. One dead-field finding (R2-F16, `NvLimits.maxduration` is declared
but never validated or read) was deferred to milestone 10 with a doc comment and a `docs/questions.md`
entry, since enforcing it is a real semantic decision, not a drive-by fix. Four NEEDS-TEST gaps found
by inspection (no defect, but no regression) were closed with new fixtures in the new
`tests/process/r2test.c`, which compiles real Nervous source through the full frontend rather than
hand-assembling bytecode arrays, deliberately avoiding the operand/offset-counting risk a
schedtest.c-style literal array would carry: R2-F17 (message appended mid-scan, not just
post-exhaustion), R2-F18 (sender reaped immediately after an ordinary return-after-send), R2-F19
(a stale timed-receive deadline is cleared before a later plain receive blocks), and R2-F20 (a
process spawned into a slot below the scheduler's cursor is still dispatched within one sweep).
`mk clean && mk tests` passed from empty, and the user then ran `rc tests/run.rc` and confirmed a
complete pass (`/tmp/results`), including every new R2 regression alongside the unchanged existing
aggregate suite. The gate is met: R2-F15 and R2-F17 through R2-F20 are `closed`; R2-F16 remains
correctly `deferred` to milestone 10.

Milestone 07 (Basic I/O) is complete. Its interface was settled this session as D054-D057 (`docs/decisions.md`): two reserved intrinsics `print`/`eprint`, each its own opcode with a destination register (correcting the earlier draft's "no destination register"), a blocking synchronous write returning `'ok`, an `io_error` fault on write failure, and an explicit dispatch-order cross-process guarantee scoped to the current single scheduler. The implementation followed in the same session: `Oprint`/`Oeprint` in the bytecode enum/disassembler/reader/verifier/executor, the two reserved names in the compiler, an `NvIO` output-stream pair on `NvScheduler` wired at spawn time, CLI installation against real stdout/stderr, `examples/hello.nv`, and a fixture for every required test named in `milestones/07-io.md` (hostless-fault, verifier-negative, round-trip, per-term-kind rendering, cross-process ordering, `eprint`'s success path, `io_error` on a closed stream, and a dispatch/completion-count comparison against an equal-cost `nop` variant). `mk clean && mk tests` passes from empty, and the user then ran `rc tests/run.rc` and confirmed a complete pass (`/tmp/results`), so this milestone moved from acceptance-pending to `complete` in the same session.

Milestone 06 (timeouts) is complete: `after duration => body` on receive, with monotonic deadlines that are always a lower bound (never fire while a matching message is queued, D048), bounded integer-or-`'infinity` durations (D049), no timer object beyond two fields on the process slot (D050), and two new bytecode instructions reaching the host only through the existing callback boundary (D051). `rc tests/run.rc` passes completely, including deliberately-added coverage for immediate match, zero timeout, `'infinity`, message-vs-expiry racing, tied and differing deadline ordering, and a runtime `bad_timeout` fault through the full execute path. One narrow gap (deadline cancellation via exit/fault/teardown, argued safe by inspection but not regression-tested) is deferred to R2, whose existing mandate already covers it.

Milestone 07 (Basic I/O) was drafted in an earlier session (`milestones/07-io.md`, decision D053) in response to the observation that the only output Nervous had was the CLI's single final root-return-value print. D053 settled that I/O joins through the same reserved-intrinsic/opcode/host-callback pattern as `self`/`send`/`spawn`, that this is not and will never be a general FFI (no raw fds or native pointers reach bytecode, matching D010; any future host resource is an opaque handle term, matching the D038 PID/Ref precedent), and that file/network I/O is a distinct, later, harder milestone deferred until after Binaries (milestone 09) so reads can produce real binaries. At that time its exact intrinsic name(s), blocking/return semantics, and fault-on-write-failure behavior were still open; this session settled all of them as D054-D057 and implemented them (see above). Existing milestones 08-10 (formerly 07-09) and R3/R4 were renumbered to make room when 07 was drafted; every cross-reference between milestone documents was updated along with `README.md` and this file.

Milestone 05 is complete. It delivers checked lifecycle and identity, process-owned resumable execution, deterministic scheduling, selective receive, arbitrary-term exit, root outcome reporting, bounded frames and source/runtime terms, and Ref-correlated RPC. Final independent re-review and all clean-gate aggregate regressions pass.

## Resume point

Milestones 00 through 07 and review R1 and R2 are all complete and user-confirmed. No review gate is
active and no milestone is currently claimed. The next forward-feature track is milestone 08
(Memory): read `milestones/08-memory.md` and `docs/questions.md`'s "Milestone 08 - Memory" section
(exact 64-bit term tagging, initial heap layout and copying-collector details, stack/frame root
enumeration, message-fragment merge timing, heap/allocation exhaustion behavior, atom lifetime and
atom-table limits -- all still open questions to settle as decisions before implementing, the same
way M06-T01/M07-T01/R2-T01 settled their milestones' interfaces first).

Two adjacent questions stay open in `docs/questions.md` and block nothing above: whether `nervous -r`
should gain a command-wide execution bound (CLI policy, not language semantics), and whether
`NvLimits.maxduration` should become authoritative (R2-F16, owned by milestone 10).

## Next coordinator actions

First run `rc tests/run.rc` to confirm the D058 syntax pass and the R2-F21/R2-F22 defect fixes (see
"Recently completed"); on a full pass, move both findings to `closed` in `docs/review-findings.md`
and drop the "await confirmation" clause from `README.md`'s "Current position". Then claim
milestone 08 (Memory), settle its open interface questions as decisions (D059 onward, following
the M06-T01/M07-T01/R2-T01 pattern of settling before implementing), then implement and test it. R3
(the memory review gate) becomes the next mandatory review gate once 08/09 are far enough along; see
README.md's milestone order.
