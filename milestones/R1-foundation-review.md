# Review R1 - Foundation Consolidation

## Purpose

Stop forward feature work long enough to challenge the frontend, bytecode, compiler, interpreter, pattern, and ownership foundations built in milestones 00 through 04 and the early milestone 05 slices.

This is a required gate before process opcodes or source-level concurrency expand the execution surface.

## Adversarial questions

- Can one instruction implementation diverge between `lib/vm.c` and `lib/exec.c`?
- Does every failed compiler allocation leave module, constant, instruction, and binding ownership valid?
- Can malformed or adversarial AST/control flow produce invalid bytecode that escapes verification?
- Are register initialization, pattern failure edges, and tentative bindings sound at every join?
- Do parser, formatter, compiler, bytecode, and VM agree on one syntax and semantic contract?
- Can one command run every accepted regression suite, or can old milestones silently regress?
- Which statements in `nervous_design.md` are superseded and likely to mislead contributors?

## Required work

1. Replace duplicated dispatch with one resumable execution engine; make legacy one-shot execution a wrapper or delete it after callers migrate.
2. Perform line-by-line ownership and error-path review of `compile.c`, execution, value, pattern-lowering, and process-core modules.
3. Add negative/adversarial tests for allocation cleanup, duplicate functions, unresolved calls, register pressure, malformed generated control flow, and unsupported compiler constructs.
4. Add one documented aggregate regression command covering frontend, bytecode, VM, pattern, and process-core suites.
5. Audit compact docs against implementation and mark misleading long-design sections as superseded or historical.
6. Record known debt explicitly; do not silently waive a finding to pass the gate.

## Deliverables

- One instruction dispatch implementation.
- Ownership/error-path review notes with every finding resolved, deferred with an owner/milestone, or proven harmless.
- Aggregate regression runner and current passing result.
- Updated documentation map and stale-design warnings.
- Focused compiler/verifier adversarial fixtures.

## Exit criterion

All accepted behavior passes through one execution engine; the aggregate suite passes; no unresolved high-severity ownership, verifier-boundary, or cleanup defect remains; and every lower-severity finding has an explicit destination milestone.

## Not in scope

New process syntax, timers, GC, binaries, multicore scheduling, optimization, or broad language features.
