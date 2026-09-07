# MiniShell Progress Log

This file records major architectural milestones rather than every commit.

## Earlier Tab5 proof-of-concept

MiniShell first proved its core ideas on M5Stack Tab5 / ESP32-P4 with ESP-IDF: resident shell, SD access, native ELF loading, `mini_api_get()` binding, foreground app return, and early transfer/power experiments. That implementation is no longer part of active `main`; it is preserved in `archive/tab5-legacy`.

## Linux reference pivot

Linux Mint on `pc-1` is now the reference implementation and full production target.

### Runtime/service/application baseline

The Linux baseline established the `M$>` shell, runtime `.so` discovery/loading, portable System/Memory/Filesystem/Time-Location/Display/Input services, portable utilities, directory iteration/`ls`, resource reporting, nano, and CI.

### Architecture/documentation audit

The Linux baseline was checked against top-down design, clean interfaces, one-owner semantics, and understandable module size. Public architecture passed; internal cleanup debt was documented.

### Legacy Tab5 cleanup

The obsolete Tab5/ESP-IDF active source path and associated old tooling/tests/docs were removed from `main`; historical state remains in `archive/tab5-legacy`.

### MiniFT8-V3 integration

MiniFT8-V3 moved into MiniShell as a runtime application. Its UI/config/scheduler/storage slice uses MiniShell Display/Input/Filesystem and returns cleanly to the shell.

### Audio ABI V1 and deterministic WAV RX

MiniFT8's first concrete RX requirement drove the first post-foundation service extension. MiniShell now has independent Audio RX/TX APIs with exact-format requests, frame transfer, app-lifecycle cleanup, and fail-safe TX abort semantics.

MiniFT8 V1 requests:

```text
12000 Hz
signed 16-bit PCM
2 channels
```

MiniShell preserves channel order but does not assign stereo/IQ meaning. The Linux reference target includes a deterministic WAV RX provider; `tests/kfs16b12k.wav` is streamed unchanged and CI verifies exact two-channel payload preservation plus teardown/reopen behavior.

Control remains separate from Audio. Device/radio `set_time` is intentionally deferred as a future Control capability.

## Current validated baseline

Portable/domain applications include MiniFT8, hello, cat, cp, date, df, free, ls, mkdir, mv, nano, rm, and rmdir.

Resident Linux commands are:

```text
help
status
apps
run <app>
<app>
exit
```

CI covers Linux runtime/integration, MiniFT8 UI/runtime integration, Audio ABI/WAV integration, and platform-neutral unit tests.

## Current housekeeping pass

Before further MiniFT8 DSP/radio expansion, the project is paying the internal architecture debt recorded in `consistency-check.md`:

```text
H1 split oversized Linux backend
H2 remove POSIX loader details from portable core
H3 split Filesystem private helpers while preserving one owner
```

After H1-H3 are complete, evaluate the cost of H5, the stateful ANSI/CSI parser, before deciding whether to implement it immediately.

## Next major development boundary

After housekeeping, MiniFT8 resumes deterministic FT8 receive processing:

```text
tests/kfs16b12k.wav
        -> MiniShell WAV Audio provider
        -> 12 kHz / S16 / 2-channel Audio ABI
        -> MiniFT8 source/profile interpretation
        -> ft8_engine
        -> decoded RX text
```

Live QMX/UAC should follow only after deterministic replay and the Linux backend cleanup are stable.
