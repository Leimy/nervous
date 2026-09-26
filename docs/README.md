# Documentation Index

This directory contains Nervous's normative documentation. The long-form `nervous_design.md` is deprecated; read these files instead.

## Core Documentation

| File | Purpose | Read First |
|------|---------|------------|
| `architecture.md` | High-level architecture, design principles, settled decisions summary | All readers |
| `language-semantics.md` | Language contract (terms, patterns, clauses, guards, processes) | Before touching code |
| `runtime.md` | 9front runtime strategy (multicore, GC, I/O, message memory) | Before touching runtime |
| `distribution.md` | Wire encoding and protocol evolution | When reading/writing distribution code |
| `language-philosophy.md` | Parsing, static analysis, first implementation milestone | Before adding new language features |
| `future-work.md` | Open questions and consolidated settled decisions (D001-D079) | When wondering "why?" |
| `decisions.md` | Compact decision records (D001-D079) | When checking decision status |

## Supporting Documentation

| File | Purpose |
|------|---------|
| `questions.md` | Unresolved semantic questions by owning milestone |
| `bytecode.md` | Instruction set, verification rules, VM semantics |
| `format.md` | Canonical formatting contract and current limitations |
| `review-findings.md` | Persistent adversarial-review findings for open gates |
| `review-findings-archive.md` | Findings from permanently closed gates |
| `review-05.md` | Completed milestone 05 adversarial-review record |

## Deprecated Files

- `nervous_design.md` — Historical reference only; superseded by the compact sources above.

## Reading Guide

### For newcomers

1. `README.md` (project root) — project map and milestone order
2. `STATUS.md` (project root) — current state and task assignments
3. `docs/architecture.md` — high-level system overview
4. `docs/language-semantics.md` — language contract
5. `docs/runtime.md` — runtime strategy

### For developers

1. `docs/architecture.md` — design principles and component roles
2. `docs/language-semantics.md` — exact matching, guards, clauses
3. `docs/bytecode.md` — instruction set and verification
4. `docs/runtime.md` — scheduler, GC, message passing
5. The active milestone file in `milestones/`

### For reviewers

1. `docs/decisions.md` — settled decisions (D001-D079)
2. `docs/questions.md` — questions raised for that milestone
3. `milestones/XX.md` — milestone scope and acceptance criteria

### When you need the "why"

Start with `docs/architecture.md`'s "Design Principles" section. For deeper rationale, read `future-work.md` (open questions) or `nervous_design.md` (deprecated long-form rationale).

### When checking a decision

Read `docs/decisions.md` for the compact record. For full historical context, see `nervous_design.md` (deprecated) or `future-work.md`'s "Decisions Now Considered Settled" section.
