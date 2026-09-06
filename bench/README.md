# Benchmarks

Test-evidence correction (T04e): historical environment-prefixed suite passes did not prove full CLI stress, because test runners used `rfork E`. The CLI/environment repair compiles; corrected normal/stress runs are pending in STATUS. Existing benchmark measurements are unchanged; this correction does not turn them into stress measurements.

These measure the scheduler and message passing. They are deliberately not in `tests/run.rc`: they take seconds to minutes, and their output is numbers to compare, not pass/fail. Run them with

```text
rc bench/run.rc          # small shapes, a few seconds each
rc bench/run.rc large    # D059 motivating sizes; needs memory for 32768+ processes
```

Every run passes `-s`, so each prints `nervous` scheduler statistics to stderr: wall time, processes spawned and peak live, dispatches and reductions, messages sent and nanoseconds per message, and the heap high-water mark with a rough bytes-per-peak-process figure. Ring hops are one message each, so for the ring shapes "ns per message" is the per-hop cost.

## Shapes

- `examples/ring.nv main nodes laps` - a token circulates a ring built so it travels *downward* through process slots. Root value is `nodes * laps`, the hop count.
- `bench/ring-up.nv main nodes laps` - the same ring built to travel *upward*. Control for D059: the two must cost the same per hop. Under the pre-D059 rotating slot scan they differed by ~20x at 65535 nodes.
- `bench/waiters.nv main waiters pings` - two processes ping-pong while `waiters` others sit blocked in receive. Tests whether blocked processes cost the running ones anything. Root value is the pong count.
- `examples/sieve.nv main limit` - the process-chain prime sieve; message volume grows roughly as `limit * primes(limit)`, and every filter is a tail-recursive receive loop.

## Baseline

Measured on the D059 build (FIFO run queue), single scheduler, one virtualized core, before milestone 08 (memory). `time` output; per-hop derived.

| shape | hops | wall | per hop |
|---|---|---|---|
| ring 10 x 200000 | 2.0M | 4.2 s | 2.1 us |
| ring 1000 x 2000 | 2.0M | 4.03 s | 2.0 us |
| ring 32768 x 2000 | 65.5M | 114.75 s | 1.75 us |
| ring 65535 x 2000, **pre-D059** | 131M | 5826 s | 44 us |

`rc bench/run.rc` (small) on the same build, from `-s`:

| shape | messages | wall | ns/message | reductions/dispatch | heap/peak process |
|---|---|---|---|---|---|
| ring 10 x 200000 | 2.2M | 4.31 s | 1960 | 24 | 1504 |
| ring 1000 x 2000 | 2.0M | 3.06 s | 1525 | 24 | 1728 |
| ring-up 1000 x 2000 | 2.0M | 3.01 s | 1499 | 24 | 1558 |
| waiters 1000 x 100000 | 201k | 0.31 s | 1547 | 24 | 1445 |
| waiters 10000 x 100000 | 210k | 0.36 s | 1732 | 24 | 1238 |
| sieve 5000 | 234k | 0.42 s | 1802 | 156 | 4158 (mailbox-heavy) |

Read: ring-down and ring-up agree (D059 holds); ten times more blocked processes costs the two busy ones ~12%, which is cache, not scheduling; a ring hop is 24 reductions, so ~65 ns per reduction, of which an interpreter loop should account for well under 10 -- the rest is allocation. The 10-node ring is slower per hop than the 1000-node ring because `main`'s lap bookkeeping is 10% of its hops instead of 0.1%. The sieve's per-process figure is mostly queued messages: `generate` runs 1000 reductions per quantum and gets hundreds of candidates ahead of `filter(2)`.

The pre-D059 row is the measurement that motivated D059 (`docs/decisions.md`): under the slot scan, per-hop cost was O(live processes) whenever the next runnable slot lay behind the cursor, which a downward ring makes every hop. With the run queue, per-hop cost is flat in process count.

At 65535 nodes the pre-08 representation could not be allocated in a modest VM; 32768 fit. Rough per-process footprint is 1-1.5 KB blocked (see `docs/questions.md`, "Milestone 08 - Memory", "Per-process footprint"). `-s`'s "per peak live process" figure is the number to watch when 08 lands.

Where the remaining ~2 us per hop goes, in order of size (all milestone 08 territory): the message copy on send plus a second full copy of the candidate at `recvbegin`; two mallocs per call or tail call for the frame and its register array; the argument tuple and message tuple allocations; and a Plan 9 malloc lock on each of those. System time is ~0: nothing here touches the kernel.

## Milestone 08 stage 1: interned atoms (D062)

`rc bench/run.rc` on the stage-1 build (atoms interned; `NvValue` struct, per-call frame mallocs, and all message copies otherwise unchanged). Same machine as the baseline.

| shape | messages | wall | ns/message | vs baseline | reductions/dispatch | heap/peak process | atoms |
|---|---|---|---|---|---|---|---|
| ring 10 x 200000 | 2.2M | 2.89 s | 1313 | -33% | 24 | 1128 | 8 |
| ring 1000 x 2000 | 2.0M | 2.12 s | 1059 | -31% | 24 | 1343 | 8 |
| ring-up 1000 x 2000 | 2.0M | 2.15 s | 1070 | -29% | 24 | 1450 | 9 |
| waiters 1000 x 100000 | 201k | 0.22 s | 1091 | -29% | 24 | 1268 | 10 |
| waiters 10000 x 100000 | 210k | 0.27 s | 1288 | -26% | 24 | 1152 | 10 |
| sieve 5000 | 234k | 0.32 s | 1368 | -24% | 169 | 4350 | 9 |

Read: a ring hop lost about a third of its cost from removing the atom `strdup`/`free` pairs alone (`loadk 'token`, the atom inside the `recvbegin` copy, the `getelem`, the `'true`/`'false` of every test, `'undefined` on empty scans) -- consistent with the pre-change estimate of ~18 malloc/free pairs per hop, roughly six of them atoms. Ring-down and ring-up still agree. Per-process footprint fell 10-25% because blocked processes no longer hold an atom string per register. The sieve's higher heap-per-process moves with its reductions-per-dispatch (156 to 169), i.e. how far `generate` runs ahead of `filter(2)`, not with this change. What remains is stage 2-3 territory: two frame mallocs per call, the tuple mallocs, and the three remaining copies of each message.

## Milestone 08 stage 2: tagged terms, frame stack, fragment mailboxes (D061, D064, D065)

`rc bench/run.rc` on the stage-2 build: a term is one tagged word, registers live on a contiguous frame stack, a message is copied exactly once into a self-contained fragment that is the mailbox entry, `recvbegin` points into it and `recvtake` adopts it. No collector yet: each process's heap is a chunk list freed at exit. Same machine as the baseline.

| shape | messages | wall | ns/message | vs stage 1 | vs baseline | reductions/dispatch | heap/peak process |
|---|---|---|---|---|---|---|---|
| ring 10 x 200000 | 2.2M | 0.77 s | 351 | -73% | -82% | 24 | 27.9 MB (see below) |
| ring 1000 x 2000 | 2.0M | 0.60 s | 299 | -72% | -80% | 24 | 337 KB |
| ring-up 1000 x 2000 | 2.0M | 0.62 s | 308 | -71% | -79% | 24 | 337 KB |
| waiters 1000 x 100000 | 201k | 0.06 s | 310 | -72% | -80% | 24 | 24 KB |
| waiters 10000 x 100000 | 210k | 0.11 s | 513 | -60% | -70% | 24 | 6.4 KB |
| sieve 5000 | 234k | 0.09 s | 388 | -72% | -78% | 169 | 70 KB |

Read: a ring hop is now ~300 ns for 24 reductions, ~12.5 ns per reduction, which is an interpreter loop's cost rather than an allocator's. What remains per hop is one fragment malloc+copy at `send`, one tuple bump allocation, and one `free` when the receiver's heap is torn down; `move`, `getelem`, `return`, pattern bindings, and the `send` result are word copies, and `call`/`tailcall`/`return` allocate nothing. Ring-down and ring-up still agree (D059). System time is still ~0.

The heap column is not comparable to earlier rows and should not be read as per-process footprint: with no collector, every tuple a process ever built is still in its chunk list at exit, so the rings report ~300 MB of dead messages (2M hops x ~150 bytes of garbage each, including chunk-doubling slack), and the waiters shapes average the two pingers' garbage over thousands of idle processes. The per-process figure becomes meaningful again when stage 3's collector lands; the ~1.1-1.4 KB stage-1 figure is the last real one. The same garbage explains the one shape that improved less: at 10000 waiters the pingers' heaps have grown past what stays in cache, so ten times more idle processes now cost the two busy ones ~65% instead of ~12%. Expect both to move with stage 3, and re-measure `ring 32768 x 2000` then -- it was not run on this build because 65M hops of uncollected garbage will not fit.

## Milestone 08 stage 3: first automatic inline collector (M08-T04b)

User reports the memory, full regression and CLI inline-stress suites passing,
then supplies the non-stress `rc bench/run.rc` output through `/dev/snarf`.
Recorded output: `inline-gc-first.txt`. These are user measurements on the
T04b working tree, not agent-run tests or an independently identified commit.

| shape | messages | wall (stats) | ns/message | vs stage 2 | collections | host high-water bytes | bytes/peak process | largest collected live heap (words) |
|---|---|---|---|---|---|---|---|---|
| ring 10 x 200000 | 2200011 | 1.695 s | 770 | 2.19x | 347690 | 24816 | 2256 | 8 |
| ring 1000 x 2000 | 2003001 | 1.436 s | 717 | 2.40x | 307490 | 3335248 | 3331 | 11 |
| ring-up 1000 x 2000 | 2004001 | 1.482 s | 739 | 2.40x | 307123 | 2764096 | 2761 | 16 |
| waiters 1000 x 100000 | 201001 | 0.132 s | 659 | 2.13x | 11766 | 3115088 | 3108 | 18 |
| waiters 10000 x 100000 | 210001 | 0.238 s | 1136 | 2.21x | 12666 | 89391824 | 8937 | 18 |
| sieve 5000 | 234091 | 0.239 s | 1022 | 2.63x | 39129 | 694080 | 2071 | 9 |

All six runs report zero GC failures and zero process faults. The sieve
exits 669 processes explicitly, as before; those are not faults. Ring-down
and ring-up are within about 3% at 1000 nodes. The rings remain faster than
the pre-tagged-term baselines, but are substantially slower than stage 2's
uncollected bump allocation. That is a correctness/performance tradeoff to
measure, not a reason to turn collection back off.

The ring collectors run roughly once per 6.3-6.5 messages while copying
very small live sets. This points to collection frequency and fixed costs
as investigation targets; the benchmark does not isolate copying, scratch
allocation, preflight, or the extra scheduler dispatch costs. Offloading
these tiny collections to a proc is not an evidence-based optimization.

The 10-node ring now reports only 24816 host high-water bytes across more
than two million messages, rather than stage 2's accumulated garbage. But
the 10000-waiter run needs separate investigation: host high-water grows
from 3.12 MB to 89.39 MB for roughly ten times the population, and cost per
message rises from 659 to 1136 ns (72%). The earlier cache/garbage explanation
in the stage-2 commentary was a hypothesis, not a proven diagnosis; the
new result does not establish its cause either.

Do not equate largest collected live heap with total process footprint:
these samples exclude retained heap capacity, frame stacks, queued
messages, process tables and allocator overhead, and only sample processes
that collected. Host high-water is a different measurement, not evidence
by itself of a leak. Instrument those components and separate startup,
steady-state traffic and teardown before choosing a fix. Process-table
reallocation, startup compaction and allocator churn are candidates to
inspect, not conclusions from these numbers.

Disposition: T04b correctness accepted on user-run tests. Performance policy
remains provisional. Next is a bounded inline performance investigation,
then the large-live-set benchmark and D068 off-process policy. No heap-size
or offload default has been selected from this first run.

## T04p diagnostic pass: reproducing the accepted checkpoint

The T04p working tree changes no heap minimum, sizing ratio, reservation
threshold, stress rule or scheduling order. It removes three auxiliary
malloc/free pairs from the common one-frame, 64-word collection (root
view, rollback scratch, and heap descriptor), leaving to-space allocation.
Larger cases use malloc-backed scratch as before. The existing descriptor
is modified only on successful commit, so rollback remains transactional.

Process-table capacity now grows geometrically, separately from the count
of initialized slots. A lowest-possibly-free hint avoids rescanning the
live prefix during append-only creation. Lowest-slot reuse and retirement
remain unchanged. The old source performed one realloc per new slot and
N*(N-1)/2 slot examinations when creating N processes without exits; this
is a demonstrated algorithmic cost, NOT a measured explanation of all
89 MB in the first waiter run. Rerun the original shapes to establish the
combined changes' actual effect:

```rc
mk tests benchmarks
rc tests/run.rc
nervous_gcstress=1 rc tests/run.rc
rc bench/run.rc
rc bench/perf.rc
```

`mk benchmarks` only compiles/links the command and `bench/perftest`; no
benchmark is executed by mk. The agent may build, but the user must run
the scripts. T04p results are recorded below, and the user subsequently
confirmed all requested normal/stress regressions passed. These commands
reproduce the accepted checkpoint or validate a future change; no rerun
is required merely because its documentation has been updated.

### Phase-separated waiter control

`bench/perf.rc` runs fresh host processes for each case. Direct interface:

```rc
bench/perftest waiters pings busy-heap-headroom profile
```

Bounds: waiters 0..65534, pings 1..10000000, headroom 0..65536, profile 0/1.
The script compares 0/1000/10000 waiters at 100000 pings, then requests
32 and 128 words of headroom on the two busy heaps, and profiles the
10000-waiter cases at headroom 0/128. Headroom is a setup-only explicit
collection request, not a new runtime option or default. Actual initial
busy capacities are printed. No shrinking keeps them available during
traffic; larger live sets may still force growth normally.

Phases: waiters spawned (before execution), waiters blocked, two busy
processes prepared, ping-pong traffic completed, and all processes drained.
A final report after runtime free distinguishes released requested storage
from the host break, which malloc need not lower. Snapshots and printing
are outside the elapsed phase intervals. Collection counters omit startup
compaction and explicit setup pre-sizing, which are included in setup
elapsed time. Traffic validates exactly twice the requested ping count in
messages and returns the requested count.

This is a diagnostic control: waiters have no argument and are spawned by
the host, not the linked stop-chain created by `bench/waiters.nv` source.
Host-driven draining also differs. Do NOT compare its elapsed total or
bytes directly to the historical whole-program waiter result. Use
`bench/run.rc` for that comparison; use phases to distinguish startup
cost from busy-process traffic and test the collection-frequency tradeoff.

### Reading the diagnostics

- Requested dynamic storage is partitioned into process table (including
  spare capacity), exec structs, heap capacity plus chunk descriptors,
  retained stacks, adopted fragments, queued fragments and result/exit
  fragments. Heap-used and stack-used bytes are subsets, not extra bytes.
  These snapshots exclude module/atom data, allocator metadata and free
  arenas, the scheduler struct itself, and transient GC scratch. They are
  not resident memory and their sum is not expected to equal break growth.
- Table counters report growth calls, address-changing growth calls, old
  requested capacity bytes on those moves, and slot examinations during
  reuse searches. Move bytes are not allocator-internal copy telemetry.
- `profile=1` enables real monotonic elapsed timing around execution,
  collection and spawn attempts. Exec includes host callbacks and any
  nested spawn time; those columns must not be added as disjoint buckets.
  GC timing excludes startup compaction. Clock reads perturb performance,
  especially small quanta, so use the matching profile=0 case for speed.
- Timing is OFF by default, including the ordinary CLI and bench/run.rc.
  Snapshots are explicit calls, never a per-dispatch process-table scan.
  Neither the injected deadline clock nor language reduction counts are
  used as profiling clocks.

These diagnostics partition execution (including preflight) from GC but
do not yet time preflight alone. If those remaining checks dominate after
the fixed-cost reductions, profile or isolate them before changing their
reservation proof. Do not choose an offload threshold from these tiny
live sets; the large-live-set latency benchmark remains future work.

## T04p first measured results

User supplied `rc bench/run.rc` and all seven `rc bench/perf.rc` cases via
`/dev/snarf` after the T04p build. This is a single-run comparison, not a
confidence interval or an agent-executed measurement. The user separately
confirmed all requested normal/stress regressions passed after this review;
T04p is accepted. No new runtime defaults are chosen here.

### Original whole-program shapes

| shape | wall (stats) | ns/message | prior inline | change | host high-water bytes | prior high-water bytes |
|---|---|---|---|---|---|---|
| ring 10 x 200000 | 1.610 s | 731 | 770 | -5.1% | 20680 | 24816 |
| ring 1000 x 2000 | 1.347 s | 672 | 717 | -6.3% | 2210024 | 3335248 |
| ring-up 1000 x 2000 | 1.355 s | 676 | 739 | -8.5% | 2317560 | 2764096 |
| waiters 1000 x 100000 | 0.130 s | 648 | 659 | -1.7% | 1755064 | 3115088 |
| waiters 10000 x 100000 | 0.152 s | 725 | 1136 | -36.2% | 17975288 | 89391824 |
| sieve 5000 | 0.229 s | 979 | 1022 | -4.2% | 713568 | 694080 |

All six retain exactly the first-inline run's message, dispatch, reduction
and GC counts, with zero GC failures and zero process faults. The sieve's
669 explicit exits remain expected. This run therefore changes observed
cost, not the amount of bytecode work or collection frequency. Ring-up
and ring-down differ by less than 1% at 1000 nodes. Not every byte measure
improves: the sieve high-water is about 2.8% higher in this sample.

The standout result is waiters-10000: host high-water falls 79.9%, from
89.39 MB to 17.98 MB, and bytes/peak process fall from 8937 to 1797. The
whole-program waiter timing penalty is now 725/648 - 1 = 11.9%, instead
of 72.4%. Table growth/search and small-GC allocation changes were applied
together, so this comparison does not independently assign their effects.

### Unprofiled control traffic

Every traffic phase sends exactly 200000 messages and performs 5000012
reductions. Headroom affects only the two busy heaps, not the waiters.

| waiters | headroom request | initial busy capacities (words each) | traffic ns | ns/message | traffic dispatches | collections | GC input words | GC output live words |
|---|---|---|---|---|---|---|---|---|
| 0 | 0 | 64 | 143868978 | 719 | 222620 | 22618 | 1408269 | 108328 |
| 1000 | 0 | 64 | 134991631 | 674 | 222620 | 22618 | 1408269 | 108328 |
| 10000 | 0 | 64 | 134438119 | 672 | 222620 | 22618 | 1408269 | 108328 |
| 10000 | 32 | 128 | 129144746 | 645 | 210667 | 10666 | 1350583 | 50662 |
| 10000 | 128 | 512 | 124590576 | 622 | 202569 | 2567 | 1311738 | 12089 |

At default busy capacity, 1000 versus 10000 blocked processes has no
measurable steady-state penalty in this sample (674 versus 672 ns/message).
The zero-waiter run is slower, so this is not evidence of a monotonic cache
effect; repeat measurements before interpreting small differences.

For the default-capacity 10000-waiter control, spawning takes 11115171 ns,
blocking 3521636 ns and draining 8895698 ns, separate from traffic. Table
creation takes 11 allocation/growth calls, 10 address changes, 1440384 old
requested capacity bytes on moved grows, and zero reuse probes. At 1000
waiters these are 7 grows, 6 moves, 88704 bytes, zero probes. The original
source's quadratic append work is absent in this diagnostic run.

Pre-sizing the two busy heaps to 512 words cuts collections by 88.7% but
traffic cost by only 7.4% relative to 64 words. Initial requested runtime
storage increases by 7168 bytes for those two heaps. This supports a
limited busy-heap experiment, not raising the minimum for every process:
applying the same extra capacity to 10000 idle heaps would add 35.84 MB.

### Where the control's memory goes

Immediately after spawning 10000 zero-argument waiters:

| component | requested bytes |
|---|---|
| Process table, 16384-slot capacity | 1441792 |
| Exec structs | 3120000 |
| Heap capacity plus chunk headers | 5360000 |
| Retained frame stacks | 5120000 |
| Adopted, mailbox, reporting fragments | 0 |
| Total | 15041792 |

Heap-used bytes are only 80000 and stack-used bytes 720000, both subsets
of the retained allocations. Host break growth is 17958744 bytes. Thus
most requested storage is ordinary retained process capacity, not live
terms. The gap is not an exact allocator-overhead measurement: break growth
also reflects free arenas, earlier allocations and uncounted host data.
The zero-waiter case needs 2896 requested runtime bytes but no new break
growth at all, illustrating why the two metrics must not be equated.

In the default 10000-waiter case, break growth is already 17958744 bytes
after spawning and remains unchanged through blocking, busy setup, 200000
traffic messages, draining and runtime free. After traffic the runtime
requests 15043408 bytes (the pinger has exited); after draining only the
1441792-byte table and 32-byte root result remain; after runtime free the
tracked requested count is zero. This is evidence against ongoing heap
accumulation in this control, not a proof about every possible allocator
leak or a reconstruction of the old 89 MB allocation history.

For busy capacities 128/512, break growth after traffic is respectively
17962880/17975416 bytes. Larger busy heaps still retain some adopted
fragments between collections, as intended.

### Profiled control: clock overhead is substantial

| busy capacity | traffic elapsed ns | ns/message | timed exec ns | timed GC ns | ns/GC attempt |
|---|---|---|---|---|---|
| 64 | 249785850 | 1248 | 165998841 | 22395597 | 990 |
| 512 | 218265584 | 1091 | 156415401 | 10729788 | 4179 |

Counts match the corresponding unprofiled cases. Profiling increases
elapsed traffic time roughly 86%/75%, so neither the timing ratios nor
ns/GC should be treated as overhead-free costs. Individual larger-space
collections are more expensive even though the aggregate collection time
falls; freeing/classifying more adopted fragments and dynamic scratch are
possible contributors, not isolated diagnoses from this experiment.

The unprofiled frequency experiment is the stronger result: removing most
collections recovers only about 7% of traffic time. Preflight, arithmetic,
call lookup, host messaging and dispatch still need examination if the
goal is to approach stage-2 throughput. In particular, source inspection
already identifies arithmetic and call-target work repeated after
reservation. A bounded attempt to remove that duplication while preserving
the reservation proof is the recommended next throughput experiment, not
a mandatory prerequisite for off-process correctness. Do not weaken checks
or offload tiny collections merely to chase a throughput number.

Disposition: T04p is an accepted save point on user-confirmed passing tests
and the recorded measurements. Implementation is paused at the user's
request; future tasks in STATUS are planned/unassigned. No new heap minimum,
sizing rule or offload threshold has been accepted. The remaining offload
measurement is a large-live-set latency baseline, not another repetition
of tiny-heap throughput alone.

Add a row here whenever a change is meant to move these numbers, with the build it was measured on.
