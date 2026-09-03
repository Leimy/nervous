# Nervous Project Status

Current operational state only. Maintained by the active coordinator; workers report changes rather than editing concurrently. Completed ledger rows and past-session narrative live in `STATUS-archive.md`; settled design lives in `docs/decisions.md`; closed review findings live in `docs/review-findings-archive.md`. Nothing an agent needs for forward work should require reading those three.

## Coordinator

```text
name/session: claude-coordinator-session-3
since: R2/milestone-07 boundary (event-labeled; no clock in this environment)
```

## Current milestone

```text
milestone: 08 - Memory (process-local heap and GC)
state: ready (design proposed, not yet recorded as decisions; no code started)
dependencies: R2, 05, 06 -- all complete
last verified build: mk clean && mk tests passes from empty
last verified test run: rc tests/run.rc full pass, user-confirmed, on the build that includes D060 guards
```

Milestone states: `not-started`, `ready`, `active`, `blocked`, `acceptance`, `complete`. Review milestones are mandatory gates; forward feature work pauses until the active review's exit criterion is met.

## Milestone ledger

Milestones 00-07 and reviews R1-R2 are complete; their rows are in `STATUS-archive.md`. Every dependency named below on an archived milestone is satisfied.

| Milestone | State | Dependencies | Summary |
|---|---|---|---|
| 08 Memory | ready | R2, 05, 06 | Tagged terms, interned atoms, per-process copying heap, message fragments merged at take, frame stack, exact accounting (proposed D061-D066, see below) |
| 09 Binaries | not-started | 04, 08 | Binary construction and exact matching |
| R3 Memory review | not-started | 08, 09 | GC roots, fragments, representation, binary ownership gate |
| 10 Multicore | not-started | R3, 05, 06, 08, 09 | Parallel schedulers and work movement; starts from one D059 FIFO per scheduler |
| R4 Multicore review | not-started | 10 | Mandatory part of milestone 10 acceptance |

## Active task ledger

No task is claimed. Next task is M08-T01 (coordinator): record the memory decisions.

## Reserved integration surfaces

Coordinator-owned by default: `README.md`, `STATUS.md`, `STATUS-archive.md`, `COORDINATION.md`, `mkfile`, `docs/decisions.md`, `docs/questions.md`, `docs/review-findings.md`, `docs/review-findings-archive.md`, shared mkfiles, shared public headers.

## Review findings

No finding is open. R2-F16 (`NvLimits.maxduration` dead field) is deferred to milestone 10. R3-F01 is closed. Ledger: `docs/review-findings.md`.

## Integration queue

```text
gate: none active. Milestone 08 is next; R3 opens once 08/09 are far enough along.
baseline: mk clean && mk tests from empty; rc tests/run.rc full pass (user-confirmed).
performance baseline: bench/README.md -- ~1.5 us per ring hop, 24 reductions per hop
  (~65 ns per reduction, mostly malloc), 1.2-1.7 KB per blocked process, flat in
  process count to 32768. These are the numbers milestone 08 is expected to move.
required first reads for milestone 08: README.md, this file, milestones/08-memory.md,
  docs/questions.md "Milestone 08 - Memory", docs/decisions.md D039/D040/D047 (provisional
  accounting that 08 replaces) and the D061-D066 proposal below.
adjacent source for milestone 08: lib/value.c (NvValue representation), lib/exec.c (frames,
  registers), lib/pattern.c, lib/process.c (mailbox fragments, byte accounting),
  include/nvvm.h, include/nvexec.h, include/nvproc.h; every test that hand-builds NvValue.
```

## Milestone 08 design proposal (to be recorded as D061-D066 when accepted)

Agreed in discussion, not yet in `docs/decisions.md`:

- **D061 tagged terms.** One 64-bit word per term. Immediates: small int (~60 bits), atom (table index), pid (slot/generation 32/32). Boxed via header word: tuple, ref, big-int (so 64-bit checked integer semantics are unchanged, D007). Future kinds (binary, bignum, map) are header kinds. Remote pids will be a boxed kind.
- **D062 interned atoms.** Global append-only table bounded by `NvLimits.maxatom`; equality is integer compare; never collected in this milestone.
- **D063 per-process Cheney heap.** Roots: every register of every frame, the receive candidate, exit reason under construction. Collection only at allocation inside the owning process.
- **D064 fragments merged at take.** Send copies once into a mailbox fragment; `recvbegin`/`recvnext` scan in place with no copy; `recvtake` adopts the fragment onto the heap's fragment list, merged at the next collection.
- **D065 frame stack.** Contiguous per-process frame stack; zero mallocs per call.
- **D066 exact accounting.** `NvLimits.maxheap` in words over heap + fragments + frames; `system_limit` on exhaustion after a collection; replaces D040.

Staging to keep the suite green: (1) atom interning alone, measured; (2) tagged terms + frame stack with a bump heap freed at exit, no collection; (3) fragments with in-place scan; (4) the collector and the milestone's required tests.

## Recently completed

All user-confirmed; details in the archive and the decisions log. Tail calls (R2-F21). Root-fault-first CLI reporting (R2-F22). Examples `ring`, `isolation`, `ioserver`. D059 FIFO run queue (R2-F23). `maxprocess` 65536. `nervous -s` and `bench/`. Unary and boolean operators. D060 guards and type tests. Documentation compaction: this file, `docs/review-findings.md`, and the two archives.

## Next coordinator actions

1. Record D061-D066 in `docs/decisions.md` from the proposal above (adjust first if the discussion changes anything), and resolve the corresponding bullets in `docs/questions.md` "Milestone 08".
2. Claim M08-T01 here, then implement in the four stages above, running `rc bench/run.rc` after each and adding rows to `bench/README.md`.
3. Open R3 when 08 and 09 are far enough along.

Two adjacent questions stay open in `docs/questions.md` and block nothing: a command-wide execution bound for `nervous -r` (CLI policy) and R2-F16.
