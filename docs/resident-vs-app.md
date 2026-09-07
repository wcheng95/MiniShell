# Resident MiniShell vs Runtime Application

## Purpose

New functionality needs an explicit placement decision. MiniShell should remain a small runtime/control plane; ordinary tools and domain behavior should remain applications.

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

## Current Linux resident shell

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
```

These use only the public MiniShell ABI and can be rebuilt for another MiniShell platform with the required capabilities.

Large domain applications such as MiniFT8, MiniCW, and MiniRTTY belong on this side of the boundary as well.

## Platform-dependent resident functions

Resident does not mean universal.

Some bootstrap/system operations exist only where a platform needs them:

```text
put/get or another recovery-transfer protocol
suspend
poweroff
board-specific recovery/bootstrap commands
```

Linux Mint does not need MiniShell `put/get` because normal host file access already exists. A small embedded target may need a serial/USB/BLE transfer command to provision or recover its storage.

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

Keeping them as apps tests the same ABI that real applications will use instead of creating privileged shell-only paths.

## Loader/container independence

Do not describe applications conceptually as "ELF apps" or ".so apps". Those are backend packaging choices.

```text
Linux/Mint      .so
NuttX           native loadable mechanism where practical
ADV             compiled-in registry acceptable when necessary
```

The application contract remains `main(argc, argv)` plus `mini_api_get()` for the current native ABI.

## One-owner rule still applies to resident functionality

Resident functionality must still have:

- one clear state/resource owner;
- a small internal interface;
- platform-private implementation below the backend boundary;
- focused tests;
- no unnecessary public ABI expansion.

Moving a feature resident is not permission to put its implementation into `shell.c`.

## Packaging small tools

Keep ordinary tools separate first because that gives:

```text
clear ownership
small understandable source
independent testing
load only what is needed
```

A future grouped `minitools` package is acceptable only if measurement shows that individual packaging creates meaningful cost. It is an optimization, not the base architecture.

## Placement examples

| Function | Default placement |
| --- | --- |
| Shell dispatch | resident |
| App lifecycle/loader contract | resident |
| Memory/FS/Time/Display/Input service semantics | resident |
| `status` | resident diagnostic |
| `ls`, `cat`, `cp`, `nano`, `free`, `df`, `date` | portable app |
| MiniFT8/MiniCW/MiniRTTY | portable/domain app |
| Recovery transfer | platform-dependent resident function |
| Power/system control | platform-dependent resident function |

See `docs/command-roadmap.md` for the current command baseline and `docs/consistency-check.md` for internal architecture debt.
