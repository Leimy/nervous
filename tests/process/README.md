# Process core tests

Build and run:

```text
mk processtest exectest schedtest iotest r2test
rc tests/process/run.rc
```

The core harness (`ptest`) covers opaque PID identity and stale generations, unique Refs, configurable process limits, FIFO/per-sender enqueue order, copied message ownership, oldest-first selective receive, unmatched-message preservation, clause selection, and dead-PID send behavior. The execution harness (`exectest`) verifies that finite reduction quanta yield without fault and resume persistent call/register state. The scheduler harness (`schedtest`) covers process-opcode host wiring, receive/timeout scheduling, fairness, and lifecycle accounting. The io harness (`iotest`, D054-D057) verifies that a process calling `print` is scheduled -- same dispatch and completion counts -- identically to one that does not, using a `/dev/null` output sink so the check needs no pipe and cannot trip the 9front closed-pipe write note.

The `r2test` harness compiles real Nervous source and checks lifecycle, mailbox, timer, fairness, and guard regressions. Its D071 / post-R2-F01 `guardboundaries` case combines branchy function, match, and receive guards, tuple equality, `and`/`or`/`not`, faults from non-boolean operands, and calls after guards have ended. It runs the same source with quanta of 1 and 1000, asserts that the single-step run actually suspends inside a guard, and checks the identical final result `${12, 'a}` and preservation of the oldest rejected message. Its dispatch loop has a finite test bound so a regression cannot silently hang this case.

The `cli/` fixtures are driven by the top-level `tests/run.rc`, not by this directory's `run.rc`: `fault`, `exit`, and `deadlock` pin the three D046 root-outcome diagnostics byte-for-byte; `orphan` (R2-F22) pins their precedence -- a root fault with a child still blocked reports `fault ...` first and the orphaned-process count second, rather than plain deadlock; the `print`/`eprint`/`io-error` fixtures pin D054-D057 output.