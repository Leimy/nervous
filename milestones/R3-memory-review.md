# Review R3 - Memory and Representation Review

## Purpose

Challenge process-local GC, message fragments, term representation, and binaries after milestones 08 and 09, before parallel schedulers make memory bugs nondeterministic.

## Adversarial questions

- Is every pointer-bearing term recognized by the collector at every safe point?
- Are all registers, frames, receive state, timers, process metadata, and message fragments rooted correctly?
- Can one process collection inspect or mutate another process heap?
- Can fragment merge, process exit, or failed allocation leak or double-free terms?
- Are deep/wide/cyclic host structures handled without C stack or size-accounting failure?
- Do PID/Ref opacity and binary representation remain distinct?
- Are all size arithmetic and allocation limits overflow-safe?

## Required work

1. Audit term tags/layout and root enumeration against every opcode and suspended process state.
2. Add heap poisoning/stress tests where practical and tiny-heap collection tests.
3. Test repeated send/receive/collect/exit cycles and allocation failures at each ownership transition.
4. Measure memory against live-data size in long-running workloads.
5. Re-run all prior suites with frequent collection forced.

## Exit criterion

Long-running tests have memory proportional to live data; no cross-process heap pointer exists; all roots and ownership transitions are documented and tested; and no unresolved high-severity memory finding remains.

## Not in scope

Generational or concurrent GC, large shared-binary optimization, multicore scheduling, or distribution.
