# MiniShell Documentation Map

Source of truth priority:

1. `include/minishell/api.h`
2. `docs/architecture/architecture.md` and `design-principles.md`
3. current service contracts under `docs/abi/`
4. runtime/application placement and shell-policy docs
5. application docs such as `docs/MiniFT8/`
6. project audit/progress records

Current ABI documents include System, Memory, Filesystem, Time/Location, Display, Input, App, and Audio contracts. `abi-foundation.md` records cross-ABI compatibility/ownership rules.

Historical Tab5/ESP-IDF-first work is preserved on `archive/tab5-legacy`; Linux Mint on `pc-1` is the reference/full production target.

```text
Application
    -> MiniShell ABI
    -> resident services
    -> private platform backends/providers
    -> Linux / NuttX / thick embedded backend / mocks
```
