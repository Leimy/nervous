# Milestone 09 - Basic Binaries

## Dependency note

Milestones 04 and 08 must complete before binary ownership integration. Review R3 follows this milestone and gates multicore work.

## Goal

Exercise the pattern compiler and allocator with deterministic binary construction and exact left-to-right binary matching.

## Read first

- `../docs/semantics.md`
- `../docs/questions.md`, section "Milestone 09"
- `04-patterns.md`
- `08-memory.md`

## Initial feature set

- Integer segments with explicit widths.
- Unsigned big-endian default.
- Explicit signed/unsigned and big/little modifiers.
- Sized byte-aligned binary segments.
- A final unsized `/binary` remainder.
- Whole values and `/binary` segments byte-aligned.

Arbitrary-width integer segments may be supported if they do not force full unaligned bit-string storage.

## Semantics

- Segments are processed left to right.
- A size may use an enclosing binding or a variable bound by an earlier segment.
- No binary-pattern backtracking occurs.
- A pattern without a final remainder must consume the complete value.
- Tentative segment bindings obey the same transactional rules as all other patterns.

## Required tests

- Exact match and trailing-byte rejection.
- Empty and nonempty remainders.
- Length-prefixed payload.
- Endian and signed decoding.
- Invalid forward size reference rejected by the frontend.
- Late failure rolls back earlier segment bindings.
- Allocation and size-limit failures are controlled.

## Exit criterion

Binary construction and matching are usable for a small length-prefixed protocol and share normal process-heap ownership.

## Not in scope

General unaligned bit strings, shared large-binary storage, compression, text semantics, or external buffer ownership.
