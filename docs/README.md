# Documentation Index

What each file in `docs/` is for, and whether it is normative. The root `README.md` gives the reading order for new contributors; `STATUS.md` gives the current state.

## Normative

When these conflict with anything else, these win. Among them, `decisions.md` is the tie-breaker.

| File | Purpose |
|------|---------|
| `semantics.md` | The language contract as implemented: values, scope, matching, guards, functions, expressions, processes, host output, and failure |
| `decisions.md` | Numbered decision records D001-D079. A later record may supersede an earlier one; each says so. |
| `bytecode.md` | Instruction set, verification rules, and VM semantics |
| `format.md` | Canonical formatting contract and current limitations |

## Overview

| File | Purpose |
|------|---------|
| `architecture.md` | How the components fit together, with pointers to the source and the decisions. It summarizes and does not decide. |

## Design rationale (not normative)

These files are the deprecated `../nervous_design.md`, split by part for readability. They explain why the language and runtime are shaped as they are. They keep the original section numbers, and their examples still use syntax that has since been superseded. For example, they write `{a, b}` for tuples, `pid <- m` for send, clause-form `fn f { (x) => ... }`, and `if` for guards. Each file begins with a note listing its known differences from the implementation.

| File | Original sections | Topic |
|------|-------------------|-------|
| `language-semantics.md` | 1-22 | Terms, matching, maps and "Decoding Is Not Dispatch", binaries, clauses, processes |
| `runtime.md` | 32-39 | Multicore scheduling, synchronization, GC, message memory, I/O |
| `distribution.md` | 40-41 | Wire encoding and network migration (not implemented) |
| `language-philosophy.md` | 42-45, closing "Central Idea" | Static analysis, parsing, first milestone, guiding principles, central idea |
| `future-work.md` | 46-47 | The original settled-decision list and open questions, annotated with what has since been decided |

Sections 23-31 (the abstract machine) were not split out. `bytecode.md` supersedes them, and they remain readable in `../nervous_design.md`.

## Working records

| File | Purpose |
|------|---------|
| `questions.md` | Open questions, by owning milestone |
| `review-findings.md` | Adversarial-review findings for gates that are still open |
| `review-findings-archive.md` | Findings from closed gates. Read only when investigating a regression. |
| `review-05.md` | The completed milestone 05 review record (historical) |

## Reading Guide

- **New contributor:** start with the root `README.md` and `STATUS.md`, then `architecture.md`, `semantics.md`, and `bytecode.md`. Then read the active milestone file in `../milestones/`.
- **Touching the frontend:** `semantics.md`, `format.md`, and D058 (current surface syntax) in `decisions.md`.
- **Touching the runtime or GC:** `architecture.md`, then D059-D079 in `decisions.md`.
- **Reviewer:** `decisions.md`, `questions.md`, the milestone file, and `review-findings.md`.
- **Wondering why:** the design-rationale files above, read with their header notes in mind. Check anything you intend to rely on against `decisions.md`.
