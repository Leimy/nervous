# Open Questions by Owning Milestone

Resolve a question only when its owning milestone needs the answer. Move the answer to `decisions.md` and remove it here.

## Milestone 06 - Timeouts

Every original question is answered in D048 through D051, including the unbounded timeout value, which D049 admits as the atom `'infinity`. One adjacent question is now worth owning explicitly.

- Command-wide execution bound: `nervous -r` runs until the scheduler is quiescent, with no reduction or wall-clock ceiling. A process looping on `after 0` stays permanently runnable, so the command neither finishes nor reports the D046 deadlock, exactly as an ordinary infinite loop already behaves. Timeouts do not create this hole but make it much easier to reach by accident. Decide whether D046 gains an optional bound, whether the R2 gate owns it, or whether it stays a documented property of the command. This is a CLI policy question, not a language-semantics one; the language answer is already the D042 quantum, which keeps a spinning process from starving its peers.

## Milestone 07 - I/O

D053 settles why this milestone exists and where it sits (after R2, before the renumbered Memory milestone) and that it is not a general FFI. The concrete interface is settled in D054 through D057: two intrinsics (`print`/`eprint`, one dedicated opcode each), a blocking synchronous write returning `'ok`, an `io_error` fault reason on write failure, and an explicit dispatch-order cross-process output guarantee scoped to the current single scheduler. See `../milestones/07-io.md`'s "Settled interfaces" for the concrete shapes implementers build to.

## Milestone 08 - Memory

Every original question is answered in D061 through D066: term tagging (D061), atom lifetime and table limits (D062), heap layout, collector, and root enumeration (D063, D065), fragment merge timing (D064), and exhaustion behavior (D066). The per-process footprint motivation (65535 ring nodes would not fit in a modest VM; ~1-1.5 KB per blocked process) is recorded in `bench/README.md`, and the two fixes it named -- a shared host callback table and word-sized terms -- are D065 and D061.

Off-process collection's architecture is settled in D067-D070 (reserve at the boundary, yield to collect; heap owner state; collector procs via `rfork(RFMEM)`; Plan 9 procs as the multicore unit). It remains milestone-08 work, not milestone 10 work, and is not implemented yet. D072 records the accepted inline integration and D073 the accepted bounded performance pass. Implementation is paused by the user; STATUS lists planned/unassigned continuation.

The PID split is already an implementation fact, not an unresolved policy choice: 30 slot / 32 generation bits (`include/nvvm.h`). Current managed execution heaps are contiguous; host/startup construction intentionally retains non-moving chunks (D072).

Policy measurements still open, to be recorded on the named decision when supported by representative runs:

- Space sizing (D069): current minimum 64 words, power-of-two growth with live+need at most half; no new default selected. T04p's busy-only experiment at 512 words cut collections 88.7% but traffic cost only 7.4%. Applying that extra capacity to 10000 idle heaps would add 35.84 MB. Adaptive hot-process sizing is optional later research, not required before off-process ownership work or accepted by this result.
- `gcoffload` threshold (D068): the heap size above which a collection goes to a separate proc. The bench that sets it is a ring with one process holding a large live set: measure the other nodes' hop latency with the threshold at 0, at infinity, and at candidate values; the crossover where the fork cost is repaid is the default.
- Opportunistic-collection threshold (D067/D072): current inline policy collects a waiting process when used/adopted words exceed half its space and exceed the last live watermark. It avoids recollecting an unchanged live set. Retain it until representative measurements justify a change; separate idle sweep cost from demand-path throughput.
- Space shrinking (D069 defers it): no policy in this milestone. Revisit if `nervous -s` shows a long-lived process that once held a large live set retaining its space indefinitely.

Off-process implementation questions to settle in the future T04c assignment, without reopening the D068 ownership split: collector-launch failure policy (for example, inline fallback versus controlled failure), publication/completion ordering, how teardown waits safely for all collectors, and how explicit snapshots avoid inspecting collecting heaps. Define tests and record any cross-cutting choice before embedding it in shared interfaces. A collector must not retain a pointer into the relocatable process table.

The next throughput candidate is duplicated arithmetic/call-target work across reservation and execution. It is a bounded optional optimization (T04q), not a language question or a dependency gate. The large-live-set latency baseline (T04r) is still needed regardless: current tiny-live-set benchmarks cannot choose gcoffload, and timing instrumentation perturbs the measured workload substantially.

## Milestone 09 - Binaries

- Exact minimal segment grammar.
- Construction evaluation order.
- Integer-width limits and byte alignment restrictions.
- Binary allocation limits and fault reasons.

## Milestone 10 - Multicore

- Scheduler quantum and detailed reduction accounting.
- Run-queue ownership and work-stealing policy. D059 already gives the single scheduler a FIFO run queue; multicore starts from one such queue per scheduler, not from D042's slot scan.
- Indexed deadlines. The idle-time deadline search and idle-time sanity scan in `nvschedstep` are still O(live processes); with tens of thousands of waiting processes and a busy timer, each idle step is a table sweep. D050 already assigns the indexed structure here.
- Migration synchronization and safe points.
- Timer ownership across schedulers.
- Shutdown and note handling.
- R2-F16: `NvLimits.maxduration` (`include/nvproc.h`) is declared but currently advisory only --
  `nvruntimeinit` does not validate it and `nvprocarmdeadline` checks the compile-time
  `NvMaxduration` ceiling (D049) unconditionally. Whichever milestone-10 work settles per-scheduler
  timer ownership should decide whether to make this field authoritative (and what validates it) or
  remove it, rather than leaving it a dead field indefinitely.

## Later

- Maps and record syntax.
- Bignum representation and promotion.
- Float64 and Float128 model.
- Link, monitor, and catch semantics.
- Portable packed bytecode encoding and code loading.
- Modules and code replacement. Already named as an open question in the original design pass (`../nervous_design.md`, section 47, "Open Questions": "module/name system," "code loading and replacement") and still fully undesigned: there is one flat function-name space per compiled module, with no import, qualification, or compilation-unit boundary. Every reserved intrinsic added so far (D045: `self`, `make_ref`, `spawn`, `send`, `receive`, `exit`; D054: `print`, `eprint` -- eight names total) claims a global identifier permanently and forever forbids a program from defining its own function of that name (D054 states this cost explicitly for `print`/`eprint`). D053's rationale for milestone 07 already anticipates more host-facing surface arriving the same way -- files, devices, and the network, each via an opaque handle term -- which would keep adding reserved global names under the current mechanism with no namespacing to distinguish "the io one" from anything else. This does not block anything built so far and should not be designed ahead of need, per this file's own rule above, but whichever milestone eventually designs modules/naming must decide whether these reserved intrinsics become qualified names in some namespace (`io.print`, and similarly for future file/network handles) or stay reserved globals permanently, and that decision interacts with atom-table lifetime (milestone 08's "Atom lifetime and atom-table limits," since a module/namespace concept likely wants machine-local interned names too) and with wire/node-identity design (since distributed code and names must ultimately travel between nodes).
- Type system. Discussed and settled for now as "no static type system": the runtime's tag checks plus the verifier are the type safety that protects the system; guards with type tests (D060) are the answer at the point of use; records (named tuple fields, compile-time sugar) belong to the modules milestone; first-class fault terms (D029's open item) come before links/monitors. A Dialyzer-style success-typing checker as a separate optional command is the only static analysis contemplated, and only after modules and binaries exist and the language has stopped moving (D012 already places static analysis outside the sequence).
- Wire encoding, node identity, and authentication.
- General FFI (deliberately not planned; see D053) and file/network I/O built on an opaque handle term, likely after milestone 09 (Binaries).
- I/O completion as a received message. Candidate shape for the file/network I/O above, to be settled as a decision before any of it is built: an I/O request returns immediately and its result arrives as `${ref, result}` in the requester's mailbox, so blocking never enters the scheduler's vocabulary (the wait happens in a helper Plan 9 process, the libthread `ioproc` pattern, and the VM only ever sees a send), timeouts and cancellation are the existing `after` clause, a read result is an ordinary copied binary fragment (D008), and an I/O server is just a pid -- which makes it location-transparent for free, the same way a 9P client cannot tell a kernel driver from a remote file server. `examples/ioserver.nv` demonstrates the client-side shape with today's `print`. Questions this forces: per-handle ordering (D036 per-sender order suggests one owning server process per handle); what a handle means when its server dies (D037 stale-pid sends drop silently -- the strongest argument yet for monitors); back-pressure, since a fast producer against a server's mailbox budget faults the *sender* with `mailbox_full` (D039); and whether completions are ordinary messages a client could accidentally consume with a wildcard receive. Interacts with milestone 10 (a completion is exactly the cross-scheduler send multicore must get right; D011 wakeup) and with distribution.
- Distribution and network migration.
