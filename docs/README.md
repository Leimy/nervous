# Documentation Index

What each file in `docs/` is for, and whether it is normative. The root `README.md` gives the reading order for new contributors; `STATUS.md` gives the current state.

## Quick Start for New Contributors

1. Read `../README.md` for the project overview
2. Read `../STATUS.md` for the current state (especially the "Current pause" section)
3. Read this file (`docs/README.md`) for the documentation structure
4. Read `semantics.md` for the language contract
5. Read `architecture.md` for high-level design
6. Read `bytecode.md` for the VM instruction set

That's enough to start contributing. Only dig into milestones/decisions as needed.

## Normative (current state)

When these conflict with anything else, these win. Among them, `decisions.md` is the tie-breaker.

| File | Purpose | When to Read |
|------|---------|--------------|
| `semantics.md` | The language contract: values, scope, matching, guards, processes, I/O, failure | Always first for semantics |
| `decisions.md` | D001-D090: settled design decisions | For implementation context |
| `bytecode.md` | Instruction set, verification, VM semantics | When touching the VM |
| `format.md` | Canonical formatting contract | When editing the formatter |

## Overview (non-normative summaries)

| File | Purpose | When to Read |
|------|---------|--------------|
| `architecture.md` | High-level component overview | To understand how pieces fit |
| `sharding.md` | Current T04a lock protection map | For multicore implementation |

## Design rationale (historical, not normative)

These files are the deprecated `../nervous_design.md`, split by part for readability. They explain *why* the language/runtime are shaped as they are. They keep the original section numbers, and their examples still use syntax that has since been superseded. Each file begins with a note listing its known differences from the implementation. Read for motivation, not rules.

| File | Original sections | Topic | When to Read |
|------|-------------------|-------|--------------|
| `language-semantics.md` | 1-22 | Terms, matching, maps and "Decoding Is Not Dispatch", binaries, clauses | For matching semantics |
| `runtime.md` | 32-39 | Multicore scheduling, synchronization, GC, message memory, I/O | For runtime overview |
| `distribution.md` | 40-41 | Wire encoding and network migration (not implemented) | For distribution design |
| `language-philosophy.md` | 42-45 | Static analysis, parsing, first milestone, guiding principles | For project philosophy |
| `future-work.md` | 46-47 | Open questions, annotated with status | For long-term plans |

Sections 23-31 (the abstract machine) were not split out. `bytecode.md` supersedes them.

## Working records (active development)

| File | Purpose | When to Read |
|------|---------|--------------|
| `questions.md` | Open questions, by owning milestone | When a question is active |
| `review-findings.md` | Adversarial-review findings for open gates | Before a review gate closes |
| `review-findings-archive.md` | Findings from closed gates | For regression investigation |
| `review-05.md` | Completed milestone 05 review record | For M05 history |

## Current stopping point

Start with `../RESUME.md` and `../bench/T02-results.md` for the paused M10
checkpoint-779643. T02 is functionally accepted; controlled performance
follow-up and R4 remain open. `sharding.md` is the current protection map.
Do not reload historical review/design records simply to resume.

## Reading Guide (streamlined)

- **New contributor:** `README.md` → `STATUS.md` → this file → `semantics.md` → `architecture.md` → `bytecode.md`
- **Frontend work:** `semantics.md` → `format.md` → D058 in `decisions.md`
- **Runtime/GC work:** `architecture.md` → D059-D079 in `decisions.md`
- **Scheduler/multi-proc work:** `architecture.md` "Scheduler" → D070, D074, D081-D090 → `sharding.md`
- **Reviewer:** `decisions.md` → `questions.md` → active milestone → `review-findings.md`
- **Wondering why:** the design-rationale files above, read with their header notes in mind. Check anything against `decisions.md`.
