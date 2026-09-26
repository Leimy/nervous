# Nervous Architecture

A high-level map of how the pieces fit together, with pointers to the decisions that settle each part. This file summarizes; it does not decide. `docs/semantics.md` is the normative language contract, `docs/decisions.md` (D001-D079) the normative design record, and `STATUS.md`'s "Current implementation map" the file-level map of the source. On any conflict those win.

## System Overview

Nervous is a portable 64-bit register-based abstract machine with Erlang/BEAM-inspired lightweight processes, asynchronous message passing, selective pattern matching, and process-local garbage collection. Today one scheduler runs inside the `nervous` Plan 9 proc; `rfork(RFPROC|RFMEM)` is used only for optional off-process garbage collection (D068, D074). Multiple schedulers sharing memory through `rfork` are milestone 10 (D070).

### Key Components

| Component | Responsibility | Source |
|-----------|----------------|--------|
| **Frontend** | Lexer, parser, AST, AST printer, canonical formatter, previous-syntax (`-F`) converter | `lib/lex.c`, `lib/parse.c`, `lib/ast.c`, `lib/format.c` |
| **Compiler** | AST to symbolic bytecode, including clause, guard, pattern and binary lowering | `lib/compile.c`, `lib/patcompile.c`, `lib/patbc.c` |
| **Bytecode** | "Symbolic Bytecode v0" text format, reader, writer, verifier (`docs/bytecode.md`) | `lib/bytecode.c`, `lib/bcread.c`, `lib/verify.c` |
| **Interpreter** | Register VM with instruction-boundary heap reservation | `lib/exec.c`, `lib/vm.c` |
| **Runtime** | Tagged terms, heaps, fragments, pattern matcher, process table, mailboxes | `lib/value.c`, `lib/alloc.c`, `lib/pattern.c`, `lib/process.c` |
| **Collector** | Per-process Cheney copier, inline or off-process | `lib/gc.c`, `lib/sched.c` |
| **Scheduler** | FIFO run queue, deadlines, collection triggers, host callbacks (`print`, `eprint`, process intrinsics) | `lib/sched.c` |
| **CLI** | `nervous(1)`: format, compile, run from source (`-r`) or saved bytecode (`-X`) | `cmd/nervous/main.c`, `man/1/nervous` |

### Data Flow

1. **Source** -> frontend (parse, format) -> AST
2. **AST** -> compiler -> symbolic bytecode -> verifier
3. **Bytecode** -> interpreter -> terms
4. **Interpreter** <-> scheduler (dispatch, yield, receive wait, collection request)
5. **Interpreter** <-> host callbacks (I/O, process intrinsics; D043, D053)

## Design Principles

1. **Patterns are fundamental.** Clause dispatch, `match`, `receive`, and binary decoding all use one exact, transactional matcher (D001, D003).
2. **Single-assignment variables.** A variable binds once; a bound variable in a pattern tests equality (D002).
3. **Exact matching.** Tuples and binaries match exactly unless a remainder is bound explicitly. Maps, when they exist, will match named keys by presence (D001).
4. **Asynchronous messaging.** Send never blocks; selective receive scans the mailbox oldest-first (D035).
5. **Process-local heaps.** No pointer crosses from one heap into another, so each heap is collected independently (D008, D063, D064).
6. **Portable bytecode.** The instruction set exposes no host C structs, pointers, or Plan 9 descriptors (D010).
7. **Simple synchronization.** Semaphores (`tsemacquire`/`semrelease`) for wakeup (D011), a spinlock `Lock` for one-word heap-owner transitions (D074), and `QLock` for shared queues when multicore needs them. Nothing lock-free until measurement says so.

## Term Representation (D061, D062, D076)

A term is one 64-bit word with a low-bit tag:

- **Immediate:** small integers, atoms (index into an append-only interned table), and PIDs (slot and generation).
- **Boxed:** a pointer to a header word (kind and length) and a body. The boxed kinds so far are tuples, Refs, boxed 64-bit integers, and binaries.

Terms are immutable, so copying a term inside a process copies one word. Equality is structural and type-sensitive (D028). Deep copies happen only at process boundaries: send, spawn arguments, and results or exit reasons handed to the host (D064).

## Scheduler (D041, D046, D050, D059, D074)

- One scheduler owns every language process.
- **FIFO run queue.** Dispatch takes the head, and every transition into `runnable` appends at the tail (D059).
- **Process states:** `runnable`, `running`, `waiting`, `exited`, `retired` (D041).
- **Deadlines** live in process slots. With nothing runnable, the scheduler sleeps until the nearest deadline (D050).
- **Off-process collection.** A heap at or above the `gcoffload` threshold is collected by a forked proc while other processes run. The scheduler skips that process until it folds the completion. The default threshold is 0, which means never (D074, D075).

## Garbage Collection (D063-D069, D072, D074, D075)

- **Algorithm:** breadth-first Cheney copy. The to-space is sized from live data plus the pending need (D069).
- **Collection points.** Allocating instructions reserve their words before they execute. A failed reservation yields `NvCollect` to the scheduler, so the interpreter never collects mid-instruction (D067, D072).
- **Roots:** the active registers of the process's frame stack, and nothing else (D063, D065, D069).
- **Message fragments.** A send copies the message once into a fragment owned by the receiver's mailbox. `recvtake` adopts the fragment onto the receiver's heap, and the next collection merges it (D064).
- **Accounting.** `maxheap`, `maxmessage`, and `maxmailbox` are word counts, checked exactly (D066).
- **Off-process collection:** see Scheduler above. Stress mode `-G` forces a collection at every reservation (D069).

## Binaries (D076-D079)

Binaries are boxed, self-contained byte strings. They are never views or slices.

- **Segments:**
  - `v:W[/signed|/unsigned][/big|/little]`: an integer segment. `W` is a literal 8, 16, 32, or 64 (D078).
  - `v:size/binary`: a sized binary segment; `size` is a byte count.
  - `v/binary`: an unsized binary segment. In a pattern it must be last and binds the remainder.
- **Matching** runs left to right with no backtracking, and a trailing byte is a mismatch. Bad sizes or shapes in matched data are mismatches, not faults (D077).
- **Construction** evaluates all segment operands left to right, then builds the result by appending segments. Construction errors fault with `badarith`, `overflow`, or `bad_binary` (D077, D079).
- **Size** is bounded by the process heap word budget, and binary opcodes are banned in guards (D079).

## Distribution Strategy (not implemented)

- The planned wire encoding hides local atom IDs, PIDs, and Refs behind logical node, incarnation, and generation identities.
- Dispatch stays exact. Payload evolution is meant to go through map field extraction; see `language-semantics.md` Section 7, "Decoding Is Not Dispatch".
- Network process migration is not a v1 promise. External resources are capabilities.

See `distribution.md` for the rationale.

## Not Yet Implemented

- **Multicore schedulers** (milestone 10, D070).
- **Lists, maps/records, floats, `@` whole-value binding.** D020 reserves their delimiters (`[...]`, `#{...}`), but none of them has an implementation.
- **Links, monitors, catch.** A fault ends only the faulting process.
- **Module/name system.** There is one flat function namespace.
- **Bignums.** Out-of-range 64-bit arithmetic faults `overflow` (D007). Bignum promotion is kept compatible for later.
- **Distribution** and a compact binary bytecode encoding.
- **Static analysis** (optional and future).

## Portability

The implementation is 9front-native: `rfork`, `tsemacquire`/`semrelease`, `Lock`/`QLock`, `uptime` for the monotonic clock. The portable part is the contract: source semantics and bytecode never depend on those primitives, host structs, or file descriptors (D010). A port to another OS would reimplement the scheduler, collector-launch, and clock layer, not the language or the bytecode.
