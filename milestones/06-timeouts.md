# Milestone 06 - Receive Timeouts

## State and dependency note

Complete. Every interface below is settled (D048 through D052; D052 is an unrelated decision that briefly shared D048's number by accident, see `../docs/decisions.md`) and implemented. `rc tests/run.rc` passes completely, with coverage added for immediate match, zero timeout, `'infinity`, message-vs-expiry racing, tied and differing deadline ordering, and a runtime `bad_timeout` fault through the full execute path.

One item from "Required tests" below is deferred rather than closed here: a dedicated regression for deadline cancellation specifically via process exit, fault, or scheduler teardown. It is argued safe by inspection (`nvprocexit` frees the mailbox/exec unconditionally regardless of `hasdeadline`; D037/D041 already prevent a reused slot from inheriting a stale wait state), and falls inside R2's existing mandate to inspect every timer ownership path for leaks and dangling references, so it is tracked there rather than blocking this milestone. See `STATUS.md` for the current coordinator state; R2 is now the active mandatory gate.

**Correction from R2 (R2-F15, `docs/review-findings.md`):** the inspection argument above is half wrong. `nvprocexit` freeing the mailbox/exec unconditionally is correct and verified. But D037/D041 do *not* actually zero `hasdeadline`/`deadline` on exit or slot reuse -- those decisions govern PID aliasing and state-transition rules, not field zeroing. The claim held only because an incidental guard elsewhere (the idle deadline scan gating on `state == Prwaiting`) made the stale field unreachable, not because anything cleared it. R2-T01 fixed this directly in `lib/process.c` (`nvprocexit` and `nvprocspawn`'s reuse branch now clear both fields) and added an executable regression (`tests/process/r2test.c`'s `h1regression`) rather than leaving the claim as inspection-only.

## Goal

Add monotonic deadlines to selective receive without changing mailbox semantics or losing wakeups.

## Read first

- `../docs/semantics.md`
- `../docs/questions.md`, section "Milestone 06"
- `05-processes.md`

## Scope

- Duration-expression lowering to integer nanoseconds, with its accepted domain settled by M06-T01.
- `after duration => expression` on receive.
- Monotonic scheduler clock.
- Waiting-process timer structure.
- Expiration, cancellation, and message-arrival interaction.

With one scheduler, no semaphore wakeup is needed yet; design the state transition so milestone 09 can wake an idle scheduler safely.

## Settled interfaces

Normative text is D048 through D051. This section names the concrete shapes implementers must build to.

Source and AST. A receive may end with at most one `after duration => body` clause, placed after every pattern clause. The duration evaluates to nanoseconds in 0 through `NvMaxduration` (10^18, about 31.7 years) or to the atom `'infinity`, which blocks without arming a deadline. The lexer gains `Tafter`; `Ereceive` gains a duration expression and a timeout body; the canonical formatter emits the clause last, and the existing frontend harness already checks idempotence.

Bytecode. Two new instructions, emitted in this order:

```text
arm:     recvdeadline dur        ; evaluate duration once, arm absolute deadline
begin:   recvbegin cand, found
check:   testatom found 'true -> wait
         <clause tests -> next clause>
         recvtake
         <clause body>
         jump end
advance: recvnext cand, found
         jump check
wait:    recvwaitdeadline begin  ; blocks and resumes at begin; falls through when expired
         <after body>
end:
```

`recvdeadline` sits above the retry label, so the D044 retry target cannot re-arm the deadline: it is computed once and survives every rescan. Expiry is checked at `wait`, after a scan has failed, which is what makes D048's message-first guarantee structural rather than a timing rule.

Verifier. `recvwaitdeadline` has two successors, unlike terminal `recvwait`: its fallthrough edge joins control-flow and definite-initialization analysis, the reachable-fallthrough-at-end rule applies, and the timeout body is thereby proved to read no receive-clause binding. Operand checks mirror `recvwait` for the retry target and `Oexit` for the duration register.

Runtime. `NvProcess` gains an armed absolute deadline and its valid flag; there is no timer object, queue, token, or identity. `NvLimits` gains the accepted duration bound. The scheduler holds an installed clock (`uptime` in production, an injected counter in tests) and, when no process is runnable, advances to the earliest armed deadline or reports the existing D046 deadlock.

Failure and cleanup. A duration outside D049's domain faults `bad_timeout`; a literal out-of-range duration is a compile error with source position. Arming allocates nothing, so no new allocation-failure path exists; cancellation and teardown ride the existing exit and runtime-free paths.

Only now should the coordinator create disjoint parser/compiler, scheduler/timer, and test tasks. Public headers and shared build files remain coordinator integration surfaces.

## Required tests

All timing-sensitive tests use the injected clock and must not depend on host sleep duration.

- Immediate match does not wait or leave a timer.
- Zero timeout with and without a matching queued message.
- Unmatched messages survive timeout in original order.
- Repeated wakeups and rescans do not extend a deadline.
- Timer cancellation after a message match, process exit, fault, and scheduler teardown.
- A cancelled/stale timer cannot wake a reused process slot or fire twice.
- Message-versus-deadline boundary follows D048: a timer fires while a matching message is queued, and the message is still selected rather than the timeout branch.
- A duration that is negative, non-integer, or above the accepted bound faults `bad_timeout`, and an out-of-range literal fails to compile.
- An `'infinity` duration blocks without arming a deadline, never runs the timeout body, and is still observed by D046 deadlock reporting.
- With nothing runnable and a deadline armed, the production scheduler waits rather than polling the clock, while the injected clock advances instantly.
- Many timers expire in deterministic deadline order where deadlines differ and in the settled tie order where equal.
- Invalid duration, overflow, malformed timeout bytecode, and allocation failures are controlled and ownership-clean.
- Existing no-timeout receive behavior and the complete milestone 05 aggregate suite remain unchanged.

## Exit criterion

The M06-T01 decisions and interfaces are recorded before implementation. Then waiting processes wake for messages or deadlines exactly once, with no stale timer firing after successful receive, exit, fault, slot reuse, or teardown. All required deterministic-clock tests and the complete aggregate suite pass. R2 becomes the active mandatory gate.

## Not in scope

Multicore wakeup, distributed time, timer migration, or timer services independent of receive.
