# Nervous Project Status

Operational source of truth. Settled design is in `docs/decisions.md`; benchmark evidence is in `bench/README.md`; regression coverage is in `tests/memory/README.md`. Older milestone history lives in `STATUS-archive.md` and the review archives.

## Checkpoint and authorization

```text
milestone: 08, 09, R3 COMPLETE; 10 - Multicore IN PROGRESS: T00a, T00b,
  T01 landed and accepted (round 3, all three suites + -p runs +
  benchcmp green, user-run).  See "ROUND 3 RUN RESULTS" below for the
  numbers and "T01 STILL OPEN" for what remains before T02.
checkpoint: M10-T04a correctness tests user-reported passing; full
  bench/runs/shards-first.txt inspected, three repeats + profiled controls
  complete with 'shards: all cases passed'. CPU/GC scaling restored;
  see bench/shards-results.md. Keep this first-slice checkpoint, not full
  T04/R4 acceptance. Interactive DEL/no-orphans check unconfirmed.
  Previous checkpoint: M10-T01 round 3 accepted. Docs brought
  current in the same session: README.md ("Multicore", current position,
  handoff), docs/semantics.md (scheduling paragraph, D086 restatement),
  docs/architecture.md (Scheduler section), docs/README.md,
  docs/questions.md (M10 answered/sharpened), docs/decisions.md (D083 and
  D085 amendments), milestones/10-multicore.md ("M10-T01 -- landed"),
  bench/README.md ("M10-T01"), man/1/nervous (-w, -p, -s rows).
implementation: M10-T04a sharded blocking locks FIRST SLICE RUN,
  user-observed tests green; measured CPU/GC win, retained. User
  authorized trying the split after rejecting
  retries; no libthread conversion or lock-free queue. Local dispatch,
  yield, receive/deadline callbacks, GC mark/fold and owned table walks
  take only NvSched.lock. Structural/cross-owner ops still take machine
  gate + ALL shards ascending, reverse release. No shard-to-global
  upgrade. Reap/kick gaps retain Prrunning/oncpu exclusion. All-shard
  ownership changes and directory growth stabilize routing under any
  local shard. Scans check owner before foreign exec/heap/state reads;
  idle sweep tests offlaunched before collector-mutated heap fields.
  Idle decision now committed while exact snapshot is still protected.
  No quantum, heap policy, FIFO, pendingwake or GC-credit change.
  nvschedinit defaults sharded=1; N=1 still lock-off. Diagnostic
  sharded=0 is the same-build single-QLock control. Counters partition
  LOGICAL protected sections and acquire elapsed time into local/global;
  a global gate is nsched+1 physical QLocks, not one. See
  docs/sharding.md for protection map, proofs, limitations and commands.
  Read-only Sonnet review completed: no new concurrency defect found.
  Coordinator fixed the inherited maxtermwork setter race by taking the
  all-shard gate; clarified quiescent IO/clock/locking configuration and
  memory snapshots. Note-handler lock-free stop word remains R4 scope.
  Changed: lib/sched.c, include/nvsched.h, cmd/nervous/main.c,
  tests/process/schedtest.c (bounded cross-proc independence probe +
  both modes at N=1/2/4), bench/scale.c (optional sharded control and
  counter checks), bench/shards.rc (new same-build comparison),
  docs/sharding.md, bench/scale.md, milestones/10-multicore.md,
  STATUS.md. mkfile unchanged.
  `mk nervous tests benchmarks` clean. User reports requested tests
  passed; suite logs not independently inspected. Full shards capture
  inspected, all three repeats and profile controls pass accounting.
  Four balanced workers, medians global -> sharded: N=2 / 64 words
  1.248 -> 0.549 s; N=4 1.398 -> 0.311 s. With 4096-word workers:
  N=2 0.576 -> 0.419 s; N=4 0.812 -> 0.226 s. Sharded N=4 vs N=1
  lock-off: 2.95x (64 words), 3.58x (4096). Natural-placement 16-worker
  cases also improve. Repeat-1 N=4/64: 646213 global sections becomes
  646201 local + 21 global, with same dispatches/collections/work.
  Evidence and all medians: bench/shards-results.md; source review and
  tests do NOT close R4. DEL/no-orphans check remains unconfirmed.
  All-shard sends/ref/steals/idle remain serialization points; NOT a
  final mailbox split, full T04, or R4 acceptance. T02 still open.
  Prior retry experiment RUN, REJECTED, REMOVED: N=4 / 64 words median
  1.404 s ordinary, 1.570 s 32 tries, 2.094 s 128; zero retry successes
  in all six runs despite ~20M/82M misses. Evidence retained in
  bench/runs/lockretry-first.txt and bench/scale.md. No retry knob.
  Prior scale capture: bench/runs/scale-first.txt; larger heaps gave
  N=2 speedup, N=4 remained limited before this split. New capture
  establishes a CPU/GC win, NOT message-heavy scaling. Final build clean;
  coordinator write set released, no runtime edit on results recording.
  Suggested commit: 'M10-T04a: shard local scheduling and GC; preserve
  all-shard structural gate'. Do not narrow sends/steals blindly: measure
  message-heavy shapes and retain routing/lifetime/termination proofs.
  T02 deadline work and R4 remain open. Sub-agent policy
  revised (see /usr/dave/local_models.md, "Economic Review"): cloud
  Sonnet for bounded read-only audits, coordinator for edits; local
  models only for independent second-opinion review or doc summaries.
active source assignment: none. M10-T04a write set released; build,
  source review, user-observed tests and full benchmark capture green.
  Only documentation changed while recording results.
  The M10-T04a implementation entry above supersedes older task history
  and next-sequence notes below where they describe the stage-1 lock.
next: M10-T00a (segmented process table, D082) is DONE: all three suite
  invocations pass (user-run).  Uncommitted; suggested message
  "M10-T00a: segmented process table (D082)".  bench/run.rc was
  rerun after T00b step 1 (bench/runs/T00b-small.txt, counts identical);
  T00b steps 2-6 are built on top and awaiting the suite (see below).
  T00a lesson: nvprocat is a macro that evaluates its slot argument
  twice; the first build passed `r->nslot++` and the root spawned into
  slot 1 while queued/pid'd as slot 0 (`scheduler: bad_pid`, 0
  dispatches).  Header now warns.  Build with `mk nervous tests
  benchmarks`; `mk tests benchmarks` alone does not relink the CLI
  binary the suite runs first.
  Changed: include/nvproc.h (chunk/nchunk, NvProcchunk=1024, nvprocat
  macro; nalloc/process gone; tablemoves/tablemovebytes retired at 0),
  lib/process.c (growth appends a chunk; nvruntimefree frees chunks),
  lib/sched.c (nvprocat everywhere; tablebytes = nchunk*NvProcchunk*
  sizeof), and mechanical nvprocat conversion in tests/memory/autotest.c
  + offloadtest.c, tests/process/ptest.c + schedtest.c + r2test.c,
  bench/perftest.c + largelive.c + latency.c.  The ONLY assertion values
  that changed are in autotest.c:tablegrowth (nalloc 256/1/2 -> nchunk 1;
  tablegrows 5 -> 1; each with a "was:" comment) and receivetake's
  tablebytes formula.  Acceptance: the three suite invocations pass and
  bench/run.rc within noise.  Then T00b (spec in milestones/10-multicore.md
  "Tasks").  Suggested commit: "M10-T00a: segmented process table (D082)".
  Note the design docs (D081-D089 etc.) are also uncommitted.

M10-T00b partial (local-model evaluation session, see
  /usr/dave/local_models.md "Qwen3-Coder-Next -- Evaluated"): the `-p N`
  parsing slice landed -- `NvMaxsched = 64` enum in include/nvsched.h
  (before the NvSched* state enum); cmd/nervous/main.c gains `-p
  schedulers` in usage, `vlong nsched` (default 1), `case 'p'` validating
  1..NvMaxsched via parseint with a "bad -p" diagnostic, and a post-
  ARGEND rejection of N>1 as "multicore not yet enabled" (exits
  "unsupported").  Drafted by local Qwen3-Coder-Next to an exact spec,
  coordinator-verified line by line; `mk nervous tests benchmarks` clean.
  nsched is not yet threaded into runscheduled -- the rest of T00b
  (machine/scheduler split, owner field, counters, -s rows) is still open
  and should consume it.  Uncommitted.
  T00b step 1 -- NvSched split, BUILT, NOT YET RUN.  include/nvsched.h
  now has `struct NvSched` (coordinator-written) holding every per-proc
  field that previously lived on NvScheduler: currentslot/generation/
  valid, dispatches, reductions, timerwakes, collections, gcfailed,
  last/maxlivewords, gcinput/outputwords, gcdemand, gcidle,
  gcoutstanding, gcofffallback, gcsem, gchold, gclaunched, gccredits,
  gcidlestep, profile, execns, gcns, spawnns, plus `int index`.
  NvScheduler keeps runtime/module/host/clock/io/quantum/maxtermwork,
  completed/faulted/exited, root* and lastexit/lastfault, and gains
  `NvSched **sched; int nsched;` (1).  nvschedinit mallocs sched[] and
  sched[0] (D074: RFMEM does not share stacks); nvschedfree frees them
  after gcdrain.  lib/sched.c converted (local Qwen draft; coordinator
  removed five unused `sc` locals, fixed a declaration-after-statement
  in gcdrain, hardened nvschedgchold, and rewrote nvschedstep to use
  one `sc` instead of eight `s->sched[0]->` derefs on the dispatch
  path).  main.c printstats reads s->sched[0]->*.  Fixtures converted
  by text-only renames (local Qwen, one file per exchange, ledgers
  reported): tests/process/iotest.c, schedtest.c; tests/memory/
  autotest.c, offloadtest.c; bench/perftest.c, largelive.c, latency.c.
  Two over-conversions of `completed` (iotest, autotest) were caught by
  the compiler and reverted; no assertion VALUE was changed per the
  ledgers, but the user should eyeball `git diff tests bench` -- every
  hunk should be a bare `sched[0]->` insertion.  `mk nervous tests
  benchmarks` clean, no warnings.  bench/run.rc RUN by the user and
  saved as bench/runs/T00b-small.txt: counts identical to T04p, ns/msg
  -44% across every shape (spans all of M09, not attributable to T00),
  ring-10 high-water +376% from D082 chunk granularity (expected;
  recorded in bench/README.md).  Acceptance still owed: the three suite
  invocations (which also cover the T00a rerun).
  New tool: bench/benchcmp (bench/benchcmp.c, `mk benchmarks`) parses
  one or two saved run.rc captures and prints a table with per-shape
  deltas, counts compared exactly, costs by percent with a -t threshold;
  exit status "counts" if any count differs.  Qwen draft from a spec
  with a Bio API crib; coordinator fixed accumulation bugs in the
  summary tallies, a ulong/%llud mismatch, column widths, and collapsed
  60 lines of copy-pasted cost comparison into one helper.  Compiles;
  NOT YET RUN -- smoke test is `bench/benchcmp bench/runs/T00b-small.txt
  bench/runs/T00b-small.txt`, expected "6 shapes compared, 0 count
  changes, 0 cost flags".  Future run.rc runs: `rc bench/run.rc >[2]
  bench/runs/<tag>-small.txt` then benchcmp against T00b-small.txt.
  T00b steps 2-6 BUILT (coordinator, this session), NOT YET RUN; `mk
  nervous tests benchmarks` clean, no warnings.  What landed:
  (2) run queue: new `NvRunq {head, tail, nrunnable, enqueues}` in
  include/nvproc.h; NvRuntime has `NvRunq **runq; int nrunq` (malloc'd,
  runtime-owned, one made by nvruntimeinit; `nvruntimesetnrunq(r, n)`
  grows to n <= NvMaxsched, which moved from nvsched.h to nvproc.h);
  NvSched.runq points at runtime.runq[index].  process.c runenq/runrm
  pick the queue by `p->owner` (static runqof); `nvprocrunhead(r, sched,
  &slot)` gained the scheduler index.  (3) `NvProcess.owner` (int), set
  by new `nvprocspawnon(r, owner, ...)` before runenq; `nvprocspawn` is
  the owner-0 wrapper so ptest.c and friends did not change.
  Rejects a missing queue with "bad_scheduler".  (5) `void *NvExec.sched`
  (opaque NvSched*), set in step() right before nvexecrun and cleared
  right after; currentpid(e) reads it (nil -> bad_process_context);
  hostspawn passes e->sched so a bytecode child is owned by the
  spawner's scheduler; nvschedspawn (host) uses sched[0].  (4) D088
  counters on NvSched (remoteenq, stealstaken/given, wakessent/recv,
  sleeps/sleepns, lockacq/lockwaitns -- all 0 until T01; deadline
  fires = timerwakes; arrivals counted at NvRunq.enqueues); main.c
  printstats now sums dispatches/reductions/timerwakes/collections/
  gcfailed over sched[], takes max of maxlivewords, keeps "last" from
  sched[0], and appends one `stats: sched N: ...` row per scheduler
  AFTER the unchanged totals lines (benchcmp ignores the row).  (6)
  runscheduled(..., nsched, stats); the N>1 "multicore not yet enabled"
  refusal moved from main() into runscheduled, so -p is like -s:
  accepted and ignored by -x/-t/-c.  Also: sched.c's GC helpers
  (gcsample, gcwait, doinline, collect, gcfold, gcfoldall, gcdrain) take
  an explicit NvSched*; nvschedstep is now a facade over `static step(s,
  sc, ...)`; every process-table scan in step() and gcfoldall filters on
  `p->owner == sc->index` (no-op at N=1); nvschedfree/nvschedgchold loop
  over nsched.  Fixture edits (mechanical, value-preserving unless
  noted): schedtest.c (runhead index arg; runq fields via sched[0]->runq;
  NEW check that parent/child owner == 0), autotest.c tablegrowth
  (r.runq[0]->...; NEW checks: owner == 0, spawn onto missing queue ->
  bad_scheduler, nvruntimesetnrunq(2) then spawn/head/exit on queue 1),
  autotest.c requests/receivetake, r2test.c belowcursorfairness,
  bench/largelive.c + latency.c (nrunnable via sched[0]->runq).
  FIRST RUN RESULT: `nervous: nvschedfree: 1 collector(s) never
  signalled` after ~100 s.  Diagnosed (not reproduced -- no exec here)
  as a pre-existing use-after-free in offloadtest.c's holdfalseidle:
  the hook fakes every fold while the real children are parked, so
  nothing waits for them; drive() then frees the four NvExecs within
  microseconds and each child, waking up to 5 ms later, does
  lock(&e->heap.lock) on freed memory -- benign while the freed block
  held a zero lock word, a forever-spinning child (never semrelease's)
  once T00b's larger NvExec/NvProcess changed what lands there.
  Production is not exposed: a real fold only sees idle after the child
  published it, and the child's only touch after that is gcsem, which
  outlives the credits drain.  FIX (built, not run): holdfalseidle now
  tsemacquire's each of the N credits (counting them into gccredits)
  right after releasing the hold and before driving, so every child is
  provably finished with e while e is alive.  ALSO: NvGcdrainmax 5000
  -> 500 (10 s, not 100 s, before nvschedfree's sysfatal; it is a
  failure detector, a collector finishes in ms).  If the sysfatal
  recurs, the suite output will say which binary; report it -- that
  would mean a second cause.
  T00b ACCEPTED: user reports the suite passes with the holdfalseidle
  fix ("seems to work now").  Still worth doing once, not blocking: a
  NEW capture `rc bench/run.rc >[2] bench/runs/
  T00b-full.txt` compared with the tool against the step-1 capture:
  `bench/benchcmp bench/runs/T00b-small.txt bench/runs/T00b-full.txt`
  (there is no T04p file -- T04p exists only as a README table; the
  step-1 capture is the first saved one and is the baseline from here
  on, as bench/README.md says).  Expect 0 count changes and no cost
  flags; the new `stats: sched 0:` row is ignored by benchcmp.
  Suggested commit once green: "M10-T00b: machine/scheduler split,
  per-scheduler run queue, owner, D088 counters, -p".
  M10-T01 steps 1-6 and 8 BUILT (coordinator), NOT YET RUN; `mk nervous
  tests benchmarks` clean, no warnings.  User approved the T01 spec and
  D090.  What landed:
  (8) D090 recorded in docs/decisions.md; lib/exec.c: new
  `nvexecinternmodule(m)` interns every Katom + the four fixed atoms;
  called by nvschedinit (once, before any proc) and nvexecinitw (no-op
  scan after the first); fixedatom/boolatom are pure reads; Oloadk/
  Otestatom fault `bad_constant` on a NvNil atom instead of interning;
  the five fixed-atom fault sites say bad_constant, not out_of_memory
  (all unreachable for a live exec).
  (1) `nvschedsetnsched(s, n, err, nerr)` (nvsched.h/sched.c): before
  the root spawn only; grows runq[] via nvruntimesetnrunq, allocates
  sched[1..n-1] via new static schedalloc (also used by nvschedinit;
  sched[] is now sized NvMaxsched up front).
  (2) `QLock lock` and `QLock iolock` EMBEDDED in NvScheduler (spec said
  malloc'd pointer; changed because nvmachinerun needs the whole
  NvScheduler malloc'd anyway -- it holds the runtime -- and fixtures at
  N=1 never fork a scheduler proc, so their stack QLock is fine).
  lockrt(s, sc)/unlockrt(s) in sched.c count lockacq/lockwaitns
  (profile-gated clock).  TAKEN AT N=1 TOO, deliberately: QLock is not
  recursive, so a nested take in the N=1 suites deadlocks visibly rather
  than racing invisibly at N>1.  Held: step() from entry through
  nvprocdispatch, released for nvexecrun, retaken for post-run
  bookkeeping through nvprocexit; every host callback (send, ref, spawn,
  recv*, deadline) for its own runtime call; nvschedspawn.  Released
  across: nvexecrun, clock.wait, every tsemacquire, rfork, and an INLINE
  collection -- collect() marks heap.owner = NvHeapCollecting under the
  lock first and nvprocsteal refuses a non-idle heap, so the process is
  unmovable while collected with the lock free (D089 kept).  print/
  eprint: iolock around print+newline+flush (D086), never with lock.
  (3) NvRuntime.wakehook(aux, from, owner)/wakeaux, called from new
  `nvprocwakefrom(r, from, slot)` (nvprocwake = from -1);
  `nvprocsendfrom(r, from, ...)` (nvprocsend = from -1); hostsend passes
  e->sched->index; deadline wakes pass sc->index.  sched.c's wakehook:
  from != owner -> remoteenq++; if owner's idle bit set -> clear it,
  nidle--, semrelease(owner->sem), wakessent++.  Own-queue pushes need
  no hook: step() kicks the lowest idle scheduler (kickidle) once per
  dispatch when idlemask != 0 && own queue still non-empty.
  (4) `nvprocsteal(r, slot, newowner)` in process.c (Prrunnable, not
  offlaunched, heap idle; runrm/owner=/runenq).  sched.c: trysteal
  (head of the most loaded other queue, one candidate), idle() = the
  D085 protocol (lock; stopping? steal? drain stale sem credits; set
  bit; machineidle() -> NvStopIdle; else sleep bound = deadlinems(wakeat)
  or NvGcwaitmaxms, capped at NvGcwaitms if gcoutstanding; tsemacquire
  with lock released; clear bit if still set).  NvSched gained `long
  *sem`, `wakeat`, `wakeatvalid`.  step() split: `idlestep()` (lock held
  on entry, released on every return) is the old idle branch; at N>1 it
  NEVER blocks -- the three blocking points (clock.wait, two gcwaits)
  return NvSchedIdle with wakeat/wakeatvalid set instead; at N=1 they
  block as before with the lock released around the wait.  At N>1 an
  expired deadline found by idlestep is woken there (now >= earliest).
  (5) NvScheduler gained idlemask/nidle/stopping/stoperr/finished and the
  NvStop* enum; stopmachine() (first reason wins, semrelease every sem);
  machineidle() = nidle == nsched && every sched has gcoutstanding 0,
  !wakeatvalid, empty queue.  gcdrain now RETURNS -1 with a message
  (nvschedfree sysfatals on it; a forked proc records it in stoperr) and
  takes the lock around each gcfoldall walk.  nvmachineinterrupt(s) sets
  NvStopInterrupt without the lock and wakes all -- NOT yet wired to a
  note handler in main.c.
  (6) `nvmachinerun(s, err, nerr)`: rfork(RFPROC|RFMEM|RFNOWAIT) for
  sched[1..n-1] (each runs schedproc then _exits), schedproc(sched[0])
  inline, tsemacquire(finished, 60 s) per forked proc (sysfatal if one
  never finishes), maps stopping -> NvSchedDone/Idle/Error.  schedproc:
  loop while !stopping: step; Progress continue; Done/Error ->
  stopmachine; Idle -> idle().  Then gcdrain; forked procs semrelease
  finished last.  main.c runscheduled: NvScheduler and both Biobufs are
  now MALLOC'D (mallocz / Bfdopen -- forked procs share heap, not the
  main proc's stack); calls nvschedsetnsched(nsched) then nvmachinerun
  for every N (the "multicore not yet enabled" refusal is gone).
  NOT done yet: (7) docs/semantics.md D086 restatement; (9) bench/run.rc
  -p rows; tests/multicore/ fixtures (order.nv, migrate.nv, exitstorm.nv,
  mctest.c, README) -- candidates for Coder-Next, disjoint new files;
  the post-landing Sonnet lock-discipline audit.
  ROUND 1 RUN (user) RESULTS: cpubound 5600000 at N=1/2/4, work spread
  evenly (dispatches ~54K per sched at -p 4), N=1 0.60 s, -p 2 0.77 s,
  -p 4 0.90 s -- NO speedup: cpubound.nv collects at nearly every
  dispatch (215385 GC / 215393 dispatches, 272 red/dispatch: 11 live
  words, 3-word tuple per iteration, D069's 64-word minimum space fills
  every ~20 iterations), so it measures dispatch+collect overhead, not
  CPU; needs a fixture with a bigger live set or a larger minimum space
  (measure first).  ring 1000 2000 = 2000000 at -p 2 (1.93 s vs 0.94 s)
  and -p 4 (4.68 s): correct; slowdown = global lock + steal CHURN
  (120K steals each at -p 2: one runnable token stolen back and forth).
  FAILURES: (a) tests/run.rc `binfault-overflow`: empty fault line in
  all three suite variants -- main.c used Bfdopen, and Bterm CLOSES a
  Bfdopen'd stream's fd, so every fprint(2,...) after Bterm(berr) was
  lost (fault line, `scheduler:` error line, deadlock line).  (b) sieve
  5000 at -p 4 stopped after 13 processes with NO message (same fd bug
  hid it) -- almost certainly the on-CPU wait race: hostrecvwait marks
  Prwaiting a few instructions before the interpreter yields; at N>1 a
  remote sender sees Prwaiting in that window, wakes and ENQUEUES a
  process that is still running (step's post-run check then reports
  "yielded process has bad lifecycle state"; worse, a third scheduler
  could steal and run it concurrently).  benchcmp T00b->T01 at N=1:
  +13-17% wall on rings/waiters, +5% sieve (7.4 lock acquisitions per
  ring hop), +33 KB fixed high-water (malloc'd Biobufs + machine).
  ROUND 2 CHANGES (built clean, not run): main.c malloc+Binit instead of
  Bfdopen (Bterm now only flushes).  NvProcess.oncpu/pendingwake (BEAM's
  RUNNING/ACTIVE split): nvprocdispatch sets oncpu; nvprocwakefrom on an
  oncpu process sets pendingwake and returns without enqueueing; new
  `nvprocoffcpu(r, pid)` -- called by step under the lock right after
  nvexecrun -- clears oncpu and performs the deferred enqueue (returns 1;
  step's NvYield check then accepts Prrunnable); both cleared at exit
  and slot reuse.  NvStealmin = 2: trysteal only from a queue with >= 2
  runnable, kickidle only when >= 2 remain after the pop (a lone runnable
  is run by its owner; remote wakes onto an idle owner still kick).
  NvScheduler.locking: lockrt/unlockrt are no-ops unless set; set by
  nvschedsetnsched (N>1) or new nvschedsetlocking(s, on) for fixtures
  (never off at N>1).  N=1 therefore no longer takes the lock.
  ROUND 2 RUN OWED (same commands as round 1; expectations: all three
  suites pass; sieve -p 4 prints all primes + 'ok, or a VISIBLE
  `scheduler:` line; ring -p 2 steals should collapse to ~0 and wall
  should improve vs 1.93 s; benchcmp T00b->T01 at N=1 should be within
  noise).  Then commit.
  ROUND 2 RUN RESULTS: (a) all three suites FAILED at ptest "send wakes
  waiting process once" -- the fixture drives nvprocdispatch bare (so
  oncpu is set), hand-drives the receive to Prwaiting, then sends and
  expects Prrunnable; under D081 that wake is correctly DEFERRED
  (pendingwake) because nothing ever called nvprocoffcpu.  ptest was the
  only bare-dispatch fixture (Haiku audit of the other 8 fixtures +
  identity replace_string probes on autotest/r2test/offloadtest: zero
  nvprocdispatch calls).  Because the suite stops at the first failure,
  nothing after ptest ran in round 2.  (b) sieve -p 4 PASSED (all primes
  + 'ok; steals 6-404 per sched, not 0 -- fine).  (c) ring -p 2 HUNG.
  Diagnosis: a lost-wakeup window in idle(): idlestep returns Idle and
  releases the lock; a remote wake then enqueues onto THIS queue and,
  seeing no idle bit, sends no kick; idle() then set the bit and slept
  the full NvGcwaitmaxms (60 s) with a runnable on its own queue, never
  re-checking it.  Round 1 was hiding this: trysteal at >= 1 let the
  other scheduler take the lone token off the sleeper's queue (part of
  the 120K ring steals); NvStealmin = 2 removed that accident, so both
  schedulers slept and the ring stalled 60 s per occurrence.
  ROUND 3 CHANGES (built clean, not run): lib/sched.c idle() now calls
  finddispatchable on its own queue under the lock, before trysteal and
  before setting the idle bit (finddispatchable rather than nrunnable so
  a queue whose runnables are all under off-process collection still
  sleeps bounded rather than spinning).  tests/process/ptest.c lifecycle
  block rewritten to the D081 contract and extended: send while oncpu
  defers (state Prwaiting, pendingwake, queue length unchanged), a second
  send in the window is idempotent, nvprocoffcpu returns 1 and enqueues
  exactly once, then (mailbox drained) a receive blocks again, offcpu
  with nothing pending returns 0, and a send to the now off-CPU waiter
  makes it Prrunnable immediately.  Its ok line is now "ok - explicit
  process lifecycle transitions, deferred and immediate wakes".
  ROUND 3 RUN RESULTS (user): ACCEPTED.  All three suite invocations
  pass END TO END (first time every fixture after ptest ran on T01 code).
  sieve -p 4: correct, sleeps 3-6 per sched (was 118), 0.69 s.  ring
  -p 2: 2000000, 1.303 s (round 1: 1.93 s; N=1: 0.83 s), steals 18
  total (was ~120K); sched 0 did 1.97M dispatches, sched 1 36K -- the
  18 nodes stolen during build each cost a remote wake + kick + sleep
  cycle (~34K sleeps per sched), and 16.3M lock acquisitions (~8 per
  hop) is the stage-2 number.  ring -p 4: 2000000, 1.485 s.  cpubound
  -p 4: 5600000, 0.886 s, no speedup (GC per dispatch, as diagnosed in
  round 1); sched 2 got 0 dispatches -- 4 workers, NvStealmin=2 left no
  backlog by the time it woke (load-balance data point for T03, not a
  bug).  benchcmp T00b->T01 at N=1 (bench/runs/T01-small.txt): 0 count
  changes, 0 cost flags, every shape within +-2.6%; the round-1 +13-17%
  is gone (lock off at N=1); ring 10 high-water +8.3% is the fixed
  malloc'd machine/Biobufs against a 98 KB baseline.
  Docs: D083 gained the NvStealmin amendment, D085 the own-queue
  re-check amendment (its text already said "every queue"; the first
  landing only checked the others).
  Suggested commit (everything since T00b): "M10-T01: N scheduler procs
  under the stage-1 lock; oncpu/pendingwake wake deferral; idle protocol
  with own-queue re-check; NvStealmin; D090 atom pre-intern; -p live".
  T01 CLOSE-OUT (after round 3, same session, coordinator, no sub-agent
  spend): (7) docs/semantics.md restatement DONE.  (9) bench/run.rc
  gained `multi` (-p 1/2/4 x ring/sieve/cpubound via `benchp`; header
  carries `-p N` after the args so benchcmp keys are distinct; at -p>1
  dispatch/reduction count changes are expected, documented in the
  script) -- NOT YET RUN; T03 runs it.  Note handler DONE: main.c
  `interrupt` (atnotify around nvmachinerun, "interrupt" only, calls
  nvmachineinterrupt; every proc in the note group runs it, harmlessly);
  nvmachinerun's `finished` wait now retries on -1 instead of dying.
  Lock-discipline audit DONE by the coordinator reading sched.c end to
  end: all five questions clean (pairing on every return path; iolock
  only from inside a quantum; nothing held across nvexecrun/clock.wait/
  tsemacquire/rfork/doinline; every scan under the lock; heap Lock only
  inside the QLock or alone, one word).  One observation, commented in
  step(): the root-exit nvfragcopy runs under the lock, once per run,
  accepted.  nvprocsteal's offlaunched guard confirmed present in
  process.c (one reference).  Build clean.  NOT VERIFIED BY RUN: the
  note handler and the finished-wait retry need `rc tests/run.rc` plus
  one interactive check (`nervous -p 2 -r examples/ring.nv main 1000
  200000`, DEL it, expect "scheduler: interrupted" and exit status run,
  no orphaned procs in `ps`).
  tests/multicore/ WRITTEN, NOT RUN: order.nv (required test 2, root
  'ok), migrate.nv (test 3, 'ok; PID compared against the parent's
  spawn-returned pid sent in as a message), exitstorm.nv (test 6,
  ${'done, 20000}; -s dropped must be 19900), README.md (what each
  proves/does not; tests 4 and 5 and the fragment-leak check deferred to
  a C fixture mctest.c that needs a new NvSched idle hold hook -- not
  written, because it cannot be validated without a run).  tests/run.rc
  runs the three at -p 1/2/4 after the io-error check and before "all
  passed"; a fixture failing at -p 1 is a fixture bug.  FIRST RUN OWED:
  the three suite lines; if a fixture fails at -p 1 fix the fixture, if
  only at -p 2/4 it is a real finding -- report the value and the -s
  rows.  Then the DEL test above.  Then commit.
  T01 STILL OPEN after that: mctest.c (test 4 hold point, leak check).
  Known
  performance facts to carry into T02/T03, not to fix now: -p N is
  slower than N=1 on every message-bound shape (global lock ~8
  acquisitions per hop; idle/kick/sleep cycle per cross-scheduler hop);
  cpubound.nv needs a bigger live set or larger D069 minimum space
  before it can show a speedup.
  New fixture bench/cpubound.nv (T03's CPU-bound shape, D088): k workers
  each sum (i*i)%7 for i=1..n in a pure tail-recursive loop and report one
  message; root value for `main` (4 workers, 700000) should be 5600000
  (period-7 residues sum to 14).  Coordinator-checked against lib/parse.c
  but NOT yet run -- user: `nervous -r bench/cpubound.nv main` and
  confirm 5600000 before wiring it into bench/run.rc (a `bench
  $benchdir/cpubound.nv main 4 700000` line in the small set).
  Finding 7 audit (run-time atom interning) ANSWERED, and it matters for
  T01: yes, bytecode execution can call nvatom().  (a) lib/exec.c
  fixedatom()/boolatom() lazily intern 'true/'false/'ok/'undefined into
  FILE-SCOPE STATICS (cachedtrue etc.) on first use from Olt..Oge,
  Oistype, Oprint/Oeprint, Orecvbegin/Orecvnext -- a real run-time
  intern, and under RFMEM the statics are shared, so two schedulers'
  first comparisons race on both the cache write and the table insert.
  (b) Oloadk/Otestatom keep a "defensive" nvatom fallback when
  k->atom == NvNil; nvexecinit pre-interns every Katom so it is normally
  dead, but nvexecinit itself runs at every bytecode spawn (hostspawn ->
  spawn -> nvexecinitw), scanning the shared m->konst.  (c) No nvatom in
  sched.c/process.c; pattern.c reads via nvtermatom only.  Cheapest fix
  for T01: pre-intern the four fixed atoms in nvschedinit (or nvverify)
  so fixedatom never allocates, and make the Oloadk/Otestatom fallback a
  fault rather than an intern; then nvatom needs no lock on any
  interpreter path and D070's lock is for host/test code only.  Not yet
  decided; record as a D0xx when T01 is cut.

M10 opening -- the coordinator's full pre-implementation design review
  is in milestones/10-multicore.md ("Coordinator design review"): 7
  findings on the existing design, 11 proposed decisions, a test
  strategy, staging T00-T04 + R4, and hazards.  It supersedes the short
  list below, which is kept as the original question set.  Nothing in
  it is decided until the user approves and the accepted proposals are
  written as D081+ in docs/decisions.md; then T00 (refactor, no new
  behavior) is cut first.  Decisions that most need the user's call:
  staged global lock first (P1); segmented table (P2); wake-to-home
  ownership (P3); deadline check per dispatch, which changes observable
  timing under load in single-scheduler mode (P5, finding 1); one code
  path with N=1 as the same code (P10).
  Original question list:
  a. Process table and PID ownership: one shared NvRuntime table under a
     QLock, or per-scheduler tables with a global slot->scheduler map.
     PIDs must not change on migration (required test).
  b. Mailbox locking: a sender on scheduler A appending to a process
     owned by scheduler B.  D068 already says the mailbox is
     scheduler/sender territory; M10 must make that a per-process lock
     or a per-owner-scheduler lock, and must preserve the M05
     per-sender FIFO contract across the lock.
  c. Wakeup: a receive-wait on B, a send from A -- how A makes the
     process runnable on B (or steals it) without a lost wakeup or a
     double enqueue (two required tests).  The D074 semaphore pattern
     (tsemacquire idle / semrelease wake) is the intended primitive.
  d. Work movement: steal vs. push, and the safe points (a process is
     only movable while suspended and not NvHeapCollecting -- the D068
     `owner` field already encodes the second condition).
  e. Timers: which scheduler owns a deadline and how a sleeping
     scheduler's tsemacquire bound is computed when the earliest
     deadline belongs to a process it does not own.
  f. Off-process GC under multiple schedulers: gcoutstanding /
     gclaunched / gccredits are per-scheduler today; a migrated process
     with a collector outstanding must fold on its new owner or be
     unmovable until folded (simplest: unmovable while offlaunched).
  g. Statistics: the "measurement requirement" wants queue, steal,
     wakeup, migration and reduction counts before any lock-free work;
     decide the counters and the -s output shape up front.
  h. Single-scheduler mode must be bit-identical to today (required
     test): keep the current nvschedstep path as the N=1 case rather
     than a special case of the new one.
source control: M09-T01..T04 and the docs split are committed and pushed
  by the user.  UNCOMMITTED as of this checkpoint (D080 files added
  below): the R3-F02 fix
  (lib/value.c, include/nvvm.h), the M09-T05 results and R3 opening
  (bench/README.md, STATUS.md, milestones/09-binaries.md,
  docs/review-findings.md); and the D080 work charging (include/nvvm.h,
  include/nvexec.h, include/nvsched.h, lib/value.c, lib/exec.c,
  lib/sched.c, cmd/nervous/main.c, tests/process/exectest.c,
  bench/latency.c, bench/latency.rc, docs/decisions.md,
  docs/bytecode.md, docs/review-findings.md, README.md, STATUS.md);
  and the R3 closing set (lib/verify.c R3-F03, tests/memory/autotest.c,
  tests/process/ptest.c, tests/process/exectest.c,
  milestones/R3-memory-review.md, docs/review-findings.md, README.md,
  STATUS.md); and the R3-F05/F06 fix (lib/sched.c, include/nvexec.h,
  include/nvsched.h, tests/memory/offloadtest.c, docs/decisions.md D074
  amendments, docs/review-findings.md, milestones/R3-memory-review.md).  Suggested messages: "milestone 09 complete (T05 latency
  results); open R3; R3-F02 nvfragcopy size guard"; "D080: charge
  traversal work as reductions; -w work ceiling (R3-F04)"; "R3:
  coverage fixtures for ownership transitions and live-data memory;
  R3-F03; coverage map and exit statement".  The
  coordinator commits nothing; ask the user at a checkpoint.
source control: user committed and pushed M09-T01..T04 plus the
  docs/ split ("milestone 09 binaries (T01-T04); split design doc into
  docs/ (WIP)").  A follow-up docs-only commit marks the split files as
  non-normative rationale and restores docs/semantics.md as the
  contract.  The coordinator commits nothing; ask the user at a
  checkpoint.
```

Milestone 08 (Memory) and CLI-T01 are complete and accepted; their full narrative, evidence, task ledger and review notes are in `STATUS-archive.md` ("Milestone 08 detail"). Settled policy from 08 lives in `docs/decisions.md` (D061-D075); benchmark evidence in `bench/README.md`.

## Milestone ledger

00-07 and R1-R2 are complete; their historical rows are archived.

| Milestone | State | Dependencies | Remaining scope |
|---|---|---|---|
| 08 Memory | **complete** | R2, 05, 06 | none -- all tasks (T04a/b/p/e/c/r/d) accepted |
| 09 Binaries | **complete** | 04, 08 | none -- T01-T05 done; T05 results in `bench/README.md` |
| R3 Memory review | **complete** | 08, 09 | none -- six findings fixed with regressions; D080 |
| 10 Multicore | **in progress** | R3, 05, 06, 08, 09 | T00a/T00b/T01 landed (`-p N` live). Open: T01 fixtures + `-p` bench rows + lock audit + note handler; T02 deadline heap (D084); T03 measurement; T04 lock split if justified; R4 |
| R4 Multicore review | not-started | 10 | Mandatory multicore acceptance review |

## Task ledger

The milestone-08 task ledger is archived in `STATUS-archive.md`. Still open from 08: M08-T04q (optional, deferred; see its section below). Milestone-09 tasks are under "M09-Tasks (in progress)".

## M09-Tasks (in progress)

### Progress (current)

- **M09-T01 Bbin runtime: done.** A first draft came from a long Qwen run. Claude's review fixed: word-vs-byte arithmetic in `nvbinapp` (heap corruption); no D067 reservation for the new opcodes in `lib/exec.c`; verifier dataflow and guard edges for them in `lib/verify.c`; 32-bit `1L` shifts; the signed range check; zero padding; `lib/pattern.c` length types; D079 fault names. Nine opcodes: `binalloc`, `binappint`, `binappbin`, `binappend`, `bintestbinary`, `binintget`, `binbinget`, `binremget`, `binend`. Tests: construction/encoding checks in `tests/process/ptest.c`; VM opcode cases A-F in `tests/process/exectest.c` (construction, append, match, mismatch-to-fail-target, faults, remainder). `docs/bytecode.md` documents the opcodes. User-confirmed passing.
- **M09-T02 frontend: done.**
  - Lexer tokens `<<`/`>>`/`:`, and parser (`lib/parse.c`) producing `Ebinagg` with one `Ebinseg` per segment. Encoding of a segment is `ival = (kind<<8) | (width<<2) | (signed<<1) | little`, kinds `Binsegint`/`Binsegsized`/`Binsegrest` (header `include/nervous.h`). Decisions made while implementing (within D076): `<<>>` is the empty binary; segment values and sizes are one primary or unary expression (parenthesize anything looser); `/binary` excludes int modifiers; a rest segment takes no modifiers and must be last; `patternok` accepts binaries with pattern-form values.
  - AST printer (`lib/ast.c`), fixtures `tests/frontend/binary.nv/.ast`, `bad-binwidth`, `bad-binrest`.
  - Formatter (`lib/format.c`, rules in `docs/format.md`), fixture `binary-fmt.nv/.fmt`.
  - AST->`Pbin` conversion (`lib/patcompile.c`, `convertbin`), rejecting a size variable bound by the same or a later segment of that binary. Tests: nine cases in `tests/pattern/parsetest.c` (`binpatterns`), covering most of the milestone's required matching tests at the `nvpatternmatch` level.
  - `lib/patbc.c` `compilebin`: `Pbin` -> `bintestbinary`, one get per segment, `binend` unless a rest segment ends it (a discarded rest emits nothing). A size variable must already be bound when its segment is lowered, which also rejects `${<<p:n/binary>>, n}`. Repeated names test equality like `Pvar`.
  - `lib/compile.c` `patchtests` now repatches the binary ops' fail targets (field `b` for `bintestbinary`/`binintget`, `c` for `binbinget`, `a` for `binend`); without it every binary mismatch jumped to pc 0.
- **M09-T03: done.** `compilebinagg`: evaluates every segment value and size left to right, then `binalloc` plus one append per segment into fresh registers. `is_binary` type test (usable in guards); `guardok` already rejects construction in guards (D079).
- **Also fixed:** `bintestbinary` was written and read with one operand instead of two (`lib/bytecode.c`, `lib/bcread.c`), so saved bytecode lost the fail target and a failing test looped at pc 0 under `-X`.
- **End-to-end test:** `tests/frontend/binary-run.nv/.out`, run by the new `ran` helper in `tests/frontend/run.rc` both via `-r` and via `-c` + `-X`. `-r` output user-confirmed to match all 14 expected values.
- **M09-T04: done, accepted (user-confirmed full suite).** Fault fixtures `tests/frontend/binfault-*.nv` (eight, via the new `faulted` helper), compile rejections `binreject-*.nv/.err` (three, via `rejected -c`), and the exit-criterion example `examples/protocol.nv` (via `ran`, expected output `tests/frontend/protocol.out`). All outputs were first produced by a user run and checked against predictions before being saved. Head-pattern compile errors now report the first parameter's position instead of 0:0 (`clausetuple`). Required tests reconciled in `milestones/09-binaries.md`, "Implementation state".
- **M09-T05: done.** User ran `rc bench/latency.rc`; results and their reading are in `bench/README.md` ("First run"). Headline: equality/print/copy on a shared `${x,x}` chain cost ~16x per four levels of depth (116ms/653ms/188ms at depth 24); a hog running `==` on depth-16 chains stalls peer round trips 27ms median / 58ms max against a 0.5us baseline; a hog dispatch is min(quantum, heap headroom) because each tail call allocates a 3-word argument tuple, and the reduction quantum bounds nothing inside one instruction. Original build description follows. `bench/latency.c`, `bench/latency.rc`, a `latency` mkfile target (in `benchmarks` and `clean`), and the "M09-T05" section of `bench/README.md`. Part 1 times host-side `nvtermequal`/`nvtermprint`/`nvfragcopy` on independently built shared `${x,x}` chains (depth 1-24; the traversals do not detect sharing, so cost doubles per level), plus a linear-chain probe of the depth-256 limit. Part 2 (`latency hop depth peers rounds`) times peer round trips before and while a hog evaluates `a == b` on two depth-d chains; one equality is one reduction regardless of cost (`run()` in `lib/exec.c`), which is the isolation question for R3. Provenance: part 1 drafted by local Qwen (analysis correct; Claude fixed a 32-bit `1UL<<255` shift and a check applied to the wrong chain shape); part 2 drafted by local GLM after Qwen stalled, then largely rewritten by Claude (the draft omitted the equality and used an unallocated pid array). `mk benchmarks` builds clean. The three pre-run risks the README section originally listed (word-count formulas, a possible `Idle` abort while the hog waits on `after 0`, and whether `a == b;` parses) were each checked against `lib/value.c`, `lib/process.c` and `lib/parse.c` and hold; see the README section. Only the run itself remains.
- **Remaining:** user runs `rc bench/latency.rc`; record results; then milestone 09 can close and R3 opens.
- **R3 pre-gate audit (while T05 awaits its run):** a Sonnet read-only audit of the nine `bin*` opcodes across `value.c`/`exec.c`/`gc.c`/`verify.c`/`pattern.c` (D063 pointer discipline, 32-bit widths, reservation exactness, bounds, encoding, GC sizing, verifier rules) found one real defect and one cosmetic one, both recorded in `docs/review-findings.md`: **R3-F02** (medium, fixed-pending-verification) -- `nvfragcopy` passed an unguarded 64-bit byte size to `malloc(ulong)` and truncated `count` into `NvFrag.nword`; a guard now returns `NvTermlimit` (`lib/value.c`, contract comment in `include/nvvm.h`); `mk tests benchmarks` clean. **R3-F03** (low, open) -- vacuous `(x&3) > 3` flag check in `verify.c`. Questions A, C-G of the audit came back clean with quoted evidence. R3-F02 needs the user to rerun `rc tests/run.rc` and `nervous_gcstress=1 rc tests/run.rc` before it closes.

The original plan follows. Design settled in D076-D079 (representation, grammar/evaluation, width/alignment, sizing reservation and fault reasons). Write sets are reserved for the named task. T01 and T02 are disjoint behind the settled interfaces (the D076-D079 opcode operand meanings and the `Ebinagg`/`Ebinseg` AST shapes) and may run in parallel; T03 depends on both; T04 depends on T03. T05 is independent of T01-T04 (it measures the pre-existing term machinery, not binaries) and may run in parallel with any of them.

| Task | State | Dependencies | Write set (canonical, exclusive) | Objective |
|---|---|---|---|---|
| M09-T01 Bbin runtime | done | 08 | `include/nvvm.h`, `include/nvexec.h`, `lib/value.c`, `lib/gc.c`, `include/nvpat.h`, `lib/pattern.c`, `lib/exec.c`, `include/nvbc.h`, `lib/verify.c`, `lib/bytecode.c`, `docs/bytecode.md` | `Bbin` boxed kind and `Vbin`/`is_binary`; `nvbin`/`nvbinlen`/`nvbinbytes`; `nvbinbuild`/`nvbinbinget`/`nvbinremget` (self-contained byte copies, budget-aware); equality/print/heapcopy/fragcopy support; new opcodes `binalloc`, `binbinget`, `binremget` (D079 sizing reservation: allocate, retry from clean state, no mid-copy collection); verifier operand + guard-region rules (binaries excluded from guards, D060/D071); disassembly text for the new opcodes. |
| M09-T02 Binary frontend | done | 08 (independent of T01; builds on the coordinator-settled `include/nvpat.h` `Pbin` interface) | `include/nervous.h`, `lib/parse.c`, `lib/lex.c`, `lib/ast.c`, `lib/patcompile.c` (AST->`NvPattern` conversion, `nvpatternfromexpr`), `lib/patbc.c` (pattern->bytecode lowering, `nvpatterncode`), `lib/format.c`, `docs/format.md`, `tests/frontend/*` (new fixtures only) | `<< ... >>` aggregate with the D076 segment grammar (integer segments with literal widths {8,16,32,64} and `/signed`/`/unsigned`/`/big`/`/little` modifiers; `:size / binary` and `/binary` segments; final unsized remainder); `Ebinagg`/`Ebinseg` AST nodes (parser does not validate forward/backward references or pattern-mode legality; the compiler and pattern checker do); `patternok` accepts binaries; `nvpatternfromexpr` converts `Ebinagg` to the `Pbin` `NvPattern`; `nvpatterncode` lowers binary patterns to `binalloc`/`binbinget`/`binremget` with a byte-position register; guard check rejects binaries (D079); canonical formatting for the aggregate (D021, `docs/format.md`); lexer/AST plumbing (a `Tbinopen`/`Tbinclose` token pair is expected -- the exact token names are an implementer detail to report, as with any new token). |
| M09-T03 Binary compiler lowering | done | T01, T02 | `lib/compile.c`, `docs/decisions.md` (only if an implementation-level refinement is needed; report it) | Lower `Ebinagg` construction to `binalloc` (D077: evaluate operands left to right, compute total, single allocation, fill left to right) and the D079 construction faults (`bad_binary`, `overflow`, `badarith`); wire `is_binary` into `typetest` and the D060 guard/type-test surface; forward-reference size validation in patterns is already the pattern checker's (T02) -- this task is construction side and the intrinsic wiring only. |
| M09-T04 Tests + exit-criterion example | done | T03 | `tests/bytecode/*`, `tests/pattern/*`, `tests/vm/*`, `tests/process/*`, `tests/run.rc`, `examples/README.md`, new `examples/protocol.nv` (the milestone exit-criterion example: a small length-prefixed protocol), `milestones/09-binaries.md` (implementation-state section only) | Every "Required tests" item from `milestones/09-binaries.md`: exact match + trailing-byte rejection, empty/nonempty remainders, length-prefixed payload, endian + signed decoding, invalid forward size reference rejected by the frontend, late failure rolling back earlier segment bindings, allocation/size-limit failures controlled. Golden tests in the existing suites; the example demonstrates the exit criterion (length-prefixed protocol over process-heap-owned binaries). |
| M09-T05 Latency-isolation measurement (R3 prep) | built; awaiting user run of `rc bench/latency.rc` | 08 | `bench/latency.c` (new), `bench/latency.rc` (new), `bench/README.md`, `mkfile` (only the new target + `benchmarks`/`clean` entries, as T04r did) | First numbers for REVIEW-impressions.md's third isolation leg (now in R3's scope): structural equality and print cost on independently built shared graphs (`${x,x}` chains at increasing depth), copy of shared deep terms, and small-message hop latency measured with an expensive-term peer running concurrently on the same scheduler. Measurement only -- no mechanism changes (work-sensitive charging, resumable traversals, bounded exports, or I/O offloading are named there as possible later mechanisms; this task measures, it does not choose). |

Notes on the write sets. The three pattern files do different things and are split to keep T01/T02 disjoint: `lib/patcompile.c` is the AST-to-`NvPattern` conversion (`nvpatternfromexpr`, T02), `lib/patbc.c` is the pattern-to-bytecode lowering (`nvpatterncode`, T02), and `lib/pattern.c` is the runtime matcher (`nvpatternmatch`, T01) that both the pattern harness and the source path use, where a `Pbin` pattern matches a `Bbin` term. The one genuinely shared surface, `include/nvpat.h` (the `Pbin` kind and `NvBinseg` struct), is settled centrally in `milestones/09-binaries.md`'s "Settled interfaces" section before either task starts, so T01 owns it and T02 builds to it without editing it -- this is the coordinator-settles-the-interface rule from COORDINATION.md's parallelism policy, not an exception. If a task finds it needs a file outside its set, it reports a blocker, it does not expand scope. `include/nvproc.h` is in no M09 set (no `NvLimits` field is added: D078/D079 deliberately reuse the heap word budget rather than a new binary byte limit, so there is no `.gcoffload`-style field to thread through the construction sites). The `mkfile` change in T05 follows the T04r precedent (new build target only, no behavior change to existing targets).

## Recommended next sequence

**Milestones 08 and 09 and R3 are complete.** R3 closed with six findings fixed with regressions (D080 work charging; R3-F05/F06 in the off-process collector), and the three suite invocations are the regression bar. **Milestone 10 (Multicore) is in progress**: design recorded as D081-D090; T00a (segmented table), T00b (machine/scheduler split) and T01 (N procs under the global lock) are landed and accepted -- `nervous -p N` works, N=1 cost is unchanged. The recommended order from here: (1) commit T01 (message in the checkpoint block); (2) finish T01's open items -- `tests/multicore/` fixtures (sub-agent candidates: disjoint new files), `bench/run.rc` `-p` rows, the note handler, and the read-only lock-discipline audit of `lib/sched.c`; (3) T02, the per-scheduler deadline heap and per-dispatch deadline check (D084, resolves R2-F16 and the "timers starve under load" finding); (4) T03 measurement, which first needs a `cpubound.nv` that does not collect on every dispatch; (5) T04 only if T03's numbers say so; (6) R4.

## T04q duplicate interpreter work (planned, optional, deferred; not a T04c dependency)

Source inspection found arithmetic results and call-target resolution computed during reservation preflight and then computed again for execution. A narrow pass can remove this duplication. Not required before or after T04c; pursue only if the user wants the throughput work specifically.

- Read `lib/exec.c`, `include/nvexec.h`, D063/D067/D071-D073 and the memory test coverage first.
- Reuse scalar arithmetic results or function indices within the SAME instruction attempt where safe. Discard/recompute the plan after NvCollect; never retain an unrooted heap pointer across movement. Avoid persistent module caches or public ABI changes unless separately justified.
- Preserve exact operand errors, overflow/divide behavior, guard fault transfer, call/tailcall accounting, unchanged pc/reductions on collection requests, and one-time side effects.
- Acceptance: forced build, normal/stress regressions, quanta 1/1000, and repeated unprofiled original/control benchmarks. Keep measured wins or a justified simplification; do not weaken checks to chase the stage-2 number.
- Stopping rule: one bounded attempt, then proceed or defer.

## Current implementation map

- Representation/accounting: `include/nvvm.h`, `lib/value.c`. Tagged words, interned atoms, immutable sharing, independent fragments. Managed execution heaps are contiguous; host/startup construction still uses non-moving chunks.
- Collector: `lib/gc.c`. Cheney copy; address classification includes adopted fragments and excludes stable external fragments. Source headers can be restored on failed trials; roots commit only after all fallible work. Small rollback scratch uses the C stack; the heap descriptor is reused only on successful commit.
- Roots/reservations: `include/nvexec.h`, `lib/exec.c`. Active registers only, retained stack capacity charged, small root-view scratch on the C stack, NvCollect request/retry and guard-aware failure. Standalone servicing in `lib/vm.c` preserves the reduction budget.
- Processes: `include/nvproc.h`, `lib/process.c`. Segmented process table (D082: 1024-slot chunks, stable `NvProcess*`), fragment mailboxes, non-consuming recvneed before take, lowest-free hint. Retired slots can outnumber the configured live-process limit; FIFO links use indices. One run queue per scheduler (`NvRunq`, owned by the runtime, indexed by `NvProcess.owner`); `nvprocspawnon`, `nvprocsteal`, `nvprocwakefrom`/`nvprocsendfrom` with the runtime `wakehook`, and the `oncpu`/`pendingwake` wake deferral with `nvprocoffcpu` (M10-T01).
- Scheduler: `include/nvsched.h`, `lib/sched.c`. `NvScheduler` is the shared machine (runtime, module, host table, clock, io, root, `QLock lock`/`iolock`, idle mask, stop state, `sched[]`); `NvSched` is one scheduler proc's state (run queue, `sem`, GC bookkeeping, D088 counters). `nvschedstep` is the N=1 facade over `step(s, sc)`; `nvmachinerun` rforks `sched[1..N-1]` and runs `schedproc` (step / idle / stopmachine) in every proc. M10-T04a local shard protects dispatch/yield/receive/GC; machine gate + all shards protects structural/cross-owner operations. Runtime locks are released across `nvexecrun`, blocking waits, `rfork` and inline collection; not taken at N=1 unless forced (`locking`). See docs/sharding.md for the map and same-build global control. Wake-to-home, `trysteal` at `NvStealmin = 2`, `kickidle`, `idle()` with own-queue re-check, `machineidle` termination, `gcdrain` per proc. Inline demand/idle collection, explicit storage snapshots and opt-in profiling, plus (M08-T04c, D074) off-process collection: a heap owner state and `Lock`, `rfork(RFPROC|RFMEM|RFNOWAIT)` collector procs restricted to a bare `NvExec*`/semaphore argument list, a locked completion fold, never-spin/never-false-idle scheduler waiting on a malloc'd completion semaphore bounded by the nearest deadline, a capped idle-sweep launch burst, and a bounded teardown drain. Default dispatch still has neither snapshot scans nor profiling clock reads on the no-collector path.
- Binaries (M09, D076-D079): `Bbin` terms in `include/nvvm.h`/`lib/value.c` (`nvbin`, `nvbinapp`); runtime matching in `lib/pattern.c`; nine `bin*` opcodes in `lib/exec.c`, verified in `lib/verify.c`; frontend in `lib/lex.c`, `lib/parse.c`, `lib/patcompile.c`, `lib/patbc.c`, `lib/compile.c`, `lib/format.c`.
- CLI: `-H` word budget, `-G` stress, `-o words` off-process threshold (M08-T04d), `-w visits` traversal ceiling (D080), `-p schedulers` (M10-T01; `runscheduled` mallocs the machine and both Biobufs and runs `nvmachinerun` for every N), `-s` statistics with one `sched N:` row per scheduler. `nervous_gcstress=1`/`nervous_gcoffload=words` set the matching CLI defaults; `-o`/`$nervous_gcoffload` apply only to `-r`/`-X` (the standalone `-x`/`-t` executor has no scheduler). Default is 0 (never off-process, D075, measurement-confirmed) -- unaffected by every test/benchmark's own independent `NvLimits.gcoffload = 0` construction.
- Tests: `tests/memory/gctest.c` (seven groups), `autotest.c` (six groups), and `offloadtest.c` (eight off-process lifecycle groups, M08-T04c/T04d), included by `tests/run.rc`. Build-only benchmark target: `mk benchmarks` produces `bench/perftest`, `bench/largelive` and `bench/latency` (M09-T05).

## Remaining cautions

- `gcoffload` default (0, D075) and the idle-sweep cap (`NvGcsweepcap=8`, still unmeasured/provisional) are recorded; adaptive sizing and heap shrinking remain unimplemented. D069 defers shrinking; `docs/questions.md` records the remaining open policy questions (what would reopen the `gcoffload` default, the persistent-collector-pool idea).
- Collector cost includes classification/freeing of adopted fragments and scratch/trial work, not just copying live words. Transient old/new/scratch space is not the retained-data maxheap budget.
- Host malloc failure paths have source review but no deterministic injection coverage. Root descriptors, C pointers and custom host callback contracts are trusted; verifier guarantees apply to bytecode, not arbitrary host metadata.
- Atom synchronization is settled by D090 (no interpreter path writes the table; host `nvatom` calls must not overlap a running machine). Multi-scheduler ownership is live (D083): the invariant "exactly one owner at every instant" now rests on the owner shard/all-shard transfer gate plus `oncpu`/`pendingwake`. M10-T04a source review and user-observed tests are green; required deterministic idle/wake race fixtures and R4 remain open.
- `RFMEM` shares data, bss and heap, not stacks: anything a forked scheduler or collector touches must be malloc'd. `nvmachineinterrupt` is wired to the CLI note handler; the interactive interrupt check and lock-free stop-word review remain acceptance/R4 work. Deadlines are still slot-scanned at idle only (D084/T02 not landed), so `after` still cannot expire while anything is runnable, at every N.
- Message-bound shapes were slower at N>1 under stage 1 (~8 global sections per hop; one idle/kick/sleep cycle per cross-owner hop). They have not been remeasured under the retained first-slice split. The CPU/GC win is not a message-heavy scaling claim.
- post-R2-F01 is closed with D071 evidence. R2-F16 is resolved by D084 when T02 lands. `REVIEW-impressions.md` is not declared wholly resolved; R3 remains a mandatory future gate.

## Resumption checklist

1. M10 is in progress: read the checkpoint block (its head, then "ROUND 3 RUN RESULTS" and "T01 STILL OPEN"), "Recommended next sequence", then `milestones/10-multicore.md` ("M10-T01 -- landed" for what differs from the spec, then the T02 spec under "Staging"), and D081-D090. `bench/README.md` "M10-T01" has the first `-p` numbers. Milestone 09's task detail under "M09-Tasks" and the R3 material are history now. If instead resuming T04q (still optional, still deferred) or anything not scoped above, fall back to the fuller read list in step 3.
2. Confirm actual source-control state with the user; preserve the accepted checkpoint before new edits. A commit message is not evidence of a commit. As of the M09-T04 checkpoint, the user reports committing and pushing (see "source control" in the checkpoint block).
3. (Only if step 1's fast path doesn't apply) Read README, this status, `docs/semantics.md` (the normative contract; the `docs/` design-rationale files split from `nervous_design.md` are not), `milestones/09-binaries.md` and D076-D079 for milestone 09; for R3 preparation, `milestones/R3-memory-review.md` and D061-D075 (D074 especially, including its coordinator-review amendments) and the task's adjacent source/tests. Read COORDINATION before assigning workers.
4. Assign exact exclusive canonical paths; keep shared headers/build/docs coordinator-owned unless explicitly transferred. Every milestone-08 task (T04a-T04d), CLI-T01, and M09-T01..T04 are done, with their write sets released. M09-T05's row in "M09-Tasks (in progress)" has its exact write set; assign it and start.
5. Build with mk after edits. User runs behavioral tests/benchmarks; never execute scripts, cleaning, installation or source-control commands through the compilation-only mk tool.
6. Sub-agents: read `/usr/dave/local_models.md` "Economic Review" first -- the current policy is cloud Sonnet (`claude-sonnet-5`) for bounded read-only audits with a numbered deliverable and mandatory source quotes (the R3-F02 audit is the template: seven files in order, seven questions, ~35 calls, `maxrounds 40`), the coordinator for every edit, and local models only for second-opinion review or doc summaries. If working with a sub-agent again: raise `maxrounds`/`autocontinue` with two SEPARATE ctl writes, not combined with a `model` write in the same call -- combining them was observed this session to silently reset both back to their defaults (20/0), costing a wasted round-capped exchange before it was caught. Verify by reading `ctl` back before sending the task prompt.
