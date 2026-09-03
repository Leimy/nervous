# Tests

Tests are organized by the milestone and semantic contract they exercise.

- `frontend/`: Nervous source paired with expected AST or diagnostics.
- `bytecode/`: symbolic bytecode paired with expected verification, disassembly, trace, value, or fault.
- `vm/`: single-process VM values, faults, arithmetic, calls, and traces.
- `pattern/`: transactional matcher and clause-selection behavior.
- `process/`: PID/Ref identity, copied FIFO mailboxes, and selective receive.
- `language/`: source programs paired with expected values, exits, or deterministic traces.
- `stress/`: long-running scheduler, mailbox, allocation, and GC tests.

Prefer small textual golden cases. Each failure should name the source fixture and show the smallest useful difference.

Build every accepted test executable, then run the aggregate regression suite:

```text
mk tests
rc tests/run.rc
```

`mk tests` only compiles artifacts. `tests/run.rc` executes frontend golden tests, symbolic bytecode, the one-process VM, transactional patterns/compiler lowering, and process/mailbox/resumable-execution suites in that order. Individual suite runners remain available for focused diagnosis.

Tests should not rely on scheduler timing unless the test specifically exercises timing. Concurrency tests should communicate completion through Nervous messages rather than sleeping for guessed intervals.
