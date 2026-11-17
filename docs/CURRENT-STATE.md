# Current State

This file gives the minimal context needed to understand where development is paused and what needs to be done next.

## M10 Multicore Status

**Milestones 00-09 and R1-R3 are complete.** M10 (Multicore) is in progress but paused.

### Completed M10 work:
- T00a (segmented process table, D082): done
- T00b (machine/scheduler split, ownership, counters): done
- T01 (N scheduler procs under global lock, D081): done
- T02 (indexed deadline heaps, D084): **functionally accepted** at checkpoint-779643

### Open M10 items:
1. **Performance investigation**: T02 is ~6-20% slower than pre-T02 at N=1; controlled A/B investigation needed (see `bench/T02-results.md`)
2. **Deterministic idle-window wake fixtures**: M10-T04
3. **Interrupt/no-orphans check**: interactive DEL test
4. **Exit-traffic fragment accounting**: M10-T04
5. **Full T04**: mailbox split
6. **R4**: mandatory multicore review gate

## Key Files to Read First

When resuming work:

1. **`../RESUME.md`** - the handoff summary (what you're reading now)
2. **`../bench/T02-results.md`** - performance evidence and proposed next task
3. **`docs/sharding.md`** - current lock protection map (for scheduler changes)
4. **`docs/decisions.md`** D081-D090 - the M10 design decisions
5. **`docs/semantics.md`** - the language contract

## Current Pause Point

- User stopped after reviewing checkpoint-779643
- T02 is functionally accepted
- **No active assignment** - wait for user authorization
- No runtime/test changes since checkpoint-779643

## Proposed Next Task (NOT authorized)

A bounded performance investigation comparing pre-T02 vs T02 to identify the cause of timing regressions. See `bench/T02-results.md` for details.

## What's Actually Open

| Item | State | Owner |
|------|-------|-------|
| T02 performance investigation | proposed, not authorized | user |
| Deterministic idle-window fixtures | open | M10-T04 |
| DEL/no-orphans check | open | M10 |
| Exit-traffic fragment accounting | open | M10-T04 |
| Full T04 (mailbox split) | open | M10 |
| R4 (mandatory review) | open | M10 |

## How to Validate Changes

After any runtime changes, run:
```rc
rc /usr/dave/work/nervous/tests/checkpoint.rc
```

This builds the working tree, runs all regression modes, and saves labeled logs.

## Documentation Structure

See `docs/README.md` for the full documentation index. The key principle:

- **Normative files** (`semantics.md`, `decisions.md`, `bytecode.md`, `format.md`) are the authoritative contract
- **Design rationale files** explain why things are shaped as they are but may use superseded syntax
- **Historical files** (`STATUS-archive.md`, review archives) are for reference only

## Source Code Map

| Area | Key files |
|------|-----------|
| Frontend | `lib/lex.c`, `lib/parse.c`, `lib/ast.c`, `lib/format.c`, `lib/compile.c` |
| Bytecode | `lib/bytecode.c`, `lib/bcread.c`, `lib/verify.c` |
| Runtime | `lib/value.c`, `lib/process.c`, `lib/pattern.c`, `lib/exec.c` |
| GC | `lib/gc.c`, `lib/alloc.c` |
| Scheduler | `lib/sched.c`, `include/nvsched.h`, `include/nvproc.h` |

See `STATUS.md`'s "Current implementation map" for more detail.

## What This Means for LLM Context

This project has:
- 90+ design decisions (D001-D090) in `docs/decisions.md`
- 15 milestones in `milestones/`
- 5 design rationale files in `docs/`
- Historical archives in `STATUS-archive.md`

**The key simplification**: Most tasks only need a subset. For M10 work, focus on:
- D081-D090 (multicore decisions)
- `docs/sharding.md` (lock protocol)
- `docs/semantics.md` (language contract)
- `bench/T02-results.md` (current evidence)
- The active milestone `milestones/10-multicore.md`

Don't reload the full decision log or design rationale files unless a specific question requires it.
