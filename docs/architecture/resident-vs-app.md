# Resident MiniShell vs Runtime Application

## Purpose

New functionality needs an explicit placement decision. MiniShell should remain a small runtime/control plane; ordinary tools and domain behavior should remain applications.

The maintained targets are Linux Mint and Cardputer ADV.

## Primary rule

Keep functionality resident when MiniShell itself needs it to:

```text
start/stop applications
own shared state/resources
bootstrap or recover the platform
diagnose MiniShell/platform health
apply trusted runtime policy
```

Make functionality an application when MiniShell remains a healthy and usable runtime without it.

A useful question is:

> If this feature disappeared, would MiniShell still be able to host, diagnose, and recover applications on this platform?

## Current resident shell

```text
help
status
apps
run <app>
exit
```

Direct `<app>` invocation uses the same internal launcher as `run <app>`.

The shell is a control plane. It should dispatch to small owners rather than become the implementation home for unrelated features.

## Current portable applications

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
ft8
```

These use only the public MiniShell API and can be built for Linux or ADV when the required capabilities are available.

Large domain applications such as MiniFT8, Keyer/MiniCW, and MiniRTTY belong on this side of the boundary as well.

## Platform-dependent resident functions

Resident does not mean universal.

Some bootstrap/system operations exist only where a platform needs them:

```text
put/get or another recovery-transfer protocol
suspend
poweroff
board-specific recovery/bootstrap commands
```

Linux Mint does not need MiniShell `put/get` because normal host file access already exists. A small embedded target such as ADV may need a serial/USB/BLE transfer command to provision or recover its storage.

Likewise, a desktop MiniShell does not need a fake `poweroff` command merely because an embedded target may have one.

The correct rule is:

> Platform-specific resident capability is allowed when it serves the platform; fake parity is not a goal.

## Why ordinary utilities are apps

Utilities such as `ls`, `free`, `df`, and `date` are useful consumers of reusable services:

```text
ls    -> Filesystem dir_open/read/close
free  -> Memory get_info
df    -> Filesystem space
date  -> Time/Location UTC
```

Keeping them as apps tests the same API that real applications use instead of creating privileged shell-only paths.

## Loader/container independence

Do not define an application conceptually by its container format. `.so` and `.elf` describe packaging on a backend, not the application's domain architecture.

```text
Linux/Mint          .so
Cardputer ADV V1    compiled-in registry
Cardputer ADV next  runtime external .elf
```

The ADV static registry remains available during transition/testing, but runtime loading is now an active MiniShell architecture target.

ADV application resolution is:

```text
1. compiled-in application
2. /flash/<app>.elf
3. /sd/<app>.elf
```

The same external ELF may live in either location; when both copies exist, `/flash` wins. Therefore both of these are valid Keyer installations:

```text
/flash/keyer.elf
/sd/keyer.elf
```

The source-level application contract remains MiniShell public API plus the application's own modules. Loader details such as path discovery, ELF parsing, relocation, symbol resolution, execution context, and unloading stay resident/private to MiniShell and the ADV backend.

External loading does not make Keyer platform-aware. The same Keyer source must not include ESP-IDF, FreeRTOS, M5/Cardputer, FATFS, or ELF-loader interfaces.

A formal cross-release binary ABI is still intentionally deferred. During this early phase an external application may need to be rebuilt for the matching MiniShell API generation.

## One-owner rule still applies to resident functionality

Resident functionality must still have:

- one clear state/resource owner;
- a small internal interface;
- platform-private implementation below the backend boundary;
- focused tests;
- no unnecessary public API expansion.

Moving a feature resident is not permission to put its implementation into `shell.c`.

The runtime ELF loader itself is resident because MiniShell needs it to discover/load/start/unload applications. Keyer remains an application because MiniShell does not need CW behavior to remain a healthy runtime.

## Packaging small tools

Keep ordinary tools separate first because that gives:

```text
clear ownership
small understandable source
independent testing
load/select only what is needed
```

A future grouped `minitools` package is acceptable only if measurement shows that individual packaging creates meaningful cost. It is an optimization, not the base architecture.

## Placement examples

| Function | Default placement |
| --- | --- |
| Shell dispatch | resident |
| App lifecycle/discovery/loader boundary | resident |
| ADV ELF loader | resident/private backend/runtime mechanism |
| Memory/FS/Time/Display/Input service semantics | resident |
| `status` | resident diagnostic |
| `ls`, `cat`, `cp`, `nano`, `free`, `df`, `date` | portable app |
| MiniFT8/Keyer/MiniRTTY | portable/domain app |
| Recovery transfer | platform-dependent resident function |
| Power/system control | platform-dependent resident function |

See `../project/command-roadmap.md` for the current command baseline and `../project/consistency-check.md` for architecture audit/history.
