# Codex task mailbox

This directory is the durable supervisor <-> Codex engineering handoff for MiniShell.

Roles are defined in repository-root `AGENTS.md`.

## Active task

```text
T017-adv-usb-uac-rx.md   READY
```

T017 brings the pinned MiniFT8-V2 QMX USB-host UAC mechanics into the ADV backend while preserving MiniShell ownership: native 48k/S24 stereo becomes canonical 12k/S16 stereo below the Audio API, continuous capture survives synchronous decode, bare ADV ft8 defaults to uac:qmx, and the hardware gate is live decoded RX messages across consecutive slots. CDC-ACM is included as a companion if current IDF/component APIs remain compatible, but no CAT policy commands are sent in this task.

Workflow: lazy ring reached Audio.open but 64 KiB allocation exceeded 39,936-byte largest block -> reduce ADV ring to 2048 frames (~8 KiB) -> re-run gates -> supervisor re-review -> resume QMX hardware validation -> measure high-water -> acceptance/merge -> delete T017 branch.

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
