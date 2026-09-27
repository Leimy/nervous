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

## Coverage map (written at the end of the review; evidence, not proof)

Each required-work item mapped to what actually asserts it. Produced from a read-only Sonnet audit of the fixtures (`docs/review-findings.md`, leg R3-A3), checked by the coordinator, with the gaps it found closed in the same pass. "Suite" means `rc tests/run.rc`.

| Item | Evidence | Status |
|---|---|---|
| 1 tags/layout and root enumeration against every opcode and suspended state | Audit legs R3-A1 (bin* opcodes) and R3-A2 (root table: every non-register location classified rooted / immediate / dead; mid-scan collection; idle sweep; off-process collector scope; rollback; table realloc), both with coordinator verification against `gc.c`/`process.c` | done, documented in the findings ledger |
| 2 tiny-heap collection | `gctest.c:scratchpaths` (64-word `Minspace` trial, forced restore and scratch promotion); `autotest.c:requests` (`maxheap` 64 fails the initial stack charge; 65/129 exercise the exact boundary); `autotest.c:receivetake` (65 vs 67 words decides adoption); `autotest.c:guardlimit` (exhaustion inside a guard is clause failure) | covered |
| 2 stress | `NvLimits.gcstress` in every `autotest` case that has a stress loop; `nervous_gcstress=1 rc tests/run.rc` as a required suite invocation | covered |
| 3a send copy fails, nothing enqueued | `ptest.c:mailboxlimits` now saves `mailboxwords`/`head`/`tail` before each `mailbox_full` refusal (aggregate boundary, oversized message, `NvNil`) and asserts all three unchanged after | covered (added in R3) |
| 3b recvtake reservation and budget failure | `autotest.c:receivetake`: after a failed reservation the candidate is still `scan`/`head` with `adopted == nil`; the budget failure faults `system_limit` with the fragment still queued | covered |
| 3c collection failure during merge | `gctest.c:failures`: `NvTermlimit` with `exhausted`, roots bit-identical, `cur`/`words`/`adopted` unchanged | covered |
| 3d exit frees heap, adopted and queued fragments | `autotest.c:receivetake` snapshot after exit: `nexec`, `heapbytes`, `stackbytes`, `mailboxbytes`, `adoptedbytes` all zero; `r2test.c:sendthenexit` for queued-at-exit; Plan 9 pool checks would trip a double free under the suite | covered by accounting; no dedicated double-free fixture (judged not worth one) |
| 3e spawn argument copy fails | `autotest.c:spawncopyfail`: child stack floor 512 + 400-word copy > `maxheap` 700 while the root's 465 fits; spawner faults `system_limit`, `nlive == 0`, snapshot `nexec == 0`, under stress 0 and 1 | covered (added in R3) |
| 3f root return / exit reason copy fails | `exectest.c`: depth ceiling refused at `return` (`result == nil`) and at `exit` (`exitreason == nil`); D080 work ceiling refused at `return` (`worktests` 4a) | covered (exit half and nil checks added in R3) |
| 4 memory proportional to live data | `autotest.c:churnflat`: fixed 21-word live set churning 9 words of garbage per iteration under gcstress; heap capacity sampled every step after `Settleafter` collections must never change, and is bounded; live set returned intact after >= 20 collections. `bench/largelive.c`/`bench/README.md` T04p high-water corroborate at scale as measurements, not assertions | covered (added in R3) |
| 5 all suites under forced collection | Required invocations: `rc tests/run.rc`; `nervous_gcstress=1 rc tests/run.rc` (inline, every reservation); `nervous_gcstress=1 nervous_gcoffload=1 rc tests/run.rc` (every real collection in a forked collector proc). The CLI runner checks the environment reached the binary (`tests/cli/run.rc`). `offloadtest.c` covers the off-process ownership/wakeup/teardown mechanism deterministically with hold points | covered; the third invocation was a one-off before R3 and is now a documented requirement |
| cross-process heap pointer impossible | Structural: every boundary deep-copies (`nvfragcopy` at send/return/exit, `nvheapcopy` at spawn), the collector classifies by `cur`/`full`/`adopted` ranges only (R3-A2), `gctest.c:fragments` proves a message survives its sender's heap being freed | documented and tested at the unit level |
| deep/wide host structures | `gctest.c:shapes` (10000-deep chain collected iteratively), `ptest.c:termdepth`/`chain` (copy past the ceiling refused) | covered |
| PID/Ref/binary opacity | `ptest.c:main`, `gctest.c:kinds`, `exectest.c:bintests` | covered |
| size arithmetic overflow | `gctest.c:failures` (`~0ULL` stack words refused), R3-F02 guard in `nvfragcopy`, `nvbinwords` and `spacecap` guards | covered |
| latency isolation (third leg) | M09-T05 measurement, R3-F04, D080, `exectest.c:worktests`, `bench/latency.rc` before/after rows | covered; the property R4 inherits is stated in D080 |

## Exit statement

Written when the coverage map above was completed; the gate closes when the user's run of the three suite invocations passes on the build that contains the R3 additions (`spawncopyfail`, `churnflat`, the `mailboxlimits` and `exectest` additions, D080, the `VISIT` macro, the R3-F03 cleanup).

- Memory proportional to live data: asserted deterministically by `churnflat` (capacity settles and never moves across >= 20 collections with a fixed live set), corroborated at scale by the T04p and largelive measurements.
- No cross-process heap pointer: structural (deep copy at every boundary; the collector's address classification never reaches another heap), audited in R3-A2, unit-tested in `gctest.c:fragments`.
- Roots and ownership transitions documented and tested: the R3-A2 root table is the documentation; transitions 3a-3f each have an asserting fixture named above.
- No unresolved high-severity finding: R3-F04 (latency) closed by D080 with measurement; R3-F02 closed; R3-F03 (cosmetic) closed; R3-F01 closed in M05; R3-F05 (off-process idle collections were no-ops -- found by the closing run's third invocation, the one this review made mandatory) and R3-F06 (teardown semrelease-after-free) fixed with regressions, closing on the rerun.

Note for the record: R3-F05 was found by exactly the mechanism this review added -- the off-process suite invocation had been run once before, manually, and happened to pass; making it a requirement and running it under load is what exposed a bug that had been in the D074 implementation since M08-T04c. The D074 lifecycle tests were checking the *protocol* (owner state, fold, wakeup) and never asked whether the collection had happened.

What R4 inherits from R3, stated once: (1) a heap is touched by exactly one party at a time and the party is recorded on the heap (`owner`), the D068/D074 protocol; (2) no instruction runs for free beyond its reduction charge -- traversal work is charged exactly and deterministically (D080), so per-scheduler latency is a function of the quantum, not of data shape; (3) the three suite invocations above are the regression bar, and every one of them must pass unchanged with more than one scheduler.

## Not in scope

Generational or concurrent GC, large shared-binary optimization, multicore scheduling, or distribution.
