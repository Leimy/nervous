# Nervous Architecture

This file provides a high-level overview of the Nervous system's architecture and summarizes the settled design decisions that are most relevant for understanding how the pieces fit together.

## System Overview

Nervous is a portable 64-bit register-based abstract machine with Erlang/BEAM-inspired lightweight processes, asynchronous message passing, selective pattern matching, and process-local garbage collection. It uses Plan 9's `rfork` for multicore scheduling and shares memory between schedulers through `RFPROC|RFMEM`.

### Key Components

| Component | Responsibility |
|-----------|----------------|
| **Language Frontend** | Parser, lexer, AST construction, AST printer, canonical formatter |
| **Compiler** | AST → symbolic bytecode, verification, pattern compilation, binary lowering |
| **Bytecode** | Portable instruction set (symbolic text format for diagnostics) |
| **Virtual Machine** | Register-based interpreter for symbolic bytecode |
| **Scheduler** | Cooperative scheduling, process lifecycle, mailbox management |
| **Garbage Collector** | Copying collector, off-process collection, fragmentation handling |
| **Host Boundary** | `print`, `eprint`, process intrinsics, I/O callbacks |
| **Runtime Library** | Process table, atom table, message fragments, type tests |

### Data Flow

1. **Source** → Frontend (parse, format) → AST
2. **AST** → Compiler (verify, compile) → Bytecode
3. **Bytecode** → VM (execute) → Term values
4. **VM** ↔ Scheduler (dispatch, yield, collect)
5. **VM** ↔ Host (I/O, process intrinsics)

## Design Principles

1. **Patterns are fundamental** — dispatch, matching, and binary protocol decoding all work through patterns
2. **Single-assignment variables** — variables bind once, never reassigned
3. **Exact matching** — tuples, lists, and binaries match exactly; maps/records match by key presence
4. **Asynchronous messaging** — sends don't block; selective receive scans the mailbox
5. **Process-local heaps** — each process has its own garbage-collected heap; collections don't stop other processes
6. **Portable bytecode** — instruction set is independent of host C structs, pointers, or Plan 9 descriptors
7. **Simple synchronization** — `tsemacquire`/`semrelease` for scheduler wakeup; `QLock` for shared queues (until measurements show otherwise)

## Term Representation

Terms are 64-bit words:
- **Immediate values**: small integers, atoms, PIDs (tagged in payload)
- **Boxed values**: pointers to header + body (tuples, Refs, boxed integers, binaries, maps)
- **Tagged term model**: low bits indicate kind; boxed terms have a header word followed by body

Equality is structural and type-sensitive. Deep copies happen only at process boundaries (message sending, exit, spawn).

## Scheduler Design

- **Single scheduler**: one `rfork` process owns all language processes
- **FIFO run queue**: dispatch takes the head, re-enqueues on yield (D059)
- **Process states**: `runnable`, `running`, `waiting`, `exited`
- **Deadlines**: attached to process slots; scheduler blocks on nearest deadline when nothing runnable
- **Off-process GC**: large collections fork a separate collector proc; scheduler folds completion lazily (D074)

## Garbage Collection

- **Copying collector**: breadth-first Cheney copy; live data determines to-space size (D069)
- **Process-local heaps**: each process has contiguous bump-pointer heap; no shared memory
- **Message fragments**: copied once at send, merged at receive (D064); fragments become part of receiver's heap
- **Roots**: all register slots and frame stack (D063); nothing else
- **Off-process**: collections above `gcoffload` threshold run in separate `rfork` proc; default 0 (D075)

## Binary Protocol Support

Binaries are boxed terms with opaque byte storage:
- **Self-contained**: never a view or slice; always copied on send/receive
- **Segment grammar**: integer segments `{:W[/signed][/big|/little]}`, sized binary segments `{/binary}`, unsized remainder `{/binary}`
- **Exact matching**: no backtracking; trailing-byte rejection for non-remainder final segments
- **Construction**: left-to-right evaluation, then append-style allocation (D077)
- **Ownership**: process-local; copied at send, consumed by receive

## Distribution Strategy

- **Wire encoding**: hides local atom IDs, PIDs, Refs; uses logical node/incarnation/generation
- **Dispatch vs field extraction**: tuples dispatch by exact tag/arity; map/record fields use ordinary lookup (Section 7)
- **Version skew**: handled by keeping dispatch exact; payload evolution via map decoding
- **Network migration**: deferred to future milestone; external resources are capabilities

## Key Design Decisions

This section summarizes the settled decisions that shape the architecture:

### D001-D079 (full list in `docs/decisions.md`)
- **Tagged terms** (D061) — word-sized immutable terms, single copy for moves
- **Interned atoms** (D062) — indexed table, machine-local IDs
- **Copying collector** (D063/D067/D068) — per-process heaps, reservation at instruction boundaries, off-process collection
- **Fragment merging** (D064) — message copy at send, adoption at receive, collector merges
- **Frame stack** (D065) — contiguous stack, registers are roots
- **Exact accounting** (D066) — word-based `maxheap`, `maxmessage`, `maxmailbox`
- **Guards** (D060) — clause refinements, fault-means-false rule
- **Off-process collection** (D074) — `rfork` collector procs, completion semaphore, never-spin waits
- **`gcoffload` default** (D075) — 0 (never off-process), measurement-driven
- **Binary matching** (D076-D079) — minimal segment grammar, append construction, sizing reservation

## Not Yet Implemented

- **Multicore schedulers** (milestone 10) — multiple `rfork` processes sharing memory
- **Module/name system** — flat function namespace, no import/qualification
- **Distributed execution** — network process migration, wire encoding
- **Maps/records** — keyed aggregate syntax still undecided
- **Bignum promotion** — 64-bit overflow exits, future bignum term kind
- **Static type system** — only optional Dialyzer-style success typing

## Portability Considerations

The design is intentionally 9front-native but aims to be portable:

- **`rfork`**: Plan 9's fork with shared memory; would need `clone()` on Linux-like systems
- **Semaphores**: `tsemacquire`/`semrelease` exist on 9front; pthreads on Linux
- **`QLock`**: cross-platform spinlock; measurements needed before lock-free structures
- **Bytecode format**: intentionally independent of host structs, pointers, file descriptors

Milestone 10 will need to replace 9front-specific primitives with portable equivalents if the target platform is not Plan 9.
