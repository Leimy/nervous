# Symbolic bytecode tests

Build the working-tree executable, then run the automated suite from any directory:

```text
cd /usr/dave/work/nervous
mk all
rc tests/bytecode/run.rc
```

To test another executable, pass its path:

```text
rc tests/bytecode/run.rc /path/to/nervous
```

The runner uses a private directory under `/tmp`, removes it on success or failure, and exits nonzero at the first failed assertion.

For each fixture under `valid/`, it checks that `nervous -b` succeeds without diagnostics and reproduces the canonical input byte for byte.

For each fixture under `invalid/`, it checks that `nervous -b` exits unsuccessfully, writes no standard output, and produces the exact paired `.err` diagnostic. Cases cover:

- incorrect constant kind;
- non-contiguous instruction labels;
- a direct read of an uninitialized non-argument register;
- initialization missing from one join predecessor;
- a back edge that must not initialize function entry;
- reachable fallthrough from a function.

## Guard boundary regressions (D071, post-R2-F01)

The `guard-*` fixtures exercise the raw-bytecode verifier independently of the source compiler's guard restrictions. The runner discovers them through its existing directory globs; no build or runner changes are needed.

Invalid cases cover:

- A guarded call to a faulting one-instruction callee, carrying a caller failure target that would be outside the callee's instruction range (`guard-call`). This is the cross-frame safety regression; verify it with `-b`, do not execute it with an old binary.
- Tail calls, returns, explicit exits, output, sends, and receive consumption inside guards.
- Nested guards, unmatched `guardend`, and an unclosed region.
- Jumps into a body or closing delimiter, escape without `guardend`, and crossing between two distinct guard bodies.
- A conditional test escaping its region and both plain/timed receive retry edges entering a region.
- Fault targets within the same region or another guarded region.
- An unreachable guarded call, proving structural checks do not depend on reachability.
- Reading a register at a guard's failure target that is initialized only inside the guard, preserving definite-initialization safety.

Valid cases cover:

- `guard-branch.nvb`: local branches, tests, tuple construction, type tests, and explicit `fail` within a guard; branches may meet at its closing delimiter.
- `guard-sequential.nvb`: a fault target that enters the next guard with guard mode off, and normal completion of that second guard.
- `guard-loop.nvb`: backward edges within a guard and reentry through `guard` after closing it. This is a verification-only fixture and intentionally loops when executed.
- `guard-skip.nvb`: jumping over an entire well-formed unreachable guard.
- `guard-call-after.nvb`: an empty guard and a normal call after its closing delimiter.

The valid fixtures are round-trip checks, not runtime checks. For a small optional execution check with the rebuilt binary:

```text
./nervous -x tests/bytecode/valid/guard-branch.nvb main
# expected: 'true
./nervous -x tests/bytecode/valid/guard-sequential.nvb main
# expected: 2
```

`rc tests/run.rc` also runs the existing source-level guards and scheduler/receive-guard regressions. Compilation alone does not establish that these behavioral checks passed.
