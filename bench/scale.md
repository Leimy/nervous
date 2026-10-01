# Multicore scaling diagnostic

This is a diagnostic, not a new runtime policy or a replacement for
`bench/run.rc multi`. Build with `mk benchmarks`, then from the project root:

```rc
rc bench/scale.rc smoke && rc bench/scale.rc >bench/runs/scale-first.txt >[2=1]
```

The smoke batch checks short runs (13 iterations, deliberately not a multiple
of seven), pre-sizing, forced N=1 locking, balanced placement and profiling.
The full batch repeats each unprofiled case three times, then runs separate
profiled controls. Successful completion ends with `scale: all cases passed`.
The first user-run capture is `bench/runs/scale-first.txt`; smoke and full
completed successfully, including all three repeats and profiling. The agent
does not execute these scripts.

## What is timed

`scale.c` compiles the actual `bench/cpubound.nv`. It host-spawns the root at
`collect(workers, 0)` and each child at `worker(rootpid, iters)`, so it uses the
existing arithmetic loop, result send, and result receive implementation.
It does NOT execute `main` or `start`: creation and pre-sizing are completed
before the timed interval. Every child starts on scheduler 0 unless the
balanced-placement control moves it round-robin before execution.

The interval is `nvmachinerun`: scheduler proc creation, computation, collection,
result messages, exits and scheduler shutdown/drain. Parsing, compilation,
process creation, explicit pre-sizing, reporting and final runtime teardown
are excluded. Compare this tool's cases with EACH OTHER, not its elapsed
numbers directly with the CLI's whole-program wall statistic.

Each run checks the aggregate arithmetic result against an independent
period-seven formula, exactly workers messages accepted, no dropped messages,
workers+1 processes completed, no remaining live processes, no faults, and no
failed/outstanding collections. An interrupt stops the machine cleanly and
fails the diagnostic rather than reporting a successful partial run.

## Controls

- Four workers x 700000 iterations and sixteen x 175000 each do the same
  2.8M total arithmetic iterations. Worker/collector setup and reporting
  overhead differ slightly. Both return 5600000.
- Headroom 0 leaves normal initial worker capacities. Requests 128 and 1024
  use the existing stopped-owner `nvexeccollect` API on workers only. With
  the current collector these should give initial capacities 64, 512 and
  4096 words; the tool reports actual capacities, which are authoritative.
  These are extra-space requests, NOT a heap budget or a changed minimum.
- N=1 lock 0 versus lock 1 isolates uncontended runtime locking overhead.
  N>1 always requires locking. It cannot be disabled in a multicore case.
- Natural placement (balanced=0) starts all children at home on queue 0,
  requiring ordinary stealing. Balanced placement distributes workers
  round-robin before starting any scheduler. Ordinary stealing remains
  enabled in both modes. Setup transfers do not increment steal counters.
- Two independent N=1 processes each do the full 4x700000 workload. Compare
  EACH one's elapsed time with the isolated N=1 baseline, not their sum.
  If each remains near baseline, doubled throughput is feasible without
  shared runtime/allocator state. This is not a precise synchronized-start
  measurement; the two processes launch sequentially in the background.
  Their output is buffered into separate temporary files and included in
  the capture. Success markers check both background outcomes.

## Read the output

`result:` contains elapsed nanoseconds and totals. `sched N:` shows work
balance, demand/idle GC, steals, idle sleeps and runtime lock acquisitions.
GC input/output word totals help distinguish collection frequency and work.
Pre-sizing collections are setup and absent from those scheduler GC counters;
host setup takes locks with no scheduler context and is uncounted in the
reported acquisition totals, as are setup-only ownership transfers.

Use medians/ranges of the THREE unprofiled repeats, not the fastest result.
Compare N=2/4 with the matching N=1 case at the same worker count/headroom.

- More heap headroom restores scaling: frequent collections and their lock/
  allocator interactions are implicated. It does not by itself distinguish
  allocator contention from runtime-lock contention or other GC overhead.
- Balanced placement helps while natural placement does not: initial work
  distribution / steal policy is limiting. Check dispatch distribution.
- Sixteen workers help versus four: four-worker load balance is a confound,
  not evidence that all N schedulers were equally busy.
- Independent processes scale but the shared machine does not: look at shared
  runtime synchronization, allocator synchronization and cache traffic.
- Independent processes also slow badly: investigate host CPU availability,
  VM scheduling and contention before attributing all loss to Nervous.

Profiling reads clocks around every lock acquisition, dispatch and collection.
It can drastically change contention; these rows are NOT throughput scores.
`lock-acquire-elapsed` includes acquisition and timing overhead, not just
blocked time. It omits unlock/handoff costs. Exec includes host callbacks and
any lock acquisition within them. Per-proc timings overlap, so summing them
and subtracting from wall time is invalid. Zero lock time in an unprofiled
run means UNMEASURED, never "uncontended". Idle sleep counters do not include
blocking inside QLock or libc malloc.

Direct invocation (all numeric bounds are checked):

```text
bench/scale bench/cpubound.nv schedulers workers iters headroom lock profile balanced [sharded]
```

No heap minimum, collector algorithm, mailbox protocol, run queue policy or
runtime lock is modified by the original diagnostic. The existing correctness
suites remain the acceptance bar for any subsequent runtime optimization.

## First capture: measured conclusions

Medians of three unprofiled runs, seconds. N=1 skips the runtime lock.

| workers | initial capacity | N=1 | N=2 | N=4 |
|---|---|---|---|---|
| 4 | 64 | 0.879 | 1.196 | 1.268 |
| 4 | 512 | 0.795 | 0.602 | 0.866 |
| 4 | 4096 | 0.802 | 0.525 | 0.793 |
| 16 | 64 | 0.877 | 1.157 | 1.371 |
| 16 | 512 | 0.804 | 0.547 | 0.871 |
| 16 | 4096 | 0.793 | 0.486 | 0.743 |

Independent processes each took 0.888..0.920 s versus the isolated 0.879 s
median, supporting nearly doubled throughput at two CPUs without shared
runtime/allocator state. Balanced N=4 placement still took 1.324 s (64 words)
and 0.785 s (4096 words), so poor stealing is not the sole bottleneck.
N=1 forced locking costs about 1% at 64 words. Larger worker heaps remove
98.7% of collections but only about 9% of N=1 elapsed time; the much larger
N=2 benefit implicates the interaction of collections, dispatch, runtime
synchronization and allocation, not just collection execution cost.

In the profiled balanced N=4 / 4096-word case, each scheduler has about
0.208..0.209 s exec, 0.0012 s GC, and 0.653..0.659 s lock acquisition against
0.890 s wall. This points at runtime-lock contention in the INSTRUMENTED
run; profiling itself changes contention, including a clock read while
holding the lock. It is not an unprofiled time attribution.

## Bounded QLock retry experiment: REJECTED AND REMOVED

User capture: `bench/runs/lockretry-first.txt`. All correctness/accounting
checks passed. The experiment made at most 1/32/128 canqlock attempts before
falling back to ordinary qlock, with 0 as the unchanged acquisition algorithm.
Three unprofiled repeats used balanced placement, reversed attempt order on
the middle repeat, and otherwise identical work/headroom/quantum. Medians,
seconds (compare within THIS capture, not to the older machine/layout run):

| schedulers | initial heap | ordinary qlock | 1 attempt | 32 attempts | 128 attempts |
|---|---|---|---|---|---|
| 2 | 64 | 1.300 | 1.286 | 1.404 | 1.809 |
| 4 | 64 | 1.404 | 1.405 | 1.570 | 2.094 |
| 2 | 4096 | 0.622 | 0.625 | 0.642 | 0.670 |
| 4 | 4096 | 0.851 | 0.855 | 0.894 | 1.006 |

At N=4 / 64 words, all three 32-attempt runs reported ZERO retry successes
against roughly 20M failed attempts per run. The 128-attempt runs likewise
reported zero retry successes and roughly 82M misses per run. At N=4 / 4096
words retry successes were only 2..8 per run for either retry bound, against
millions of misses. One attempt gives no consistent meaningful improvement;
32/128 are substantially worse, including instrumentation costs.

Mechanism: canqlock acquires the same QLock on success (not a check followed
by a second acquisition), but its failed attempts still touch shared state.
Local libc's qunlock keeps q->locked set when handing to a queued waiter, so
there is no free window for retries during sustained queued handoff. This
explains why preserving queued-waiter priority does not make retrying useful.
The results reject THIS bounded retry approach, not every possible adaptive
lock implementation. No further spin-count tuning is justified here.

Removed lib/sched.c's retry branch, experimental NvSched/NvScheduler fields,
scale's optional retry argument/counters and bench/lockretry.rc. The original
scale interface and ordinary qlock acquisition have been restored. Captures
and this evidence remain. Next work should reduce shared-runtime lock traffic
or split its protection domains with an explicit ownership/lock-order design,
not replace sleeping synchronization with a raw spinlock. Preserve the oncpu/
pendingwake handshake, GC exclusion, PID lifetime, FIFO, lost-wakeup and
termination invariants and validate normal/stress/off-process/multicore tests.

## Sharded blocking locks: first slice RUN, CPU/GC WIN

Current mechanism/protection map: `docs/sharding.md`. Local dispatch, yield,
receive/deadline callbacks, and GC bookkeeping now use only the owner shard.
Structural/cross-owner operations still take machine gate + all shards; sends,
refs, steals and exact idle/termination are NOT yet independent. This removes
common CPU/GC-path global acquisitions without adding a partially enqueued
state or an unlocked read of the resizable chunk directory. User-observed
correctness tests pass; full capture bench/runs/shards-first.txt is complete
and reviewed. Paired medians and analysis are in bench/shards-results.md:
N=4 balanced four-worker global -> sharded is 1.398 -> 0.311 s at 64 words,
0.812 -> 0.226 s at 4096. This supports retaining the first slice, not full
multicore acceptance or a message-heavy scaling claim.

The optional final argument is now `sharded` (0|1, default 1), NOT the removed
locktries argument. 0 uses the old single QLock protection domain through
the SAME interpreter/queue/GC/idle code; 1 uses local shards and all-shard
structural gates. Never change this configuration while executing. Existing
scale.rc calls remain valid, but use the dedicated same-build comparison:

```rc
rc bench/shards.rc smoke
rc bench/shards.rc >bench/runs/shards-first.txt
```

Full repeats are unprofiled, reverse mode order on repeat 2, keep work/heap/
placement matched, and include N=1 lock-off/forced-lock and natural-placement
controls. Profiled controls are separate. Run the normal/stress/off-process
suites BEFORE trusting performance, using the working-tree executable.

New `lock domains N:` rows partition LOGICAL protected sections; the tool
asserts local+global == lockacq. One global section in sharded mode takes
nsched+1 physical QLocks, so the count is NOT a hardware/physical lock count.
`profile domains N:` likewise partitions acquisition-elapsed time. A mostly
local CPU case should move nearly all common acquisitions to local while
retaining a small number of global send/reap/idle/kick sections. Investigate
large global counts rather than declaring 'sharded' sufficient by itself.
Old retry/scale captures remain historical evidence, not identical-layout
controls for this new build. The reviewed sharded capture establishes the
CPU/GC-path win; see shards-results.md for scope and remaining checks.
