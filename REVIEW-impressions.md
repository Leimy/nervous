# Nervous: Design and Implementation Impressions

## Scope

This is an initial assessment, not a completed adversarial review or a formal milestone gate. It is based on the current milestone 08 stage-2 implementation, with the stage-3 collector design recorded but not yet implemented.

Reviewed material included README and STATUS, the semantic contract and decision log, the term representation header, value/interpreter/scheduler/verifier implementation, concurrency regression tests, the test runner, benchmark notes, and examples including `ioserver.nv` and `isolation.nv`.

`mk tests` succeeded without diagnostics. This was an incremental build, not a clean rebuild. The regression scripts were not executed during this review; `rc tests/run.rc` remains a user-run check. Benchmark numbers below are recorded project measurements, not measurements independently reproduced during this review.

No source files were changed.

## Overall assessment

Nervous is a coherent language/runtime experiment, with a stronger foundation than the "experimental" label might suggest. Its best feature is that the implementation choices serve a clear goal: lots of isolated, communicating processes, without exposing host machinery to programs.

The architecture deserves continued investment. The next challenge is not adding more language features, but demonstrating that its isolation model holds up under memory pressure, expensive terms, and slow host operations.

## Strengths

### A clear language identity

Erlang-style processes, selective receive, transactional patterns, immutable terms, and proper tail calls fit together naturally. The surface syntax is approachable without hiding the concurrency model.

`examples/ioserver.nv` is particularly convincing: it expresses a useful Ref-correlated request/reply abstraction using a small set of primitives. It demonstrates what the language is for better than an arithmetic example alone could.

`examples/isolation.nv` is also useful because it shows both the benefit and the current limit of fault isolation: a worker can fail without taking down its siblings, but without monitors its caller observes only a missing reply.

### Explicit ownership in the implementation

Ownership is visible in the code, not merely asserted in the design document. The division between heap terms, queued fragments, adopted fragments, and exported results is understandable.

In `lib/sched.c`, details such as reacquiring process-table pointers after execution and separately owning root and exit results show care around lifetime boundaries. These are the kinds of small implementation rules that make later concurrency work tractable.

### Tests aimed at real failure modes

`tests/process/r2test.c` exercises important boundary cases:

- Stale deadlines after process exit and slot reuse.
- A timed receive followed by a plain receive.
- Appending a message during an active receive scan.
- A copied message surviving immediate sender exit.
- Run-queue fairness independent of slot number.
- Exact resource-limit boundaries.
- Faulting guards preserving unselected messages.

The injected clock and single-instruction scheduling are good tools for making these cases deterministic. The source-to-runtime fixtures also test integration rather than relying exclusively on hand-written bytecode.

### Evidence-driven performance work

Testing both ring directions exposed a genuine scheduling pathology rather than merely producing a flattering microbenchmark. The FIFO run queue addresses that pathology directly.

The recorded progression from roughly 1.5-2 us to roughly 300 ns per message is encouraging. More importantly, the benchmark notes explicitly identify the current memory figures as including uncollected garbage, rather than presenting them as a footprint improvement.

That honesty should remain part of the benchmarking discipline as GC lands.

## Concerns and recommended investigations

### 1. Instruction fairness is not latency isolation

In `lib/exec.c`, an instruction costs one reduction, but structural equality, copying, and printing can do substantial work inside a single instruction.

The tagged representation makes this more important. Repeatedly constructing a tuple of the form `${x, x}` creates a small shared graph whose expanded tree is enormous. The recursive traversals in `lib/value.c` do not preserve or memoize that sharing. Comparing two independently built, structurally equal graphs of this shape can take exponential work relative to their stored size, even below the depth limit.

Message word limits bound the expansion accepted by fragment copying, but they do not solve the general problem of expensive equality or printing. Root return and explicit exit also invoke fragment copying with an effectively unlimited word budget in the current interpreter.

Separately, `print` and `eprint` perform synchronous host I/O. A blocked output stream therefore blocks the sole scheduler, not merely the printing actor. Wrapping that callback in a language-level device process does not remove the host-level blocking.

These are not arguments against the architecture. They are reasons to distinguish three different properties:

- **Fault isolation:** one actor's failure does not terminate unrelated actors.
- **Memory isolation:** one actor's heap lifetime does not invalidate another's data.
- **Latency isolation:** one actor cannot monopolize the scheduler through expensive runtime work or a blocking host operation.

The first two have a clear implementation story. The third needs more qualification and measurement.

Recommended tests and measurements:

- Equality between independently built shared tuple graphs.
- Copying and printing shared, deep, and wide terms.
- Small-message hop latency while another actor performs expensive term operations.
- Behavior when output is slow or blocked.
- Controlled failure when an expanded result exceeds a practical export budget.

Possible later mechanisms include work-sensitive reduction charging, resumable traversals, bounded exports, or host-I/O offloading. Measurements should determine which mechanisms are justified; this review does not propose implementing all of them now.

### 2. The collector is the next real proof point

The representation work is promising, but without GC, long-running message loops accumulate garbage. That is a deliberate and honestly documented stage-2 limitation, not an accidental omission.

The instruction-boundary reservation design is a good direction. It reduces the number of partially executed states the collector must understand and makes safe points explicit.

The implementation sequence should first establish the inline collector and exhaustive stress mode, then validate off-process collection. The latter avoids concurrent mutation of the collected heap, but still introduces:

- Ownership publication between Plan 9 procs.
- Completion and wakeup ordering.
- Teardown while collections are in flight.
- Failure to create a collector proc.
- Host-process resource pressure when many collections overlap.
- Interaction between collection completion and runnable/deadline state.

The existing plan deliberately introduces this ownership protocol before multicore scheduling. That is defensible, provided the inline path remains a simple reference implementation and the off-process path is measured rather than assumed to improve latency.

Acceptance should emphasize bounded memory proportional to retained data, correct roots under forced collection, and the effect on unrelated actors' latency, not only aggregate throughput.

### 3. Revisit the verifier with hostile bytecode

There is a suspicious mismatch between compiler-generated code and accepted hand-written bytecode around guards.

`lib/verify.c` validates a `guard` target, but does not enforce the compiler's assumption that guards cannot call or nest. `lib/exec.c` keeps the guard failure target on the execution rather than on an individual frame. The implementation's safety argument therefore depends on constraints that the verifier does not currently appear to establish.

This is an investigation target, not a reproduced failure. Before treating verification as a strong safety boundary, test bytecode that:

- Calls a function while guard mode is active.
- Faults in that callee.
- Nests guards or reaches `guardend` without a corresponding entry.
- Jumps into or out of a guard region.
- Returns or tail-calls while guard mode is active.

The important question is whether every accepted bytecode control-flow shape is safe, not merely whether the compiler emits well-structured guards.

### 4. Compact documentation is drifting from the implementation

The decision log is useful, but readers currently have to reconcile too many versions of the truth.

Examples observed during this review:

- README describes milestone 08 as next, while STATUS describes it as active.
- `docs/semantics.md` still describes mailbox limits in bytes rather than words.
- That document says negative literals both are and are not patterns.
- It describes receive timeouts as future milestone work even though they are implemented.
- Some resource-accounting and term-depth descriptions still refer to the old representation or milestone numbering.

For a project that relies heavily on explicit contracts, keeping the compact semantic document current matters more than preserving detailed milestone narration in several places.

Recommended documentation discipline:

- Keep STATUS authoritative for operational progress.
- Keep `docs/semantics.md` authoritative and current for the compact language contract.
- Use decisions for rationale and explicit supersession.
- Keep historical implementation narration in archives rather than requiring readers to reconstruct the present from it.
- Update the compact contract in the same change that alters a semantic rule or accounting unit.

## Suggested priorities

1. Finish GC and exact accounting, validating inline collection before off-process collection.
2. Run the complete regression suite under both required GC stress configurations.
3. Add adversarial shared-term workloads and measure interference with small-message actors.
4. Investigate guard control flow at the raw-bytecode verification boundary.
5. Reconcile the compact semantic contract and README with the current implementation.
6. Build one slightly larger example with meaningful state and a sustained lifetime, beyond ring and sieve workloads.

Keep the language small while doing this. The project already has enough expressive power to expose the important runtime questions.

## Bottom line

The interesting question is no longer whether these primitives can be implemented. It is whether their clean isolation model survives memory pressure, expensive terms, and slow host operations.

The current structure gives Nervous a good chance of answering that well.
