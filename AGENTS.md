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
- reviewing Codex commits/PRs against the task and architecture
- diagnosing test failures with the architect
- updating canonical documentation after accepted changes

The supervisor should not silently redesign an architect-approved boundary merely to make implementation easier.

### Engineer — Codex

Owns:

- implementing the active task exactly as specified
- keeping changes minimal and local to the task
- adding/updating tests required by the task
- running the available build/test suite before handoff
- documenting implementation details and remaining risks in the task file/PR

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

Codex records implementation notes in the same task file or in the PR body as required by the task. The supervisor reviews the resulting commit/PR from GitHub.

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
4. Codex works on a dedicated branch and opens a PR unless the task explicitly says otherwise.
5. Codex records implementation notes and test results.
6. Supervisor reviews the diff/PR against the task and repository architecture.
7. Architect tests on real hardware/host as needed.
8. Supervisor records accepted test evidence and updates canonical docs.
9. Architect/coordinator decides when to merge/close.

## Branch / PR convention

Preferred Codex branch:

```text
codex/T###-short-name
```

Preferred PR title:

```text
T###: short task name
```

Do not mix unrelated cleanup into a task PR.

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
Commit/PR reference
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
