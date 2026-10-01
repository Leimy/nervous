# M10-T04a first sharded-lock results

Evidence: user-run `bench/runs/shards-first.txt`, read by the coordinator.
The capture has all three unprofiled repeats (mode order reversed on repeat
2), four separate profiled controls, and ends `shards: all cases passed`.
Every diagnostic invocation checks result 5600000, exact accepted message
and completed-process counts, no remaining live processes/faults/drops/failed
or outstanding collections, and local/global counter partitions. The user
also reports the requested correctness tests passed; suite logs were not
independently inspected. The interactive DEL/no-orphans check is unconfirmed.

## Same-build comparisons

Medians of three UNPROFILED runs, seconds. Total arithmetic work is matched:
4 workers x 700000 or 16 x 175000. 'Initial heap' is the reported capacity,
not a new minimum or budget. 64 corresponds to headroom 0; 4096 to 1024.
Global/sharded are the same executable and interpreter/GC/queue code; only
the runtime lock domain configuration differs. Compare within this capture,
not to older scale/retry captures with different layouts.

| schedulers | workers | initial heap | placement | global | sharded |
|---|---|---|---|---|---|
| 2 | 4 | 64 | balanced | 1.248 | 0.549 |
| 4 | 4 | 64 | balanced | 1.398 | 0.311 |
| 2 | 16 | 64 | balanced | 1.312 | 0.550 |
| 4 | 16 | 64 | balanced | 1.438 | 0.327 |
| 2 | 16 | 64 | natural | 1.225 | 0.536 |
| 4 | 16 | 64 | natural | 1.446 | 0.327 |
| 2 | 4 | 4096 | balanced | 0.576 | 0.419 |
| 4 | 4 | 4096 | balanced | 0.812 | 0.226 |
| 2 | 16 | 4096 | balanced | 0.547 | 0.418 |
| 4 | 16 | 4096 | balanced | 0.833 | 0.228 |
| 2 | 16 | 4096 | natural | 0.506 | 0.412 |
| 4 | 16 | 4096 | natural | 0.814 | 0.241 |

Four-worker balanced raw triplets, nanoseconds (repeat order 1/2/3):

| schedulers | initial heap | mode | elapsed triplet |
|---|---|---|---|
| 2 | 64 | global | 1248017602 / 1271721214 / 1234373133 |
| 2 | 64 | sharded | 549833326 / 548569635 / 547195741 |
| 4 | 64 | global | 1384109807 / 1408955165 / 1397672654 |
| 4 | 64 | sharded | 313471857 / 308853264 / 310720605 |
| 2 | 4096 | global | 587583544 / 576497262 / 559700353 |
| 2 | 4096 | sharded | 421912825 / 417021535 / 418891682 |
| 4 | 4096 | global | 832633044 / 806938283 / 812246944 |
| 4 | 4096 | sharded | 227586032 / 225979773 / 226039029 |

## Scaling and interpretation

Four-worker N=1 lock-off medians are 0.917 s (64 words) and 0.809 s (4096).
Sharded N=2 therefore gives about 1.67x / 1.93x scaling, and N=4 gives about
2.95x / 3.58x. Against the SAME-N global control, sharded N=4 is about
4.50x faster at 64 words and 3.59x faster at 4096. These are different ratios;
do not call the former a 4.5x speedup over single-scheduler execution.

N=1 forced-lock medians, global versus sharded, are 0.907 / 0.905 s at 64
words and 0.803 / 0.809 s at 4096. Differences are small relative to the
multicore effect and do not establish a meaningful single-core improvement.

Counters match the design. In repeat 1, balanced N=4 / 64 words has 646213
logical global sections in the control. The sharded run has 646201 local
sections and ONLY 21 global sections (total 646222). Both execute 215393
dispatches and 215384 collections, the same reductions and GC word counts.
In balanced N=4 / 4096, sharded runs have 19..22 global sections instead of
roughly 123460 global sections. Global section counts are not physical QLock
counts: each all-shard section takes N+1 QLocks. Host setup and context-free
stop calls are uncounted in both modes.

Natural-placement 16-worker cases also improve strongly, so this is not a
win limited to setup-balanced placement. Larger heaps improve sharded scaling
further; they are diagnostic pre-sizing, not a runtime heap-policy change.
The results support KEEPING this first slice: global dispatch/GC protection
was a major bottleneck in this workload. They do not isolate the remaining
allocator/cache/OS costs or establish scaling beyond four schedulers.

## Profiling is not the throughput score

The profiled balanced N=4 / 4096 pair takes 0.915 s global versus 0.466 s
sharded, while the unprofiled medians are 0.812 / 0.226 s. Per-proc measured
acquisition elapsed drops from about 0.670..0.674 s to 0.074..0.079 s.
These timings include clock/acquisition overhead, omit unlock/handoff and
overlap across procs; they must not be summed and subtracted from wall time.
Profiling substantially perturbs the sharded workload. Use the unprofiled
repeats to score performance, these rows only as explanatory evidence.

## Checkpoint and remaining scope

Keep M10-T04a as a user-observed correctness-green and measured CPU/GC scaling
checkpoint. No further runtime edit was made when recording these results.
Suggested commit subject: `M10-T04a: shard local scheduling and GC; preserve
all-shard structural gate` (the coordinator does not execute source control).

This is not a complete mailbox split or full multicore acceptance. Send/ref/
steal/idle still use all shards; the fixture sends only one result per worker.
No message-heavy ring/sieve speedup is inferred. Before narrowing that gate,
measure representative message-heavy workloads and retain explicit slot
lifetime/routing, ownership, wake and termination protocols. D084/T02's
per-dispatch deadlines, deterministic wake-window coverage, the interactive
interrupt check and mandatory R4 remain open.
