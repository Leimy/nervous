# Review R2 - Concurrency and Lifecycle Review

## Purpose

Adversarially review completed milestones 05 and 06 before GC changes value ownership and before more term kinds expand message copying. Milestone 05's independent reviews and clean-gate follow-ups are complete; their persistent entries include R2-F01 through R2-F13 in `../docs/review-findings.md`. The formal R2 gate becomes active after milestone 06 so timer ownership and message/deadline races are reviewed together with the established process invariants. Use `../docs/review-05.md` as the completed process baseline and record all new formal-R2 findings in the persistent ledger.

## Adversarial questions

- Can a process be runnable, waiting, timed, or exited in contradictory combinations?
- Can any wakeup enqueue a process twice or be lost?
- Can selective receive reorder or lose unmatched messages?
- Does per-sender ordering survive every enqueue path?
- Can stale PIDs, dead sends, process-slot reuse, or initial-process exit violate lifecycle rules?
- Can sender memory be released immediately after every successful send?
- Do reduction yields preserve fairness under spawn/send/receive-heavy workloads?
- Are process, mailbox, message, and timer limits overflow-safe and deterministic?

## Required work

1. Model process state transitions explicitly and test every legal and illegal edge.
2. Stress deterministic round-robin fairness, process exit, stale identities, selective receive, and timer/message races.
3. Inspect every mailbox and timer ownership path for leaks, duplicate ownership, and dangling process references.
4. Verify copied message graphs share no sender-owned mutable storage.
5. Run the aggregate regression suite plus concurrency stress tests under deliberately tiny limits.

## Exit criterion

No lost/duplicate wakeup, message, timer, or process ownership is found; lifecycle/state invariants are documented and enforced; fairness and ordering tests pass; and message ownership remains independent of sender lifetime.

## Not in scope

Process-local GC implementation, binaries, multicore schedulers, or lock-free queues.

## State note (formal gate)

The formal review ran (STATUS.md task R2-T01): four adversarial lenses covering every question
above, one real defect found and fixed (R2-F15, the deferred milestone-06 deadline-on-teardown gap,
closed with an executable regression rather than only an inspection argument), one dead-field
finding deferred with a named destination milestone (R2-F16), and four NEEDS-TEST gaps closed with
new regressions (R2-F17 through R2-F20) in `tests/process/r2test.c`. See `../docs/review-findings.md`
for exact citations and evidence.

Per each adversarial question:

- **Contradictory state combinations?** No -- `state` is a plain `int` written only by guarded,
  mutually exclusive transition functions; `scanning` implies `state == Prrunning`. `hasdeadline` is
  not state-guarded by construction, but every path that can make a slot dead or reused now clears it
  (R2-F15), and every other reader gates on `state == Prwaiting` first.
- **Lost or duplicate wakeup?** No -- there is no separate run queue; "wakeup" is a single-`int` state
  assignment, idempotent by construction, and the R2-F05 post-exhaustion race guard is confirmed
  present on both `nvprocrecvwait` and `nvprocrecvwaitdeadline`.
- **Selective receive reorder/lose messages?** No, including the previously-untested mid-scan-append
  case (R2-F17).
- **Per-sender ordering?** Yes, trivially: `nvprocsend` is the only enqueue path in the codebase.
- **Stale PIDs / slot reuse / initial-process exit?** No live defect; `lookup()`'s live-state filter,
  generation retirement at `~0UL`, and the root `isroot` generation check all hold.
- **Sender memory released after send?** Yes, including the previously-untested
  send-then-immediate-return-then-reap case (R2-F18).
- **Fairness under spawn/send/receive-heavy load?** Yes -- the dispatch sweep is structurally total
  (every slot visited exactly once per sweep), so no starvation is reachable; the previously-untested
  below-cursor slot-reuse case is now covered (R2-F20).
- **Overflow-safe and deterministic limits?** Yes for every arithmetic path actually enforced
  (mailbox accounting, `valuesize`, deadline addition, frame counter, slot growth); `NvLimits.maxduration`
  itself is not enforced at all (R2-F16, deferred, not a safety defect since ignoring it just falls
  back to the compile-time ceiling).

Required work items 1 (state invariants documented) and 4 (copied-graph independence) are closed by
inspection plus the existing/new regressions above. Item 3 (mailbox/timer ownership paths) is closed
by R2-F15's fix. Items 2 (fairness/lifecycle stress) and 5 (tiny-limits stress) are **partially**
implemented: `tests/process/r2test.c`'s `belowcursorfairness` and `tinylimits` cover the core
boundaries (process-count/slot-reuse, mailbox-byte, frame-depth, term-depth, and one below-cursor
fairness case), but the fairness investigator's fuller stress designs (a dedicated spawn-storm
reduction-floor test; `maxmessage`-exact-boundary; tiny `maxduration` pending R2-F16;
whole-aggregate-suite-under-tiny-limits) were not implemented and are recorded in
`../docs/review-findings.md` rather than only in a since-discarded scratch file.

**Exit criterion status: met.** The user ran `mk tests` and `rc tests/run.rc` and confirmed a complete
pass, including every new R2 regression in `tests/process/r2test.c`. Every finding above is now
`closed` except R2-F16, which is correctly `deferred` (a named, owned deferral is a valid final state,
not an open item). R2 is `complete` in `STATUS.md`; milestone 08 (Memory) is the next available
forward-feature track.
