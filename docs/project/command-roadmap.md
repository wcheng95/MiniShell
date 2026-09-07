# MiniShell Command Roadmap

This document records shell/application command placement and priorities. It does not define the public ABI.

## Current Linux resident shell

```text
help
status
apps
run <app> [...]
<app> [...]
exit
```

Resident commands should stay limited to lifecycle, discovery, platform identity/status, and functions that cannot naturally live as portable applications.

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
MiniFT8
```

These are runtime-loaded native applications using MiniShell public services rather than direct Linux/POSIX APIs.

## Placement rule

Prefer a portable application when the behavior can be expressed through public MiniShell services.

Prefer a resident command only when it fundamentally controls MiniShell itself, runtime app lifecycle, or a platform operation that is intentionally outside the portable ABI.

Examples:

```text
apps            resident: discovers runtime applications
run             resident: transfers foreground ownership to an app
ls              app: uses Filesystem directory iteration
cat/cp/mv/rm    apps: use Filesystem operations
free/df/date    apps: use Memory/Filesystem/Time services
```

## Platform-dependent commands

`put/get` and power-management commands are not part of the Linux baseline. A future target may expose them when they correspond to real platform behavior; MiniShell should not invent fake cross-platform equivalents.

## Audio and Control

Audio and Control are services, not shell commands by default. Domain applications such as MiniFT8 should consume them directly through the public ABI.

If diagnostic tools are later useful, prefer small ordinary apps such as an `audio-info` or `control-info` probe rather than growing the resident shell.

## Near-term housekeeping

Before expanding the command set, finish the current internal cleanup:

```text
split Linux backend
remove POSIX loader semantics from portable core
split Filesystem private helpers
then evaluate stateful ANSI/CSI parser cost
```

The command roadmap should not drive new ABI work ahead of application requirements.
