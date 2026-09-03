# Review R4 - Multicore Adversarial Review

## Purpose

Treat milestone 10 as a semantics-preservation exercise, not merely a parallelism feature. Challenge ownership, wakeup, migration, and shutdown before multicore completion is accepted.

## Adversarial questions

- Can two schedulers ever own, enqueue, execute, mutate, or collect one process simultaneously?
- Can wake-before-sleep, message/timer races, or shutdown lose a runnable process?
- Can migration change PID identity, mailbox order, timer behavior, or process-local heap ownership?
- Can cross-scheduler send observe partially copied messages or exited/reused slots?
- Are locks held across allocation, execution, or blocking operations?
- Does single-scheduler mode produce identical language-visible results?

## Required work

1. State and instrument the ownership transition protocol.
2. Add assertions/counters for duplicate queueing, ownership violations, wakeups, steals, and migrations.
3. Stress adversarial timing with repeated migration, exit, sends, timers, GC, and scheduler sleep/wake.
4. Compare deterministic semantic traces between one and multiple schedulers where ordering permits.
5. Review every lock and semaphore path, including error and shutdown paths.

## Exit criterion

No duplicate ownership, lost wakeup, unsafe migration, cross-heap mutation, or scheduler-dependent semantic result remains; instrumentation supports the claim; and the full aggregate/stress suite passes in one- and multi-scheduler modes.

## Not in scope

Distribution, remote migration, lock-free queues, or scheduler-visible source semantics.
