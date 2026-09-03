# Milestone 02 - Symbolic Bytecode

## Goal

Define a clear register-machine contract that can represent the early language without committing to the final packed portable encoding.

## Read first

- `../docs/semantics.md`
- `../docs/decisions.md`
- `../docs/bytecode.md`

## Scope

- In-memory instruction and operand definitions.
- Symbolic text form for tests.
- Constant pool for integers, atoms, and function references.
- Control flow, calls, tail calls, tuple construction, and primitive tests.
- Verifier and disassembler.

## Minimal instruction families

```text
load/move
make_tuple
jump
call/tailcall/return
test_atom/test_int/test_equal/test_tuple_arity
get_element
fail
```

The exact spelling is an implementation choice. Pattern-specific failure paths must be explicit enough for milestone 04.

## Verifier minimum

Reject:

- invalid opcodes or operand kinds;
- out-of-range registers and constants;
- jumps outside a function;
- reads of registers not definitely initialized where practical;
- malformed call targets and argument-tuple registers;
- control flow that falls out of a function.

Keep verifier assumptions documented. Do not silently trust compiler output.

## Deliverables

- Bytecode data structures.
- Symbolic reader/writer or fixture format.
- Verifier.
- Disassembler with stable output.
- Compiler lowering for the non-pattern frontend subset, or hand-authored fixtures if lowering is deferred briefly.

## Exit criterion

Valid fixtures round-trip through the symbolic representation, invalid fixtures are rejected predictably, and disassembly is sufficient to diagnose interpreter behavior.

## Current implementation state

Complete. Structures, ownership, canonical symbolic reading and disassembly, structural verification, definite-register-initialization dataflow, fixtures, and the `-b` verification/disassembly command mode are implemented. The automated fixture suite passed and final source review found no acceptance-blocking defect on 2026-08-18.

## Not in scope

Final packed file encoding, GC maps beyond the contract needed later, process opcodes, receive opcodes, binaries, or optimization.
