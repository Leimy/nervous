# Milestone 05 Adversarial Review Handoff

This is the compact handoff for an independent review of milestone 05. The first external review was received in `/tmp/nervous-review.txt` and dispositioned as R1-F11 through R1-F14 (now in `docs/review-findings-archive.md`, R1 is a permanently closed gate), R2-F04 through R2-F07, and R3-F01 (both still in `docs/review-findings.md`). STATUS is authoritative for which fixes still await execution. Read this after `README.md`, `COORDINATION.md`, and `STATUS.md`. The long `nervous_design.md` is historical context, not the normative contract; use `docs/semantics.md`, decisions D035-D048, and the milestone document when they differ.

## Review state

Milestone 05 is complete. The final independent re-review in `/tmp/nervous-rereview.txt` verified every first-review closure and recommended acceptance. The complete aggregate suite also closes clean-gate findings and evidence gaps R2-F08 through R2-F13. Use `docs/review-findings.md` for exact closure evidence and retain this document as the completed review handoff.

## Implemented contract

- PIDs are opaque slot/generation values; stale identities do not alias reused slots, and generation-exhausted slots retire.
- Refs are opaque incarnation/counter values.
- Messages are copied at send into receiver-owned mailbox values. Current byte charging is the provisional D040 host-representation estimate, not the later GC fragment format.
- Mailboxes preserve FIFO arrival and per-sender order. Selective receive scans candidates oldest-first and clauses in source order, preserving unmatched candidates.
- Process states are explicit: free, runnable, running, waiting, exited, and retired. Only a running process may execute receive operations.
- One deterministic scheduler dispatches rotating slots for finite quanta. Normal return, VM fault, and explicit exit reap only that process.
- Portable process bytecode uses host callbacks rather than exposing scheduler pointers or process-table layout.
- Source forms are `self()`, `make_ref()`, `spawn(worker, args...)`, `send(pid, message)`, `receive { ... }`, and `exit(reason)`.
- `nervous -r source entry [args...]` compiles, verifies, and schedules source. It prints a normal root result, fails on root fault or explicit exit, and reports idle live processes as deadlock. Child failures remain isolated.

## Primary code map

```text
include/nvproc.h       process, mailbox, lifecycle, and limits interface
lib/process.c          process table, PID/Ref, message copying, receive scan ownership
include/nvsched.h      scheduler state and retained root outcome
lib/sched.c            dispatch, host callbacks, blocking, reaping, root reporting
include/nvexec.h       resumable executor and host boundary
lib/exec.c             process and receive instruction execution
include/nvbc.h         bytecode instruction contract
lib/verify.c           operand, control-flow, and definite-initialization checks
lib/compile.c          source lowering, including receive retry control flow
lib/parse.c            current source grammar
lib/format.c           canonical process-source formatting
cmd/nervous/main.c     `-c` source compilation and `-r` scheduler execution
```

## Evidence map

```text
tests/process/ptest.c          mailbox accounting, PID/Ref identity, FIFO,
                               selective order, lifecycle, dead/stale sends,
                               and the D047 term-depth boundary
tests/process/exectest.c       finite-quantum yield, resumable execution, and
                               per-execution call-depth exhaustion
tests/process/schedtest.c      host operations, receive block/wake/rescan/take,
                               explicit exit, root outcome retention, fairness,
                               completion, fault isolation, many-process progress
tests/pattern/compiletest.c    source process lowering and malformed forms
tests/frontend/process.nv      canonical source syntax fixture
tests/bytecode/receive.nvb     receive instruction round trip
tests/bytecode/bad-receive-*   verifier-negative receive fixtures
tests/run.rc                   aggregate suite plus executable arithmetic,
                               concurrent ping/pong, Ref-correlated RPC, and
                               exact fault/exit/deadlock CLI diagnostics
```

Build with `mk tests`. Run the actual regression programs with `rc tests/run.rc`; `mk tests` compiles but does not execute the scripts.

## Known acceptance gaps, not hidden claims

1. No known milestone 05 defect or acceptance-evidence gap remains; R2-F08 through R2-F13 are aggregate-verified and closed.
2. Exact per-process and aggregate heap byte accounting remains milestone 07 work; D039/D047 no longer claim it for milestone 05.
3. Iterative traversal of arbitrary malformed host-created values remains milestone 07 hardening; milestone 05 bounds all values admitted through language construction/copy APIs.
4. Timeout syntax, timers, and message/deadline races belong to milestone 06 and are intentionally absent.
5. Process-local heaps and GC-owned message fragments belong to milestone 07. Milestone 05 uses recursively copied host `NvValue` graphs and D040 accounting.
6. The single scheduler has no multicore wakeup, migration, or work stealing; those belong to milestone 09.
7. An intentionally nonterminating runnable program keeps the CLI running. There is no command-wide reduction or wall-clock limit in D046.

Item 1 records the closed acceptance state. Treat items 2-7 as scope boundaries unless the implementation already violates safety or its documented contract.

## High-value adversarial questions

- Can any allocation failure leave a process live without an executor, lose an owned term, double-free a message, or corrupt root outcome reporting?
- Can process-table growth during nested spawn invalidate any pointer other than the parent pointer already reacquired by the scheduler?
- Can malformed but verifier-accepted receive control flow call begin/next/take/wait in an illegal phase or expose an uninitialized candidate?
- Can a quantum boundary during an active receive scan reorder, skip, or consume the wrong message after another sender appends?
- Can wakeup be lost between scan exhaustion and transition to waiting in the single-scheduler model?
- Does every successful send remain valid after immediate sender exit and cleanup, including nested tuples, atoms, PIDs, and Refs?
- Are mailbox and message size computations overflow-safe for malformed or deeply nested host values?
- Can root PID slot reuse cause a child outcome to be mistaken for the root after the root is reaped?
- Does CLI cleanup remain correct for compile failure, scheduler-init failure, root-spawn failure, deadlock, fault, explicit exit, and normal completion?
- Do compiler receive-clause bindings remain tentative until complete pattern success and local to the selected body across every failure edge?

## Reviewer output

Record each concern with severity, exact file/function, violated decision or invariant, a minimal reproducer or proof argument, and required evidence. Classify it as a defect, documentation mismatch, missing acceptance evidence, or out-of-scope proposal. Do not edit coordinator-owned files during a read-only review. Return findings to the coordinator for entry in `docs/review-findings.md`.
