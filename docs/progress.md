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

## Current validated baseline

Current portable applications:

```text
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

CI covers the Linux runtime/integration suite and the retained platform-neutral unit suite.

## Remaining internal housekeeping debt

Deferred for later:

1. split the oversized Linux backend;
2. remove POSIX details from portable core paths;
3. split Filesystem private helpers while preserving one owner;
5. make the Linux ANSI/CSI parser stateful across split reads.

See `consistency-check.md` for details.

## Next major development boundary

After housekeeping, new service work should be driven by MiniFT8-V3 requirements rather than generic shell feature accumulation.
