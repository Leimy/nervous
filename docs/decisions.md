# Decision Log

This file contains compact cross-cutting decisions. Add rationale only when it prevents a likely reversal; detailed discussion belongs in `../nervous_design.md` or a dedicated decision record.

## D001 - Exact positional matching

Tuple, list, and binary patterns are exact unless the syntax explicitly binds a remainder. Map patterns, when introduced, match named keys by presence.

## D002 - Single assignment

Variables bind once. `_` never binds. Repeated variables in a pattern test equality.

## D003 - Transactional pattern bindings

Bindings produced while trying a pattern are committed only after the complete pattern and guard succeed.

## D004 - Ordered clauses still matter

Different tuple arities are disjoint, but same-shape patterns can overlap. Function, match, and receive clauses retain source order within overlapping cases.

## D005 - Named functions first (superseded in part by D019)

The early language has statically named functions and no closures. The former source-level arity identity rule is superseded by D019.

## D006 - Expression blocks

Blocks return their final expression. A trailing semicolon is permitted and ignored.

## D007 - Checked integer overflow (reason representation superseded by D029)

Small-integer overflow exits the process abnormally. D029 settles the early VM reason as `overflow`, superseding the original `{'error, 'overflow}` spelling. Silent wrapping is forbidden; later bignum promotion remains compatible.

## D008 - Copied ordinary messages

Ordinary message terms are copied on send into a self-contained mailbox fragment. This is the initial runtime policy used to preserve process-local garbage collection.

## D009 - One scheduler first

Concurrency is implemented and tested cooperatively before multicore scheduling is introduced.

## D010 - Portable machine boundary

The bytecode contract does not expose native pointers, C struct layout, scheduler identity, or Plan 9 descriptors.

## D011 - Multicore wakeup

When multicore schedulers are introduced, idle scheduler wakeup uses `tsemacquire` and `semrelease`. Initial shared queues may use `QLock`.

## D012 - Scope of the first sequence (I/O carve-out refined by D053)

Maps, floats, distribution, links, monitors, general FFI, code replacement, static analysis, and network process migration are deferred beyond milestone 09. D053 pulls a narrow, specific form of host output into scope as its own milestone without reopening general FFI.

## D013 - Early lexical scope

A named function clause or ordinary zero-argument function body is one lexical scope. Nested blocks do not create shadowable variable namespaces: a name bound anywhere along the current control path cannot be rebound. Bindings introduced inside a `match` clause are local to that clause body and do not escape the `match`. The same rule will apply to receive clauses.

## D014 - Evaluation order

Call arguments, tuple elements, and later aggregate elements evaluate from left to right. Operator operands evaluate left to right; `and` and `or` then short-circuit.

## D015 - Initial lexical forms (comment rule superseded by D020)

Integer literals are unsigned decimal digits; negative values use unary `-`. Identifiers match `[A-Za-z_][A-Za-z0-9_]*`, with `_` reserved as the discard pattern. Atoms use a leading quote followed by the same identifier body, such as `'ok`; quoted or escaped atom names are deferred. The original `#` comment rule is superseded by D020.

## D016 - Initial operators

From lowest to highest precedence: `or`; `and`; `==` and `!=`; `<`, `<=`, `>`, and `>=`; `+` and `-`; `*`, `/`, and `%`; prefix `not`, unary `-`, and unary `+`; then calls. Binary operators associate left. Comparison and equality operators do not chain specially.

Semantics (settled when the compiler gained these operators, after D058): `and`, `or`, and `not` accept exactly `'true`/`'false` and fault `match_fail` otherwise, the D058 `if` rule applied uniformly -- there is one boolean discipline in the language, not two. `and`/`or` short-circuit (D014) and lower onto the same `testatom` shape as `if`; bindings inside a short-circuited right operand are local to it (D013). Unary `-` is checked `0 - x`, folding a negative literal to a constant; unary `+` is `x + 0`, i.e. an integer check. No new opcodes and no new fault reasons.

## D017 - Process syntax deferred

Milestone 01 does not parse `spawn`, send, receive, or named function-reference syntax. Their surface forms will be settled before milestone 05 rather than preserved as unchecked early AST nodes.

## D018 - Brace expression disambiguation (superseded by D020)

The temporary comma-versus-semicolon brace rule belongs only to syntax v1 and is retained solely by its compatibility parser.

## D019 - Functions receive argument tuples

Every function semantically receives one tuple. A clause head is a tuple pattern, and a conventional call `f(a, b)` supplies the tuple `${a, b}`. `f()` supplies `${}`. Source-level function identity is its name, not name/arity, and clauses of different tuple arities may coexist. Direct calls may pass fields in registers and avoid materializing the tuple when unobservable. The frontend may store tuple fields as a list internally, but that list is not a second language concept.

## D020 - Delimiter families and comments

Blocks use `{...}`, tuples use `${...}`, maps use `#{...}`, lists use `[...]`, binaries use `<<...>>`, and parentheses are reserved for grouping and conventional call syntax. Line comments begin with `//`; block comments remain deferred. Each aggregate kind is known from its opening delimiter, and one-element tuples need no trailing-comma convention.

## D021 - Canonical formatting

Nervous has one canonical source layout produced by its formatter. Formatting is semantics-preserving and idempotent; stylistic alternatives are not configuration options. The first formatter writes to stdout only. Compatibility parsers may feed the common AST, but only the current syntax has a formatter.

## D022 - Comments are preserved trivia

Comments are ordered source trivia owned by the AST root. They do not affect runtime semantics, but rewriting must preserve their text and order. The canonical formatter emits all comments as `//`; syntax v1 `#` comments are migrated. A trailing comment may become a standalone comment before the next syntactic node until full node extents are tracked, but it may never be discarded.

## D023 - Symbolic bytecode limits

A symbolic module has at most 65535 constants, 65535 functions, and 65535 instructions per function. A function declares between 1 and 256 X registers. These are verifier limits for the first machine contract, not promises about a final packed encoding.

## D024 - Constant pool and calls

Constants are typed integer, atom, or function-name entries. Instructions refer to constants by zero-based index. A call target must be a function constant. Calls consume one register containing the semantic argument tuple and write one result register; a direct-call implementation may later avoid materializing that tuple.

## D025 - Frames and roots

Milestone 02 bytecode has X registers and explicit call/return behavior but no exposed Y-frame allocation instruction. Every initialized X register is conservatively a root at an allocation or call safe point. A later frame design may refine roots without changing source semantics.

## D026 - Explicit failure and control flow

Jumps and pattern-test failure continuations are absolute zero-based instruction indices within the current function. Tests do not bind on failure. Falling off a function is invalid; every reachable path ends in return, fail, tailcall, or another explicit transfer.

## D027 - Symbolic bytecode format and verification

The milestone 02 format is canonical line-oriented text intended for fixtures and diagnostics, not distribution. The verifier rejects malformed operands, limits, invalid constants and targets, reads of registers not definitely initialized on every incoming path, and reachable fallthrough. Disassembly emits the same canonical form so valid input round-trips textually.

## D028 - Initial VM equality

Equality is structural and type-sensitive. Integers compare by value, atoms by interned identity/name, and tuples by equal arity followed by recursive left-to-right element equality. Values of different kinds are unequal. Cycles are impossible in the milestone 03 value subset.

## D029 - Initial VM faults

Runtime faults are deterministic symbolic reasons rendered as `fault <reason>`. Reasons include instruction failures such as `bad_function`, `bad_tuple`, `bad_element`, `bad_constant`, `badarith`, `divide_by_zero`, and `overflow`; pattern or explicit-failure atoms; host/context failures such as `bad_process_context` and `bad_state`; and controlled resource reasons such as `out_of_memory`, `system_limit`, `mailbox_full`, and `reduction_limit`. This list is descriptive rather than a closed language-level reason type until first-class fault terms are introduced. A verified program must fault rather than corrupt memory or abort the host process.

## D030 - Initial reduction accounting

Every dispatched bytecode instruction consumes one reduction. Milestone 03 execution accepts an explicit finite reduction limit and faults with `reduction_limit` before dispatching an instruction when no reductions remain. This gives deterministic bounded execution now; scheduler quantum policy remains owned by milestone 09.

## D031 - Arithmetic and ordering deferral

The milestone 02 instruction set contains no arithmetic or ordering opcodes. Milestone 03 does not add them in its first interpreter slice. Ordering is limited to integers when comparison opcodes are introduced. Integer division will truncate toward zero and remainder will have the dividend's sign; division by zero faults with `divide_by_zero`, and the minimum-integer divided by minus one faults with `overflow`.

## D032 - Initial clause lowering

Milestone 04 lowers clauses as linear source-order attempts. Exact tuple arity permits later optimization but does not reorder overlapping clauses. Each attempt uses private tentative bindings and commits them only after the complete pattern succeeds.

## D033 - Initial matching failure reasons

A failed asserted binding exits with reason atom `match_fail`. Exhausting function clauses exits with reason atom `function_clause`. A `match` expression with no selected clause also uses `match_fail` until a distinct reason is justified.

## D034 - Guards and whole-value binding deferred

Milestone 04 initially implements literal, variable, wildcard, and tuple patterns. Guards and whole-value `@` syntax are deferred. This keeps the first shared matcher focused on exact shape, repeated-variable equality, transactional bindings, and source-order clauses without preventing either feature from being added later.

Guards are settled by D060; whole-value `@` remains deferred.

## D035 - Mailbox and selective-receive order

Each mailbox stores messages in arrival order. Selective receive scans oldest to newest and consumes the first message for which a clause succeeds; clauses are tried in source order within each candidate message. Earlier unmatched messages remain in their original relative order. Mailbox indexing may optimize this later without changing the result.

## D036 - Per-sender ordering

Messages sent from one sender to one receiver are enqueued in sender program order. Messages from different senders have no relative ordering guarantee. The single-scheduler implementation preserves this naturally; later multicore queues and migration must retain it.

## D037 - Dead PID sends and VM lifetime

Send is asynchronous and returns the sent message. Sending to a dead or stale PID drops the message and still returns it; stale generations never alias a replacement process. The VM continues after the initial process exits and stops when no runnable or waiting language processes remain.

## D038 - Opaque PID and Ref terms

PIDs and Refs are distinct opaque term kinds, not binaries. A local PID contains a process-table slot and generation. A Ref contains a machine incarnation and monotonic counter. Programs may compare and transmit them but may not construct them from integers/binaries or destructure their representation.

## D039 - Initial process resource limits (aggregate accounting refined by D047)

Process count, mailbox bytes, single-message bytes, call depth, and term depth are configurable implementation limits, not language maxima. Accounting is overflow-safe and exhaustion is controlled: spawn/runtime allocation and call-depth exhaustion report `system_limit`, and a send that exceeds mailbox, message, or configured term-depth limits reports `mailbox_full`; messages are never silently truncated or partially enqueued. Exact aggregate heap accounting depends on the milestone 07 process-local heap and is not claimed by milestone 05.

## D040 - Provisional mailbox byte accounting

Until the R3 heap/term representation is selected, mailbox and message byte limits charge the recursive host representation used by `NvValue`: each value node, tuple header, and atom string including its terminator. This is deliberately conservative implementation accounting, not a source-language size or stable external metric. It does not yet charge allocator metadata or the `NvMessage` queue node. R3 owns replacing this estimate with accounting for the final copied mailbox fragment/heap representation while preserving overflow-safe all-or-nothing enqueue behavior.

## D041 - Single-scheduler process lifecycle

Milestone 05 uses explicit mutually exclusive process states. Spawn creates `runnable`; dispatch changes `runnable` to `running`; quantum exhaustion changes `running` to `runnable`; an unmatched receive changes `running` to `waiting`; message arrival changes `waiting` to `runnable`; and exit changes any live state to `exited`. A generation-exhausted exited slot becomes permanently `retired` instead of wrapping and reissuing an old PID identity. Only a `running` process may execute receive, duplicate dispatch and invalid yield edges are rejected, and PID lookup recognizes only `runnable`, `running`, and `waiting` as live.

## D042 - Initial scheduler ownership and order

The milestone 05 scheduler borrows one verified module that must outlive it. Each live scheduled process owns exactly one heap-allocated `NvExec`, released on process exit or runtime teardown. Dispatch scans process slots in rotating order, gives one runnable process one finite quantum, and advances the cursor before the next dispatch. Execution yield returns a still-running process to `runnable`; a process that blocked during execution remains `waiting`. Normal return and runtime fault both exit only that language process, while the scheduler continues until no live process remains or all remaining processes are waiting.

Dispatch selection is superseded by D059 (FIFO run queue); the rest of this decision stands.

## D043 - Process instruction host boundary

Portable bytecode does not contain scheduler pointers or process-table layout. `NvExec` invokes optional host callbacks for `self`, `makeref`, `send`, `spawn`, and receive scanning; executing a host operation without a host faults with `bad_process_context`. The scheduler installs callbacks using its currently dispatched PID. `send` copies into the destination mailbox and returns the sent value, including for a dead PID; `spawn` takes a statically named function constant and one semantic argument tuple. A process operation may grow the process table, so scheduler code must reacquire slot pointers afterward and computes its next round-robin cursor from the resulting table.

## D044 - Receive bytecode and explicit exit

Receive bytecode uses an opaque host-owned scan phase. `recvbegin value found` starts at the oldest message and `recvnext value found` advances; both always initialize distinct destinations with a copied candidate or `'undefined` plus a boolean atom. Compiled pattern tests use tentative registers only on success. `recvtake` atomically removes the selected candidate. After an exhausted scan, `recvwait retry` clears scan state, makes the process waiting, and resumes at `retry` after wakeup, so the deadline-free milestone 05 receive restarts from the oldest retained message. Invalid scan-phase operations fault rather than exposing mailbox pointers. A scan may span scheduler quanta, but only its owning process may consume that mailbox candidate.

`exit reason` is a terminal instruction with an arbitrary term reason. It produces a distinct `NvExit` outcome rather than flattening the reason into a diagnostic string. The scheduler moves the reason into scheduler-owned reporting storage before releasing the process execution and mailbox, requiring no allocation during exit transfer.

## D045 - Compact source process forms

The initial source forms are reserved intrinsic expressions: `self()`, `make_ref()`, `spawn(worker, args...)`, `send(pid, message)`, `receive { pattern => body; ... }`, and `exit(reason)`. The first `spawn` argument is a statically resolved function name, not an evaluated value; its remaining arguments form the callee's semantic argument tuple. `send` returns the sent value. `receive` scans candidates oldest-first and tests clauses in source order, consumes a candidate before evaluating its selected body, preserves unmatched messages, and restarts at the oldest retained message after wakeup. Receive-clause bindings are tentative during matching and local to that clause body. These intrinsic names are reserved from ordinary call dispatch in the early subset.

## D046 - Source scheduler CLI reports the root outcome

The source execution command is `nervous -r source entry [args...]`. It compiles the source, spawns the named entry as the scheduler root, and runs until no process remains or the live system has no runnable process. A normal root return is retained across process reaping and printed after all processes finish. A root VM fault or explicit `exit(reason)` is reported as failure. A live system with no runnable process is reported as deadlock. Child faults and exits remain isolated and do not replace the root outcome; they affect the command only when their disappearance leaves the root or another live process permanently blocked. CLI arguments use the existing integer-or-quoted-atom convention and arrive as the root function argument tuple.

Rationale: retaining root identity gives source execution a stable observable result without weakening process isolation. Waiting for scheduler quiescence prevents the command from silently abandoning children, while distinguishing `Done` from `Idle` makes blocked programs diagnosable.

Precedence (clarified by R2-F22): the root outcome is reported before scheduler idleness. If the root faults or exits and the surviving processes then block forever, the command prints the root diagnostic first and the orphaned-process count as a secondary `deadlock:` line; idleness caused by the root's death is a consequence, not the headline. Only a run whose root is still alive, or finished normally, when the system goes idle is reported as plain deadlock.

## D047 - Provisional frame and term-depth safety

Milestone 05 bounds each scheduled execution's non-tail call-frame depth through `NvLimits.maxframe`; exceeding it faults only that process with `system_limit`. Tail calls retain their current depth. Standalone execution has a conservative default frame limit, while a scheduler installs its configured value. Exact frame/heap byte accounting remains milestone 07 work.

"Tail calls retain their current depth" is a source-level guarantee, not merely a property of the `tailcall` opcode (R2-F21: the compiler originally never emitted it, so the sentence was vacuous and every recursive message loop grew a frame per message). The compiler must lower every call in tail position to `tailcall`; tail position is defined normatively in `semantics.md` ("Functions"): a clause body, the last expression of a tail block, and every arm of a tail `if`/`match`/`receive` including the `after` body. A later direct-call optimisation (D019, D024) must preserve this.

Program-created value graphs are acyclic and have a portable provisional maximum depth of `NvMaxtermdepth` (256). Tuple construction and value copying reject deeper values before recursively traversing them, and bytecode tuple construction faults with `system_limit` when it reaches that ceiling. Each runtime may choose a smaller send limit through `NvLimits.maxtermdepth`; a send above that runtime depth is rejected as `mailbox_full`. This bounds recursive size, copy, equality, print, and cleanup for values admitted through the language APIs; iterative traversal and final representation accounting remain milestone 07 responsibilities.

Rationale: these limits prevent one process from exhausting the host call stack or growing unbounded interpreter frames before process-local heaps exist, without pretending that milestone 05 implements the aggregate heap accounting owned by milestone 07.

## D048 - Receive deadlines observe messages first

A receive with an `after` clause computes one absolute deadline when the receive is entered, before the first mailbox scan, so the scan belongs to the interval. That deadline survives every block, wakeup, and rescan until the receive is left. Rescanning never restarts or extends it.

Expiration is observed only where a complete oldest-first scan has already failed to select a candidate. A process therefore never takes the timeout branch while its mailbox holds a message its clauses accept, even when the deadline expired before that message arrived: a timer wakeup resumes scanning at the oldest retained message, and the timeout branch runs only once that scan is exhausted.

`after` is consequently a lower bound on waiting, never an upper bound. A timeout fires no earlier than its deadline and may fire later. Programs needing an upper bound need a mechanism the early subset does not provide.

`after 0` performs exactly one nonblocking oldest-first scan and takes the timeout branch only if that scan selects nothing. This is the general rule applied to an already-expired deadline, not a special case.

Rationale: making expiry observable only at scan exhaustion removes every message-versus-timer race from the language semantics instead of resolving it with an undocumented host timing rule, and leaves D035 selective-receive order untouched. The cost is stated honestly above rather than hidden: timeouts are late, never early.

## D049 - Timeout durations are bounded integers

A timeout duration evaluates either to an integer count of nanoseconds in the closed range 0 through `NvMaxduration`, or to the atom `'infinity`, which arms no deadline and makes the receive block exactly as a receive with no `after` clause does. Any other value, including a negative integer, an integer above `NvMaxduration`, and any other atom or term, faults the process with reason `bad_timeout`. When the duration is a literal, the compiler rejects an out-of-range one at compile time with source position rather than deferring to a runtime fault.

`NvMaxduration` is 1000000000000000000 nanoseconds, about 31.7 years. The value is deliberately far beyond any real receive timeout: it exists to make the arithmetic total, not to express a policy, and it leaves roughly a factor of nine of headroom below the signed 64-bit nanosecond maximum.

Admitting `'infinity` costs no new mechanism. It is the case that matters when a caller computes a timeout and sometimes means "no deadline"; a statically infinite wait is already written by omitting the `after` clause. A process blocked on an infinite timeout is an ordinary waiting process, so D046 deadlock reporting still observes it.

Bounding the input domain, rather than checking each sum, makes deadline arithmetic total: `NvMaxduration` is chosen so that any monotonic clock reading plus any accepted duration cannot overflow the signed 64-bit nanosecond domain. Implementations still compute that sum with the checked-addition helper and fault `system_limit` if the assertion is ever violated.

This follows Erlang, which requires a receive timeout to be an integer or `infinity`, reports `timeout_value` otherwise, and bounds the accepted range instead of promoting large values. Arbitrary-precision integers are the wrong tool here: a deadline is a machine quantity, not a mathematical one, so an unbounded numeric type only relocates the failure. If bignums are added later, an out-of-range bignum duration fails this same range test, preserving D007's promise that later bignum promotion stays compatible.

A duration is an ordinary integer or the atom `'infinity`, not a distinct term kind. A separate duration type would move this check into a constructor rather than remove it; only static proof of non-negativity could remove it, and static analysis is outside the first milestone sequence.

## D050 - Deadlines belong to the process slot

A waiting process owns its deadline in its own process-table entry. Milestone 06 adds no timer object, timer queue, cancellation token, or timer identity. Arming is an assignment, cancelling is clearing the field, and teardown is the existing process exit and runtime free path.

A deadline therefore cannot outlive its process, cannot fire twice, and cannot wake a reused slot, because it shares the slot and generation lifetime that D037 and D041 already define for PIDs. Equal deadlines are ordered by ascending slot index; under D059 this holds because the wake scan enqueues tied processes in ascending slot order, so they dispatch in that order. The single scheduler may scan slots for the earliest deadline; milestone 09 may replace that scan with an indexed structure without changing any rule above.

The scheduler reads time through an installed clock interface, never a native clock call reachable from bytecode or process code. The production clock is `uptime` from time(2): monotonic nanoseconds since boot. `nsec` must not be used, because it reports settable wall-clock time since the epoch and a deadline built on it moves when the clock is set. Tests install a deterministic counter clock and never sleep on host time.

A scheduler step that finds no runnable process consults armed deadlines. If none exists the system is deadlocked and D046 reporting is unchanged. Otherwise the scheduler advances to the earliest deadline, makes those processes runnable, and reports progress.

Advancing must wait, not spin. A production scheduler with nothing runnable and an armed deadline blocks the host process until that deadline rather than polling the clock, so an idle program consumes no processor. The injected test clock advances instantly instead, which is what keeps deterministic tests free of host sleeping. Milestone 09 replaces this blocking wait with the D011 semaphore wakeup so that another scheduler can interrupt it.

## D051 - Timeout source and bytecode interface

A receive may end with at most one `after duration => body` clause, written after every pattern clause. A receive without an `after` clause keeps exactly its D045 meaning.

Bytecode gains two instructions. `recvdeadline duration` evaluates the duration and computes the absolute deadline once; the compiler emits it before the retry label so that the D044 retry target cannot re-arm it. A duration of `'infinity` arms no deadline. `recvwaitdeadline retry` blocks like `recvwait` when no deadline is armed or the armed deadline has not expired, resuming at `retry`; when the armed deadline has expired it clears receive scan state and falls through to the next instruction, which begins the timeout body.

An infinite timeout therefore needs no separate opcode or control flow: it is the unarmed case, and `recvwaitdeadline` degenerates to `recvwait`. The timeout body remains statically reachable, so verification is unaffected even though that body cannot run at run time.

Unlike `recvwait`, which is a terminal transfer, `recvwaitdeadline` has two successors. The verifier adds its fallthrough edge to both control flow and definite-initialization analysis, applies the reachable-fallthrough-at-end rule to it, and thereby proves that the timeout body reads no receive-clause binding. `recvwait` and every existing milestone 05 module keep their current meaning and remain valid.

Neither instruction carries a clock pointer, timer identity, or process-table layout; both reach the host through the D043 callback boundary.

## D052 - Provisional source nesting safety

Parser-produced expressions have a portable provisional maximum nesting depth of `NvMaxsourcedepth` (256). The parser rejects a recursive expression entry beyond that ceiling with `nesting too deep`. Because every normal AST is parser-produced, this also bounds recursive pattern validation, formatting, compilation, printing, and cleanup over accepted source trees. A later iterative frontend may raise or remove the provisional ceiling without changing source semantics.

Rationale: source input is untrusted and must not exhaust the host C stack before bytecode verification or runtime limits can apply.

## D053 - I/O joins through the existing host boundary; there is no general FFI

Nervous gains host output through the same mechanism that already gives it processes: a reserved source intrinsic, a portable bytecode instruction that carries no host detail, and an `NvExecHost` callback the scheduler or CLI installs. This is milestone 07 (I/O), inserted immediately after R2 and before the renumbered Memory milestone, precisely because it needs nothing from either: it allocates nothing long-lived, retains no term past one call, and does not touch heap representation.

"FFI" in Nervous never means an escape hatch to arbitrary C functions, libc, or Plan 9 syscalls reachable directly from bytecode. D010 already forbids exposing native pointers, C struct layout, or Plan 9 descriptors to bytecode; this decision states plainly that host I/O does not reopen that boundary. Every host resource a program can hold is an opaque term kind constructed and destructured only through host callbacks, following the D038 PID/Ref precedent exactly. Milestone 07 introduces no such handle (it is output-only, into terms Nervous already has); a later milestone extending to files, devices, or the network would introduce one (a `Vfile`-shaped opaque handle, not a raw file descriptor) rather than any general call-into-C mechanism.

Rationale: a general FFI would let one process corrupt the host process, read or write memory outside its own terms, or violate every ownership/isolation guarantee milestones 05-06 already established, for a feature whose actual need so far is "print a value" and, later, "read and write named host resources." Plan 9's uniform file model already gives the second need a narrow, well-precedented shape (open/read/write/close by path, exactly one opaque handle kind) without requiring a general foreign-call boundary at all. If a real, unavoidable need for calling arbitrary native code ever appears, it deserves its own decision record arguing why the narrow boundary is insufficient, not a quiet expansion of this one.

Note (not a reversal): this decision routes I/O through the same reserved-name/opcode mechanism the process intrinsics already use because that is the only extension mechanism the language currently has, not because I/O particularly belongs there. There is no module or namespace concept yet (`../nervous_design.md`, section 47; `questions.md`'s "Later" section, "Modules and code replacement"), so `print`/`eprint` join a flat, permanently-growing set of reserved global names (D045's six plus these two) rather than becoming, say, `io.print`. Anticipated future host-facing capabilities (files, network) named just above would keep adding to that same flat set under the current mechanism. Whether that stays acceptable, or whether a future module/naming milestone turns these into qualified names, is tracked in `questions.md` and is deliberately not decided here.

## D054 - I/O intrinsic names and bytecode shape

Milestone 07 adds two reserved source intrinsics, `print(value)` and `eprint(value)`, writing to host stdout and stderr respectively, rather than one intrinsic with a stream-selector argument. Two fixed names match the existing one-intrinsic-per-operation style (`self()`, `make_ref()`, `send(pid, message)`, `spawn(worker, args...)`, `exit(reason)`) better than a selector, and are simpler to verify: each gets its own fixed operand shape instead of a shared shape plus a validated selector value.

Both join `reserved()` in the compiler alongside `self`, `make_ref`, `send`, `spawn`, and `exit`: a Nervous program may no longer define a function named `print` or `eprint`, and any existing program that called its own function of either name would now be rejected as a reserved-intrinsic redefinition. This is the same compatibility cost D045 already accepted for the other five names, applied consistently; no current example or fixture defines or calls a function named `print` or `eprint`.

Each intrinsic compiles to its own dedicated bytecode instruction, `print dst value` and `eprint dst value`, following the two-register (destination, source) shape already used by `Otailcall`/`Orecvbegin`/`Orecvnext`. This corrects an inconsistency in the original milestone 07 draft, which described "no destination register": every Nervous expression yields a value (D045's `send` already returns the sent value; see D055 below for what `print`/`eprint` return), so a destination register is required exactly as it is for `Oself` and `Osend`, and the draft's phrase should be read as superseded by this decision. The draft also said "one new bytecode instruction"; this decision settles on two, one per intrinsic, matching D054's naming choice above.

## D055 - I/O blocking, return value, and reduction accounting

`print` and `eprint` block the calling process until the underlying host write completes; there is no queued or deferred write. This costs nothing in the current single-cooperative-scheduler model (D009): every instruction already dispatches synchronously within one process's quantum, and no other process can observe an interleaved partial write because only one process executes at a time. A later multicore milestone (currently milestone 10) that lets multiple schedulers call host I/O concurrently must revisit whether concurrent writes to the same stream need serialization or an explicit relaxation of the ordering guarantee in D057; this decision does not pre-answer that.

On success, both intrinsics return the atom `'ok`, not the printed value. Unlike `send`, whose return of the sent value lets a caller keep or forward what it just transmitted, `print`/`eprint` exist purely for a side effect; `'ok` signals success without inventing a reason to hand the value back through the call. The host callback slot therefore only performs the write and reports success or failure; the bytecode dispatcher (`nvexecrun`), not the host, constructs the `'ok` result on success, matching how arithmetic and comparison instructions construct their own result values.

Every dispatch of `print` or `eprint` consumes exactly one reduction, the same D030 rule every other instruction already follows; no exception is introduced. Neither instruction ever yields, blocks the process in the scheduler sense, or changes process lifecycle state (D041): a process that prints and continues is scheduled identically to one that does not print.

## D056 - I/O failure reason

A write failure (for example, a closed or broken stdout/stderr) is a per-process fault with reason `io_error`, joining the D029 list of host/context failure reasons alongside `bad_process_context` and `bad_state`. It is not a host-level `sysfatal` and not silently ignored: the writing process fails exactly as it would on any other controlled resource exhaustion, and every other live process continues unaffected, consistent with D029's "a verified program must fault rather than corrupt memory or abort the host process."

## D057 - I/O cross-process ordering guarantee

Because one scheduler dispatches exactly one process per quantum (D009, D042), output from different processes is already totally ordered by dispatch order with no additional mechanism: if process A's `print` dispatches before process B's, A's bytes appear before B's, and a single `print`/`eprint` call's write is never interleaved mid-value with another process's write. This decision states that guarantee explicitly rather than leaving it an unstated accident of the current scheduler.

The guarantee is scoped to the current single-scheduler implementation. A later multicore milestone (currently milestone 10) that runs more than one scheduler concurrently must explicitly decide whether to preserve this total order (for example, by serializing host I/O through one owner) or to relax it, and must document whichever choice it makes; this decision does not bind that future choice, only names it as an open obligation.

## D058 - Surface syntax v3: declaration heads, keyword process forms, `if`

Supersedes the surface forms in D017 and D045, the clause-head and formatter details in D019 through D021, and the compatibility scope of D018. Semantics are unchanged: every rule in D019 (one argument tuple per function, clauses of different arity may coexist) and D045 (send/receive/spawn/exit behaviour) still holds; only the spelling changed. The previous syntax is retained solely as the `-A`/`-F` compatibility input so that existing source is rewritten mechanically by the formatter; syntax v1 is dropped.

Functions. A declaration is `fn name(pattern, ...) { ... }`. Adjacent declarations of one name are that function's clauses in source order; a non-adjacent redeclaration is still the existing "duplicate function" error, so an accidental second definition is caught rather than silently appended. There is no separate zero-argument body form: `fn main() { ... }` is a clause with an empty head, and calling it with arguments is a `function_clause` fault exactly as for any other arity mismatch. The parser needs no lookahead: after `fn name` it always sees `(`. `${...}` is consequently only ever a tuple.

Rationale: `${x} => body;` made D019's semantic statement ("a clause head is a tuple pattern") the visible syntax, and forced the parser to parse a whole expression before knowing whether it had a clause head or a body beginning with a tuple literal. A parenthesised head reads as a parameter list to everyone and is unambiguous from the first token. Repeating the name per clause follows Erlang, where it has proven readable for decades; each clause body is a `{}` block, which is what 9front's editor selection tools work on.

Clauses and terminators. A `match` or `receive` clause is `pattern => body`. `;` is required after an expression body unless the clause list closes immediately, and optional after a block-like body (`{...}`, `match`, `receive`, `if`), which closes itself. The same rule applies to expressions inside a block. This removes every `};` from the language and follows the C/Go/rc convention that a brace-closed construct needs no terminator. In statement position a block-like expression followed directly by another expression is two expressions; followed by an operator it is one, exactly as a `match` followed by `+ 1` already was, and the formatter makes whichever reading was parsed visible.

`if`. `if cond { ... } else if cond { ... } else { ... }` is sugar with no bytecode of its own: the condition must evaluate to `'true` or `'false`, any other value faults `match_fail`, and there is no truthiness rule. Branches are blocks; a missing `else` yields `'ok`, so `if` may be used purely for effect (`if next != 'nil { next ! ${'done} }`) without inventing a unit term. Branch bindings are local to the branch, as `match` clause bindings are (D013). The compiler lowers it to two `testatom` tests, so the verifier's existing definite-initialisation and reachability analysis covers it unchanged.

`==` and `!=`. Both are lowered onto the existing `testeq` pattern test selecting one of the two boolean atoms; no value-producing equality opcode is added. This is D028 structural equality made available to expressions, which is what makes `if` useful in practice.

Process forms are keywords, not calls: `self`, `mkref`, `spawn name(args...)`, `pid ! message`, `exit reason`, and `receive { ... }` as before. Each has its own AST node. `spawn`'s target is a function name by grammar, so "the first argument is not evaluated" is no longer a special rule attached to something that looks like a call, and `exit` visibly does not return. `!` associates to the right (`a ! b ! m` sends `m` to `b`, then the returned `m` to `a`) and sits just above `=`, so `x = pid ! msg` binds the sent message. `exit` takes an operand at `or` precedence or tighter, so `exit reason` and `exit ${'error, r}` need no parentheses while a send or binding operand does. Lexically, `!` immediately followed by `=` is not-equal and a lone `!` is send; the canonical formatter always writes both with surrounding spaces, so formatted source cannot confuse them. `self`, `mkref`, `spawn`, `exit`, `if`, and `else` are reserved words; `send`, `make_ref`, and `ref` are ordinary identifiers again.

Rationale: interprocess communication is the language's first-class concern (D009, D045), and the previous call spelling hid three non-call properties (an unevaluated argument, a non-returning form, and names reserved by the compiler rather than the lexer) behind ordinary call syntax. `!` follows Erlang directly; `<-` was rejected because `x <-1` already means `x < -1`.

`print` and `eprint` stay call-shaped and remain the only compiler-reserved names (D054). They are host I/O, not process algebra, and are expected to become qualified names when a module system exists (`questions.md`, "Modules and code replacement"); giving them keyword syntax now would have to be undone then.

Formatter. One declaration per clause, declarations separated by one empty line, expression clause bodies on the clause line (`pattern => expr;`), block bodies opened on the clause line with no trailing `;`, `else if` chains flattened onto the closing brace. `nervous -F old.nv` converts previous-syntax source to this layout in one step; the frontend fixture `tests/frontend/compat.nv` is the reference.

## D059 - FIFO run queue

Supersedes D042's dispatch selection. The runtime keeps one FIFO run queue of `runnable` process slots, and the scheduler dispatches its head. Every transition into `runnable` appends at the tail: spawn, a quantum yield, a message arriving for a `waiting` process, and a deadline expiring for one. Every transition out removes: dispatch, or the exit of a process that was never (or not yet again) dispatched. The queue therefore contains exactly the `runnable` slots, and dispatch is O(1) in the number of live processes. The queue is intrusive -- two slot-index links per process entry, no allocation -- and uses indices rather than pointers because spawn may reallocate the process table.

Dispatch order is "runnable longest first", deterministic, and independent of slot number. This changes no existing guarantee: D036 per-sender message order and D057 dispatch-ordered output are stated in terms of dispatch order, whichever rule produces it; D050's ascending-slot tie order for equal deadlines is preserved because the wake scan enqueues in ascending slot order; and in every existing test fixture the FIFO order coincides with the rotating scan it replaces (a spawned child still runs before its parent's next quantum, because the parent re-enqueues behind it when its quantum ends).

Rationale (D059): D042's rotating slot scan cost O(live processes) per dispatch whenever the next runnable slot lay behind the cursor, which is not an edge case -- it is every hop of a ring whose token happens to travel downward through slot numbers. `examples/ring.nv` at 65535 nodes x 2000 laps (131M hops) took 5826 s under the scan, ~44 us per hop against ~2 us at 10 or 1000 nodes; the same program built to spawn in the opposite order would have taken about 4.5 minutes. A scheduler whose per-message cost depends on spawn order is not acceptable for a language whose headline feature is very many processes, so this is settled now rather than left to milestone 10. Confirmed after the change: the same ring at 32768 nodes x 2000 laps (65.5M hops) ran in 114.75 s, 1.75 us per hop -- flat with the 2.0-2.1 us measured at 10 and 1000 nodes, where the scan would have cost ~22 us per hop.

## D060 - Guards

Settles the guard half of D034. A function clause, `match` clause, or `receive` clause may carry one guard: `fn f(patterns) when guard { ... }` and `pattern when guard => body`. `when` is a reserved word. The guard is evaluated after the clause's pattern has matched and bound its variables, and before the clause is selected: the clause is selected only if the guard evaluates to exactly `'true`. A guard that evaluates to `'false`, to any non-boolean term, or that *faults* -- `badarith` on a non-integer operand, `overflow`, `divide_by_zero`, `system_limit`, anything -- makes the clause fail and the next clause is tried, exactly as a pattern mismatch would. A guard can never terminate the process. This is Erlang's rule and it is what makes `when is_int(x) and x > 0` mean what it looks like: the second test never sees a non-integer, and the first test's failure is silent selection, not a crash.

Guard expressions are restricted so that "cannot terminate the process" is the only effect a guard can have. A guard is built from literals, variables (pattern-bound or earlier), tuple construction, the arithmetic, comparison, equality, and boolean operators (with D016's short-circuit and strictness rules), and the type-test intrinsics. It may not contain a call to a user function, a binding, a block, `match`, `receive`, `if`, `spawn`, `!`, `self`, `mkref`, `exit`, `print`, or `eprint`; the compiler rejects these with a source position. Guards therefore never allocate anything that outlives the clause, never touch the mailbox, never change process state, and are bounded in reduction count by their source size.

Five reserved call-shaped intrinsics join `print`/`eprint` (D054): `is_int(x)`, `is_atom(x)`, `is_tuple(x)`, `is_pid(x)`, `is_ref(x)`, each returning `'true` or `'false` for any term and never faulting. They are ordinary expressions usable anywhere, not guard-only forms, because "what kind of term is this" is a question programs ask outside guards too; they are the answer to the type-system question (see the record of that discussion in `questions.md` if it is added) at the level this language operates on: tags, tested at the point of use. There is one bytecode instruction, `istype dst src kind`, with the kind as an immediate operand. A future `is_binary` joins the same shape in milestone 09.

Bytecode gains two instructions for the fault rule. `guard fail-target` enters guard mode: until the matching `guardend`, any fault the executing instruction would have raised instead transfers control to `fail-target` and leaves guard mode. `guardend` leaves guard mode normally. The compiler brackets every guard's code with the pair and follows the `guardend` with a `testatom result 'true fail-target`, so both the fault path and the not-`'true` path reach the same clause-failure label with guard mode off. The verifier treats `guard` as having two successors (its fallthrough and the fail target), which is sound for definite initialization because registers are only ever set, so the register set at the `guard` is a subset of the set at every instruction the fault could come from. Guard mode is a single field on the execution state, not a stack: guards cannot call, so they cannot nest, and a quantum expiring inside a guard resumes with the field intact.

Also settled here, because it falls out of the same pass: a negative integer literal is a pattern (`fn f(-1) { ... }`, `match x { -1 => ... }`), compiled as the integer constant it denotes. Before this it was rejected as "not a pattern" because the parser sees unary minus applied to a literal.

Rationale: guards are the pragmatic answer to "what type is this" in a dynamically tagged language with pattern-matched dispatch, and the alternative -- nested `if` inside clause bodies -- both loses the flat clause structure and cannot express "skip this clause" without an explicit fall-through call. The fault-means-false rule is the load-bearing part: without it a type test in a guard has to be written defensively in a specific order, and a guard on a message from another process becomes a way for that process to crash the receiver. Restricting guard expressions to side-effect-free forms is what makes fault-means-false safe to implement as a simple control transfer, since nothing partially done needs undoing. The `guard`/`guardend` pair costs two reductions per guarded clause attempt; a guard-free clause costs nothing new. Milestone 10 still owns the multi-scheduler questions (run-queue ownership, work movement, and wakeup, D011); it starts from one FIFO per scheduler rather than from a slot scan. The remaining O(live processes) scans -- the idle-time deadline search and the idle-time "no process left running" check -- run only when nothing is runnable and are the indexed-deadline work D050 already assigns to milestone 10.

## D061 - Tagged terms

A term is one 64-bit word. A few low bits are the tag; the tag distinguishes immediates from boxed terms, and a boxed term is a pointer to a header word followed by its body, the header carrying the boxed kind and the body length in words. Immediate kinds: small integer (the payload bits, sign-extended), atom (D062 table index), and PID (slot and generation packed into the payload; the split is an implementation constant, `NvLimits.maxprocess` may not exceed the slot field, and D041's retirement of a slot at generation exhaustion applies at the representation's generation ceiling rather than at `~0UL`). Boxed kinds now: tuple (n element words), ref (incarnation and counter, D038), and boxed integer (a 64-bit value outside the immediate range, so D007's checked 64-bit semantics are unchanged and arithmetic on a boxed result reboxes). Boxed kinds later: binary (milestone 09), bignum, map, remote PID. Adding a kind is adding a header value; the collector needs to know only header kind and length.

Terms are immutable, so within one process a copy of a term is a copy of its word and structure is shared freely. `move`, `getelem`, `return`, send's result value, and pattern bindings become single word copies; nothing inside a heap is ever deep-copied. Deep copies happen only at the process boundary (D064). Equality stays structural (D028); identical words are a fast path, not a definition.

Construction therefore no longer traverses its elements, so D047's portable depth ceiling (`NvMaxtermdepth`) is enforced where traversal happens instead of where a term is built: the message copy on send (`mailbox_full`, as D047 already says), structural equality, and printing (`system_limit`), each with an explicit depth counter. The collector is breadth-first (D063) and needs no bound. This keeps the guarantee D047 exists for -- no admitted value can exhaust the host C stack -- without charging every tuple construction for it.

Rationale: `NvValue` today is a ~24-byte struct that owns its storage, so every register move is a deep copy and every atom is a heap string; a ring hop (24 reductions) costs ~18 malloc/free pairs and four full tree walks of the message, which is essentially all of the ~1.5 us per hop in `bench/README.md`. A word-sized immutable term is the representation that makes copying a register free and makes a copying collector possible; both properties are needed, and neither can be bolted onto the struct.

## D062 - Interned atoms

Atoms are indices into one runtime-wide, append-only atom table; the index is the term's payload and atom equality is integer comparison. The table is bounded by `NvLimits.maxatom`. No source form creates an atom from run-time data -- atoms enter only as module constants, as the fixed set the runtime itself produces (`'true`, `'false`, `'ok`, `'undefined`, fault reasons), and through the host API -- so the table is fully populated at module load and `maxatom` exhaustion is a load or API error, not a process fault. Atoms are never collected in this milestone. Atom text is kept for printing, formatting, and host-boundary conversion only.

Whether a later module or code-loading milestone needs collectable or namespaced atoms is tracked in `questions.md` ("Modules and code replacement"); this decision is compatible with either answer because nothing outside the table ever holds atom text.

## D063 - Per-process copying collector (when and by whom superseded by D067, D068)

Each process owns one heap: a contiguous block of words with bump-pointer allocation. When an allocation does not fit, the process's heap is collected: a Cheney breadth-first copy of every live term into a fresh to-space, after which the old space and every adopted fragment (D064) are freed. The to-space is sized from the live data found (grow when live data exceeds half the space; never below a small minimum); D069 fixes the constants. The original text here said collection happens inside the owning process's allocation and that the scheduler never collects on a process's behalf; D067 moves the collection point to an instruction boundary chosen by the scheduler and D068 lets a separate Plan 9 proc perform it while the scheduler runs other processes. What stands unchanged: no process ever scans, moves, or frees another process's heap from *bytecode*; the algorithm, root set, classification rule, and C-variable rule below.

Roots are exactly: every register of every frame on the process's frame stack (D065), and nothing else. Registers are initialized to a non-pointer word at frame entry so the whole register area is scannable without a validity bit. `NvExec` holds no heap word outside the frame stack; anything it must keep across an allocation is either in a register or has already left the heap as a fragment (D064). Receive scan state points at mailbox fragments, which do not move.

The collector classifies a boxed word by address: inside the from-space or inside an adopted fragment, it is copied and forwarded; anywhere else -- a still-queued mailbox fragment under scan, a host-owned fragment -- it is left unchanged, because that memory is stable for as long as the process can reach it. A register that still points into a mailbox fragment the process later takes is thereby kept correct, since adoption makes the fragment movable and the next collection forwards the register.

Implementation rule, load-bearing: no C variable holds a heap pointer across anything that may allocate. Reread through the register (or other root) after every allocation, including allocations made inside host callbacks. The frame stack and fragments do not move during collection; heap objects do.

Rationale: a copying collector's cost is proportional to live data, not to garbage, and an actor's live set is tiny (a ring node's is one pid and one token), so collections are cheap and the heap compacts itself; allocation is a pointer bump. Generational collection and collection concurrent with the *owning* process (barriers, incremental copying) are out of scope: with per-process heaps this small there is no pause problem for them to solve. What D068 adds is collection concurrent with the *scheduler*, which needs neither, because the process being collected is stopped.

## D064 - Message fragments merged at take

A send copies the message once, into a self-contained fragment: one contiguous allocation outside every heap, holding a word count and the message's terms with internal pointers relative to the fragment. The copy counts words as it goes and stops at the first word past the smaller of `NvLimits.maxmessage` and the receiver's remaining mailbox budget, so a rejected send (`mailbox_full`, D039) costs at most the limit and enqueues nothing. The fragment is owned by the receiver's mailbox and is the `NvMessage` (there is no separate queue node with a separate copy). Sends to a dead PID copy nothing.

`recvbegin` and `recvnext` place the candidate in the destination register as a word pointing into the fragment; no copy is made and pattern matching reads the fragment in place. `recvtake` unlinks the fragment from the mailbox and pushes it onto the process heap's adopted-fragment list, where it counts as heap words (D066) and is merged into the heap proper by the next collection, which copies its live parts and frees it. Fragments still queued when a process exits are freed with the mailbox.

Terms cross the host boundary the same way: root arguments in, the root result and exit reasons out, and messages removed by the host API, all travel as fragments. This makes a process heap freeable the moment the process exits, with nothing else pointing into it. D044's "no allocation during exit transfer" becomes "one bounded allocation whose failure is reported as `system_limit`".

Rationale: this is D008 made concrete. Copying at send is the price of process-local collection and is paid once; today the same message is walked or copied four times (size, send copy, send result, receive copy). Merging at take rather than at arrival means a process that never takes a message never pays for it, and merging by the collector rather than by `recvtake` means take is O(1) and the collector remains the only code that moves terms.

## D065 - Frame stack

Each process owns one contiguous frame stack of words. A frame is a fixed header (function, pc, caller frame offset, caller destination register) followed by that function's `nreg` register words. `call` pushes a frame; `return` pops it; `tailcall` overwrites the current frame in place after reading its argument word, so D047's "tail calls retain their current depth" is literally true of the stack. No call, tail call, or return allocates. The stack grows by reallocation when full, bounded by `NvLimits.maxframe` frames (D047) and the D066 word budget; because it can move on a push, C code holds register pointers only between calls, which the interpreter already respects since only it pushes frames.

The frame stack is the complete root set (D063). A collection walks it frame by frame, skipping headers; it does not move stack words.

`NvExec` refers to the scheduler's host callback table by pointer instead of holding a copy; the table is identical in every process and was a measurable share of the per-process footprint.

## D066 - Exact accounting

`NvLimits.maxheap` is a per-process budget in words covering the heap space in use, every adopted fragment, and the frame stack. An allocation that cannot be satisfied within the budget after a collection faults `system_limit`; the process's state is unchanged by the failed attempt, so the fault is controlled (D029). Mailbox limits (`maxmailbox`, `maxmessage`) become word counts of fragments including their headers, checked all-or-nothing during the D064 copy. Supersedes D040 and the byte-accounting language of D039; D047's depth ceiling stands as described in D061.

Rationale: these are the numbers the runtime actually allocates, counted where it allocates them, so they are exact by construction rather than an estimate of a representation the program never sees. A single word budget per process is what lets `nervous -s` report footprint honestly and what a later multicore scheduler can enforce without knowing anything about term shapes.

## D067 - Collection points: reserve at the boundary, yield to collect

Supersedes D063's "when". The interpreter never collects. Every instruction that consumes heap or frame-stack words declares its need before it starts and executes only if the reservation succeeds; otherwise the process yields with the collection request, and the scheduler decides what happens next.

Reservation. The allocating instructions are few and their needs are known from their operands: `tuple` (1 + n words), `mkref` and a boxed-integer result (header + body), `call` (frame header + the callee's `nreg`, on the frame stack), and `recvtake` (the fragment's word count, charged to the budget though allocated outside the space). `spawn` copies into the child's fresh heap, which is sized for the argument, and reserves nothing in the parent. A reservation succeeds when the words fit both the current space and the D066 budget. When it fails, `nvexecrun` returns a new outcome, `NvCollect`, carrying the words needed; the pc is unchanged, no reduction is charged, nothing has been written, and the process stays `runnable` (D041 gains no state -- what needs collecting is the heap, D068). For `recvtake` the candidate stays queued: D044 already guarantees only the owner consumes it and the owner is not running. Because reservation precedes execution, D066's "state unchanged by the failed attempt" holds by construction rather than by rollback, and a rejected instruction is re-executed from its reservation after the collection.

Adopted fragments count toward the reservation. A process that receives much and builds little would otherwise never trigger a collection and its adopted list would grow without bound; charging adoption at `recvtake` is what makes D064's "merged by the next collection" actually happen.

Triggers. Demand: the `NvCollect` outcome. Opportunistic: a `waiting` process whose heap words (used plus adopted) exceed a threshold may be collected while it waits, by the scheduler's idle step (D050) or as a policy of D068 -- this is the case that is entirely off the critical path, a process blocked in `receive` giving its garbage back without being scheduled. Never: a `running` process, and never from bytecode.

After a collection the scheduler re-dispatches the process; the demand case re-executes the same reservation. The collector sizes the to-space for live data plus the requested need (D069); if that cannot be done within `maxheap`, it reports exhaustion through the heap and the scheduler faults the process with `system_limit` (D066) before re-dispatch. Collection consumes no reductions (D030 unchanged; quantum policy stays with milestone 10).

Rationale: an interpreter that collects mid-instruction has to make every partially built value a root and every host callback GC-safe. Reserving first, as BEAM's `test_heap` does, makes the instruction boundary the only safe point and makes it the scheduler's decision who performs the collection and on which Plan 9 proc -- which is the property D068 needs and the reason this is a decision rather than an implementation detail.

## D068 - Heap ownership; collection runs beside the scheduler

Ownership split, load-bearing. A process's heap, frame stack, and adopted-fragment list are touched by exactly one party at a time: the interpreter while the process is `running`, or one collector while the heap is under collection. The mailbox -- its queue links, word count, and queued fragments -- belongs to the scheduler and to senders and is never read or written by a collector. Queued fragments do not move, and D063's classification rule leaves any register pointing into one alone, so the collector needs no knowledge of the mailbox at all. Consequently a send to a process under collection is an ordinary send, and a deadline firing for one is an ordinary wakeup.

Heap owner state. Each heap carries an owner state, `idle`, `running`, or `collecting`, orthogonal to the D041 lifecycle. Dispatch rule: the scheduler dispatches a process only when its heap is `idle`. A `runnable` process whose heap is `collecting` is moved to the tail of the D059 queue and the next process is dispatched; the D059 invariant (the queue holds exactly the runnable slots) is preserved. If every runnable process is under collection, the scheduler waits for a completion with the D011 mechanism (`tsemacquire`, bounded by the nearest armed deadline), never by spinning; D050's idle rule is otherwise unchanged. Runtime teardown waits for every in-flight collection before freeing process memory.

Who collects. The collector is one function, `nvheapcollect(heap, stack, need)`, that knows nothing of `NvExec`, the scheduler, or the mailbox; it can therefore run on any Plan 9 proc sharing the runtime's memory. For each collection the scheduler chooses: inline (call it, then continue) or off-process (mark the heap `collecting`, `rfork(RFPROC|RFMEM|RFNOWAIT)`; the child collects, marks the heap `idle`, `semrelease`s the scheduler's semaphore, and `_exits`). There is no persistent helper proc and no work queue: a collector proc exists for exactly one collection. Policy is a single runtime knob, `NvLimits.gcoffload`, in words of heap to scan (used plus adopted): collections at or above it go off-process, below it inline. `0` forces every collection off-process and is a test setting; the default is provisional and is to be set from the bench (`questions.md`).

A collector proc does no I/O, takes no locks other than the heap's, allocates only through the shared malloc (which is already proc-safe), and cannot fault the process; exhaustion is recorded on the heap (`NvHeap.exhausted`, already per-heap) and acted on by the scheduler at the next dispatch. A collector proc must exit with `_exits`, not `exits`, so it runs no atexit handlers of the runtime.

This is not concurrent garbage collection in the usual sense and needs none of its machinery: the process whose heap is being collected is stopped and holds no pointer anywhere but its frame stack, so there are no barriers, no read of a half-moved object, and no interleaving with a mutator. The concurrency is between the collector and the scheduler running *other* processes, which touch disjoint memory by the ownership split above.

Rationale: per-process heaps make collection cost proportional to one process's live data and let every other process's data stand still, but in a single Plan 9 proc the scheduler's time is the only time there is, so an inline collection of a large heap still delays every process queued behind it. Moving that collection to another proc is what turns "no other process is paused" from a statement about memory into a statement about latency, and the whole copy-on-send design (D008, D064) was chosen to make exactly this possible: no pointer crosses a heap boundary, so a heap can be handed to another proc with one state word. Fork-per-collection rather than a helper pool because collection cost scales with live data while a fork costs a fixed few tens of microseconds -- so off-process collection only pays for large heaps, and for those the fork is noise. Doing this before milestone 10 rather than inside it is deliberate: it is the simplest instance of "which proc owns this process's memory right now", proved under one scheduler where the only concurrent parties are the scheduler and collectors, and milestone 10's migration and work movement inherit the protocol instead of inventing it under harder conditions.

## D069 - Exhaustion, sizing, and stress

Exhaustion is judged on live data, never on allocation rate. `maxheap` (D066) bounds what a process retains: live heap words after collection plus its frame stack. A collection sizes the to-space for live data plus the requested need; if the smallest such space exceeds the budget, the process faults `system_limit` (D067). A process that allocates fast and retains little never faults.

Sizing constants, provisional until the bench moves them: the space is never smaller than 64 words (512 bytes; the per-process footprint of a 65535-node ring is why not larger); the to-space is the smallest power-of-two multiple of that in which live plus need occupies at most half. Spaces do not shrink in this milestone -- a process whose live set collapses keeps its space until exit; shrinking is deferred to `questions.md`. The frame stack grows by doubling, is counted in the budget, and is never collected or moved by the collector. A fresh process's initial space is sized to its argument tuple by the same rule.

Stress mode is part of the design, not a debugging afterthought: `NvLimits.gcstress` forces a collection at every reservation, and the full test suite is required to pass under stress twice -- once with every collection inline and once with `gcoffload` 0 (every collection in a separate proc). With a moving collector, a C variable holding a heap pointer across an allocation (the D063 rule) is a use-after-move that normal trigger frequency hides indefinitely and that the off-process path turns into a race; stress at every boundary is the only test that finds it deterministically. The `nervous` CLI exposes both knobs.

Implementation note (M08-T04a, explicit inline core only): the frame charge is retained stack capacity (`nstack`), including slack after a pop, while only active register slots are roots. Heap accounting remains used/live object words, not unused heap capacity. The core receives root ranges and a separate stack-word charge; an exec adapter constructs those ranges without exposing frame metadata to the collector.

The core accepts host/startup chunks as from-space and produces one contiguous chunk. Trial copying records source addresses alongside to-space, restores forwarding headers on a failed trial, and commits roots only after copying and sizing succeed. Growth retries restore and recopy into a larger power-of-two space; no in-progress space is reallocated. M08-T04b enables automatic inline collection on managed execution heaps (D072); host construction still uses non-moving chunks. Scratch storage and linear adopted-fragment address classification still need measurement before stage-3 performance acceptance.

## D070 - Plan 9 procs are the unit of parallelism

Milestone 10 runs one scheduler per Plan 9 proc. The procs share one address space (`rfork(RFPROC|RFMEM)`), coordinate through the D011 semaphores and through locks in that shared memory, and share the process table, the atom table, and mailboxes; heaps and frame stacks are owned per D068 and are never shared. The collector proc of D068 is the first proc to share the runtime's memory, and its protocol -- an owner state per heap, a dispatch rule that skips what it does not own, completion signalled by semaphore -- is the template for handing a process between schedulers.

What milestone 10 must add, named here so stage 3 does not accidentally preclude it: a lock on atom interning (the table is append-only, so readers need none); a rule for the process table, which spawn may grow (D043's "reacquire after spawn" becomes a lock, or the table becomes fixed-size at `maxprocess`); a per-mailbox lock on enqueue that preserves D036 per-sender order; one D059 queue per scheduler. Not decided here: work movement and stealing, migration safe points (a process is movable exactly when its heap is `idle`, which is the D068 state -- the safe point is the same instruction boundary as D067), timer ownership, and the I/O ordering obligation D055/D057 leave open.

Rationale: this is the D009/D011 plan made explicit now that D068 has fixed the one primitive it rests on. Plan 9 makes a shared-memory proc a single `rfork` and gives semaphores that work across them; there is no reason to build a thread layer or an event loop in front of that.

## D071 - Verified guard regions

Closes the verifier gap identified as post-R2-F01; strengthens the symbolic bytecode contract without changing D060 source semantics. Guard safety must be established by verification, not assumed because the source compiler normally emits well-structured code.

Within each function, `guard` and `guardend` delimit non-nested lexical regions. Every `guard` has one matching `guardend`, and an unmatched `guardend` is invalid. These structural rules apply to all instructions, including unreachable code, like operand validation.

Only value operations, pattern tests, arithmetic and comparison, `istype`, `nop`, local jumps, explicit `fail`, and the closing `guardend` are allowed inside a region. Calls, tail calls, returns, process operations, I/O, and nested guards are rejected. `fail` is allowed because it implements the fault-means-clause-failure path used by compiled boolean operators; it clears guard mode through the interpreter's normal fault path.

Every ordinary control-flow edge preserves the active guard identity, except the fallthrough of `guard` (which enters its region) and `guardend` (which leaves it). A jump or test may branch within a region, including to its closing `guardend`, but cannot enter the body from outside, bypass the close, or cross to another guard's body. A guard's exceptional failure target must be outside every active region. It may point to a subsequent `guard` instruction because that instruction is entered with guard mode off. Calls and returns consequently occur only with guard mode off, so the execution-wide failure target can never be interpreted in a different frame.

The existing definite-initialization edge from `guard` to its failure target remains conservative and sound: no call or frame replacement can occur inside the region, and the permitted instructions never uninitialize a register. Normal guard completion still passes through `guardend` before testing the guard's result. Raw bytecode may contain loops within a region; this decision proves guard-state safety, not termination or bounded traversal cost.

Rationale: merely banning calls between adjacent delimiters is insufficient if a branch can enter, escape, or cross a region. A lexical region map plus edge checks provides a small, linear structural pass ahead of definite-initialization analysis and states exactly what the interpreter relies on.

## D072 - Automatic inline collection integration

The first D067 host implementation collects inline. `nvexecrun` returns `NvCollect` with the pending word charge, leaving the instruction's pc, registers and reduction count unchanged. The execution's durable state remains `NvYield`. The scheduler first returns its process to the FIFO tail exactly once, then services the stopped owner's request. The next dispatch retries the instruction. Standalone execution services requests inline within the same reduction budget instead; collection never consumes reductions or emits an instruction trace line.

Reservation distinguishes bump-space demand from the added budget charge: tuples, boxed integer constants/results and Refs need bump space; taking a fragment and growing retained stack capacity need additional budget. Tailcall growth counts too. Adopted words participate in space pressure even with unlimited maxheap, so a receiver that builds little still collects. The current collector conservatively sizes to-space for the added charge even when it represents adoption/stack growth; optimizing that slack is measurement work, not a change to accounting.

A failed collection is remembered for the instruction's retry. The interpreter charges that attempted instruction once and delivers `system_limit` or `out_of_memory` through its usual fault path. This refines D067's shorthand that the scheduler faults exhaustion before re-dispatch: D060 requires a guard's resource fault to remain clause failure, and the scheduler must not bypass that rule. No host side effect or message take occurs on the failed attempt. `recvneed` validates and sizes the selected queued fragment without unlinking it; only the subsequent successfully reserved `recvtake` transfers ownership.

An execution's initial stack capacity is charged before copying its argument; the copied argument is rooted and compacted before execution begins. Managed heaps refuse unreserved chunk growth and remain contiguous. Host construction and startup copying retain non-moving chunk allocation so unregistered C temporaries cannot be invalidated by a constructor. Collection remains explicit and external to constructors.

Inline stress forces one collection at every allocating-instruction reservation, including calls/tailcalls, then permits one retry. The CLI exposes `-G` and `-H words` (non-negative signed-64-bit decimal; 0 means unlimited). `nervous_gcstress=1` supplies the same stress default to CLI invocations in regression scripts; it does not override C tests' explicitly installed runtime limits. The dedicated automatic-memory tests run both stress settings and quanta 1/1000.

At idle, a waiting process with used/adopted words above half its space and above its last live sample may be collected without running bytecode or touching its mailbox. Failure of this optional attempt leaves it waiting with its roots intact. An unchanged live set is not repeatedly collected. This threshold is provisional pending measurement.

Statistics distinguish host allocator high-water bytes from per-process live heap samples after successful collections; neither is mislabeled as an aggregate live footprint. There is no off-process collector, gcoffload option, heap-owner lock protocol, or milestone-08 acceptance claim in this slice. D068-D070 remain the next work after user verification of inline behavior.

## D073 - Amortized table storage and explicit performance diagnostics

The initialized process-slot count is separate from allocated table capacity. Capacity grows geometrically (initially up to 16 slots, bounded by the initial live-process limit and host allocation size); unused capacity is not initialized, scanned or exposed as a PID. A lowest-possibly-free hint advances on successful spawn and is lowered on exit. It preserves lowest-slot reuse while avoiding repeated live-prefix scans during append-only creation. Generation-exhausted slots remain retired, so initialized/capacity counts may exceed maxprocess, which bounds live processes rather than historical identities. Growth checks the PID slot domain and malloc byte-size limit. FIFO links remain indices, and callers still reacquire pointers after spawn.

For the common small collection, up to eight frame-root views and 64 rollback source-address slots use bounded C-stack scratch. Larger cases allocate scratch dynamically, including promotion after a restored trial. The current chunk descriptor is reused but is not modified until successful commit. This removes three auxiliary allocator pairs from a one-frame, 64-word collection without retaining per-process scratch or changing roots, budgets, heap sizing, collection triggers or rollback semantics.

Performance diagnostics are explicitly separated from scheduling policy. Requested-storage snapshots traverse initialized slots, chunks and fragments only when a host asks between dispatches with exclusive access. They distinguish retained capacity from used words and exclude allocator/module/atom storage and transient collection scratch. Optional real-monotonic timing is off by default; it measures execution (including host callbacks), collection and spawn, with nested spawn time documented as overlapping execution. It does not use or alter the injected deadline clock or reduction accounting. No snapshot scan or profiling clock read is added to the default dispatch path.

The T04p phase benchmark is a diagnostic control, not a replacement for the original whole-program benchmark. It separates host-driven waiter setup, blocking, ping-pong traffic and draining, and can pre-size only the two busy heaps via explicit setup collection requests. This experiments with frequency/space tradeoffs without selecting a new default. Source inspection establishes the eliminated quadratic append costs and allocator calls; runtime effects, the remaining waiter high-water gap, and any offload threshold still require user-run measurements.

## D074 - Off-process collection: ownership protocol, scheduler wait, and teardown

Settles the concrete mechanism D068 named but did not specify. Every D068 invariant stands unchanged: heap, frame stack, and adopted-fragment list are touched by exactly one party at a time; the mailbox remains scheduler/sender territory a collector never reads; `nvheapcollect` and `nvexecgc`/`nvexeccollect` are expected to need no change -- they are already scheduler- and exec-agnostic pure functions operating only on a stopped process's own heap and stack, which is exactly what makes them callable from a forked proc.

### Owner state

`NvHeap` (`include/nvvm.h`) gains an owner field with three values, matching D068 exactly: `NvHeapIdle`, `NvHeapRunning`, `NvHeapCollecting`, plus a `Lock` (spinlock, not `QLock`: the critical section is one word, and a queueing lock is the wrong tool for it, matching D070's "locks in shared memory") guarding transitions into and out of `NvHeapCollecting`. Reads that only decide dispatch order (skip a collecting heap vs. dispatch it) do not need the lock; only the read-modify-write transitions do.

In single-scheduler milestone 8, `NvHeapRunning` is set only for the duration of `nvexecrun` on the currently dispatched process. It is a defensive invariant today (a `running` heap can never legally be handed to a collector) rather than a load-bearing dispatch state, because dispatch already implies `idle -> running -> idle` and D067 already forbids collecting a running process. It becomes load-bearing once milestone 10 has more than one interpreter proc.

### Launch (performed by the scheduler proc only)

Both trigger sites in `nvschedstep` -- the demand path (the `NvCollect` outcome) and the opportunistic idle sweep (D067) -- choose inline or off-process from `NvLimits.gcoffload` (new field, word units matching the existing `e->heap.words`+adopted charge): below the threshold, call `nvexecgc` inline exactly as today; at or above it, launch a collector:

1. Precondition, assert rather than merely trust: heap owner is `NvHeapIdle` and the process is `Prrunnable` or `Prwaiting`, never `Prrunning`.
2. Under the heap's Lock, set owner to `NvHeapCollecting`.
3. Record `gcinputwords` and `gcdemand`/`gcidle` exactly as the inline path does today; these are known before collection runs and need not wait for completion.
4. `rfork(RFPROC|RFMEM|RFNOWAIT)`. The child calls `nvexecgc(e)` unchanged, takes the heap's Lock, sets owner to `NvHeapIdle`, releases the Lock, `semrelease`s the scheduler's completion semaphore by 1, and `_exits(0)` (not `exits`, so no runtime atexit handler runs twice). The child touches only this `NvExec`'s heap, frame stack, and the bookkeeping fields `nvexecgc` already writes (`gcpending`, `gcretry`, `livewords`, `collections`); it never touches `NvScheduler`, the mailbox, or any other process's state, matching D068's ownership split exactly.
5. `s->gcoutstanding` (new counter) is incremented at launch and decremented at the completion fold below; it is the only new cross-cutting state a collector's existence adds to the scheduler struct.
6. `rfork` failure is not a condition to hide: fall back to an inline `nvexecgc(e)` immediately in the parent, and count it separately (`s->gcofffallback`, new counter) from ordinary inline collections, so a measurement run can see how often launch actually failed without conflating it with a deliberate policy choice to collect inline.

### Completion (folded lazily, scheduler proc only)

A collector signals only that some collection finished, via the semaphore count; it never says which process. The scheduler discovers completion by re-examining a slot, in exactly one place: whenever it is about to dispatch a `Prrunnable` slot whose heap owner is `NvHeapCollecting`, it does not dispatch. It requeues that slot to the run-queue tail (preserving the D059 invariant that the queue holds exactly the runnable slots) and tries the next slot instead, never blocking as long as some other runnable slot's heap is not collecting. The first time a slot's heap owner reads `NvHeapIdle` while `NvExec.offlaunched` (new bit, set at launch, matching the existing `gcpending` style in `nvexec.h`) is still set, the scheduler folds `e->gcretry`/`e->livewords` into `s->collections`/`s->gcoutputwords`/`s->gcfailed`/`s->lastlivewords`/`s->maxlivewords` -- the same accounting `gcsample()` already performs for the inline path -- clears `offlaunched`, and decrements `gcoutstanding`. Nothing else changes: `nvexecrun`'s existing retry logic (`prepare()`'s `retry = e->gcretry`) already resumes the pending instruction whether the collection that resolved it ran inline or off-process, because both paths converge on the same `NvExec` fields (D072). No `exec.c` change should be needed for this.

### Never spin, never falsely report idle or deadlock

Requeuing a collecting slot must not become a tight loop when every runnable slot is collecting. The scheduler must treat "no runnable slot is currently dispatchable" as a condition distinct from "no runnable slot exists" (today's only test, `!nvprocrunhead`). When every runnable slot is collecting, or the runnable queue is empty but `gcoutstanding > 0`, the scheduler computes the earliest armed deadline exactly as today and blocks in `tsemacquire` on the completion semaphore, bounded by that deadline if one exists, or by a bounded repeated wait if none does (`tsemacquire` takes a millisecond count, not "forever"; looping on a spurious return costs only a re-check, never a lost wakeup, because `semrelease`'s count persists exactly as D011 already relies on). This is exactly why the completion signal reuses `semrelease`'s counting behavior rather than `rendezvous`: a wakeup issued before the sleeper commits to sleeping must never be lost.

Corollary, which must be true and should be asserted rather than merely assumed: `NvSchedIdle` (D046's deadlock report) is returned only when `nrunnable == 0`, no deadline is armed, *and* `gcoutstanding == 0`. A background collection in flight for a blocked receiver is progress, not deadlock, and must never surface as one.

### Idle-sweep concurrency is bounded

D067's opportunistic idle sweep can now examine many waiting processes in one pass and choose off-process for several of them; unlike the demand path this can fork a burst of collectors in a single scheduler step. Cap the number of collectors launched in one sweep (a new small constant or `NvLimits` field, implementer's choice, recorded in the handoff) rather than forking one per qualifying waiter unconditionally. The cap exists to bound worst-case Plan 9 process-table pressure, not because concurrent collection of unrelated heaps is unsafe -- it is exactly the case D068 exists for.

### Diagnostics

`nvschedmemory` (D073) must not read `e->heap.cur`/`full`/`adopted` while a collector may be rewriting them. It skips any process whose heap owner is `NvHeapCollecting`, reports how many were skipped (`NvMemstats.ncollecting`, new field), and remains what D073 already calls it: an explicit, low-frequency diagnostic snapshot -- now honestly partial during heavy offload, rather than racing.

### Teardown

A process cannot exit while its own heap is collecting: dispatch already requires `NvHeapIdle` (above), and `NvDone`/`NvFault`/`NvExit` are only ever produced by `nvexecrun` running the currently dispatched process, which by construction started from an idle heap and touched nothing else in between. `nvprocexit` therefore needs no new synchronization. `nvschedfree` does: it must drain `s->gcoutstanding` to zero -- blocking on the completion semaphore and discarding or harmlessly performing each wakeup's stats fold, either is fine since the runtime is being freed either way -- before freeing any process, heap, or stack memory a collector might still hold a pointer into.

### Test determinism

Timing-dependent behavior -- send during collection, a deadline firing during collection, teardown with a collector in flight, every runnable process collecting at once -- must be testable without relying on real scheduling races. The implementation must expose a deterministic hold point a test can use to park a launched collector before it publishes completion (before it sets owner idle / releases the semaphore) until the test explicitly releases it; absent or a no-op in production. The exact shape (a callback, a semaphore address installed on the scheduler, a counted gate) is the implementer's choice, but it is a new field on `NvScheduler` and must be reported in the handoff as a semantic/interface decision per `COORDINATION.md`, since it is the kind of addition normally reserved to the coordinator.

### Shared-memory placement (load-bearing; verify before trusting any test)

`rfork(RFPROC|RFMEM)` shares only the data and bss segments between parent and child; per `fork(2)`, "other segment types, in particular stack segments, will be unaffected." `NvScheduler` is caller-allocated and is a plain stack local in every existing call site (`cmd/nervous/main.c`'s `main`, and every test's `main`). Any word a collector child writes to, or `semrelease`s, must therefore live in malloc'd (or static) storage, never as a field embedded directly in a caller's `NvScheduler` -- if it is, the child's writes land in its own private copy of the parent's stack and the parent never observes them, while `tsemacquire`'s timeout still eventually expires, so a test can pass by timeout polling with the wakeup mechanism silently dead the whole time. Concretely: the completion semaphore is a `long *gcsem`, allocated with `mallocz(sizeof(long), 1)` in `nvschedinit` and freed in `nvschedfree`; the test hold point, if it needs a word the collector child reads or writes, is allocated the same way. `NvHeap.owner`/`Lock` need no such care because `NvHeap` lives embedded in `NvExec`, and every `NvExec` is already `mallocz`'d (`lib/sched.c`'s `spawn`); `e` is the only pointer the collector child may receive (see "Collector child argument scope" below).

### `gcoffload` default and every existing call site (must be fixed together)

`gcoffload == 0` means *never* off-process, matching this codebase's existing convention that a zero-valued limit is always the safe/inert default (`maxheap` 0 is unlimited, `gcstress` 0 is off). This supersedes D068's original "0 forces every collection off-process" spelling; record the force-all-off-process test setting as a distinct value instead (a threshold of 1 word already achieves it, since every real collection scans at least one word, or a separate boolean field if that reads better -- implementer's choice, report which).

This polarity choice does not by itself make existing code safe: `NvLimits` is a plain struct, and every one of the following call sites builds one by individual field assignment *without* zeroing the struct first, so an added field they do not explicitly set is indeterminate stack garbage, not 0, regardless of which polarity was chosen. Every one of these must gain an explicit `.gcoffload = 0;` (or equivalent) alongside its other field assignments, as part of this task's write set (not optional, not deferred to T04d):

- `cmd/nervous/main.c` (one `NvLimits limits;` construction)
- `tests/process/ptest.c` (`mailboxlimits`, `timeouts`, and `main` -- three constructions)
- `tests/process/schedtest.c` (one construction)
- `tests/process/iotest.c` (one construction)
- `tests/process/r2test.c` (`h1regression`, `crossreceiveleak`, `midscanappend`, `sendthenexit`, `belowcursorfairness`, `tinylimits` (three separate constructions inside it), `receiveguard`, `guardboundaries` -- roughly nine constructions)

`tests/memory/autotest.c`'s `limits()` helper and `bench/perftest.c` already `memset` the struct to zero before assigning fields, so they are safe as-is regardless of polarity, but add the explicit line there too for consistency with every other field. `tests/memory/gctest.c` and `tests/process/exectest.c` construct no `NvLimits` at all and need no change. This list was produced by reading every file in `tests/`, `bench/`, and `cmd/`; grep the tree for `NvLimits` before starting to confirm nothing else constructs one, and report the final confirmed list in the handoff.

Because this change touches files outside `lib/` and `include/`, the exclusive write set for this task (see `STATUS.md`'s M08-T04c section) includes exactly the files listed above, for this purpose only -- do not make unrelated edits to them.

### Locked completion fold (arm64/7c ordering)

Dispatch-order reads of heap owner that only decide "skip this collecting slot" do not need the Lock -- a stale `Collecting` costs only a wasted requeue. But the completion fold reads `e->gcretry`/`e->livewords`, words the collector child wrote before its `unlock`, and `unlock` only guarantees release ordering, not acquire ordering, for a reader that never takes the lock. On an architecture without strong store ordering (this project builds with `7c`, i.e. arm64), an unlocked read of those fields after observing `owner == NvHeapIdle` is not guaranteed to see the child's writes. The fold must take the heap's Lock (lock, read/consume `owner` and, if idle with `offlaunched` set, the fields, unlock) before trusting `e->gcretry`/`e->livewords`. This is one lock acquisition per dispatch attempt of a slot with `offlaunched` set, not per dispatch in general, so it costs nothing on the common path.

### Wait and drain robustness

`tsemacquire` returns -1 on interrupt (a note delivered to the note group), which the collector-wait loop must treat as "recheck and retry," not as a hard scheduler error. `nvschedfree`'s drain of `gcoutstanding` to zero must be a bounded loop (a generous but finite number of wait iterations) that fails loudly -- an assertion or a reported error, implementer's choice, but not silent and not an unbounded `semacquire` -- if outstanding collectors never reach zero (for example, a collector proc that was killed), rather than hanging teardown forever on a single lost wakeup.

### Collector child argument scope

The collector child launched in step 4 of "Launch" above must receive only the one `NvExec *e` it is to collect (and the malloc'd `gcsem` pointer needed to signal completion). It must not receive, retain, or be able to reach `NvRuntime *`, `NvProcess *`, or `NvScheduler *` -- not because Plan 9 fails to share the memory (`RFMEM` shares all of it), but because D068's ownership split is a source-level discipline the collector's own code must visibly respect: if the collector function's arguments are just `e` and `gcsem`, there is no path by which it could accidentally touch the mailbox, the process table, or scheduler stats, and a reviewer can see that from the function signature alone.

### Folding a waiting process's completion (amendment; found in coordinator review, M08-T04c)

The completion fold above is reached only through the dispatch path, which examines only `Prrunnable` slots. A collection launched by the opportunistic idle sweep (D067) targets a `Prwaiting` process, which by definition is not in the run queue and may never become runnable again on its own -- exactly the case of a genuinely deadlocked program. Left unaddressed, such a process's completion is never folded: `offlaunched` never clears, `gcoutstanding` never returns to zero, and the "never falsely report idle" corollary above degenerates into "never report idle again at all", permanently disabling D046 deadlock detection from the first such completion onward.

The fix is a second fold path, `gcfoldall`, that walks every initialized process slot -- runnable or waiting, dispatched or not -- and gives every `offlaunched` exec a chance to fold via the same locked check `gcfold` already performs. It is called once per idle-branch pass, before the `gcoutstanding == 0` decision, and at the start of every `nvschedfree` teardown iteration (see below): both call sites need an accurate count precisely at the moment they are about to act on it, whether that action is reporting idle or deciding whether teardown may proceed.

### Teardown must not treat the semaphore as a completion counter (amendment; found in coordinator review)

An earlier draft of "Wait and drain robustness" above decremented `gcoutstanding` once per successful `tsemacquire`, treating the semaphore's count as a 1:1 proxy for "one more completion is now foldable". That invariant does not hold: `gcfold` (via the ordinary dispatch path, or via `gcfoldall`) decrements `gcoutstanding` whenever it observes an exec go idle, entirely independent of whether anyone has consumed that exec's `semrelease`. A completion folded through ordinary dispatch therefore leaves a stale, uncollected credit sitting in the semaphore's count. Semaphores are fungible integers, not tagged per-completion tokens, so a later `tsemacquire` in `gcdrain` can consume that stale credit and decrement `gcoutstanding` to zero while a *different*, genuinely still-running collector proc is still rewriting memory `nvschedfree` is about to free -- a real use-after-free, not a theoretical one.

The semaphore must be used purely as a sleep/wakeup primitive in this drain, never as a count of outstanding work: `gcdrain` calls `gcfoldall` to (re-)establish the true `gcoutstanding` value on every iteration, both before the wait loop starts and after every `tsemacquire` (whether it timed out, was interrupted, or succeeded on a stale or fresh credit makes no difference, since `gcfoldall` re-derives the truth from the locked owner check regardless).

### The idle sweep must gate on `offlaunched`, not a raw owner read (amendment; found in coordinator review)

D067's opportunistic sweep decides whether a waiting process is eligible for a *new* collection launch. An earlier draft gated this on `e->heap.owner != NvHeapCollecting`, read without the heap's Lock. Unlike the diagnostic and dispatch-decision cases above, this one is not merely an ordering hazard: if the collector has already published `NvHeapIdle` for an exec whose completion this scheduler has not yet folded (because `gcfoldall` has not run since), the sweep would see "not collecting" and could launch a *second* collector for the same exec while its first completion is still unfolded -- double-incrementing `gcoutstanding`, silently discarding the first collection's `gcretry`/`livewords`, and risking two collector procs genuinely running against the same heap concurrently if timing were ever unlucky enough.

The correct gate is `!e->offlaunched`. Unlike `owner`, `offlaunched` is touched only by the scheduler proc itself (set by `collect()`, cleared only by `gcfold` after its own locked, synchronized check), so it needs no lock and is authoritative: it is true exactly when a launch/fold cycle is already in flight for that exec, which is precisely the condition under which no new launch may be considered, independent of what `owner` currently reads.

### The wait must drain stale semaphore credits before blocking (amendment; found in coordinator review)

Because `gcfold`'s ordinary dispatch-path fold decrements `gcoutstanding` without ever consuming the corresponding exec's `semrelease` (see the previous two amendments), every completion folded that way leaves an uncollected credit sitting in `gcsem`'s count. Left undrained, the bounded `tsemacquire` wait in the "collector outstanding" branch would return immediately on a stale credit on every subsequent idle pass for the rest of the scheduler's life -- not a correctness bug (`gcfoldall` still keeps `gcoutstanding` accurate regardless of what the semaphore reads), but it turns the "never spin" wait into exactly the busy loop this design exists to prevent.

Before the bounded wait, drain the semaphore to zero with a non-blocking `tsemacquire(gcsem, 0)` loop, then call `gcfoldall` again and recheck `gcoutstanding == 0`. This recheck is also the lost-wakeup guard for a completion landing in the narrow window between the earlier `gcfoldall` (used for the `gcoutstanding == 0` idle/deadlock decision) and this point: it is caught here and reported as progress immediately, rather than costing a full wait period before the next call notices it.

### What this does not settle

The actual `gcoffload` default *value* (T04d, measurement-driven); whether a CLI flag exposes it before T04d (optional, implementer's call, not required for this task's acceptance); work stealing, migration, or any milestone-10 concern (D070 already separates these). This decision is scoped to one scheduler proc launching and reaping collector procs for its own processes' heaps.
