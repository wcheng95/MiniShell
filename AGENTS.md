# MiniShell engineering roles

This repository uses a three-role engineering workflow.

## Roles

### Architect / tester / coordinator — Wei

Owns:

- product intent and architecture decisions
- hardware choices and constraints
- acceptance of behavior changes
- hands-on Linux/ADV/QMX testing
- coordination of when Codex is asked to implement a task
- final merge/release decisions

The architect may change or override any task requirement.

### Supervisor — ChatGPT

Owns:

- translating architecture decisions into bounded engineering tasks
- checking current repository state and relevant reference implementations
- defining invariants, non-goals, acceptance criteria, and tests
- preserving MiniShell ownership boundaries
- reviewing Codex commits/diffs against the task and architecture
- diagnosing test failures with the architect
- updating canonical documentation after accepted changes

The supervisor should not silently redesign an architect-approved boundary merely to make implementation easier.

### Engineer — Codex

Owns:

- implementing the active task exactly as specified
- keeping changes minimal and local to the task
- adding/updating tests required by the task
- running the available build/test suite before handoff
- documenting implementation details, local test results, and remaining risks in the task file

Codex does **not** own product architecture. If a task appears to require an architectural change, stop and report the conflict instead of inventing a new boundary.

## Shared communication channel

Engineering handoff between supervisor and Codex is repository-based, not chat-memory-based.

Task packets live under:

```text
docs/project/codex/
```

Use one file per task:

```text
docs/project/codex/T###-short-name.md
```

The architect/coordinator tells Codex which task file to execute. Codex must read that task file before coding.

Codex records implementation notes in the same task file. The supervisor reviews the resulting commit diff from GitHub. Pull requests are optional and are not part of the normal cleanup loop.

## Task lifecycle

```text
DRAFT
  -> READY
  -> IMPLEMENTING
  -> REVIEW
  -> TESTING
  -> COMPLETE
```

`BLOCKED` may be used from any active state.

Typical flow:

1. Architect makes/approves an architectural decision.
2. Supervisor creates a `READY` task packet.
3. Architect/coordinator asks Codex to implement that task.
4. Codex works on a dedicated branch, builds and tests locally, records the exact commands/results, and pushes one reviewable commit.
5. Supervisor reviews the diff from current `main` to that commit against the task and repository architecture.
6. If the diff is clean and the required local tests passed, the supervisor fast-forwards `main` to the reviewed commit. No PR is required.
7. Architect tests on real hardware/host when the task requires hardware evidence.
8. Supervisor records accepted evidence and updates canonical docs.
9. Stop and return to the architect only when an architectural decision, behavior choice, or hardware result is required.

## Branch / commit convention

Preferred Codex branch:

```text
codex/T###-short-name
```

Normal handoff is a pushed commit SHA, not a PR. Keep one bounded task in the branch and do not mix unrelated cleanup into the reviewed commit.

GitHub Actions may run after pushes, but they are asynchronous smoke evidence. For the normal cleanup loop, **local build/test results plus supervisor diff review are the gate**; do not wait for GitHub-hosted CI unless a task explicitly requires it.

## Implementation rules

- Read `docs/README.md` and the task packet before editing.
- Treat `include/minishell/api.h` as the public API source of truth.
- Application code must use MiniShell APIs and must not reach directly into Linux, POSIX, ALSA, ESP-IDF, board, USB, FATFS, or GPIO implementation layers unless the task is explicitly a platform/backend task.
- Preserve established ownership boundaries. `app_controller` remains the application coordinator where documented.
- Pure domain modules must remain platform-independent.
- Do not add heap allocation to modules whose docs/tests define a no-heap rule.
- Do not change persisted configuration formats, public APIs, file formats, or UI contracts unless the task explicitly authorizes it.
- Do not weaken tests to make a product bug pass. If a test encodes a stale assumption, explain why and update it narrowly.
- Prefer measured evidence over speculative tuning.
- Keep comments focused on invariants and non-obvious reasons, not narration of obvious code.

## MiniFT8 reference rule

For MiniFT8 behavior that is meant to preserve V2 semantics, use the pinned reference unless the task states a deliberate V3 difference:

```text
repository: wcheng95/Mini-FT8
commit:     491e757ae6b1e4cfd2b9a6ba10f48b35643849e0
```

Do not reproduce V2 platform coupling. Preserve behavior while adapting platform access through MiniShell APIs and V3 ownership boundaries.

## Required Codex handoff notes

Every implementation handoff must state:

```text
Implementation summary
Files changed
Behavior/invariants preserved
Tests run and results
Hardware/manual validation still required
Known limitations or risks
Commit reference
```

If implementation diverged from the task, call that out explicitly and explain why. Do not hide deviations inside code.

## Current canonical project state

Use these before relying on older stage documents:

```text
README.md
docs/README.md
docs/project/progress.md
docs/MiniFT8/README.md
docs/MiniFT8/development.md
docs/MiniFT8/ui.md
```

Detailed `rx-*` and `as-*` documents are design/regression history and remain useful, but current-state documents take precedence when status text conflicts.


## MiniFT8 About version rule

The MiniFT8 `V -> 6 About` screen is the architect's on-device binary identity
for ADV.

For any task that changes production code linked into the MiniFT8 executable,
update the About version to:

```text
MiniFT8-V3.<task number>
```

Examples:

```text
T085 -> MiniFT8-V3.085
T091 -> MiniFT8-V3.091
```

Do not bump the MiniFT8 version for documentation-only, test-only, or unrelated
MiniShell tasks.

Keep the matching About-string assertion in the task's UI tests. Do not add Git
SHA, timestamps, generated build metadata, or a version framework unless the
architect explicitly requests it.
