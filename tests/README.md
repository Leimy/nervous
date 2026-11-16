# Tests

Tests are organized by the milestone and semantic contract they exercise.

## One-command M10 checkpoint

From any directory:

```rc
rc /usr/dave/work/nervous/tests/checkpoint.rc
```

The runner builds the **working-tree** CLI, tests and benchmarks (no install),
then runs, sequentially:

1. The full regression suite in normal, GC-stress inline, and GC-stress
   off-process modes. Each suite already includes the multicore fixtures at
   `-p 1/2/4`; C fixtures keep their own explicit GC settings.
2. `bench/shards.rc smoke`, checking the same-build global/sharded controls.
3. The small benchmarks and an exact-count comparison with
   `bench/runs/T01-small.txt`, if present. Cost flags need human review;
   they do not fail the run.
4. Three captures of `bench/run.rc multi` (ring, sieve and CPU/GC shape at
   `-p 1/2/4`), each with a `benchcmp` table. These are measurements, not
   additional golden-output tests. Review against the immediate previous
   checkpoint and retain historical T01 as a separate comparison;
   scheduling-dependent count differences are not automatic failures.

It prints progress and creates `bench/runs/checkpoint-<pid>/`, containing
`summary.txt`, a start timestamp, and a separate combined stdout/stderr log
for every step. The first failed command stops the runner with a nonempty
exit status, prints its log, and preserves all evidence. No `tee` pipeline
can hide a failed command. An optional single argument chooses a **new**
results directory (relative to the invoking directory); its parent must
exist. Existing directories are refused, never overwritten. Inherited
`nervous_gcstress` and `nervous_gcoffload` do not select the test modes.

Send the printed results-directory path for review. Run on an otherwise idle
machine for useful timings. The already-recorded full shard comparison is
not repeated here. The reviewed checkpoint-779643 closes T02's functional
run, not R4 or its performance follow-up. The runner does not add the missing
deterministic idle-window fixture or test interactive DEL/no-orphans. There is no external
watchdog: a scheduler deadlock can still hang a step; the last `RUN` line
identifies it. A missing final `PASS automated checkpoint` is not success.

The first complete user run, `bench/runs/checkpoint-20674/`, passed all
regression modes and shard smoke checks; the coordinator inspected the logs.
The small comparison had no count changes but four cost flags, retained for
review rather than called a performance pass. See
`bench/checkpoint-results.md` for measurements and remaining scope. The
runner's failure paths have not been deliberately fault-injected.

## T02 additions (passed checkpoint-779643)

The same checkpoint command now includes these tests through the existing
process and memory suite executables; no new manual invocation is needed:

- `ptest`: indexed timer growth past eight, ordered firing, reverse-index
  invariants, re-arm without stale accumulation, cancellation/exit/reuse,
  wake/steal/reblock, both timer/send orders in the deferred-wake window,
  and configured maxduration boundaries.
- `schedtest`: timeout eligibility with a permanently runnable peer at
  quanta 1 and 10, both lock domains; tie order and timer storage accounting;
  real scheduler procs with a shared logical clock at N=1/2/4.
- `offloadtest`: expiry for the collecting process itself, no premature
  dispatch, no duplicate enqueue, and message-first rescan after GC.

Checkpoint-779643 passes these additions and the full suite in all three
invocations; the raw logs were inspected. See `bench/T02-results.md`.
Checkpoint-20674 predates these changes and is the pre-T02 comparator.
Performance follow-up remains open: the small suite has 12 timing flags
against T01, and is 6.3..9.5% slower than checkpoint-20674. The heap's new
per-slot index and retained array capacity also change footprint figures.
The no-deadline fast path avoids clock reads; no new heap-sizing policy was
introduced. These tests do not replace the outstanding idle-window/R4 checks.

## Individual suites

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
