# MiniShell Progress Log

This file records major architectural milestones rather than every commit.

## Earlier Tab5 proof-of-concept

MiniShell originally proved its core ideas on M5Stack Tab5 / ESP32-P4 with ESP-IDF:

- resident `M$>` shell;
- microSD access;
- native runtime ELF loading;
- `mini_api_get()` binding;
- foundational service design;
- foreground app return to shell;
- early transfer/power experiments.

The detailed `task*.md` documents are retained as historical validation records.

## Linux reference pivot

The project then made Linux Mint on `pc-1` the reference implementation and full production target. This established a platform-independent application core and MiniShell ABI before later embedded ports.

### Linux Stage 1 — runtime baseline

Completed:

- dedicated Linux build (`build-linux`);
- `M$>` shell;
- runtime `.so` application discovery;
- `apps`;
- `run <app>` and direct `<app>` invocation;
- `dlopen()`/`dlsym()`/`dlclose()` private loader;
- clean build guard against stale ESP-IDF CMake caches;
- Linux CI.

Key commits include `abe4b0b8`, `17cba66c`, `92aa870e`, and `03a00f47`.

### Linux Stage 2 — portable service backend

Merged as `5e3f5ac`.

The existing portable System, Memory, Filesystem, Time/Location, Display, and Input services were reused above a Linux/POSIX backend. App lifecycle cleanup and terminal foreground handoff were validated.

### Linux Stage 3 — portable applications

Merged as `8060222`.

Portable apps were built from the same MiniShell-ABI source as runtime Linux modules:

```text
hello
cat
cp
mkdir
mv
nano
rm
rmdir
```

Functional and PTY tests validated file utilities and a real nano edit/save/exit cycle.

### Directory iteration / `ls`

Merged as `faa74ea`.

Filesystem ABI gained append-only application directory iteration:

```text
dir_open
dir_read
dir_close
```

`ls` became a portable application built on those primitives. This was driven by real application requirements: MiniFT8 needs directory discovery for logs/files.

### Generic resource/time baseline

Merged as `4e6424f`.

Completed:

- resident internal resource policy;
- enforced MiniShell memory/storage budgets;
- Linux defaults of 8 MiB memory and 64 MiB logical storage;
- configurable Linux limits;
- portable `free`;
- portable `df`;
- append-only Filesystem `space(path)`;
- portable `date`;
- Linux UTC loaded from system at startup with session-only MiniShell re-anchoring when `date` sets UTC.

At this point the generic Linux MiniShell baseline was considered complete.

### Nano inverse cursor

Merged as `effe6ba`.

Display text support gained optional append-only `write_at_attr()` and `MINI_TEXT_ATTR_INVERSE`. Linux maps inverse text to terminal reverse video. Nano uses the portable attribute rather than embedding ANSI sequences.

### Root `ls` path consistency

Merged as `f4c85af`.

MiniShell root listing now prints directly usable logical paths:

```text
M$> ls
/sd
/flash
```

Listings inside those roots remain ordinary relative entry names.

## Current validated baseline

The Linux reference currently has 7 CI tests covering:

1. shell/app smoke behavior;
2. service semantics and lifecycle;
3. terminal input;
4. portable utility apps;
5. nano PTY edit/save/exit and inverse cursor rendering;
6. directory iteration and `ls` behavior;
7. resource quota, `free`, `df`, and `date` behavior.

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

## Architecture housekeeping audit

After the generic baseline, the repository was reviewed against top-down design, clean interfaces, one-owner semantics, and understandable module size.

Result:

- public application architecture: good;
- service ownership: good;
- application modularity: good;
- internal housekeeping debt remains in Linux backend size, Filesystem service size, core POSIX shell leakage, dormant old Tab5 code, and terminal CSI parser robustness.

See `docs/consistency-check.md` for the detailed audit and prioritized cleanup list.

## Next major development boundary

After housekeeping, new service work should be driven by MiniFT8-V3 requirements rather than by generic shell feature accumulation. Audio/radio/etc. should be designed top-down only when the application behavior requires them.
