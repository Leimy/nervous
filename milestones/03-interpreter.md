# Milestone 03 - Single-Process Interpreter

## Goal

Execute verified bytecode for one process with clear traces and deterministic behavior.

## Read first

- `../docs/semantics.md`
- `../docs/decisions.md`
- `../docs/decisions.md`, D028 through D031
- `02-bytecode.md`

## Scope

- PC, X registers, frames/Y slots as required, and call stack.
- Integer and atom values plus tuples.
- Calls, tail calls, return, branches, construction, and primitive tests.
- Checked arithmetic.
- Stable abnormal-exit reporting.
- Optional instruction trace mode.

A bounded arena or deliberately simple allocator is acceptable. Real process-local GC belongs to milestone 07.

## Work

1. Settle equality, arithmetic, and initial reduction questions. Initial rules are recorded in D028 through D031; arithmetic opcodes are deferred from the first interpreter slice.
2. Implement verified-bytecode loading into VM structures.
3. Implement instruction dispatch and explicit fault paths.
4. Confirm tail calls do not grow the call stack.
5. Add execution tracing suitable for golden tests.

## Required tests

- Constant-returning function.
- Nested calls and register movement.
- Deep tail recursion in constant stack space.
- Tuple construction and element access.
- Checked overflow and division faults if division is included.
- Invalid bytecode rejected before execution.

## Exit criterion

A small compiled or hand-authored program executes to a value or a precise fault, with deterministic disassembly and trace output.

## Current implementation state

Complete. D028 through D031 settle the initial semantics. The VM implements argument-tuple entry in X0, calls, constant-space tail calls, tuples, primitive tests, checked integer arithmetic, signed division and remainder, integer ordering, stable values/faults, deterministic traces, and a finite reduction limit. `nervous -x` executes and `nervous -t` traces hand-authored bytecode. The expanded bytecode and VM suites passed and final source review completed on 2026-08-18.

## Not in scope

Multiple language processes, mailbox state, timeouts, GC, binary terms, or multicore execution.
