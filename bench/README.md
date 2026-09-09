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

## M08-T04r: large-live-set latency baseline

Every measurement above uses a tiny collected live set (the largest recorded, from stage 3, was 18 words) and every benchmark and the CLI hardcode `gcoffload = 0` (never off-process). None of it can justify an offload threshold: off-process collection exists to protect *other* processes from one *large* collection's pause, and nothing so far has a large collection to measure against.

`bench/largelive.c` (`mk benchmarks` builds it as `bench/largelive`) is that fixture. One "owner" process holds a big live tuple (`retainwords`) across tail calls while repeatedly allocating and discarding garbage (`churn` words per outer iteration, `iterations` outer iterations total), so every collection of its heap has to copy the whole big tuple -- the scenario D068's off-process mechanism was designed for, which nothing measured before this exists. `peers` trivial echo processes (receive `'tick`, send `'tock` to themselves, block again) sit otherwise idle; the host times each one's round trip by driving `nvschedstep` until the global sent-message counter advances by 2, the same `nsent`-delta technique `bench/perftest.c` already uses to verify counts, repurposed here as a per-round clock.

Correction: an earlier draft of this section claimed "peers never allocate, so any latency this fixture finds is attributable to the owner." That is wrong -- `recvtake` adopts each 1-word tick/tock fragment onto a peer's own heap (D064), so a peer's heap does slowly grow and will itself eventually collect, tiny and cheap but real. This matters for choosing `gcoffload` in a run: a threshold low enough to also catch a peer's own tiny heap confounds the owner-only comparison this fixture exists to isolate. Worse, chasing this down surfaced a real scheduler bug, not just a fixture wording error: see "False idle when every runnable slot was collecting" in `docs/decisions.md`'s D074 section, found via this fixture and fixed in `lib/sched.c`. `bench/largelive.rc` therefore does not use `gcoffload=1` ("always off-process", peers included) as a normal case; it uses a threshold comfortably above any peer's own heap size and below `retainwords`, so only the owner's collections move off-process.

Each run measures two phases against the same spawned peers: `baseline` (before the owner is spawned) and `loaded` (once it is running), reported as full distributions -- min/mean/p50/p90/p99/p99.9/max, not just an average -- so the baseline-to-loaded delta is the measurement-overhead control STATUS's task description asked for, isolating the owner's effect from fixed round-trip/dispatch cost without needing a second invocation compared by hand. `gcoffload` is a direct CLI argument (word units, same meaning as `NvLimits.gcoffload`): 0 is never off-process (today's only reachable value anywhere else in this tree), a mid-range threshold isolates the owner (see above), and any other value is a candidate. `retainwords`, `churn`, `iterations`, `peers`, and `rounds` are independent dials, so the same tool measures forced-inline, off-process-above-a-threshold, and other candidates without changing what workload is being measured.

```text
mk benchmarks
rc bench/largelive.rc
```

`bench/largelive.rc` runs a 50000-word live set and a 500000-word live set, each at `gcoffload` 0 and a threshold that catches only the owner (plus one more candidate at 50000 words), 200 peers x 200 rounds (40000 round trips per phase). This is a measurement tool, not a policy change: it selects no default and alters no runtime behavior anywhere else. `mk` only compiles/links it; the user must run the script and report the output for these results to be recorded here, exactly as every other section in this file was populated. No result is fabricated in its absence.

Interpretation note before the first real run: expect `loaded`'s p50 to jump well above `baseline`'s even with no GC pause involved at all -- an owner that never blocks shares the FIFO run queue with peers one scheduler quantum at a time (D059), so a peer waiting behind even one of the owner's ordinary (uncollected) quanta pays that quantum's dispatch cost as ordinary latency. That is ordinary quantum-sharing, not a collection pause. The GC-attributable cost is what shows up as extra tail beyond that baseline shift -- p99/p99.9/max, and specifically whatever grows between `gcoffload=0` and a threshold that moves the owner's collections off-process. Report both: the p50 shift (scheduling, expected, not a GC finding) and the tail shift (the actual measurement this fixture exists for).

Once run, record here: whether `gcoffload=0`'s `loaded` p99/p99.9/max show a visible tail over `baseline` at each live-set size, beyond the p50 quantum-sharing shift (evidence a large inline collection is a real, not merely theoretical, pause); whether an owner-only off-process threshold's tail is smaller, larger, or about the same (whether the fork's own cost is negligible next to the pause it replaces, as D068's rationale assumed, or is itself now the dominant cost at these sizes); and the owner's own collection count/live-words (printed directly from its `NvExec`, not the scheduler-wide aggregate, which also includes every peer's own tiny collections) from each run. T04d's job is choosing a default `gcoffload` value and any other constants from this data; this section's job is only to record what was measured.

### Repeated runs (M08-T04d): a real, confirmed crossover, bracketed not located

`rc bench/largelive.rc 3` (after the `-o`/`$nervous_gcoffload` CLI plumbing and `bench/largelive.rc`'s repeat-count support were added) ran each of the five shape/`gcoffload` rows three times back to back. `rc tests/run.rc`, `nervous_gcstress=1 rc tests/run.rc`, and `nervous_gcstress=1 nervous_gcoffload=1 rc tests/run.rc` (the last one genuinely new coverage: the entire test suite forced through off-process collection via the CLI default, not reachable before this session) all passed in full across every invocation, including the new deterministic `holdfalseidle` regression for the D074 amendment. Three runs per row, not a rigorous confidence interval, but enough to tell a real effect from the single-run noise the previous section flagged and correctly declined to act on.

| retainwords | gcoffload | loaded p50 (ns), 3 runs | loaded p99.9 (ns), 3 runs | loaded max (ns), 3 runs | owner's own collections, 3 runs |
|---|---|---|---|---|---|
| 50000 | 0 (inline) | 14558 / 14713 / 15100 | 206058 / 209852 / 206986 | 289379 / 307576 / 283261 | 196 / 196 / 196 |
| 50000 | 1000 (off-process, owner only) | 15642 / 14635 / 15332 | 461209 / 450445 / 462912 | 693208 / 694989 / 771108 | 150 / 111 / 142 |
| 50000 | 25000 (off-process, owner only) | 14635 / 15022 / 15100 | 420710 / 460977 / 455944 | 659833 / 688949 / 633582 | 101 / 116 / 121 |
| 500000 | 0 (inline) | 14713 / 14713 / 14325 | 48010 / 48630 / 47081 | 4627733 / 4328211 / 4395735 | owner exits before report every run (rootstate Done) |
| 500000 | 1000 (off-process, owner only) | 929 / 929 / 929 | 23696 / 27645 / 24160 | 2102621 / 2726368 / 1741304 | 5 / 7 / 6 |

Reading this with the repeats in hand, not just one sample:

- **The 25000 row's earlier single-run p50 anomaly (2478 ns) is resolved as noise, not a real effect.** All three repeats show p50 approximately 14.6-15.1k ns, matching every other 50000-word row. Closed.
- **At retainwords=50000, off-process collection makes the tail consistently, repeatably worse -- roughly 2.2x, at both thresholds tried.** p99.9 goes from approximately 206-210k ns (inline) to approximately 420-463k ns (off-process); max goes from approximately 283-308k ns to approximately 634-771k ns. This holds in all 6 off-process runs (3 at gcoffload=1000, 3 at gcoffload=25000) against all 3 inline runs. The threshold *value* (1000 vs 25000) barely matters here -- both land in the same worse range -- which is itself informative: what matters is whether the collection being offloaded is big enough to repay the fork, not exactly where the threshold is set.
- **At retainwords=500000, off-process collection makes p99.9 and max consistently, repeatably better.** p99.9 roughly halves (48k to approximately 24-28k ns); max drops to roughly a third to a half (4.3-4.6M ns to 1.7-2.7M ns). This direction is real: the worst-case pause is genuinely smaller under off-process at this live-set size, in all 3 repeats.
- **Fixture defect in the 500000 rows, found while writing this up -- record it, don't paper over it:** at churn=200/iterations=4000, the *inline* owner (gcoffload=0) finishes all 4000 iterations and exits *during* the loaded phase's 40000 round trips every single run (`rootstate Done` before the report, every time) -- so a fraction of those round trips were measured with no owner running at all. Meanwhile the *off-process* owner (gcoffload=1000) is still mid-run at the report (5-7 collections, nowhere near 4000 iterations) -- it made far less progress in the same wall-clock window, consistent with its own collector children taking real CPU away from it. The two rows are measuring different-duration workloads, not identical ones observed two ways. This means: the max/p99.9 *direction* above is trustworthy (the worst inline pause is real and happened while the owner was genuinely alive and colliding with peers), but **the p50 collapse to a flat 929 ns is not "off-process made peers fast" -- it is mostly "the off-process owner barely ran, so peers rarely contended with it at all."** Do not read the 500000 p50 comparison as a real per-round-trip latency win; read only p99.9/max from those two rows. A future run that raises `iterations` for the 500000 shape (e.g. to 40000, so neither owner finishes early) would remove this confound, but is not needed to reach the decision below.

**Decision (M08-T04d): `gcoffload` default stays 0 (never off-process), unchanged.** The crossover between "off-process hurts" (50000 words, confirmed) and "off-process helps" (500000 words, confirmed on p99.9/max) is real but only *bracketed*, not *located* -- somewhere between 50000 and 500000 words, and this data cannot say where, because the threshold value itself barely moved the result at the small end (see above). Picking any specific positive default between those two brackets would be guessing at a number this evidence does not support, which is exactly what this section declined to do with one run and still declines to do with three. Concretely: every workload this tree currently exercises (every existing benchmark and test) has a live set far smaller than even the 50000-word "hurts" bracket, so 0 costs nothing today and a wrong positive guess would actively regress every one of them by roughly 2x tail. See `docs/decisions.md` (D075) for the full record, `docs/questions.md` for what would reopen this (a real workload with a live set past roughly 100k words, or the persistent-collector-pool mechanism refinement, which would move the crossover left rather than require guessing a threshold). `NvGcsweepcap = 8` (the idle-sweep per-pass launch cap) is left unmeasured and explicitly provisional: this fixture's peers never grew large enough to trigger the sweep path at all, so it was never exercised under real load.

Add a row here whenever a change is meant to move these numbers, with the build it was measured on.
