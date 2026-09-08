# MiniShell Progress Log

Current baseline:

- Linux Mint on `pc-1` is the reference/full production target.
- Runtime app discovery/loading and `M$>` are established on Linux.
- System, Memory, Filesystem, Time/Location, Display, Input, and Audio services have automated coverage.
- MiniFT8 is integrated for UI/config/storage.
- Audio V1 is implemented; MiniFT8 requests 12 kHz/S16/two-channel transport with channel meaning owned by MiniFT8.
- Linux deterministic WAV RX replays `tests/kfs16b12k.wav` unchanged.
- Control remains independent from Audio; device `set_time` is deferred.
- Historical Tab5 work is preserved on `archive/tab5-legacy`.
- The application-facing contract is the **MiniShell API**. Backward source/binary compatibility is not frozen during this early architecture phase; a formal ABI may be introduced later if independently built applications require it.

## Housekeeping paydown completed

The architecture-audit debt H1-H5 is paid:

```text
H1 Linux backend split
H2 POSIX loader semantics removed from portable core
H3 Filesystem private helpers split while preserving one owner
H4 legacy Tab5 active tree removed and archived
H5 terminal ANSI/CSI + UTF-8 parser made stateful across reads
```

The full Linux integration suite and strict unit suite remain green.

## Current priority: ADV backend + MiniFT8 profiles

RX-1A is complete and remains the frozen decoder/golden baseline. RX-1B is intentionally **paused** while the architecture is exercised across a second real backend and a second MiniFT8 profile.

Current target matrix:

```text
Linux backend + DESKTOP profile
Linux backend + ADV profile
ADV backend   + ADV profile
```

Cardputer ADV V1 will use a compiled-in application registry. MiniShell and MiniFT8 are built into one ESP-IDF firmware image. Runtime `.elf` loading is **deferred, not rejected**; it may be explored later for suitable lower-RAM applications without blocking the initial ADV backend.

## A0 progress

API terminology cleanup is complete:

```text
MINISHELL_ABI_VERSION -> MINISHELL_API_VERSION
mini_api_t.abi_version -> mini_api_t.api_version
docs/abi/ -> docs/api/
old append-only compatibility policy removed
```

The Linux build, all 11 integration tests, and the platform-neutral unit suite are green after the rename.

The remaining A0 work is the substantive portability slice: remove Linux-style stdin/stdout/startup assumptions from the resident MiniShell shell before adding `platform/adv/`.

MiniFT8-V2 remains reference material for proven Cardputer hardware behavior only; V2 will not be modified or refactored for this work.

The cross-platform checkpoint focuses first on System/Memory, Display/Input, Filesystem, Time/Location, static app lifecycle, and MiniFT8 UI/config behavior. Live ADV QMX Audio is deliberately deferred until the RX/TX vertical slice actually needs it.

Canonical plans/policy:

```text
docs/project/adv-backend-plan.md
docs/api/api-foundation.md
```

After the Linux+ADV-profile versus ADV+ADV-profile validation checkpoint passes, resume RX-1B.
