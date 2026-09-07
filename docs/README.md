# MiniShell Documentation Map

MiniShell documentation is grouped by purpose so active design stays easy to navigate.

## Source of truth

When documents disagree, use this order:

1. `include/minishell/api.h` — implemented public application ABI.
2. `architecture/architecture.md` and `architecture/design-principles.md` — current architecture/design rules.
3. Current service contracts under `abi/`.
4. Runtime/application placement and shell-policy documents.
5. Application documentation such as `MiniFT8/`.
6. Project audit/progress records.

## Public ABI documents

```text
abi/
├── abi-foundation.md
├── app-abi.md
├── system-abi.md
├── memory-abi.md
├── filesystem-abi.md
├── time-location-abi.md
├── display-abi.md
├── input-abi.md
└── audio-abi.md
```

`include/minishell/api.h` remains the executable source of truth.

## Application documentation

Small apps share `docs/apps/`. MiniFT8 uses its own `docs/MiniFT8/` subtree because its architecture/development record is substantial.

## Project records

`docs/project/` contains the command roadmap, architecture consistency/debt audit, and milestone progress log.

## Legacy Tab5 work

Historical pre-Linux implementation state remains on `archive/tab5-legacy`. It is not active source.

## Reference platform

Linux Mint on `pc-1` is the reference implementation and full production target:

```text
Application
    -> MiniShell ABI
    -> portable MiniShell services/runtime
    -> private platform backend/providers
    -> Linux / NuttX / thick embedded backend / mocks
```

Platform-specific functions are allowed when genuinely platform-specific; MiniShell does not create fake operations merely to equalize targets.
