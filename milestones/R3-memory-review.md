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
- (Latency isolation, from REVIEW-impressions.md, not yet measured anywhere): can one process monopolize the scheduler or block host output through expensive term work (structural equality and printing on shared graphs built independently, e.g. `${x,x}` chains) or a blocking `print`/`eprint` write? This is the third isolation leg (fault, memory, latency) and the only one without a measurement; R3 owns producing the first numbers for shared-graph equality cost, copy/print of shared deep terms, and small-message hop latency with an expensive-term peer running concurrently, so that R4's multicore acceptance criteria have a defined latency property to preserve rather than an unmeasured impression.

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
