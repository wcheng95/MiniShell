# Codex task mailbox

This directory is the durable supervisor <-> Codex engineering handoff for MiniShell.

Roles are defined in repository-root `AGENTS.md`.

## Active task

None.

Recently completed:

```text
T025-shell-aliases.md            COMPLETE
T026-rr73-signoff.md             COMPLETE — temporary compatibility workaround
```

Earlier consolidated MiniFT8 tasks:

```text
T022-linux-qmx-first-qso.md      COMPLETE
T023-cq-beacon-ui.md             COMPLETE
T024-offset-source-random.md     COMPLETE
```

Current baseline is on `main`. T025 provides resident aliases from
`/flash/minishell/alias.txt`. T026 is explicitly accepted as a temporary
pinned-V2 keyword-before-grid workaround for exact `RR73`; permanent
RR73-vs-locator disambiguation is deferred.

Workflow remains: one temporary task branch -> local build/tests -> supervisor
diff review -> real-system validation when required -> fast-forward `main` ->
delete the temporary branch.

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
