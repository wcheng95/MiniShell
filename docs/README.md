# MiniShell Documentation Map

This directory contains the current MiniShell design for the Linux-first reference implementation.

## Source of truth

When documents disagree, use this order:

1. `include/minishell/api.h` — implemented public application ABI.
2. `architecture.md` and `design-principles.md` — current architecture and design rules.
3. Service documents: `system-abi.md`, `memory-abi.md`, `filesystem-abi.md`, `time-location-abi.md`, `display-abi.md`, and `input-abi.md`.
4. `app-abi.md`, `command-roadmap.md`, and `resident-vs-app.md` — application/runtime placement and shell policy.
5. `consistency-check.md` and `progress.md` — audit/debt and milestone record.

`abi-foundation.md` remains a useful foundational design record, but the public header and current service documents win if details have evolved.

## Legacy Tab5 work

The earlier Tab5/ESP-IDF-first implementation, milestone documents, ELF tests, transfer/power experiments, and associated build files were removed from active `main` after the Linux baseline became canonical.

They remain available in the Git branch:

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
