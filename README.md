# Nervous

Nervous is an experimental concurrent language and portable 64-bit virtual machine for 9front.

## How to read this repository

Minimum context for making progress, in order -- about 60 KB total, most of it `decisions.md`:

1. `README.md` (this file).
2. `STATUS.md` -- current state only, short by design.
3. `docs/decisions.md` -- normative; the settled design. Skim the headings, read the decisions the current milestone touches.
4. `docs/semantics.md` -- the compact language contract.
5. `docs/questions.md` -- only the section for the milestone being worked.
6. The active milestone file under `milestones/`.
7. `docs/bytecode.md` if touching the compiler, verifier, or VM; `docs/format.md` if touching the parser or formatter.

Read `COORDINATION.md` only when more than one agent is working. Do not read `STATUS-archive.md`, `docs/review-findings-archive.md`, `docs/review-05.md`, or `nervous_design.md` for forward work; they are historical records, kept for audit and for the rare regression investigation into closed work. `docs/review-findings.md` is short and worth a glance only if a review gate is open.

Full map:

- `README.md`: project map and milestone order.
- `COORDINATION.md`: multi-agent roles, ownership, handoff, and integration protocol.
- `STATUS.md`: active coordinator, task assignments, write ownership, and milestone state.
- `docs/semantics.md`: compact normative rules shared by early milestones.
- `docs/format.md`: canonical formatting contract and current limitations.
- `docs/decisions.md`: settled decisions and short decision records.
- `docs/questions.md`: unresolved semantic questions, with the milestone that needs each answer.
- `docs/review-findings.md`: persistent adversarial-review findings, severity, ownership, and disposition, for gates still open to new entries.
- `docs/review-findings-archive.md`: findings from permanently closed gates (currently R1), relocated out of `review-findings.md` to keep it short. Read only when investigating a regression in already-closed work.
- `docs/review-05.md`: completed milestone 05 adversarial-review record, code/evidence map, and scope boundaries.
- `milestones/`: one bounded implementation context per file.
- `nervous_design.md`: long-form design, rationale, and future direction. Read it only when a milestone document points to it or a design question needs the original rationale.

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

CLI evidence correction (T04e): empty `nervous_gcstress` incorrectly caused usage, and test runners discarded inherited stress via `rfork E`. Both are now repaired and compile; fresh user-run normal/CLI-stress suites are pending. Earlier passes retain normal and explicit C stress evidence, not full CLI-stress coverage. Feature work remains paused; see STATUS.

Milestones 00-07 and reviews R1-R2 are complete. Milestone 08 is active: verified guard boundaries, automatic process-local inline GC, accounting/stress controls, and the first bounded performance pass are accepted on user-confirmed passing tests. The latest benchmark evidence is in `bench/README.md`; historical pre-GC numbers are not current performance claims.

Implementation is paused at the accepted T04p save point at the user's request. Off-process GC, its ownership/wakeup/teardown tests, and final policy/acceptance measurements remain milestone-08 work. No review gate is currently active; R3 is still future work. `STATUS.md` is authoritative for current assignments and resumption.

## New-coordinator handoff

Read `STATUS.md` and `milestones/08-memory.md`. D061-D073 record the current design; the status lists accepted evidence and planned, unassigned next tasks. All completed-task write sets are released. Obtain user go-ahead and assign exact paths before starting implementation. Commit/push status has not been independently established; a supplied commit message does not prove a commit. R2-F16 remains deferred to milestone 10.

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
```

Missing, empty or `0` means normal execution; `1` enables stress, and `-G` also enables it. Other nonempty environment values produce a diagnostic naming `nervous_gcstress`. The repaired runners use `rfork e` to preserve the inherited setting in a private environment. The environment setting makes CLI invocations in the suite use stress mode; C fixtures retain their explicit settings, and the automatic-memory fixture runs both normal and stress configurations. `rc tests/memory/run.rc` isolates collector and reservation/retry tests. This inline checkpoint is accepted; off-process collection and final memory-policy measurements remain future milestone-08 work. See STATUS for the implementation pause and planned continuation.

## Working with multiple agents

Before assigning work, claim the coordinator role in `STATUS.md` and follow `COORDINATION.md`. Every task must have a bounded objective, dependencies, acceptance checks, and an exclusive canonical write set. Workers do not edit shared status, decision, header, or build files unless those paths are explicitly assigned.

Milestones and mandatory review gates are integrated in order. A review gate may pause an active capability milestone when accumulated risk is recognized. Parallel work is permitted only for disjoint files behind settled interfaces; one coordinator reviews and builds all handed-off work before acceptance.

## Document rule

Keep each milestone self-contained and small. Record a new cross-cutting semantic decision in `docs/decisions.md`; record implementation details only in the milestone that owns them. Do not grow this README into a second design document.
