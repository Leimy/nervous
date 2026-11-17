# M10-T02 checkpoint-779643 review

Evidence: user-run `bench/runs/checkpoint-779643/`. Inspected the summary,
build log, all three full regression logs, shard smoke log, raw small capture
and comparison, and all three raw multicore captures and their tables.
Comparison below uses the immediate pre-T02 `checkpoint-20674` results as
well as the runner's historical T01 baseline. No runtime change made during
this review; original captures remain untouched.

## Disposition

Accept T02's functional checkpoint: indexed deadlines, expiry under runnable
load, and authoritative runtime maxduration have passed the requested tests.
Do NOT interpret this as unchanged performance or full M10/R4 acceptance.
Record a performance follow-up before another runtime optimization: the
post-T02 timings are consistently higher, including workloads with zero
recorded timer wakes. These sequential captures are not a controlled A/B
experiment, so they establish a regression signal, not its precise cause.

## Correctness evidence

- Build passes without diagnostics (incremental relink, not a clean rebuild).
- Normal, stress-inline and stress-offload each finish with
  `all nervous regression suites passed`.
- All three logs explicitly include the new heap growth/rearm/removal/reuse/
  migration/duration-limit test, continuous runnable-load expiry test,
  real scheduler-proc deadline tests in both locking modes at N=1/2/4,
  and the collecting-owner deadline/message precedence test.
- Existing timeout, GC, source and multicore order/migration/exit-storm
  tests remain green. C fixtures use their own GC settings; the environment
  modes do not imply every C fixture itself ran under three GC policies.
- All eight global/sharded smoke cases verify their arithmetic/accounting
  and the log ends `shards: all cases passed`. Short smoke timings are not
  a replacement for the full sharded throughput comparison.
- All 27 multi invocations report zero faulted processes, dropped messages
  and failed collections. Ring prints 2000000, CPU/GC prints 5600000 in
  every run. Invariant spawned/completed/exited/sent counts match the prior
  checkpoint: ring 1001/1001/0/2003001, sieve 670/1/669/234091,
  CPU/GC 5/5/0/4. Sieve stdout is discarded by the runner, so this capture
  does not independently verify its prime output.

## Single-scheduler comparison

`small-compare.log`: six shapes, **zero count changes, 12 cost flags** against
T01-small.txt. Those are wall time and ns/message for each shape, not twelve
independent problems; the two timing metrics share the same elapsed time.
All six wall times are 13.3..20.0% above T01. No memory cost flag is reported.

Against the immediate pre-T02 capture (one small sample per checkpoint):

| shape | pre-T02 seconds | T02 seconds | change |
|---|---:|---:|---:|
| ring 10 200000 | .980 | 1.073 | +9.5% |
| ring 1000 2000 | .888 | .945 | +6.4% |
| ring-up 1000 2000 | .866 | .934 | +7.9% |
| waiters 1000 100000 | .083 | .090 | +8.4% |
| waiters 10000 100000 | .097 | .105 | +8.2% |
| sieve 5000 | .142 | .151 | +6.3% |

The high-water increment versus pre-T02 is exactly 8192 bytes for each
one-process-chunk case and 81920 bytes for the 10000-waiter case. This is
consistent with the timerindex/layout addition growing each NvProcess by
8 bytes and allocation in 1024-slot chunks (ten chunks for 10002 processes).
Do not mistake the large percentage on tiny workloads for a per-live-process
heap increase or evidence of a growing timer leak. Actual indexed timer
arrays, when used, have separate retained capacity; these high-water samples
are not an allocator-independent snapshot of that capacity.

## Multicore medians

Median `stats: wall` of the three runs, seconds. Each cell is
pre-T02 -> T02 (change). These compare two different captures, not interleaved
executables on the same run.

| shape | N=1 | N=2 | N=4 |
|---|---|---|---|
| ring 1000 2000 | .891 -> .980 (+10.0%) | 1.083 -> 1.149 (+6.1%) | 1.126 -> 1.197 (+6.3%) |
| sieve 5000 | .146 -> .157 (+7.5%) | .318 -> .331 (+4.1%) | .341 -> .355 (+4.1%) |
| CPU/GC | .622 -> .668 (+7.4%) | .354 -> .381 (+7.6%) | .202 -> .211 (+4.5%) |

T02 raw triplets (repeat order):

| shape | N=1 | N=2 | N=4 |
|---|---|---|---|
| ring | .918 / .980 / .983 | 1.142 / 1.149 / 1.152 | 1.197 / 1.197 / 1.192 |
| sieve | .155 / .158 / .157 | .333 / .331 / .327 | .358 / .351 / .355 |
| CPU/GC | .666 / .668 / .675 | .381 / .383 / .380 | .211 / .214 / .211 |

CPU/GC scaling survives: about 1.75x at N=2 and 3.17x at N=4 relative to
this capture's N=1. All four schedulers do comparable work in every N=4 CPU
run, unlike pre-T02's third repeat; that observation does not establish a
load-balancing fix (none was implemented).

Ring remains slower at N=2/4 than N=1; two N=4 schedulers do zero dispatches
in all repeats, consistent with its single-token dependency. Sieve is about
2.11x/2.26x slower at N=2/4 than N=1. Message-heavy scaling is not solved.
All raw multi rows report zero timer wakes. Unprofiled lock-time zeros mean
timing is disabled, not zero contention.

## Next investigation and remaining gates

Recommended next step: a bounded performance investigation, preserving the
accepted deadline semantics. Use the exact pre-T02 and T02 source/binaries
under matched compiler/configuration, alternate execution order, repeat on
an otherwise idle machine, and retain raw counts/timings. Compare N=1 first
(ring, sieve, CPU/GC); this separates empty-deadline-path and layout costs
from multicore scheduling. Inspect the dispatch helper and untimed receive
clear/remove path, but do not assume either explains the whole difference;
CPU frequency, system load, allocator and code/data layout remain confounders.
Do not disable busy timer checks globally or move them back to idle just to
recover a benchmark number. The existing captures alone cannot isolate causality.

Still open: deterministic idle-window wake fixture, interactive DEL/no-orphans,
fragment-leak accounting under exit traffic, mandatory R4, and full T04.
Heap-allocation-failure rollback remains source-reviewed, not fault-injected.
The captured summary's OPEN line is the runner's static pre-review message;
it does not negate the now-reviewed T02 functional pass. Commit state remains
unconfirmed; no source-control operation was performed.
