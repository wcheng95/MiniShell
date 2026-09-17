# Codex task mailbox

This directory is the durable supervisor <-> Codex engineering handoff for MiniShell.

Roles are defined in repository-root `AGENTS.md`.

## Active task

```text
T001-architecture-boundary-audit.md   READY
```

No feature implementation should proceed until T001 is reviewed by the supervisor and the architect decides how to handle any findings.

## Rule

One task = one file:

```text
T###-short-name.md
```

The task file is the authoritative implementation prompt. Chat messages may discuss or refine the work, but Codex should implement only the repository task selected by the architect/coordinator.

## Status values

```text
DRAFT         supervisor is still shaping the task
READY         architect-approved / ready for Codex
IMPLEMENTING  Codex is working
REVIEW        implementation is ready for supervisor review
TESTING       supervisor review passed; architect is validating
COMPLETE      accepted
BLOCKED       needs architecture/input/evidence before continuing
```

## Handoff protocol

Supervisor writes:

- objective
- current context
- source of truth/reference behavior
- architectural constraints
- implementation scope
- explicit non-goals
- acceptance criteria
- required automated tests
- required manual/hardware tests

Codex returns:

- implementation summary
- exact files changed
- notable design/implementation choices
- tests run and results
- validation still required
- limitations/risks
- commit SHA / PR

Supervisor then reviews the actual diff/PR, not only the implementation note.

Architect/tester supplies real-system results. The supervisor records accepted evidence and updates current-state documentation after acceptance.

## Branching

Preferred:

```text
branch: codex/T###-short-name
PR:     T###: short task name
```

Keep unrelated cleanup out of the task branch.

## Template

Copy `TASK_TEMPLATE.md` for a new task.
