# Milestone 10 - Multicore Runtime

## Dependency gates

Milestones 05, 06, 08, and 09 and Review R3 must complete before multicore integration begins. (Milestone 07, Basic I/O, is not a dependency; it does not touch process ownership, scheduling, or heap representation.) Review R4 is part of milestone 10 acceptance and must complete before milestone 10 can be marked complete.

## Goal

Run the already-correct process machine in parallel across a small number of `rfork` scheduler processes without changing language semantics.

## Read first

- `../docs/decisions.md`
- `../docs/questions.md`, section "Milestone 10"
- `05-processes.md`
- `06-timeouts.md`
- `08-memory.md`

Consult the runtime sections of `../nervous_design.md` for the original synchronization rationale.

## Scope

- Shared-memory `rfork` scheduler processes.
- Per-scheduler run queues.
- `QLock`-protected cross-scheduler operations initially.
- Work stealing or explicit work transfer.
- Safe ownership transitions only at scheduler safe points.
- Idle sleep with `tsemacquire`; wakeup with `semrelease`.
- Timer ownership compatible with scheduler sleep deadlines.

## Central invariant

At every instant exactly one scheduler owns and may execute or mutate a Nervous process. A running process never migrates; only a suspended process changes ownership.

## Required tests

- Parallel CPU-bound processes execute on multiple schedulers.
- Heavy cross-scheduler messaging preserves the milestone 05 ordering contract.
- Repeated migration does not change a PID or corrupt heap state.
- A wakeup issued just before sleep is not lost.
- Timer and message wakeups do not enqueue a process twice.
- Process exit during cross-scheduler traffic is safe.
- Single-scheduler mode retains identical language-visible results.

## Measurement requirement

Collect basic queue, steal, wakeup, migration, and reduction counts before considering lock-free structures. Complexity requires measured justification.

## Exit criterion

Stress tests run with several schedulers without duplicate ownership, lost wakeups, cross-heap mutation, or language-visible scheduler dependence.

## Not in scope

Distribution, remote process migration, lock-free queues, hard-real-time guarantees, or scheduler-aware source semantics.
