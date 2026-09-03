# Status Archive

This file holds completed `STATUS.md` ledger rows that will never change again:
milestones 00 through 05 and review R1, all in the `complete` state, and their
released task rows. Nothing here is deleted, only relocated out of
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
