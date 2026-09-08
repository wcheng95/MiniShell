# MiniShell Documentation Map

Canonical priority:

1. `include/minishell/api.h`
2. `docs/api/api-foundation.md`
3. architecture/design-principle docs
4. current service-contract documents
5. runtime/application placement docs
6. application docs (`docs/MiniFT8/`, etc.)
7. project audit/progress records

Current public API covers App, System, Memory, Filesystem, Time/Location, Display, Input, and Audio.

MiniShell's public contract is currently an **API**, not a frozen long-term binary ABI. Breaking API changes are allowed while the architecture is still being established. Backward source or binary compatibility will be introduced deliberately only when independently built applications make it necessary.

Some existing service-contract files and directories still use the older `abi` terminology. Treat that naming as historical until the terminology cleanup is completed; `docs/api/api-foundation.md` is authoritative for compatibility policy.

Linux Mint on `pc-1` is the reference/full production target. Historical Tab5/ESP-IDF work is retained on `archive/tab5-legacy`.
