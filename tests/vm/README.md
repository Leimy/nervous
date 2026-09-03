# Single-process VM tests

Build the working-tree executable, then run:

```text
rc tests/vm/run.rc
```

The suite covers constant return, nested calls and moves, tuple construction and access, integer tests, checked arithmetic, signed division and remainder, integer ordering, overflow and arithmetic faults, explicit faults, unresolved function calls, deterministic trace output, verifier rejection before execution, a tail-call loop that reaches the reduction limit without growing the call stack, and `bad_process_context` faults from `print`/`eprint` under the standalone hostless execution path (D053-D057).

Execution commands are:

```text
nervous -x module.nvb entry [integer-or-atom-arguments ...]
nervous -t module.nvb entry [integer-or-atom-arguments ...]
```

Arguments beginning with a quote are atoms; decimal arguments are integers. The arguments are assembled into the semantic tuple placed in register 0. `-t` emits one stable line per dispatched instruction followed by the final value. Execution currently has a fixed one-million-reduction limit.
