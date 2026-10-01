# M10-T04a: sharded blocking locks, first slice

Implementation is user-observed correctness-green and retained after the
complete same-build benchmark capture: `bench/shards-results.md`. The user
reports requested tests passed; suite logs were not independently inspected.
This is a first-slice checkpoint, NOT full T04 or R4 acceptance. User
authorized trying sharded blocking locks after rejecting QLock retries. No
libthread conversion, lock-free queue, heap-sizing or language change is
included. This implementation note describes the current protection map;
D081's stage-1 design and the earlier full stage-2 proposal are historical
context, not claims that the entire lock split is already implemented.

## Mechanism

Each malloc'd NvSched contains a QLock. Local operations take only their
scheduler's shard. Structural/cross-owner operations take NvScheduler.lock
(the machine gate), then ALL scheduler shards in ascending index order.
Release the shards in reverse order, then the gate. No path can request the
machine gate while holding a shard. Heap Lock is nested only for the short
owner/publication operation; collector children never take a runtime lock.

| Operation/state | Protection |
|---|---|
| Local dispatch/pop, offcpu/pendingwake, yield/requeue | Owner's shard |
| Receive begin/advance/need/take/wait/deadline callbacks | Owner's shard; process cannot migrate on CPU |
| Local GC mark/launch/fold, owned deadline/idle scans | Owner's shard; heap Lock for child publication |
| Directory growth, slot allocation/reuse, spawn + exec publication | Machine gate + all shards |
| Process reap, nlive/freehint, result/root/completion aggregates | Machine gate + all shards |
| Send/copy/append/wake, ref sequence | Machine gate + all shards (NOT yet narrowed) |
| Steal, both queues and owner change | Machine gate + all shards (NOT yet a pair-only operation) |
| Idle mask, kick, sleep preparation, exact termination snapshot | Machine gate + all shards |
| Output line | Independent iolock; no runtime lock |
| Interpretation, collection body, rfork, blocking waits | No runtime lock |

The all-shard gate deliberately preserves the old atomic structural and
cross-owner operations. It is not a scalable final mailbox implementation.
In particular, remote messaging and ref allocation can still stop local
queue access briefly, and global send still includes fragment allocation.

## Why a local shard is sufficient

1. Directory realloc, nslot/nlive updates, slot reuse and owner changes need
   every shard. Holding ANY shard stabilizes that metadata. This avoids an
   unlocked read of the resizable directory and avoids adding lifetime pins.
2. A local shard authorizes reading/mutating ONLY its owned process state,
   mailbox/deadline, exec bookkeeping and queue. Table scans must test owner
   BEFORE reading exec/offlaunched/state/heap. gcfoldall and the idle sweep
   now do this. Unowned slots are skipped even though their addresses are
   stable: another scheduler may be changing their state concurrently.
3. A remote append/wake holds the destination shard as part of all shards.
   Receive's exhausted-scan check and oncpu/pendingwake handshake remain
   mutually exclusive with it. Enqueue/state transitions stay atomic; no
   'runnable but not yet enqueued' window was introduced.
4. Collection marks NvHeapCollecting before releasing the local shard.
   Steal holds all shards and refuses collecting/offlaunched execs. Child
   completion is observed under heap.lock and folded by the launcher;
   successful forks and final semaphore credits retain D074/R3-F06 lifetime
   accounting. The idle sweep checks offlaunched BEFORE inspecting heap.cur
   or heap.words, which a child could still be rewriting.
5. Dispatch establishes oncpu/Prrunning before dropping local protection.
   A rare backlog kick can then request the all-shard gate without an
   upgrade deadlock. The running process remains unstealable in that gap.
6. A terminal result clears oncpu but retains Prrunning, off every queue,
   while dropping the local shard and taking the all-shard gate for reap.
   It cannot migrate or be slot-reused; a racing message is appended then
   disposed during reap, exactly as a pre-reap send in stage 1. A terminal
   result with any other lifecycle state is an error.
7. Idle's own-queue recheck, steal, stale-credit drain, idle publication and
   snapshot remain one all-shard transaction. Wake credits persist across
   unlocking and sleeping. The idle stop decision is now COMMITTED under
   that same gate, not deferred to a release/reacquire stopmachine call.
   Collector/deadline/runnable checks and idle bits retain their old meaning.

## Controls and measurement

NvScheduler.sharded is initialized to 1. It is configuration, never mutable
while a machine is running. Diagnostic/test control 0 restores the single
stage-1 QLock, with the same interpreter, queues, GC and idle/reap code.
N=1 still skips runtime locking unless explicitly forced on; N>1 cannot
turn locking off. There is no new CLI flag or environment setting.

lockacq counts LOGICAL protected sections. With sharded=1 a local section
uses one QLock; a global section uses nsched+1. It is not a physical QLock
count. localacq + globalacq == lockacq, and localwaitns + globalwaitns ==
lockwaitns. Host setup/stopmachine with no scheduler context is uncounted,
as before. Timings are opt-in, include acquisition/clock overhead, omit
unlock/handoff costs and overlap across procs. They are not wall-time
attribution. Existing CLI totals/sched rows remain; new 'stats: locks N'
rows report the partition. scale reports and asserts both partitions.

Use bench/shards.rc for same-build global/sharded comparisons, not old
captures with different machine layouts/counters. It reverses pair order on
repeat 2, includes unprofiled N=1 lock-off/forced-lock controls, balanced
N=2/4 and natural-placement 16-worker controls, and separate profiled rows.
Heap headroom and total arithmetic work are matched within each comparison.
The full user-run capture has been reviewed: it ends with all cases passed.
Four balanced workers scale about 2.95x (64 words) / 3.58x (4096) at N=4
against the same-capture N=1 lock-off baseline. See bench/shards-results.md
for the paired medians, control ratios, accounting and profiling caveats.
No message-heavy scaling result is inferred.

## Validation

Build: `mk nervous tests benchmarks` (compilation only).

New schedtest checks:
- A child scheduler-0 step must finish within 2s while the parent holds both
  the machine gate and scheduler 1's shard. The child is forked BEFORE locks
  are held. On timeout the parent releases both, joins, then reports failure;
  success also asserts exactly two local sections and zero global sections.
- Both lock modes at N=1/2/4 run the same shared read-only bytecode with
  setup-balanced processes and real scheduler procs. They assert retained
  root value, exact completion/reap counts, empty queues and lock partitions.

User-run from project root, against working-tree binaries:

```rc
rc tests/run.rc
nervous_gcstress=1 rc tests/run.rc
nervous_gcoffload=1 nervous_gcstress=1 rc tests/run.rc
rc bench/shards.rc smoke
rc bench/shards.rc >bench/runs/shards-first.txt
```

The suites include per-sender order, migration/PID and exit-storm fixtures
at N=1/2/4. Also interrupt a long `./nervous -p 4 -r examples/ring.nv main
1000 200000` with DEL and check clean 'scheduler: interrupted', no orphaned
scheduler procs. Required deterministic idle/wake race fixtures and R4
remain open; this build/review is not full multicore acceptance.

## Read-only source review (not R4 acceptance)

A cloud Sonnet review inspected sched.c/process.c and both headers directly,
covering pairing/order, state protection, directory/owner scans, kick/reap
windows, send/wait/migration, GC publication/credit lifetime and idle/wakes.
The coordinator independently checked the critical release/reacquire gaps
and owner-before-foreign-read rules. No newly introduced concurrency defect
was found in this slice. Review is evidence, not a substitute for execution.

The review surfaced an inherited setter race: nvschedsetworklimit wrote
maxtermwork without the gate while spawn read it under the gate. The setter
now takes lockrt(s,nil), writes the value, and unlocks, synchronizing future
spawn configuration. IO/clock/locking configuration is explicitly quiescent.
Memory snapshots explicitly require no executing scheduler proc: locking the
runtime alone cannot stop private heap mutation during an interpreter quantum.
The note-handler stop word remains the pre-existing lock-free exception;
'first reason wins' under the gate is not a claim about races against notes.
That protocol needs the interactive interrupt check and remains R4 scope.

## Next boundary, only after evidence

CPU/GC scaling is restored in the measured workload. Before narrowing send
or stealing, measure message-heavy workloads and choose an explicit slot
lifetime/routing protocol with destination/pair locks or owner-local inbound
queues. Do not simply remove the all-shard gate: it
currently supplies stable table routing, ownership-change exclusion,
publication ordering and the exact termination snapshot. Deadline-heap work
(D084/T02) remains separate; deadlines still fire only on the idle path.
