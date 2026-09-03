# Process core tests

Build and run:

```text
mk processtest exectest schedtest iotest
rc tests/process/run.rc
```

The core harness (`ptest`) covers opaque PID identity and stale generations, unique Refs, configurable process limits, FIFO/per-sender enqueue order, copied message ownership, oldest-first selective receive, unmatched-message preservation, clause selection, and dead-PID send behavior. The execution harness (`exectest`) verifies that finite reduction quanta yield without fault and resume persistent call/register state. The scheduler harness (`schedtest`) covers process-opcode host wiring, receive/timeout scheduling, fairness, and lifecycle accounting. The io harness (`iotest`, D054-D057) verifies that a process calling `print` is scheduled -- same dispatch and completion counts -- identically to one that does not, using a `/dev/null` output sink so the check needs no pipe and cannot trip the 9front closed-pipe write note.

The `cli/` fixtures are driven by the top-level `tests/run.rc`, not by this directory's `run.rc`: `fault`, `exit`, and `deadlock` pin the three D046 root-outcome diagnostics byte-for-byte; `orphan` (R2-F22) pins their precedence -- a root fault with a child still blocked reports `fault ...` first and the orphaned-process count second, rather than plain deadlock; the `print`/`eprint`/`io-error` fixtures pin D054-D057 output.