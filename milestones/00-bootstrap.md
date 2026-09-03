# Milestone 00 - Bootstrap

## Goal

Create a boring Plan 9 project skeleton that can build an empty or minimal executable with `mk` and gives later milestones stable places to put code.

## Read first

- `../README.md`
- This file

No full design read is required.

## Scope

- Choose command names and source directories.
- Add the initial `mkfile` using normal Plan 9 conventions.
- Add one minimal command that prints a version or usage line.
- Establish C formatting, naming, error-handling, and header conventions.
- Establish locations for unit-style C tests and source-language golden tests.

## Proposed layout

```text
cmd/nervous/       compiler/runner command
cmd/nvdis/         bytecode disassembler, introduced in milestone 02
lib/               shared implementation
include/           internal public headers if needed
tests/frontend/    source and expected AST/errors
tests/vm/          bytecode and expected traces/results
examples/          small Nervous programs
docs/              compact cross-cutting documents
milestones/        bounded work plans
```

Do not create unused source trees merely to make the repository look complete; create each as its first file is added.

## Deliverables

- A conventional `mkfile`.
- A minimal buildable command.
- A short `CONTRIBUTING.md` or equivalent conventions file only if conventions exceed what the source makes obvious.
- Successful `mk` from the repository root.

## Completion test

A clean checkout can run `mk` without generated files already present. The resulting command starts and reports a stable usage or version string when the user runs it manually.

## Not in scope

Lexer, parser, bytecode, VM state, scheduler, GC, or speculative portability layers.
