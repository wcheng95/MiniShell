# Codex task mailbox

This directory is the durable supervisor <-> Codex engineering handoff for MiniShell.

Roles are defined in repository-root `AGENTS.md`.

## Active task

```text
none
```

Accepted cleanup now includes T014/F10+F13 canonical documentation reconciliation: ADV runtime paths/status, current public services, MiniFT8 module/logging/live-RX ownership, Keyer sidetone status, and Memory snapshot behavior now match current production source and accepted evidence.

Workflow remains: one temporary task branch -> local build/tests -> supervisor diff review -> merge/fast-forward to `main` -> delete the temporary branch.

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
- local tests run and results
- validation still required
- limitations/risks
- commit SHA

Supervisor then reviews the actual `main..<commit>` diff, not only the implementation note.

Architect/tester supplies real-system results. The supervisor records accepted evidence and updates current-state documentation after acceptance.

## Branching / handoff

Preferred:

```text
branch: codex/T###-short-name
handoff: one pushed commit SHA
```

Do not open a PR unless a task explicitly requests one. Run the required build/tests locally before handoff. Keep unrelated cleanup out of the task branch.

## Template

Copy `TASK_TEMPLATE.md` for a new task.
