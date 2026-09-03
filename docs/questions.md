# Open Questions by Owning Milestone

Resolve a question only when its owning milestone needs the answer. Move the answer to `decisions.md` and remove it here.

## Milestone 06 - Timeouts

Every original question is answered in D048 through D051, including the unbounded timeout value, which D049 admits as the atom `'infinity`. One adjacent question is now worth owning explicitly.

- Command-wide execution bound: `nervous -r` runs until the scheduler is quiescent, with no reduction or wall-clock ceiling. A process looping on `after 0` stays permanently runnable, so the command neither finishes nor reports the D046 deadlock, exactly as an ordinary infinite loop already behaves. Timeouts do not create this hole but make it much easier to reach by accident. Decide whether D046 gains an optional bound, whether the R2 gate owns it, or whether it stays a documented property of the command. This is a CLI policy question, not a language-semantics one; the language answer is already the D042 quantum, which keeps a spinning process from starving its peers.

## Milestone 07 - I/O

D053 settles why this milestone exists and where it sits (after R2, before the renumbered Memory milestone) and that it is not a general FFI. The concrete interface is settled in D054 through D057: two intrinsics (`print`/`eprint`, one dedicated opcode each), a blocking synchronous write returning `'ok`, an `io_error` fault reason on write failure, and an explicit dispatch-order cross-process output guarantee scoped to the current single scheduler. See `../milestones/07-io.md`'s "Settled interfaces" for the concrete shapes implementers build to.

## Milestone 08 - Memory

- Exact 64-bit term tagging.
- Initial heap layout and copying-collector details.
- Stack/frame root enumeration.
- Message-fragment merge timing.
- Heap and allocation exhaustion behavior.
- Atom lifetime and atom-table limits.

## Milestone 09 - Binaries

- Exact minimal segment grammar.
- Construction evaluation order.
- Integer-width limits and byte alignment restrictions.
- Binary allocation limits and fault reasons.

## Milestone 10 - Multicore

- Scheduler quantum and detailed reduction accounting.
- Run-queue ownership and work-stealing policy.
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
- Wire encoding, node identity, and authentication.
- General FFI (deliberately not planned; see D053) and file/network I/O built on an opaque handle term, likely after milestone 09 (Binaries).
- I/O completion as a received message. Candidate shape for the file/network I/O above, to be settled as a decision before any of it is built: an I/O request returns immediately and its result arrives as `${ref, result}` in the requester's mailbox, so blocking never enters the scheduler's vocabulary (the wait happens in a helper Plan 9 process, the libthread `ioproc` pattern, and the VM only ever sees a send), timeouts and cancellation are the existing `after` clause, a read result is an ordinary copied binary fragment (D008), and an I/O server is just a pid -- which makes it location-transparent for free, the same way a 9P client cannot tell a kernel driver from a remote file server. `examples/ioserver.nv` demonstrates the client-side shape with today's `print`. Questions this forces: per-handle ordering (D036 per-sender order suggests one owning server process per handle); what a handle means when its server dies (D037 stale-pid sends drop silently -- the strongest argument yet for monitors); back-pressure, since a fast producer against a server's mailbox budget faults the *sender* with `mailbox_full` (D039); and whether completions are ordinary messages a client could accidentally consume with a wildcard receive. Interacts with milestone 10 (a completion is exactly the cross-scheduler send multicore must get right; D011 wakeup) and with distribution.
- Distribution and network migration.
