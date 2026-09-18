# T### — Task name

Status: DRAFT

## Architect intent

State the user/architect decision this task implements.

## Objective

One bounded implementation outcome.

## Current context

Relevant current behavior, code state, hardware/test facts, and why this task exists now.

## Source of truth

List canonical docs, code, pinned reference commits, protocol/file-format references, or measured evidence that Codex must follow.

## Architectural constraints

- Preserve existing ownership boundaries.
- State which module owns policy/state/platform access.
- State whether public MiniShell API changes are forbidden/allowed.
- State memory/timing/platform constraints.

## Implementation scope

Expected files/modules and concrete changes.

## Non-goals

Explicitly list tempting adjacent work that is not part of this task.

## Acceptance criteria

- [ ] Required behavior 1
- [ ] Required behavior 2
- [ ] No regression to stated invariant
- [ ] Documentation/comments updated where required

## Automated tests

Commands/tests Codex must run before handoff.

```bash
cmake --build build-linux -j"$(nproc)"
ctest --test-dir build-linux --output-on-failure
```

Add task-specific tests here.

## Manual / hardware validation

Steps the architect/tester must perform after supervisor review. Write `none` when not applicable.

## Codex implementation notes

Codex fills this section before handoff.

### Implementation summary

### Files changed

### Invariants preserved

### Local tests run

### Manual/hardware validation still required

### Known limitations / risks

### Commit

## Supervisor review

Supervisor fills this after reviewing the actual `main..<commit>` diff and local test evidence. Pull requests are not required unless the task explicitly requests one.

## Architect test result

Record real host/hardware validation and final acceptance here.
