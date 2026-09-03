# Milestone 05 - Processes and Selective Receive

## Dependency gate

Review R1 must complete before scheduler integration, process bytecode, or source process syntax continues. The existing PID/Ref, mailbox, selective-receive, and resumable-execution cores may be reviewed and consolidated during R1.

## Goal

Run many lightweight Nervous processes cooperatively on one scheduler with copied asynchronous messages and selective receive.

## Read first

- `../docs/semantics.md`
- `../docs/decisions.md`, D035 through D046
- `../docs/review-findings.md`, R1-F07 and R2-F01 through R2-F03
- `../docs/review-05.md` for the current adversarial-review handoff
- `04-patterns.md`

## Scope

- Process table with stale-PID protection.
- Runnable and waiting process states.
- `spawn`, `self`, `make_ref`, send, and exit.
- Per-process mailbox.
- Message copy into a self-contained fragment.
- Oldest-first selective scanning according to the settled ordering rules.
- Cooperative scheduler with finite reductions.

No Plan 9 `rfork` schedulers are introduced here.

## Key invariant

Only the scheduler executing a process mutates that process's execution state. Mailbox insertion must not create pointers into another process's private heap.

## Required tests

- Ping/pong and RPC correlation with a Ref.
- Many spawned processes make progress.
- Unmatched messages remain in order for later receives.
- Clause order applies within each candidate message.
- Settled per-sender ordering guarantee.
- Send to dead or stale PID follows the chosen semantics.
- A sender may terminate and release its heap without invalidating queued messages.
- Reduction exhaustion yields rather than terminating the process.

## Exit criterion

A deterministic single-scheduler runtime can execute concurrent source programs and selective receive without shared process-heap pointers.

## Current implementation state

Active after the scheduler/process-op slice. D035 through D043 settle mailbox ordering, per-sender order, dead/stale PID sends, VM lifetime, opaque PID/Ref identity, configurable resource limits, lifecycle transitions, scheduler ownership/order, and the portable process-instruction host boundary. Generation retirement, copied FIFO mailboxes, oldest-first selective receive, checked dispatch/yield/wait/wake transitions, resumable execution, deterministic rotating-slot scheduling, and host-backed `self`, `makeref`, `send`, and `spawn` bytecode are implemented. Complete. D044-D048 and the full aggregate suite pass. Final independent adversarial re-review verified every first-review closure and recommended acceptance. Clean-gate regressions also pass bounded source nesting, distinct tuple-depth exhaustion, deterministic spawn diagnostics, clause-form blocking receive, receive outcomes across quanta 1-4, and formatter idempotence. Every milestone 05 finding is closed. Exact aggregate heap accounting and arbitrary malformed host-graph hardening remain explicitly owned by milestone 07. See `../docs/review-findings.md`, `../docs/review-05.md`, and `../STATUS.md`.

## Not in scope

Timeouts, GC compaction, scheduler procs, work stealing, links, monitors, remote PIDs, or mailbox indexing.
