# MiniShell Progress Log

Current baseline:

- Linux Mint on `pc-1` is the reference/full production target.
- Runtime app discovery/loading and `M$>` are established on Linux.
- System, Memory, Filesystem, Time/Location, Display, Input, and Audio services have automated coverage.
- MiniFT8 is integrated as runtime app `ft8` for UI/config/storage.
- `ft8` is FT8-only; future FT4/CW/RTTY/JS8 support will use separate applications rather than an internal protocol-mode selector.
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

## Current priority: A1 ADV build skeleton

RX-1A is complete and remains the frozen decoder/golden baseline. RX-1B is intentionally **paused** while the architecture is exercised across a second real backend and a second MiniFT8 profile.

Current target matrix:

```text
Linux backend + DESKTOP profile
Linux backend + ADV profile
ADV backend   + ADV profile
```

Cardputer ADV V1 will use a compiled-in application registry. MiniShell and runtime app `ft8` are built into one ESP-IDF firmware image. Runtime `.elf` loading is **deferred, not rejected**; it may be explored later for suitable applications without blocking the initial ADV backend.

## A0 — complete

One-time normalization completed:

```text
MINISHELL_ABI_VERSION -> MINISHELL_API_VERSION
mini_api_t.abi_version -> mini_api_t.api_version
docs/abi/ -> docs/api/
old append-only compatibility policy removed
mixed-case code/runtime names -> lowercase
apps/minift8/ -> apps/ft8/
minift8 runtime command -> ft8
/flash/minift8/station.txt -> /flash/ft8/station.txt
internal protocol Mode state -> removed from ft8
```

The `ft8` configuration now persists protocol-local scalar values such as `profile`, `band`, `skip_tx1`, and `max_retry`; no protocol `mode=` value is stored.

Resident shell/startup portability completed:

```text
core/minishell_runtime.c    portable lifecycle via minishell_run()
platform/linux/main.c       Linux entry point only
platform/linux/linux_console.c
                            owns stdin/stdout + stdio buffering
core/shell.c                no direct stdin/stdout dependency
core/platform_backend.h     private resident console read/write boundary
```

The resident console boundary is private MiniShell infrastructure; applications do not use it and continue through the public Display/Input/System APIs.

Commit `1cb44be4` (`refactor: isolate resident console and startup boundary`) passed the full Linux workflow, including all 11 integration tests and the platform-neutral unit suite.

## Next: A1

Create `platform/adv/` and the ESP-IDF/Cardputer ADV firmware skeleton. The first goal is deliberately small:

```text
firmware builds
MiniShell boots
private shell console works on Cardputer display/keyboard
platform reports ADV
static app registry lists/runs a tiny probe/hello app
app returns to M$>
```

MiniFT8-V2 remains reference material for proven Cardputer hardware behavior only; V2 will not be modified or refactored for this work.

The cross-platform checkpoint focuses first on System/Memory, Display/Input, Filesystem, Time/Location, static app lifecycle, and MiniFT8 UI/config behavior. Live ADV QMX Audio is deliberately deferred until the RX/TX vertical slice actually needs it.

Canonical plans/policy:

```text
docs/project/adv-backend-plan.md
docs/api/api-foundation.md
```

After the Linux+ADV-profile versus ADV+ADV-profile validation checkpoint passes, resume RX-1B.
