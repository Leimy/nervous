# Status Archive

This file holds completed `STATUS.md` ledger rows that will never change again:
milestones 00 through 07 and reviews R1 and R2, all in the `complete` state,
their released task rows, and condensed session narrative for work whose
durable record lives in `docs/decisions.md` and `docs/review-findings-archive.md`. Nothing here is deleted, only relocated out of
`STATUS.md` to keep the file every task must read short. Read it only for
audit trail or when a specific old task ID or acceptance summary is needed;
ordinary forward work never needs it, since `STATUS.md`'s milestone ledger
still names these milestones as dependencies by number and states they are
complete.

## Completed milestone ledger rows

| Milestone | State | Dependencies | Acceptance summary |
|---|---|---|---|
| 00 Bootstrap | complete | none | Root `mk` builds; bootstrap accepted |
| 01 Frontend | complete | 00 | Parser, formatter, migration, and comments accepted |
| 02 Bytecode | complete | 01 | Symbolic format, verifier, disassembler, and fixtures accepted |
| 03 Interpreter | complete | 02 | VM, arithmetic, tracing, and finite execution accepted |
| 04 Patterns | complete | 03 | Transactional matching and all three source matching contexts accepted |
| R1 Foundation review | complete | 00-04; early 05 cores | Single dispatch, ownership failure sweep, aggregate regressions, validation, and doc audit accepted |
| 05 Processes | complete | R1, 04 | Final adversarial re-review accepted; all findings and clean-gate evidence closed by aggregate tests |
| 06 Timeouts | complete | 05 | Receive deadlines D048-D052; `after` clause; monotonic lower-bound deadlines; one deferred gap closed by R2-F15 |
| R2 Concurrency review | complete | 05, 06 | Four-lens adversarial gate (R2-T01); R2-F15 fixed, R2-F16 deferred to 10, R2-F17-F20 regressions in `tests/process/r2test.c`; post-gate R2-F21-F23 closed |
| 07 Basic I/O | complete | 05, 06 | `print`/`eprint` via the host-callback boundary (D053-D057); no general FFI |

## Completed/released task ledger rows

| ID | Milestone | Assignee | Role | State | Depends on | Exclusive write set | Objective |
|---|---|---|---|---|---|---|---|
| M00-T01 | 00 | primary-claude-session | coordinator | done | none | released | Bootstrap accepted |
| M01-T01 | 01 | primary-claude-session | coordinator | done | M00-T01 | released | Frontend accepted |
| M02-T01 | 02 | primary-claude-session | coordinator | done | M01-T01 | released | Bytecode accepted |
| M03-T01 | 03 | primary-claude-session | coordinator | done | M02-T01 | released | Interpreter accepted |
| M04-T01 | 04 | primary-claude-session | coordinator | done | M03-T01 | released | Patterns/compiler slice accepted |
| M05-T01 | 05 | primary-claude-session | coordinator | done | R1 | released | Final adversarial acceptance and all clean-gate regressions complete |
| M05-T02 | 05 | claude-coordinator-session-2 | coordinator | done | M05-T01 | released | R2-F14 operator-chain AST depth fixed; regression and aggregate suite pass |
| R1-T01 | R1 | primary-claude-session | coordinator | done | M04-T01; early M05 cores | released | Single dispatch, allocation-failure audit, aggregate regressions, and documentation accepted |
| M06-T01 | 06 | claude-coordinator-session-2 | coordinator | done | M05-T01; M05-T02 | released | Settled timeout semantics as D048-D052 |
| M06-T02 | 06 | claude-coordinator-session-2 (self-executed) | worker | done | M06-T01 | released | `after duration => body`: token, AST, parser, formatter, printer, compiler lowering to `recvdeadline`/`recvwaitdeadline` |
| M06-T03 | 06 | claude-coordinator-session-2 (self-executed) | worker | done | M06-T01 | released | Deadline arming/expiry in `process.c`, dispatch in `exec.c`, verifier rules, clock and idle-wake in `sched.c` |
| M07-T01 | 07 | claude-coordinator-session-3 (self-executed) | coordinator | done | none | released | Settled I/O interface as D054-D057 |
| M07-T02 | 07 | claude-coordinator-session-3 (self-executed) | worker | done | M07-T01 | released | `print`/`eprint` opcodes, host callbacks, CLI wiring, every required test from `milestones/07-io.md` |
| R2-T01 | R2 | claude-coordinator-session-3 (self-executed; four read-only sub-agent investigators) | coordinator | done | M06-T01..T03; R2-F01..F14 | released | Formal R2 gate: R2-F15 fixed, R2-F16 deferred, R2-F17..F20 regressions; user-confirmed full pass |

## Condensed session narrative (milestones 06, 07, R2, and post-R2 work)

Milestones 06 and 07 were each settled as decisions first and then implemented by one coordinator in a single coherent pass rather than split across workers; both were user-confirmed by `rc tests/run.rc`. R2-T01 ran four read-only investigators in parallel (lifecycle/state, mailbox/messages, timers/deadlines, fairness/limits) against `milestones/R2-concurrency-review.md`; every claim was re-verified against source before entering the ledger; the milestone-06 "safe by inspection" deferral turned out half wrong and became R2-F15. Full findings: `docs/review-findings-archive.md`.

Surface syntax v3 (D058) landed as a frontend pass outside the milestone sequence: `fn name(patterns) { }` clause declarations, keyword process forms, `if`, `==`/`!=`; previous syntax survives only as the `-A`/`-F` compat input.

Post-R2 work in one long session, all user-confirmed, all recorded durably elsewhere: tail calls (R2-F21, D047 note); root-fault-before-idle CLI reporting (R2-F22, D046 note); examples `ring.nv`, `isolation.nv`, `ioserver.nv`; the D059 FIFO run queue after a 65535-node ring measured 5826 s (R2-F23; 25x faster after); CLI `maxprocess` 65536; `nervous -s` statistics and `bench/` with baseline numbers in `bench/README.md`; compiler support for unary `-`/`+`, `not`, short-circuit `and`/`or` (D016 note); guards with fault-means-false, five type-test intrinsics, `guard`/`guardend`/`istype` opcodes, and negative-literal patterns (D060); a `default: bad_opcode` arm in `exec.c` after a dropped `case Onop` was caught by `exectest`.
