# Multi-Agent Coordination

This protocol allows agents from different vendors or sessions to work on Nervous without sharing conversation context.

`STATUS.md` is the operational source of truth. `README.md`, `docs/`, and `milestones/` define project intent and acceptance criteria.

## Roles

### Coordinator

There is exactly one active coordinator at a time. The coordinator:

- assigns work and file ownership in `STATUS.md`;
- owns shared integration files;
- resolves semantic and interface conflicts;
- reviews actual filesystem changes rather than trusting summaries;
- runs the final milestone build and acceptance checks;
- updates milestone state after integration.

### Worker

A worker receives one bounded task and an exclusive write set. A worker:

- reads only the context named in the assignment;
- edits only assigned files;
- does not expand scope without coordinator approval;
- builds affected source with `mk` after editing;
- returns the handoff report described below.

### Reviewer

A reviewer may inspect any relevant file but does not edit unless given a separate exclusive write assignment. Review can run in parallel with implementation if it examines a stable snapshot or clearly identifies the revision/state reviewed.

## Repository ownership rules

Only the coordinator edits these integration surfaces unless an assignment explicitly transfers ownership:

- `README.md`;
- `STATUS.md`;
- `COORDINATION.md`;
- root and shared `mkfile` files;
- `docs/decisions.md`;
- `docs/questions.md`;
- shared public headers;
- milestone acceptance state.

Workers may propose changes to these files in their handoff, but must not silently make them.

An assignment lists exact writable files or directories. Ownership is exclusive until the task is marked handed off, cancelled, or released. Directory ownership includes new files below that directory but not files outside it.

Never assign overlapping write sets concurrently. Different path spellings, binds, or relative paths do not make concurrent access safe; use canonical absolute paths in assignments.

Read-only investigation may overlap any work, but findings can become stale. Review the current filesystem before applying them.

## Task lifecycle

A task has one of these states:

```text
planned
assigned
active
blocked
handed-off
integrating
done
cancelled
```

The coordinator creates a task row in `STATUS.md` before editing begins and records:

- task ID;
- milestone;
- role and assignee label;
- state;
- dependency task IDs;
- exclusive write set;
- concise objective.

The assignee changes no status directly unless assigned ownership of `STATUS.md`; it reports state changes to the coordinator.

## Dependency and parallelism policy

Milestones are integrated in the order listed in `README.md`. Work from a later milestone may begin early only when:

- its interfaces are already settled;
- its write set does not overlap active work;
- it does not force an unaccepted earlier design decision;
- the coordinator marks it explicitly as speculative or dependency-safe.

Safe parallel work usually includes:

- read-only research or review;
- disjoint test fixtures;
- disjoint implementation modules behind a settled interface;
- documentation local to one milestone;
- independent negative and positive test sets.

Unsafe parallel work includes:

- two agents editing one file;
- concurrent edits to shared headers or build files;
- implementation against an interface another agent is still designing;
- workers independently resolving the same semantic question;
- integrating later milestones before prerequisite acceptance.

## Assignment template

The coordinator should give each worker a prompt containing:

```text
Project: /usr/dave/work/nervous
Task: <task ID and objective>
Milestone: <number and name>

Read:
- README.md
- COORDINATION.md
- <milestone document>
- <specific compact docs or source files>

Do not read nervous_design.md unless a named question cannot be answered
from the compact documents. If you read it, report the relevant section.

Exclusive write set:
- <canonical absolute paths or directories>

Do not edit:
- STATUS.md
- shared mkfiles, headers, or decision documents unless listed above
- any path outside the write set

Dependencies/assumptions:
- <settled interfaces and completed task IDs>

Acceptance checks:
- <specific tests, diagnostics, or artifact requirements>
- run mk in <directory> after source edits

Return the standard handoff report. Stop and report a blocker rather than
expanding scope or making an unassigned semantic decision.
```

## Worker handoff report

Every handoff must contain:

```text
Task:
State: handed-off | blocked
Summary:
Files created:
Files modified:
Files deleted:
Build/test checks and exact results:
Acceptance criteria satisfied:
Semantic or interface decisions proposed:
Known defects or follow-up work:
Assumptions made:
```

The report is evidence, not acceptance. The coordinator verifies files and reruns the appropriate build before marking the task done.

## Semantic decisions

A worker encountering an unresolved semantic issue must:

1. check `docs/questions.md` for the owning milestone;
2. avoid embedding an arbitrary choice in a public interface;
3. report options, consequences, and a recommendation;
4. wait for the coordinator when the task cannot proceed behind a private placeholder.

After resolution, the coordinator records the answer in `docs/decisions.md`, removes or updates the question, and updates affected milestone documents if necessary.

## Integration procedure

For each handed-off task, the coordinator:

1. confirms the worker stayed within its write set;
2. reads every changed source and interface file;
3. checks assumptions against current files;
4. resolves proposed decisions centrally;
5. runs `mk` in the affected project directory;
6. runs or requests any manual runtime checks not expressible as a build;
7. marks the task `done` or returns it with a focused correction request;
8. releases the write set.

A milestone is complete only when its document's exit criterion is met. Completion is not inferred from all task rows being done.

Review milestones (`R1`, `R2`, and later review gates) are mandatory dependency gates, not optional retrospectives. Forward feature work stops when a review gate becomes active. A review finding must be fixed, disproved with evidence, or deferred to a named milestone with severity and owner. High-severity ownership, verifier-boundary, memory-safety, duplicate-execution, or lost-wakeup findings may not be deferred past the gate.

## Conflict recovery

If overlapping edits occur:

- stop both assignments;
- preserve and inspect the current file before further writes;
- choose one known-good base;
- reapply changes serially under coordinator ownership;
- rebuild and review the whole affected interface;
- record the ownership failure in the task notes so it is not repeated.

Do not ask a second agent to blindly merge two complete file rewrites.

## Context economy

Agents should not load the long design document by default. The normal context packet is:

1. `README.md`;
2. `COORDINATION.md`;
3. the assigned milestone file;
4. referenced sections of `docs/semantics.md`, `docs/decisions.md`, and `docs/questions.md`;
5. only the source files in or immediately adjacent to the assignment.

When compact documents conflict with `nervous_design.md`, stop and ask the coordinator. Do not guess which is newer.
