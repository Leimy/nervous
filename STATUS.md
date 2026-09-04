# Nervous Project Status

Current operational state only. Maintained by the active coordinator; workers report changes rather than editing concurrently. Completed ledger rows and past-session narrative live in `STATUS-archive.md`; settled design lives in `docs/decisions.md`; closed review findings live in `docs/review-findings-archive.md`. Nothing an agent needs for forward work should require reading those three.

## Coordinator

```text
name/session: claude-coordinator-session-4
since: milestone 08 opening (M08-T01, decisions recorded); event-labeled, no clock in this environment
```

## Current milestone

```text
milestone: 08 - Memory (process-local heap and GC)
state: active (stages 1 and 2 of 3 implemented; stage 2 built clean from empty, awaiting the user's
  rc tests/run.rc and rc bench/run.rc before commit)
dependencies: R2, 05, 06 -- all complete
last verified build: mk clean && mk tests passes from empty on the stage-2 tree
last verified test run: rc tests/run.rc full pass, user-confirmed, on the stage-1 tree; the stage-2
  tree has NOT yet had a test run (no shell from the coordinator's seat) -- that is the next step
source control: git, first commit pushed at the end of the post-R2 session; .gitignore covers
  object files (*.[0-9]) and built binaries -- if any were committed before it existed, git rm --cached them
```

Milestone states: `not-started`, `ready`, `active`, `blocked`, `acceptance`, `complete`. Review milestones are mandatory gates; forward feature work pauses until the active review's exit criterion is met.

## Milestone ledger

Milestones 00-07 and reviews R1-R2 are complete; their rows are in `STATUS-archive.md`. Every dependency named below on an archived milestone is satisfied.

| Milestone | State | Dependencies | Summary |
|---|---|---|---|
| 08 Memory | active | R2, 05, 06 | Tagged terms, interned atoms, per-process copying heap, message fragments merged at take, frame stack, exact accounting (D061-D066) |
| 09 Binaries | not-started | 04, 08 | Binary construction and exact matching |
| R3 Memory review | not-started | 08, 09 | GC roots, fragments, representation, binary ownership gate |
| 10 Multicore | not-started | R3, 05, 06, 08, 09 | Parallel schedulers and work movement; starts from one D059 FIFO per scheduler |
| R4 Multicore review | not-started | 10 | Mandatory part of milestone 10 acceptance |

## Active task ledger

| Task | Owner | State | Notes |
|---|---|---|---|
| M08-T01 record D061-D066, resolve questions.md "Milestone 08" | coordinator | done | this session |
| M08-T02 stage 1: interned atoms (D062) | coordinator (sub-agent implemented, coordinator reviewed + audited) | done; user asked to commit it as one change before stage 2 began | `nvatomintern` table in `lib/value.c`; bench row in `bench/README.md`: 24-33% less per message, 10-25% less heap per process. Superseded in shape by stage 2 (atoms are now term words indexing the same table). |
| M08-T03 stage 2: tagged terms, frame stack, fragment mailboxes; bump heap freed at exit, no collector (D061, D062, D064, D065; D066 word accounting for mailboxes only) | coordinator (headers + integration + review); six sub-agents by disjoint file (value.c / exec.c+vm.c / process.c+pattern.c+sched.c / main.c+exectest+ptest / schedtest+iotest+r2test / pattern tests) | implemented, built clean from empty, coordinator-reviewed; awaiting `rc tests/run.rc` + bench + commit | See "Stage 2 as built" below. |
| M08-T04 stage 3: Cheney collector, D066 exact accounting (heap + adopted fragments + frame stack), heap sizing policy, the milestone's required tests | unclaimed | next | `milestones/08-memory.md` "Required tests" and exit criterion. Starts from the notes under "Stage 2 as built". |

Stage numbering changed this session: the old stage 3 ("fragments with in-place scan") collapsed into stage 2, because once a term is a word the mailbox must store a self-contained copy anyway, and that copy *is* the D064 fragment; handing `recvbegin` a pointer into it and having `recvtake` adopt it was less code than copying it into the heap. Without a collector, adoption is just "freed with the heap at exit". So there are three stages, not four.

Stages are sequential: each must leave `mk clean && mk tests` and `rc tests/run.rc` green before the next starts. Constants deferred to measurement (PID payload split, heap initial size and growth) are noted in `docs/questions.md` "Milestone 08" and get recorded as notes on D061/D063 when settled; stage 2 fixed the PID split (30 slot bits, 32 generation bits, `include/nvvm.h`) and a provisional chunk policy (64-word minimum, doubling, 65536-word cap, `lib/value.c`) -- record them on D061/D063 once the bench confirms them.

## Reserved integration surfaces

Coordinator-owned by default: `README.md`, `STATUS.md`, `STATUS-archive.md`, `COORDINATION.md`, `mkfile`, `docs/decisions.md`, `docs/questions.md`, `docs/review-findings.md`, `docs/review-findings-archive.md`, shared mkfiles, shared public headers.

## Review findings

No finding is open. R2-F16 (`NvLimits.maxduration` dead field) is deferred to milestone 10. R3-F01 is closed. Ledger: `docs/review-findings.md`.

## Integration queue

```text
gate: none active. Milestone 08 is active (stage 2 built, stage 3 next); R3 opens once 08/09 are far enough along.
uncommitted: the stage-2 tree (six headers, six lib files, main.c, eight test programs, docs/bytecode.md,
  this file). Build clean from empty; tests and bench NOT yet run. Sequence: user runs rc tests/run.rc,
  then rc bench/run.rc; coordinator adds the stage-2 bench row; commit as one change.
performance after stage 1: ~1.06-1.31 us per ring hop (from 1.5-2.0), 1.1-1.4 KB per blocked process.
performance after stage 2 (bench/README.md, user-run): ~300-350 ns per ring hop (from 1.06-1.31 us
  after stage 1, 1.5-2.0 us at baseline), -70 to -82%; ~12.5 ns per reduction. The `-s` heap figure
  is currently uncollected garbage and not a footprint; the real per-process number returns with the
  stage-3 collector, along with the waiters-10000 cache regression it causes (513 vs 310 ns).
first test run on stage 2 found one real bug, fixed: nvtermequal took the identical-word fast path
  before its depth check, so equality and copy disagreed by one level about the depth ceiling.
  Rerun of rc tests/run.rc pending.
stage-1 notes for later stages: the atom table is process-global and unlocked (fine under D009,
  needs a lock or per-scheduler discipline in milestone 10); nvatomlimit refuses a ceiling below
  the current count, so two runtimes in one host process must agree on maxatom; nvexecinit
  rescans m->konst on every spawn (cheap no-op; a module flag would remove it if it ever shows).
baseline: mk clean && mk tests from empty; rc tests/run.rc full pass (user-confirmed).
performance baseline: bench/README.md -- ~1.5 us per ring hop, 24 reductions per hop
  (~65 ns per reduction, mostly malloc), 1.2-1.7 KB per blocked process, flat in
  process count to 32768. These are the numbers milestone 08 is expected to move.
required first reads for milestone 08: README.md, this file, milestones/08-memory.md,
  docs/decisions.md D061-D066 (the design) and D039/D040/D047 (the provisional accounting
  they replace), bench/README.md (the numbers and where the time goes today).
adjacent source for milestone 08: lib/value.c (NvValue representation), lib/exec.c (frames,
  registers), lib/pattern.c, lib/process.c (mailbox fragments, byte accounting),
  include/nvvm.h, include/nvexec.h, include/nvproc.h; every test that hand-builds NvValue.
```

## Stage 2 as built

Representation (`include/nvvm.h`, read its header comment first): `NvTerm` is one 64-bit word; low two bits tag boxed pointer / small int (62-bit) / atom index / PID (30 slot + 32 generation bits). Boxed header `NvHdr(kind, len)` with kinds `Btuple`, `Bref`, `Bint`; bit 3 of the header is reserved for the stage-3 forwarding mark. `NvNil` (0) is "no term"; `nvtermkind` reports `Vnil` for it. `NvHeap` is a chunk-list bump allocator (`cur`/`full`), plus an `adopted` fragment list and `words`/`maxwords` (D066 basis; 0 = unlimited, which every caller passes today). `NvFrag` is one malloc holding a root word and a body of words with absolute internal pointers; `nvfragwords` = body+1 is the accounting unit. `NvValue` and every `nvvalue*` function are gone.

Interfaces (`include/nvexec.h`, `nvproc.h`, `nvpat.h`, `nvsched.h`): host callbacks take `NvExec *` first and reach the scheduler via `e->host->aux`; `NvExec.host` is a pointer to the scheduler's single shared table (D065). `nvexecinit` gained a `maxheap` argument and copies its argument into the fresh heap. `NvExec.result`/`exitreason` and `NvScheduler.rootvalue`/`lastexit` are fragments, moved by pointer. `nvprocsend` copies once into a fragment that is the mailbox entry; `nvprocrecvbegin/next` return a term pointing into it; `nvprocrecvtake`, `nvprocpop`, `nvprocreceive` hand the fragment to the caller (the interpreter adopts it). `NvLimits.maxmailbox`/`maxmessage` are words; `maxheap` was added. `NvConst.atom` is a `uvlong` term.

Where the time should now go (from the exec.c handoff): per ring hop, heap allocation is one `tuple` per message construction (1+n words) and nothing else in the common path -- `move`, `getelem`, `send`'s result, `return`, pattern bindings, and boolean results are word copies or cached atoms; `call`/`tailcall`/`return` allocate nothing (frame stack reallocs amortized); `send` does exactly one fragment malloc+copy; `recvbegin` copies nothing; `recvtake` links. Remaining known costs worth measuring: `call`/`tailcall` still resolve the target function by `strcmp` over `module->func` on every call (`findfuncidx`; resolve once per `Kfunc` constant like atoms if it shows); `nvheapcopy` mallocs a scratch array per tuple copied (spawn only).

Known stage-2 limitations, all deliberate and all stage-3 work: no collector, so a process's heap grows monotonically until exit (long rings are fine in memory terms only because a node's per-message garbage is a few words; `bench/run.rc large` may not fit); the frame stack is not charged to `maxwords`; `maxheap` is 0 everywhere; heap chunk constants unmeasured. Test-side changes of intent (not just port): `exectest` "term depth" now proves the ceiling at root `return` (was: at `tuple`); `ptest` `termdepth()` now proves it at `nvfragcopy`/`nvtermequal` (was: at `nvvaluetuple`); `ptest` `mailboxlimits()` counts words (an int message is 1 word, a 2-tuple of ints is 4) and sends `NvNil` for the "malformed value" case; `r2test` T5-02 mailbox boundary uses `words = 1`; `schedtest` reads live registers through a local `REG(e,n)` macro over `e->stack`/`e->fp`. No `ok - ...` line that a `run.rc` compares changed; expected-output fixtures under `tests/process/cli` were not touched (term printing is byte-identical by construction; the first `rc tests/run.rc` will confirm).

Coordinator review notes for stage 3: `nvheapexhausted` is a per-heap field (`NvHeap.exhausted`), not a file-static -- keep it that way for milestone 10. `NvTermerror` and allocation failure are both -1 from the copy functions; callers that must distinguish pre-check `NvNil` (documented in `nvvm.h`). `sched.c` gives `rootvalue` an independent copy of a root exit reason and `lastexit` the original.

## Milestone 08 design

Recorded as D061-D066 in `docs/decisions.md`; read them there, not here. Three things changed between the discussion proposal and the record, all from reading the current code: (1) the PID payload is not a 32/32 split -- tag bits make that impossible, so the split is an implementation constant and D041's slot retirement keys off the representation's generation ceiling; (2) with structure sharing, tuple construction no longer traverses, so D047's depth ceiling is enforced at message copy, equality, and print instead of at construction; (3) two implementation rules are stated as load-bearing: within a heap a copy is a word copy (never deep), and no C variable holds a heap pointer across anything that may allocate. Constants (PID split, heap sizes) are deliberately deferred to measurement.

Where the current cost is, for whoever picks up stage 1: one ring hop is ~18 malloc/free pairs and four tree walks of the message -- `recvbegin` deep-copies the candidate, `getelem`/`move`/`return` deep-copy, `Osend` deep-copies the message once for its result register and once into the mailbox after a `valuesize` walk, every `loadk` of an atom and every `atomresult` (`'true`/`'false`) is a `strdup`, and each call or tail call is two mallocs. Stage 1 removes the strdups; stage 2 removes the deep copies and the frame mallocs; stage 3 removes the mailbox-side copies and the size walk.

## Recently completed

All user-confirmed and committed; details in the archive and the decisions log. Tail calls (R2-F21). Root-fault-first CLI reporting (R2-F22). Examples `ring`, `isolation`, `ioserver`. D059 FIFO run queue (R2-F23). `maxprocess` 65536. `nervous -s` and `bench/`. Unary and boolean operators. D060 guards and type tests. Documentation compaction: this file, `docs/review-findings.md`, and the two archives; `README.md` now carries the minimum reading list.

## Next coordinator actions

1. User runs `rc tests/run.rc` on the stage-2 tree; fix anything it finds (coordinator owns the fix, all files are released). Then `rc bench/run.rc`; add the stage-2 row to `bench/README.md`; commit as one change.
2. Stage 3 (M08-T04): Cheney collector over a single contiguous space (replace the chunk list), roots = frame stack, from-space classification by address including adopted fragments, D066 accounting including the frame stack, heap sizing policy, `NvLimits.maxheap` made real in `main.c`, and the required tests in `milestones/08-memory.md`. Same discipline: bench before/after, tests green, user-confirmed, commit.
3. Record the deferred constants as notes on D061/D063 once measured; open R3 when 08 and 09 are far enough along.

Two adjacent questions stay open in `docs/questions.md` and block nothing: a command-wide execution bound for `nervous -r` (CLI policy) and R2-F16.
