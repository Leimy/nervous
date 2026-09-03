# Symbolic Bytecode v0

This is a canonical fixture and diagnostic format, not the final portable encoding.

## Module form

```text
module
const int 42
const atom ok
const func add
func add 4
0: loadk 1 0
1: return 1
end
```

Constants and functions are numbered in declaration order. Instruction labels are canonical decimal indices and must be contiguous from zero.

## Instructions

```text
loadk dst const
move dst src
tuple dst first count
jump target
call dst function-const args
tailcall function-const args
return src
testatom src atom-const fail-target
testint src int-const fail-target
testeq left right fail-target
testarity src arity fail-target
getelem dst tuple index
add dst left right
sub dst left right
mul dst left right
div dst left right
rem dst left right
lt dst left right
le dst left right
gt dst left right
ge dst left right
self dst
makeref dst
send dst pid message
spawn dst function-const args
recvbegin value-dst found-dst
recvnext value-dst found-dst
recvtake
recvwait retry-target
exit reason
recvdeadline duration
recvwaitdeadline retry-target
print dst value
eprint dst value
guard fail-target
guardend
istype dst src kind
fail reason-const
nop
```

`tuple` reads `count` consecutive registers beginning at `first`, in ascending order. `call`, `tailcall`, and `spawn` consume one argument-tuple register. `tailcall` replaces the current frame with the callee's (same caller, same result destination) and does not count against the frame limit; the compiler emits it for every source call in tail position (`semantics.md`, "Functions", and D047), so code following it within the same function is unreachable and is not emitted. `spawn` names a statically resolved function constant and writes its new PID. `self` writes the current PID, `makeref` writes a fresh Ref, and `send` asynchronously copies `message` to `pid` and writes the sent value.

`recvbegin` starts at the oldest mailbox message; `recvnext` advances past the current candidate. Both write a copied candidate (or atom `'undefined`) and a `'true`/`'false` found flag, so both destinations are initialized on every path. `recvtake` consumes the current candidate after compiled pattern tests succeed. `recvwait` requires an exhausted scan, changes the process to waiting, and resumes at `retry-target` after a send wakes it. Candidate pattern registers are tentative and are used only on their clause success edge. `exit` terminates the process with the arbitrary term in `reason`; it has no fallthrough.

`recvdeadline` evaluates `duration` once and arms one absolute deadline (D048-D051); `'infinity` arms nothing. `recvwaitdeadline` blocks and resumes at `retry-target` like `recvwait` when no deadline is armed or it has not expired, but falls through to the next instruction, unlike `recvwait`, when the armed deadline has already expired; that fallthrough begins the receive's timeout body.

`guard` enters guard mode (D060): until the matching `guardend`, any fault the executing instruction would raise instead transfers control to `fail-target` and leaves guard mode; `guardend` leaves it normally. Guard mode is one field of the execution state, not a stack, and a quantum boundary inside a guard preserves it. `istype` writes `'true` to `dst` if `src` holds a term of `kind` (an immediate: 0 integer, 1 atom, 2 tuple, 3 pid, 4 ref, the `NvValue` kind numbering) and `'false` otherwise; it never faults.

`print` and `eprint` write `value` to host stdout and stderr respectively, rendered by the same term formatting used elsewhere (D053-D057), and write the atom `'ok` to `dst` on success; a write failure faults the process with `io_error` rather than producing a result.

Process and I/O instructions fault with `bad_process_context` outside a scheduler host (or any host that does not install the corresponding callback). Arithmetic operands must be integers and use checked 64-bit semantics. Ordering is defined for integers and writes atom `'true` or `'false`. All targets are absolute instruction indices in the current function.

## Verification

The verifier checks limits, opcode and operand kinds, register and constant ranges, constant types, control-flow targets, function-name constants, reachable fallthrough, and definite register initialization across control-flow joins.

`loadk`, `move`, `tuple`, `call`, `getelem`, arithmetic, ordering, `self`, `makeref`, `send`, `spawn`, `print`, `eprint`, and `istype` initialize their destination. `guard` has two successors, its fallthrough and its fail target; one edge from the `guard` itself is sufficient for definite initialization because registers are only ever set, so the set live at the `guard` is a subset of the set live at any instruction in the guarded region. `recvbegin` and `recvnext` initialize both distinct destination registers. `send` reads its PID and message registers; `spawn` reads its argument-tuple register; `exit` reads its reason register; `print` and `eprint` read their value register. Tests read their inputs and branch to the next instruction on success or the explicit failure target on failure. `recvwait` transfers to its retry target after wakeup; `recvwaitdeadline` has two successors, its retry target and its ordinary fallthrough into the timeout body, and both are checked. `jump`, `recvwait`, `return`, `tailcall`, `exit`, and `fail` have no fallthrough. `istype`'s kind operand must name a real term kind.

Function entry initializes X0 with the semantic argument tuple; all other X registers begin uninitialized. Calls and tail calls place their argument-tuple register in the callee's X0. All initialized X registers are conservatively roots at call and allocation safe points.
