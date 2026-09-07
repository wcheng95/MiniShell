# MiniShell Progress Log

This file records major architectural milestones rather than every commit.

## Earlier Tab5 proof-of-concept

MiniShell first proved its core ideas on M5Stack Tab5 / ESP32-P4 with ESP-IDF: resident shell, SD access, native ELF loading, `mini_api_get()` binding, foreground app return, and early transfer/power experiments.

That implementation is no longer part of active `main`. It is preserved in:

```text
archive/tab5-legacy
```

## Linux reference pivot

Linux Mint on `pc-1` is now the reference implementation and full production target.

### Stage 1 — runtime baseline

Completed dedicated Linux build, `M$>` shell, runtime `.so` discovery/loading, direct app launch, clean stale-toolchain protection, and Linux CI.

### Stage 2 — portable service backend

Merged as `5e3f5ac`.

System, Memory, Filesystem, Time/Location, Display, and Input services were reused above a Linux/POSIX backend with app-lifecycle cleanup and terminal handoff.

### Stage 3 — portable applications

Merged as `8060222`.

Portable runtime apps and PTY tests validated file utilities and nano edit/save/exit behavior.

### Directory iteration / `ls`

Merged as `faa74ea`.

Filesystem ABI gained `dir_open`, `dir_read`, and `dir_close`; `ls` became a portable app built on those same general-purpose primitives.

### Generic resource/time baseline

Merged as `4e6424f`.

Added enforced MiniShell memory/storage budgets, portable `free`, portable `df`, Filesystem `space(path)`, and portable `date` with session-only UTC correction on Linux.

### Nano inverse cursor

Merged as `effe6ba`.

Display text gained optional `write_at_attr()` with `MINI_TEXT_ATTR_INVERSE`; nano now uses inverse-video cursor rendering without embedding ANSI sequences.

### Root `ls` consistency

Merged as `f4c85af`.

MiniShell root now lists directly usable logical paths:

```text
M$> ls
/sd
/flash
```

### Architecture/documentation audit

Merged as `f8849a6`.

The Linux baseline was checked against top-down design, clean interfaces, one-owner semantics, and understandable module size. Public architecture passed; internal cleanup debt was documented.

### Legacy Tab5 cleanup

The obsolete Tab5/ESP-IDF source path, old ELF/transfer/power tests, milestone docs, and associated tooling/build metadata were removed from active `main`. The complete pre-cleanup state is preserved in `archive/tab5-legacy`.

### Documentation classification

MiniShell documentation was grouped by purpose under `docs/architecture/`, `docs/abi/`, and `docs/project/`, then extended with `docs/apps/` for ordinary applications and `docs/MiniFT8/` for the large MiniFT8 domain application.

### MiniFT8-driven rename semantics

MiniFT8's safe configuration-save requirement justified changing Filesystem `rename(old, new)` so an existing regular-file destination is replaced.

The service now protects active writable source/destination paths and keeps MiniShell quota accounting correct when a destination is replaced. Linux maps the operation to its normal POSIX rename behavior.

### MiniFT8-V3 first integrated slice

MiniFT8-V3 moved from its standalone Linux/ncurses prototype into MiniShell as the first substantial domain application.

The first integrated milestone is deliberately narrow:

```text
M$> MiniFT8
    -> MiniShell Display/Input
    -> 30x8 MiniFT8 UI
    -> config/scheduler actions
    -> MiniShell Filesystem
    -> /flash/MiniFT8/Station.txt
    -> q
M$>
```

MiniFT8 no longer has a direct Linux/ncurses platform layer in the active implementation. Its storage service uses the MiniShell Filesystem ABI rather than host stdio.

### Audio ABI V1 and deterministic WAV RX

MiniFT8's first concrete RX requirement drove the first post-foundation service extension.

MiniShell now exposes independent Audio RX/TX sub-APIs with exact-format requests, frame-based transfer, foreground-app cleanup, and fail-safe TX abort semantics. MiniFT8 V1 requests:

```text
12000 Hz
signed 16-bit PCM
2 channels
```

MiniShell preserves channel order but does not assign stereo/I/Q meaning. MiniFT8 source profiles own that interpretation.

The Linux reference target includes a deterministic WAV RX provider. `tests/kfs16b12k.wav` is streamed unchanged through the Audio ABI, and CI verifies exact two-channel payload preservation plus teardown/reopen behavior.

Control remains separate from Audio. Device/radio `set_time` is intentionally deferred as a future Control capability.

## Current validated baseline

Current portable/domain applications include:

```text
MiniFT8
hello
cat
cp
date
df
free
ls
mkdir
mv
nano
rm
rmdir
```

Current resident Linux commands:

```text
help
status
apps
run <app>
<app>
exit
```

`put/get` and power commands are platform-dependent and intentionally absent from the Linux baseline.

CI covers the Linux runtime/integration suite, MiniFT8 UI/runtime integration, Audio ABI/WAV integration, and the platform-neutral unit suite.

## Remaining internal housekeeping debt

The architecture audit recorded four unresolved housekeeping items. They are being paid before further MiniFT8 DSP/radio expansion:

1. split the oversized Linux backend;
2. remove POSIX details from portable core paths;
3. split Filesystem private helpers while preserving one owner;
4. make the Linux ANSI/CSI parser stateful across split reads.

See `consistency-check.md` for details.

## Next major development boundary

After the current housekeeping pass, MiniFT8 resumes deterministic FT8 receive processing:

```text
tests/kfs16b12k.wav
        -> MiniShell WAV Audio provider
        -> 12 kHz / S16 / 2-channel Audio ABI
        -> MiniFT8 source/profile interpretation
        -> ft8_engine
        -> decoded RX text
```

Live QMX/UAC should follow only after deterministic replay and the Linux backend cleanup are stable.
