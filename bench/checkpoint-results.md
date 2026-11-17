# M10 checkpoint-20674 results

Follow-up: `T02-results.md` reviews checkpoint-779643 after the deadline-heap
implementation. T02's functional run is now accepted; the additional timing
increase remains an explicit performance follow-up. This file is the
historical pre-T02 result, not the current task state.

User-run evidence: `bench/runs/checkpoint-20674/`, produced by
`rc tests/checkpoint.rc`. The coordinator inspected the summary, build log,
all three full regression logs, shard smoke log, small comparison, and all
three raw multicore captures and their tables. No runtime changes were made
while recording these results.

## Correctness and runner validation

- Build completed without diagnostics.
- Normal, GC-stress inline, and GC-stress off-process suites each end with
  `all nervous regression suites passed`. All include the local-shard
  independence probe, both locking modes at N=1/2/4, off-process lifecycle
  regressions, and source multicore order/migrate/exitstorm checks.
- All eight shard smoke cases passed their result/accounting checks. These
  short cases are correctness controls, not the full throughput comparison.
- The checkpoint runner completed its first real execution successfully.
  Its failure-handling branches were not deliberately fault-injected.

The three multicore captures have the expected ring root value 2000000 and
CPU/GC root value 5600000 at every N. All 27 invocations report zero faulted
processes, dropped messages and failed collections. Per shape, invariant
counts are consistent across N and repeats:

| shape | spawned | completed | exited | sent |
|---|---:|---:|---:|---:|
| ring | 1001 | 1001 | 0 | 2003001 |
| sieve | 670 | 1 | 669 | 234091 |
| CPU/GC | 5 | 5 | 0 | 4 |

The benchmark runner discards sieve stdout, so these captures establish
successful execution and accounting, not an independent prime-output check.
Do not treat scheduling-dependent peak-live, dispatch, reduction or GC counts
as exact cross-N regression invariants.

## Multicore timings

Medians of the three `stats: wall` values, seconds; these are the ordinary
CLI with natural worker placement, not the setup-balanced scale diagnostic.

| shape | N=1 | N=2 | N=4 |
|---|---:|---:|---:|
| ring 1000 2000 | 0.891 | 1.083 | 1.126 |
| sieve 5000 | 0.146 | 0.318 | 0.341 |
| CPU/GC, 4 x 700000 | 0.622 | 0.354 | 0.202 |

Raw wall triplets in repeat order:

| shape | N=1 | N=2 | N=4 |
|---|---|---|---|
| ring | .869 / .891 / .902 | 1.090 / 1.083 / 1.079 | 1.105 / 1.129 / 1.126 |
| sieve | .145 / .147 / .146 | .318 / .320 / .290 | .344 / .341 / .341 |
| CPU/GC | .634 / .622 / .618 | .354 / .359 / .353 | .198 / .202 / .270 |

CPU/GC median speedup is 1.76x at N=2 and 3.08x at N=4 relative to this
capture's N=1. In repeat 3 the N=4 run takes .270 s and scheduler 3 does zero
dispatches; the other three carry the work. This is a visible load-balance
limitation, not a wrong result. Do not hide it behind the median.

Ring is about 22% slower at N=2 and 26% slower at N=4 than N=1. A single
circulating token offers little parallel work. Sieve is about 2.18x/2.34x
slower than N=1. The retained split does not establish message-heavy scaling.
For historical context, T01-multi.txt had sieve .638/.673 s at N=2/4; the
new .318/.341 medians are roughly half those times. Ring's historical
.974/1.056 s is lower than the new 1.083/1.126. These are different captures,
not paired same-build controls; do not attribute every difference to locks.

The raw lock rows support the known remaining serialization boundary:
repeat 1 N=4 CPU/GC has only 31 counted global sections, while ring has about
2.01 million and sieve about 239 thousand. A global section acquires N+1
physical QLocks. Ring also leaves two schedulers with zero dispatches in all
three N=4 runs. Unprofiled acquisition-time fields are zero because timing is
disabled, NOT evidence of zero contention. These counts motivate investigation
but do not measure how much wall time each remaining bottleneck costs.

## Single-scheduler caution

`small-compare.log` reports **6 shapes compared, 0 count changes, 4 cost
flags** against T01-small.txt. Wall times increased for all six shapes:

- ring 10: +3.5%; ring 1000: +6.7%; ring-up: +6.9%.
- waiters 1000: +10.7%; waiters 10000: +10.2%; sieve: +10.1%.

Three flags are wall time; the fourth is waiters-10000 ns/message (+10.2%).
Memory high-water is unchanged or within 0.5%. Exact count agreement is a
correctness pass, not proof that cost is unchanged. This is one small capture
against an older baseline; it does not separate machine/load effects from a
real N=1 regression. Keep the flags visible. A performance-focused follow-up
should use repeated, interleaved old/new controls on an otherwise idle machine
before declaring either a regression or mere noise.

## Disposition and next work

Retain the first-slice sharding checkpoint with directly inspected regression
evidence and a first post-split message-heavy measurement. No need to repeat
the same correctness run merely because the measurements have cost flags.
Do not call this full T04 or R4 acceptance, or an all-workloads speedup.

Recommended next implementation is D084/T02 (deadlines checked while runnable
work exists), rather than another mailbox-lock change based solely on these
numbers. Interactive DEL/no-orphans, deterministic wake-window fixtures,
fragment-leak coverage and R4 remain open. Preserve this checkpoint in source
control before changing runtime behavior; commit state is not established by
these logs.
