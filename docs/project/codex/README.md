# Codex task mailbox

This directory is the durable supervisor <-> Codex engineering handoff for MiniShell.

Roles are defined in repository-root `AGENTS.md`.

## Active task

```text
T038-keyer-k6-field-ui.md      READY
```

T038 advances the external ADV Keyer from the working K5 engine/GPIO/sidetone
baseline to the K6 field application: dedicated 20x7 UI, UTC status row,
keyboard/message CW TX, M1-M5, corrected SKS/SKM/OFF KeyOut modes, Opt-based
three-page Operation UI, shortcuts, Tune, volume/mute and immediate persistent
settings. The proven K3/K4/K5 timing and service boundaries remain protected.

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
