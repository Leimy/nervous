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
