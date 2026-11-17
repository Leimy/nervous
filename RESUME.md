# Nervous: restart here

Paused by the user after review of `bench/runs/checkpoint-779643`.
No active worker, reserved write set, pending edit or running sub-agent from
this session. Do not begin new work until the user chooses/authorizes it.
This is a compact handoff, not a new design decision. STATUS.md is the
operational record; much of its long body is explicitly historical.

## Cheap reading order

1. This file: enough to answer "where did we stop?"
2. `bench/T02-results.md`: latest evidence, comparisons and proposed next task.
3. Only if implementing: `docs/sharding.md`, the T02 section at the top of
   `milestones/10-multicore.md`, and the adjacent source listed below.

Do NOT load the long STATUS history, all of decisions.md, nervous_design.md,
old review archives or every raw log just to restart. D084 and the sharding
protection map are the relevant decisions for the next likely task. Consult
D074 for GC/credit lifetime and D083/D085 for migration/idle changes.

## Accepted state

- Milestones 00-09 and reviews R1-R3 complete; M10 in progress, R4 not closed.
- T00a/T00b/T01 landed: segmented process table, per-scheduler queues,
  shared-memory rfork schedulers, wake-to-home, stealing, oncpu/pendingwake.
- T04a FIRST SLICE retained: owner-local QLocks for dispatch/receive/GC;
  machine gate + ALL shards (ascending acquire, reverse release) for
  structural/cross-owner operations. Never upgrade a held local shard.
  Send/copy/ref/steal/idle still globally coordinated. Not full T04.
- T02 FUNCTIONALLY ACCEPTED after checkpoint-779643: indexed deadline heaps
  on NvRuntime.runq[i] (also reached by NvSched.runq), reverse timerindex in
  NvProcess; expiry checked before dispatch even under runnable load.
  Empty timer heaps do not read the clock. Only waiting, armed slots without
  pendingwake are indexed. Wake removes the entry but retains the deadline
  for message-first rescan; reblock after a steal indexes the new owner.
  Cancellation/exit remove it; no stale-entry accumulation on rearm.
  Runtime maxduration validated 1..NvMaxduration and enforced (R2-F16 closed).
  Expiry grants FIFO eligibility, NOT a hard wall-time timeout-body bound.
- N=1 runtime locking remains off by default; N>1 requires it. Inline GC
  remains default (gcoffload=0); heap sizing, quantum and steal threshold
  NvStealmin=2 were not retuned by T02.

## Evidence and unresolved performance

Latest: `bench/runs/checkpoint-779643/`, reviewed directly, not merely a user
report. Build, normal/stress-inline/stress-offload suites, all new T02 tests,
and eight global/sharded smoke cases pass. All 27 multicore benchmark runs
have correct recorded result/accounting and zero faults/drops/failed GC.
C fixtures retain their own explicit GC settings across suite invocations.
Sieve benchmark stdout is discarded, so its prime output is not checked by
that capture; source-suite coverage is separate.

Immediate pre-T02: `bench/runs/checkpoint-20674/`, summarized in
`bench/checkpoint-results.md`. Historical baseline: `bench/runs/T01-small.txt`.
Full earlier lock-split comparison: `bench/shards-results.md`.

T02 small suite: zero count changes, 12 timing flags vs T01 (wall and ns/msg
on each of six shapes), 13.3..20.0% slower. Versus immediate pre-T02 it is
6.3..9.5% slower. All nine multicore medians also rose 4.1..10.0%.
Current medians, seconds at N=1/2/4:

| workload | N=1 | N=2 | N=4 |
|---|---:|---:|---:|
| ring | .980 | 1.149 | 1.197 |
| sieve | .157 | .331 | .355 |
| CPU/GC | .668 | .381 | .211 |

CPU/GC still scales 1.75x/3.17x. Message-heavy scaling remains poor.
High-water growth matches 8 bytes per process slot, allocated in 1024-slot
chunks. This is not proof of a timer leak. Unprofiled lock-time zeros do not
prove no contention. Separate captures are NOT controlled causal evidence.

## Proposed next task -- not authorized or started

A bounded pre/post performance investigation before another runtime
optimization. First identify/preserve the actual pre-T02 and T02 revisions
or binaries with the user; captured logs alone do not reconstruct an A/B
control. Commit state and availability of old binaries are UNCONFIRMED.
Then alternate matched binaries, repeat on an otherwise idle machine, start
with N=1 ring/sieve/CPU-GC, retain counts and raw timings. Investigate the
empty-deadline dispatch path, untimed receive clear/remove path and layout
costs; do not declare a cause or "noise" before controlled evidence.
Preserve busy deadline checks, message precedence and ownership invariants.
No blind send-lock split, spin/retry swap, heap-size change or new GC policy.

Remaining gates: deterministic idle-window wake fixture, interactive
DEL/no-orphans and stop-word review, exit-traffic fragment-leak accounting,
heap-allocation failure injection (rollback currently source-reviewed),
full T04 and mandatory R4. T02's run is NOT still owed.

## Source map and operations

T02 runtime changes: `include/nvproc.h`, `include/nvsched.h`, `lib/process.c`,
`lib/sched.c`. Tests: `tests/process/ptest.c` (heap/lifecycle/limits),
`schedtest.c` (busy timers, ties, real N=1/2/4),
`tests/memory/offloadtest.c` (collecting owner). Existing suite integration;
no new mkfile rules. Read-only runtime and fixture audits found no concrete
new bug; actual execution evidence is the checkpoint above.

Compilation only: `mk nervous tests benchmarks`.
User-run validation after future changes, one command from anywhere:

```rc
rc /usr/dave/work/nervous/tests/checkpoint.rc
```

Uses working-tree binaries, no install; writes a NEW result directory and
stops on failure. No external watchdog. Existing logs are immutable evidence;
do not edit their old static OPEN:T02 footer. No rerun is needed merely to
resume this paused checkpoint. The agent's mk tool is only for compilation,
never scripts/tests/clean/install/source control.

No commit or push was performed by the coordinator. Ask the user to confirm
source-control state before edits; preserve the accepted tree and both raw
checkpoint directories. A suggested commit message is not proof of a commit.
Shared headers/build/status/design files stay coordinator-owned. Read
COORDINATION.md before delegation and /usr/dave/local_models.md if selecting
sub-agents; this task favors coordinator edits and bounded read-only audits.

Suggested restart prompt:
"Read /usr/dave/work/nervous/RESUME.md and bench/T02-results.md only first.
Summarize the paused state and propose the bounded performance investigation;
do not change runtime code yet."
