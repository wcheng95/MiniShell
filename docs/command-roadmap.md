# MiniShell Command Roadmap

## Goal

MiniShell provides a small, familiar command environment without trying to become
a Linux distribution. Linux command names are reused when MiniShell implements the
same core idea closely enough to be unsurprising.

The governing criteria are:

```text
familiar
minimal
portable
useful
```

## Resident shell commands

Resident commands exist only when they control MiniShell itself or expose runtime
state that belongs to MiniShell.

Current Linux baseline:

| Command | Purpose |
| --- | --- |
| `help` | show shell help |
| `status` | show platform and MiniShell service availability |
| `apps` | list installed MiniShell applications |
| `run <app> [...]` | explicitly launch an application |
| `exit` | leave MiniShell |

Direct application launch is also supported:

```text
M$> nano /sd/notes.txt
```

`run <app>` and direct `<app>` use the same internal application-launch path.
There is no separate user-facing `exec` command in the Linux baseline.

`put` and `get` are platform-dependent transfer functions rather than universal
commands. A target that needs serial, USB, BLE, or another provisioning/recovery
channel may add them later. Power operations such as `suspend` or `poweroff` are
also platform-dependent; MiniShell does not add fake implementations merely to
make command sets identical.

## Portable applications

Ordinary utilities are applications and use only the public MiniShell ABI.

Current generic baseline:

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

On Linux these are runtime-loaded `.so` modules. Other platforms may use different
loading or linking mechanisms while preserving the same application-facing ABI.

### File utilities

| App | Initial behavior |
| --- | --- |
| `ls [path]` | enumerate a directory; hide dot-files by default |
| `cat <file>` | display a text file |
| `cp <src> <dst>` | binary-safe file copy |
| `mv <src> <dst>` | rename one regular file; no overwrite |
| `rm <file>` | remove one regular file |
| `mkdir <path>` | create one directory |
| `rmdir <path>` | remove one empty directory |
| `nano <file>` | small interactive editor |

`ls` uses the same Filesystem ABI directory iteration that applications such as
MiniFT8 need for discovering logs:

```text
Filesystem ABI
    +-- dir_open
    +-- dir_read
    `-- dir_close
```

### Resource and time utilities

| App | Initial behavior |
| --- | --- |
| `free` | concise MiniShell memory used/free/total |
| `df [path]` | concise MiniShell storage used/free/total |
| `date` | show MiniShell UTC |
| `date YYYY-MM-DD HH:MM:SS` | set MiniShell UTC |

`free` and `df` describe resources available through MiniShell, not the host
computer's unconstrained RAM or disk. The resource policy is resident/internal and
is enforced by the Memory and Filesystem services.

On Linux, `date` loads host UTC at MiniShell startup. Setting it re-anchors
MiniShell UTC for the current session and does not modify or persist Linux system
time. A backend that owns a writable RTC may persist the same ABI operation.

## ABI growth rule

Real application requirements drive ABI growth.

```text
define required app behavior
        |
        v
check current ABI
        |
        +-- sufficient -> implement app
        |
        `-- missing primitive
                |
                v
        is it generally useful to applications?
                |
                +-- yes -> append ABI + tests
                `-- no  -> reconsider design
```

Examples:

```text
cp       existing file read/write API was sufficient
mv       justified rename
rm       justified remove_file
mkdir    justified mkdir
rmdir    justified rmdir + MINI_ERR_NOT_EMPTY
ls       justified dir_open / dir_read / dir_close because applications need discovery
free     existing Memory ABI was sufficient once MiniShell resource policy existed
date     existing Time/Location ABI was sufficient
 df      justified append-only Filesystem space(path)
```

Do not expand an ABI merely to imitate POSIX or GNU utilities.

## Deferred commands

`cd` and `pwd` remain deferred. MiniShell currently uses absolute logical paths and
has no working-directory model.

Other commands are added only when a concrete MiniShell use case appears.
Examples not currently needed include:

```text
grep / head / tail / wc
ps / top / kill / jobs
sudo
chmod / chown
mount / umount
systemctl
```

## Principle

Keep the shell small. Keep reusable functionality in application-facing services.
Keep ordinary utilities as applications. Platform-specific functionality is
allowed to remain platform-specific when a universal abstraction would add no
value.
