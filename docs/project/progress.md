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

Cardputer ADV V1 uses a compiled-in application registry. Runtime `.elf` loading is **deferred, not rejected**; it may be explored later for suitable applications without blocking the initial ADV backend.

ADV persistent-storage policy is already decided for A3:

```text
/flash    2 MiB LittleFS initially, provisional
/sd       optional FATFS
NVS       not used
```

Compiled-in applications have priority. Future external discovery checks `/flash/apps` and then `/sd/apps` for application names not already provided internally.

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

## A1 — implementation in progress

The initial ADV backend skeleton is now checked in under `platform/adv/`.

Implemented A1 pieces:

```text
ESP-IDF firmware project targeting ESP32-S3
app_main() -> portable minishell_run()
platform name = adv
private USB Serial/JTAG bring-up console
System.write provider for the portable hello app
compiled-in application registry
portable hello app packaged statically behind a private entry rename
host-side static-registry unit test
GitHub ADV firmware build workflow using ESP-IDF v5.5.1
8 MiB flash table with a reserved 2 MiB LittleFS /flash partition
```

The USB Serial/JTAG shell is deliberately temporary A1 infrastructure. Cardputer display and keyboard are brought in during A2, where they become the proper resident/public Display/Input implementations. This keeps A1 focused on proving firmware composition and lifecycle rather than prematurely implementing the final UI backend.

A1 intentionally leaves these public services unavailable:

```text
Memory          A2
Display         A2
Input           A2
Filesystem      A3
Time/Location   A3
Audio           later RX/TX vertical slice
```

ADV A1 sets the global MiniShell memory and storage quotas to `0`, meaning no extra global quota beyond physical/backend limits. The 2 MiB LittleFS partition size is therefore not incorrectly applied as a future `/sd` storage cap.

Primary implementation commit:

```text
b795842e  feat: add ADV A1 ESP-IDF build skeleton
```

The first CI configuration pass exposed an ESP-IDF CMake restriction on source-property mutation. The static hello packaging was adjusted to an ADV-private wrapper without changing portable `apps/hello/hello.c`:

```text
142dced2  fix: package ADV hello without CMake source mutation
```

The Linux workflow and ADV static-registry unit test remain green. Firmware CI and real Cardputer hardware validation determine completion of A1.

A1 hardware exit check is deliberately small:

```text
MiniShell boots on Cardputer ADV
USB Serial/JTAG shows M$>
status reports platform : adv
apps lists hello
hello prints through System.write
hello returns cleanly to M$>
```

## Next after A1

A2 adds the real Cardputer-facing System/Memory/Display/Input providers. The agreed ADV text surface is:

```text
240 x 135 physical panel
20 columns x 7 logical text rows
12 x 16 target fixed-width font
19-pixel row pitch with a 2-pixel physical gap after row 0
```

Pixel geometry remains backend-private. The `ft8` ADV profile owns the application meaning of row 0 versus rows 1-6.

MiniFT8-V2 remains reference material for proven Cardputer hardware behavior only; V2 will not be modified or refactored for this work.

Canonical plans/policy:

```text
docs/project/adv-backend-plan.md
docs/api/api-foundation.md
platform/adv/README.md
```

After the Linux+ADV-profile versus ADV+ADV-profile validation checkpoint passes, resume RX-1B.
