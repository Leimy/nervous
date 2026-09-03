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
