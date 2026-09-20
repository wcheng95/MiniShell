# Codex task mailbox

This directory is the durable supervisor <-> Codex engineering handoff for MiniShell.

Roles are defined in repository-root `AGENTS.md`.

## Active task

T030-adv-qmx-cat.md              TESTING

T030 brings QMX CDC CAT to Cardputer ADV using the existing V2-proven composite
USB-host path. Scope is deliberately narrow: no portable MiniFT8 changes, no
public API changes, QMX UAC-IN + CDC only, and no USB Audio OUT/QDX work.
Audio-RX stop/start becomes a logical pause/resume while CDC stays alive for CAT
TX. Final hardware acceptance is the first real ADV/QMX QSO. R1 `51e45d63` passed supervisor re-review; hardware H1-H7 is now active. First real ADV/QMX MiniFT8-V3 QSO completed with VA7NRC on 20 m; final cleanup/lifecycle checks remain before COMPLETE. Supervisor review of implementation `1837f476` passed except R1 disconnected-start/late first-attach preservation, which must be fixed before hardware validation.

Recently completed:

```text
T027-nonstandard-tx.md           COMPLETE — V2-compatible hash/type-4 TX
T028-rx-message-order.md         COMPLETE — controller-owned RX priority/SNR order
T029-rx-display-lifetime.md      COMPLETE — RX rows persist through TX, clear at TX end
```

T029 live validation passed. Previous sorted RX rows remain visible during TX,
clear on successful TX completion/RX resume, and are replaced normally by the
next completed RX batch. Ordinary RX transport resets do not prematurely clear
the current displayed snapshot.

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
