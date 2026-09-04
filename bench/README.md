# Benchmarks

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

Add a row here whenever a change is meant to move these numbers, with the build it was measured on.
