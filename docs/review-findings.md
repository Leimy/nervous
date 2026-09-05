# Adversarial Review Findings

This ledger persists review concerns across status rewrites. A finding is closed only by a fix with evidence or a written argument showing it is not a defect. Deferral requires a destination milestone and owner. Review gates apply the severity rules in `COORDINATION.md`.

Findings from a gate whose milestone state is permanently `complete` are relocated to `review-findings-archive.md` to keep this file short; a section left here as a short pointer means the gate is closed for good. Read the archive only when investigating a regression in already-closed work.

States:

```text
open
investigating
fixed-pending-verification
closed
deferred
```

## R1 - Foundation consolidation

Gate complete. All 14 findings (R1-F01 through R1-F14) are closed; full table in `review-findings-archive.md`.

## R2 - Concurrency and lifecycle

Gate complete. Findings R2-F01 through R2-F23 (14 pre-gate, 6 from the formal gate, 3 post-gate defects found by `examples/sieve.nv` and `examples/ring.nv` at scale) are closed except R2-F16, which is `deferred` to milestone 10 with an owner (`NvLimits.maxduration`, see `questions.md`). Full table in `review-findings-archive.md`. Defects found in R2's area from here on take fresh IDs under R3 once that gate opens, or a `post-R2` prefix if found before it does.

## Post-R2 safety preflight

This is a bounded defect fix before M08-T04, not a reopening or completion of R3.

| ID | Severity | State | Owner | Finding | Required evidence |
|---|---|---|---|---|---|
| post-R2-F01 | high | closed | nervous-review-coordinator / PR2-T01 | `nvverify` did not enforce D060 guard structure or the ban on calls inside guards. `NvExec.guardfail` survives a call; a callee fault applies the caller's target to the callee frame, potentially indexing outside its instruction array. Guard nesting and jumps across guard boundaries also bypassed the assumed scope. Established by source inspection, not by executing a crashing program. | D071 implemented in `lib/verify.c`: structural regions, allowed guard instructions, ordinary/exceptional edge checks. Added 5 valid and 21 invalid fixtures with paired diagnostics, plus `r2test.c` source guard coverage at quanta 1 and 1000. `mk -a tests` succeeded without diagnostics; a final `mk tests` also compiled and linked the source-test addition without diagnostics. User reports both `rc tests/bytecode/run.rc` and `rc tests/run.rc` passing on this fix in the resumed session. Accepted; PR2-T01 is done and the collector hold is removed. This is user-supplied behavioral evidence, not an agent-executed test run. |

## R3 - Memory and representation

| ID | Severity | State | Owner | Finding | Required evidence |
|---|---|---|---|---|---|
| R3-F01 | medium | closed | M05/R3 coordinator | Recursive size/copy/free/equality over program-controlled tuple depth could exhaust the host C stack before controlled resource reporting | D047 caps language-created terms at depth 256 and allows a smaller runtime send ceiling; aggregate construction-boundary regression passes; arbitrary malformed host graphs and final iterative representation remain milestone 08 hardening |

## R4 - Multicore

No findings recorded yet. Populate during R4 rather than relying on memory.
