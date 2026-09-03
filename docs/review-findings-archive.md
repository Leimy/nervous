# Closed Review Findings Archive

This file holds findings from review gates that are fully closed and will not
reopen. Nothing here is deleted, only relocated out of `review-findings.md` to
keep that ledger short for agents doing ongoing work. States and severity
rules are defined in `review-findings.md` and `COORDINATION.md`. Do not copy
entries back into `review-findings.md`; if a genuinely new defect touches the
same area, give it a fresh ID in the ledger that owns the gate that is
currently active.

## R1 - Foundation consolidation (gate complete; `R1` milestone state is `complete` in STATUS.md)

| ID | Severity | State | Owner | Finding | Required evidence |
|---|---|---|---|---|---|
| R1-F01 | high | closed | R1 coordinator | Instruction dispatch was duplicated in `lib/vm.c` and `lib/exec.c`, allowing opcode semantic drift | `lib/exec.c` is now the sole dispatch; `nvexecute` delegates; aggregate VM and process suites pass |
| R1-F02 | high | closed | R1 coordinator | Compiler and pattern-lowering partial-allocation/error paths lacked a dedicated ownership audit | Audit fixed partial-function cleanup, ignored emission failure, match error leaks, transactional transfer, diagnostics, and tuple cleanup ordering; deterministic allocation-failure sweep and aggregate suite pass |
| R1-F03 | medium | closed | R1 coordinator | Accepted suites required several manual commands, allowing old milestones to regress unnoticed | `mk tests` builds all artifacts; `rc tests/run.rc` passes frontend, bytecode, VM, pattern/compiler, process, and resumable execution suites |
| R1-F04 | medium | closed | R1 coordinator | Compiler validation was incomplete for duplicate functions, unresolved calls, register pressure, unsupported constructs, and malformed generated control flow | Duplicate/unresolved, register-pressure, and unsupported-construct tests pass; `nvcompile` verifies every generated module before returning it |
| R1-F05 | medium | closed | R1 coordinator | `nervous_design.md` contains superseded syntax/function statements that can mislead contributors | Prominent historical/superseded warning links to compact normative documents and states that compact sources win |
| R1-F06 | medium | closed | R1 coordinator | Mailbox byte accounting estimates host allocation rather than a final fragment/heap representation | D040 documents the provisional estimate and assigns replacement to R3; boundary, subtraction-underflow, and malformed-value tests pass in the aggregate suite |
| R1-F07 | low | closed | M05/R2 coordinator | Source compile/execute lacked CLI integration and scheduler lifecycle reporting | D046 defines root outcome, fault, exit, and deadlock behavior; `nervous -r` compiles and schedules source; aggregate arithmetic and concurrent ping/pong command checks pass |
| R1-F08 | low | closed | R1 coordinator | `nvvm.h` use depended implicitly on `NvModule` being declared first | `value.c` and mk dependencies now include `nvbc.h` explicitly; aggregate compilation succeeds |
| R1-F09 | high | closed | R1 coordinator | Compiled patterns treated variables already bound in the lexical environment as duplicate-binding errors rather than equality tests | Pattern lowering accepts known bindings and emits `testeq`; focused test and aggregate compiler suite pass |
| R1-F10 | medium | closed | R1 coordinator | A zero-argument current-syntax function body whose first expression is a tuple was misclassified as clause-form | Parser now distinguishes tuple-leading bodies from `${...} =>` after parsing the tuple; body and clause regressions pass |
| R1-F11 | high | closed | M05 coordinator | Signed overflow in `tuple` operand-window verification could admit out-of-bounds verifier and executor access | Overflow-free subtraction check added; aggregate suite rejects both out-of-range and signed-overflow fixtures |
| R1-F12 | low | closed | M05 coordinator | D007's original overflow reason contradicted D029 and the implementation | D007 now explicitly defers reason representation to D029's `overflow` reason |
| R1-F13 | low | closed | M05 coordinator | Definite-initialization dataflow silently omitted control-flow edges to pc 0, relying on an unstated monotonic-entry invariant | The pc-0 special case was removed so backedges participate in the normal merge/fixpoint; loop-entry and aggregate verifier suites pass |
| R1-F14 | low | closed | M05 coordinator | A clause-form parse failure at the closing brace leaked the partial clause list | Parser now walks and frees the owned partial clause list before freeing the function; parse-negative and aggregate suites pass (closure is code/ownership review, not external leak instrumentation) |
