# Multicore fixtures (milestone 10)

Source fixtures that check milestone 10's required tests by their root value, so they run under the plain CLI at any `-p N`. `tests/run.rc` runs each at `-p 1`, `-p 2` and `-p 4`; `-p 1` exercises the same code with the lock off and pins the fixture itself (a fixture that fails at `-p 1` is a fixture bug, not a multicore one). Every fixture bounds its own wait with `after`, so a lost wakeup, a stalled scheduler or a dead receiver shows up as a distinct failure value within a few seconds instead of a hang.

| file | required test | property | pass value | failure values |
|---|---|---|---|---|
| `order.nv` | 2 | per-sender message order across schedulers (D036, D081) | `'ok` | `'receiver_died` (the receiver exited with `${'order_violation, pid, seq}`) |
| `migrate.nv` | 3 | PID and heap stable across steals (D083) | `'ok` | `${'pid_changed, n}`, `${'lost, k}` |
| `exitstorm.nv` | 6 | exit during cross-scheduler traffic; dead-pid sends dropped, none lost, no fault (D037) | `${'done, 20000}` | `${'lost, k}`; a `fault` line; `-s` `dropped` not equal to `k*n - quit` |

Each file's header comment says how to run it by hand with other sizes and what `nervous -s` should show.

## What these prove and do not prove

- `order.nv` checks the D036 guarantee end to end, with three senders on (at `-p 4`) up to three different schedulers and a receiver whose `'hello` registration lets `'m` messages queue behind unmatched candidates first. It does not check anything about relative order *between* senders, which D036 does not promise.
- `migrate.nv` compares each worker's `self` at the end of its churn against the PID its parent got from `spawn`, so a steal that changed identity, or corrupted the register holding `me` across a heap move, fails it. It does not by itself prove steals happened: check `steals N taken` in `-s`, which at the default sizes is non-zero at `-p 2` and `-p 4` in practice but is scheduling-dependent, so the suite does not assert it.
- `exitstorm.nv` proves the sender side of exit-during-traffic: no flooder faults or hangs when its target dies, and every post-exit send is counted as dropped. It cannot see fragment leaks from source; that check is `nvschedmemory` after stop, which needs a C fixture (below).

## Not here yet

- **Required test 4 (a wakeup issued just before sleep is not lost)** needs a deterministic hold point that parks a scheduler between setting its idle bit and `tsemacquire`, in the style of `nvschedgchold`. That is a new `NvSched` test hook plus a C fixture (`mctest.c`); it is not written yet because it cannot be validated without a run. The argument for why the window is closed is in `lib/sched.c`'s `idle()` comment and D085; the `order.nv`/`migrate.nv` runs exercise it statistically at `-p 2/4` (thousands of idle/wake cycles per run, per `-s` `sleeps`), which is evidence, not proof.
- **Required test 5 (timer and message wakeups do not double-enqueue)** has no window under the D081 stage-1 lock -- marking `runnable` and pushing are one critical section -- so there is nothing to test until T04 splits the lock. Recorded here so it is not forgotten then.
- **Fragment-leak check for `exitstorm`**: `nvschedmemory` after `nvmachinerun` returns, from a C fixture; goes with `mctest.c`.
