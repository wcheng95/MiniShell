# MiniShell Documentation Map

MiniShell documentation is grouped by purpose so the active design stays easy to navigate.

## Source of truth

When documents disagree, use this order:

1. `include/minishell/api.h` — implemented public application ABI.
2. `architecture/architecture.md` and `architecture/design-principles.md` — current MiniShell architecture and design rules.
3. Current ABI documents under `abi/`.
4. `architecture/resident-vs-app.md` and `project/command-roadmap.md` — runtime/application placement and shell policy.
5. Application documentation under `apps/` or a large application's own subtree such as `MiniFT8/`.
6. `project/consistency-check.md` and `project/progress.md` — audit/debt and milestone record.

`abi/abi-foundation.md` remains a useful foundational design record, but the public header and current service documents win if details have evolved.

## Architecture

```text
architecture/
├── architecture.md
├── design-principles.md
└── resident-vs-app.md
```

These documents define MiniShell's system model, dependency direction, ownership rules, module boundaries, and resident-vs-application placement.

## Public ABI

```text
abi/
├── abi-foundation.md
├── app-abi.md
├── system-abi.md
├── memory-abi.md
├── filesystem-abi.md
├── time-location-abi.md
├── display-abi.md
└── input-abi.md
```

`include/minishell/api.h` remains the executable source of truth. The documents explain intent, semantics, ownership, and compatibility rules.

## Application documentation

Small and medium applications share:

```text
apps/
├── utilities.md
└── nano.md
```

Large domain applications may have their own documentation subtree when that keeps their architecture and development records understandable. MiniFT8 uses:

```text
MiniFT8/
├── README.md
├── architecture.md
├── development.md
└── ui.md
```

The application source still lives under `apps/`; keeping documentation under the single top-level `docs/` tree is a repository-organization choice, not an architectural dependency.

## Project records

```text
project/
├── command-roadmap.md
├── consistency-check.md
└── progress.md
```

These files record the current command baseline, architecture audits/known debt, and major project milestones. They are useful project state, but they do not override the architecture or public ABI.

## Legacy Tab5 work

The earlier Tab5/ESP-IDF-first implementation, milestone documents, ELF tests, transfer/power experiments, and associated build files were removed from active `main` after the Linux baseline became canonical.

They remain available in:

```text
archive/tab5-legacy
```

Use that branch only when historical implementation details are needed for comparison or a future port.

## Current reference platform

Linux Mint on `pc-1` is the reference implementation and a full production target:

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
