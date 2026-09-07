# MiniShell Documentation Map

This directory contains both the current MiniShell design and historical milestone records. The distinction matters because the project pivoted from a Tab5/ESP-IDF-first implementation to Linux Mint as the reference/production platform.

## Source of truth

When documents disagree, use this order:

1. `include/minishell/api.h` — public application ABI as implemented.
2. `docs/architecture.md` and `docs/design-principles.md` — current architecture and design rules.
3. Current service documents such as `filesystem-abi.md`, `memory-abi.md`, `display-abi.md`, `input-abi.md`, `system-abi.md`, and `time-location-abi.md`.
4. `docs/app-abi.md`, `docs/command-roadmap.md`, and `docs/resident-vs-app.md` for runtime/application placement and shell policy.
5. Historical task/milestone documents.

`docs/consistency-check.md` records the latest architecture audit. `docs/progress.md` records major completed milestones.

## Current canonical documents

- `architecture.md` — system layers, dependency direction, ownership, platform model.
- `design-principles.md` — rules used for new code and reviews.
- `app-abi.md` — runtime application contract and lifecycle.
- `command-roadmap.md` — resident shell versus portable utility behavior.
- `resident-vs-app.md` — placement rules.
- `system-abi.md`
- `memory-abi.md`
- `filesystem-abi.md`
- `time-location-abi.md`
- `display-abi.md`
- `input-abi.md`
- `consistency-check.md` — current audit/debt list.
- `progress.md` — current project progress log.

`abi-foundation.md` remains a useful foundational design record, but the current public header and service documents win if details have evolved.

## Historical milestone records

The following describe the earlier Tab5/ESP-IDF development path. They are retained as engineering history and validation evidence; they are not the current implementation roadmap:

- `task0.md`
- `task1.md`
- `task2.md`
- `task4.md`
- `task5.md`
- `task6.md`
- `power-system-plan.md`

The repository also still contains dormant pre-pivot ESP-IDF/Tab5 source trees. The root Linux CMake build does not compile them. Their eventual removal or archival is tracked in `consistency-check.md` rather than silently treating them as active code.

## Current reference platform

Linux Mint on `pc-1` is the reference implementation and a full production target. Later ports adapt the same application-facing contract:

```text
Application
    |
MiniShell ABI
    |
portable MiniShell services/runtime
    |
private platform backend
    |
Linux / NuttX / thick embedded backend / mocks
```

Platform-specific functions are allowed when they are genuinely platform-specific. MiniShell does not provide fake operations merely to make every target expose an identical command set.
