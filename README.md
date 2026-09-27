# Nervous

Nervous is an experimental concurrent language and portable 64-bit virtual machine for 9front.

## How to read this repository

Minimum context for making progress, in order:

1. `README.md` (this file).
2. `STATUS.md`: current state, task ledger, and resumption checklist.
3. `docs/architecture.md`: how the components fit together (overview, not normative).
4. `docs/semantics.md`: the normative language contract.
5. `docs/bytecode.md`: instruction set and verification rules.
6. `docs/questions.md`: unresolved semantic questions by owning milestone.
7. The active milestone file under `milestones/`, and the `docs/decisions.md` records it cites.
8. `docs/format.md` if touching the parser or formatter.

`docs/README.md` indexes every file in `docs/`. The design-rationale files (`docs/language-semantics.md`, `runtime.md`, `distribution.md`, `language-philosophy.md`, `future-work.md`) explain why the language is shaped as it is. They are split from the deprecated `nervous_design.md` and still use superseded syntax, so read them for motivation, not for rules.

Read `COORDINATION.md` only when more than one agent is working. Do not read `docs/review-findings-archive.md`, `docs/review-05.md`, or `nervous_design.md` for forward work; they are historical records, kept for audit and for the rare regression investigation into closed work. `docs/review-findings.md` is short and worth a glance only if a review gate is open.

Full map:

- `README.md`: project map and milestone order.
- `COORDINATION.md`: multi-agent roles, ownership, handoff, and integration protocol.
- `STATUS.md`: active coordinator, task assignments, write ownership, and milestone state.
- `docs/README.md`: index of `docs/`, marking which files are normative.
- `docs/architecture.md`: component overview with pointers to source and decisions (not normative).
- `docs/semantics.md`: compact normative language contract.
- `docs/decisions.md`: compact decision records (D001-D080).
- `docs/language-semantics.md`, `docs/runtime.md`, `docs/distribution.md`, `docs/language-philosophy.md`, `docs/future-work.md`: design rationale split from `nervous_design.md`. They are not normative and still use superseded syntax; see each file's header note.
- `docs/questions.md`: unresolved semantic questions by owning milestone.
- `docs/format.md`: canonical formatting contract and current limitations.
- `docs/bytecode.md`: instruction set, verification, and VM semantics.
- `docs/review-findings.md`: persistent adversarial-review findings, severity, ownership, and disposition, for gates still open to new entries.
- `docs/review-findings-archive.md`: findings from permanently closed gates (currently R1), relocated out of `review-findings.md` to keep it short. Read only when investigating a regression in already-closed work.
- `docs/review-05.md`: completed milestone 05 adversarial-review record, code/evidence map, and scope boundaries.
- `milestones/`: one bounded implementation context per file.
- `nervous_design.md`: **deprecated** — historical reference only; read the files above for normative specifications.

Implementation source lives under `cmd/`, `lib/`, and `include/`. Tests and examples live under `tests/` and `examples/`.

## Milestone order

1. `milestones/00-bootstrap.md` - repository skeleton and build conventions.
2. `milestones/01-frontend.md` - lexer, parser, AST, and AST printer.
3. `milestones/02-bytecode.md` - symbolic bytecode, verifier, and disassembler.
4. `milestones/03-interpreter.md` - one-process register VM.
5. `milestones/04-patterns.md` - transactional exact matching and clauses.
6. `milestones/R1-foundation-review.md` - consolidate execution, compiler ownership, regressions, and documentation before process integration expands.
7. `milestones/05-processes.md` - cooperative processes, copied messages, and selective receive.
8. `milestones/06-timeouts.md` - timers and receive deadlines.
9. `milestones/R2-concurrency-review.md` - adversarial process, mailbox, timer, fairness, and lifecycle review.
10. `milestones/07-io.md` - basic host output (`print`/`eprint`) through the existing host-callback boundary; no general FFI.
11. `milestones/08-memory.md` - process-local heap and garbage collection.
12. `milestones/09-binaries.md` - initial binary construction and matching.
13. `milestones/R3-memory-review.md` - adversarial GC, roots, fragments, representation, and binary ownership review.
14. `milestones/10-multicore.md` - `rfork` schedulers and work movement.
15. `milestones/R4-multicore-review.md` - mandatory ownership, wakeup, migration, and shutdown review within multicore acceptance.

Distribution, maps, floats, links, monitors, general FFI, code replacement, and static analysis are intentionally outside this sequence. D053 explains why basic I/O does not need general FFI and is in scope as its own milestone instead.

## Current position

Milestones 00-08 and reviews R1-R2 are complete. Milestone 08 (process-local heaps and garbage collection, including off-process collection for large live sets) closed with M08-T04d: `docs/decisions.md`'s D061-D075 record the settled design, from tagged-term representation through the final `gcoffload` default (0, never off-process -- a real crossover was measured but only bracketed, not located, so no positive default is supported by the evidence; see D075 and `bench/README.md`). The latest benchmark evidence is in `bench/README.md`.

Milestones 00-09 and reviews R1-R3 are complete. R3 (memory and representation) closed with six findings, all fixed with regressions: D080 charges term-traversal work (`==`, `print`, boundary copies) to reductions at 8 visits each, with an optional `-w` ceiling, so no instruction holds the scheduler for free; the closing suite run under `nervous_gcstress=1 nervous_gcoffload=1` exposed that off-process idle-sweep collections had never collected anything (R3-F05, D074 amendment), fixed alongside a teardown use-after-free of the completion semaphore (R3-F06). The three `run.rc` invocations under "Inline garbage collection" are the regression bar. `milestones/R3-memory-review.md` carries the coverage map and the properties R4 inherits.

Milestone 10 (Multicore: `rfork` scheduler processes, per-scheduler run queues, work movement at safe points, with the mandatory R4 review inside its acceptance) is next and not yet started. `STATUS.md` is authoritative for current assignments and resumption.

## New-coordinator handoff

Read `STATUS.md`, `milestones/10-multicore.md`, and `docs/questions.md` ("Milestone 10") before continuing. D068/D074 (heap ownership and the collector protocol), D080 (work charging) and D070 record what milestone 10 builds on; D061-D079 the memory and binary designs beneath them. All completed-task write sets are released. Obtain user go-ahead and assign exact paths before starting implementation. Confirm source-control state with the user; a supplied commit message does not prove a commit. R2-F16 remains deferred to milestone 10.

## Try it

Build the command and test programs with `mk tests` (object files and binaries are ignored by git; `mk clean` removes them). Format a source example with `./nervous -f examples/arithmetic.nv`, compile it to verified symbolic bytecode with `./nervous -c examples/arithmetic.nv`, or execute it with `./nervous -r examples/arithmetic.nv main`. `-c`'s output is a module in the "Symbolic Bytecode v0" text format (`docs/bytecode.md`) and can be saved to a file; `./nervous -X saved.nvbc main ...` loads and verifies that file and runs it under the same process scheduler as `-r`, letting a compile step be timed once and separately from any number of subsequent runs. This differs from `-b`/`-x`/`-t`, which also load a saved bytecode file but run it through the standalone single-process executor with no host callback table installed, so a program that spawns, sends, receives or does I/O (anything using processes, like the ring/waiters/sieve examples below) faults `bad_process_context` under `-x`/`-t` and must use `-X` instead. The process example `examples/pingpong.nv` demonstrates `self`, `spawn`, `!` (copied send), selective receive, and `exit` at source level; run it through the cooperative scheduler with `./nervous -r examples/pingpong.nv main`. `./nervous -r examples/rpc.nv main` adds Ref-correlated request/reply and preservation of an earlier unmatched response. `./nervous -r examples/hello.nv main` demonstrates milestone 07's host output: it prints `'hello_world` during execution, then `'ok` as the final root value. `examples/sieve.nv` uses `if`, processes-as-data, and tail-recursive message loops; calls in tail position replace their frame (D047, `docs/semantics.md`), which is what lets a receive loop run indefinitely under the frame limit. `examples/ring.nv` (message passing around a ring), `examples/isolation.nv` (a child faults; siblings and root continue, and `after` detects the lost reply), and `examples/ioserver.nv` (I/O as a Ref-correlated message exchange with a device process) each demonstrate one runtime goal; see `examples/README.md`.

`nervous -s -r file main` adds scheduler statistics on stderr after the run (wall time, processes, dispatches, reductions, messages and nanoseconds per message, host allocation high-water mark, and per-process live heap samples after GC); `bench/run.rc` uses it to time the ring, idle-waiter, and sieve shapes, and `bench/README.md` records the baseline numbers.

Source written in the previous syntax (`fn name { ${..} => body; }`, `send(...)`, `spawn(...)`) is converted with `./nervous -F old.nv > new.nv`; see D058 in `docs/decisions.md`.

## Inline garbage collection

Execution now collects automatically at instruction boundaries. `-H words` sets the per-process budget for used heap objects, adopted fragments and retained frame-stack capacity; 0 (the default) is unlimited. `-G` forces a collection at every allocating-instruction reservation. Both apply to `-r`, `-x` and `-t`; collection itself consumes no reductions. For example:

```rc
./nervous -s -H 4096 -r examples/ring.nv main
./nervous -G -r examples/rpc.nv main
rc tests/run.rc
nervous_gcstress=1 rc tests/run.rc
nervous_gcstress=1 nervous_gcoffload=1 rc tests/run.rc
```

The three `run.rc` lines are the regression bar (R3, `milestones/R3-memory-review.md`): normal execution, every reservation collecting inline, and every real collection running in a forked collector proc. All three must pass before a change is accepted.

Missing, empty or `0` means normal execution; `1` enables stress, and `-G` also enables it. Other nonempty environment values produce a diagnostic naming `nervous_gcstress`. The repaired runners use `rfork e` to preserve the inherited setting in a private environment. The environment setting makes CLI invocations in the suite use stress mode; C fixtures retain their explicit settings, and the automatic-memory fixture runs both normal and stress configurations. `rc tests/memory/run.rc` isolates collector and reservation/retry tests. This inline checkpoint is accepted (D072); off-process collection (D074) is also implemented, accepted (M08-T04c), and policy-tuned (M08-T04d, D075).

`-o words` (D074) sets the off-process collection threshold: a heap with at least `words` used-plus-adopted words collects in a separate forked proc instead of inline, so one large collection pauses only its own process. `0` (the default) never offloads; `1` forces every real collection off-process. Only `-r`/`-X` build a scheduler, so unlike `-H`/`-G` this has no effect under `-x`/`-t`. `$nervous_gcoffload` supplies the default the same way `$nervous_gcstress` does for `-G`. The default stays `0`: M08-T04d measured a real crossover (off-process hurts at a 50000-word live set, helps at 500000) but only bracketed it, not located it, so no positive default is supported by the evidence (D075, `bench/README.md`). Every test and benchmark in this tree hardcodes `gcoffload = 0` at its own `NvLimits` construction, independent of this CLI default either way.

`-w visits` (D080) caps the node visits one traversing instruction (`==`, `print`/`eprint`, the root `return`/`exit` copy, `spawn`'s argument copy) may make; a traversal beyond it faults that process with `system_limit`, the same way the depth ceiling does. `0` (the default) is no cap. Independently of the cap, those instructions always charge one reduction per 8 visits, so `nervous -s` reduction totals for programs heavy in `==` are slightly higher than before D080. Like `-o`, this applies to `-r`/`-X` only.

## Working with multiple agents

Before assigning work, claim the coordinator role in `STATUS.md` and follow `COORDINATION.md`. Every task must have a bounded objective, dependencies, acceptance checks, and an exclusive canonical write set. Workers do not edit shared status, decision, header, or build files unless those paths are explicitly assigned.

Milestones and mandatory review gates are integrated in order. A review gate may pause an active capability milestone when accumulated risk is recognized. Parallel work is permitted only for disjoint files behind settled interfaces; one coordinator reviews and builds all handed-off work before acceptance.

## Document rule

Keep each milestone self-contained and small. Record a new cross-cutting semantic decision in `docs/decisions.md`; record implementation details only in the milestone that owns them. Do not grow this README into a second design document.
