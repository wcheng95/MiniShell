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

`put` and `get` are not Linux baseline commands. A platform that needs a serial,
USB, BLE, or other provisioning/recovery transfer channel may add resident
transfer commands later behind that platform implementation.

Power commands such as `suspend` and `poweroff` are likewise platform capabilities,
not universal Linux baseline commands.

## Portable applications

Ordinary utilities are applications and use only the public MiniShell ABI.

Current baseline:

```text
hello
cat
cp
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

`ls` is intentionally an application. It uses the same Filesystem ABI directory
iteration that applications such as MiniFT8 need for discovering logs and other
files:

```text
application
    |
    v
Filesystem ABI
    |
    +-- dir_open
    +-- dir_read
    `-- dir_close
    |
    v
platform filesystem backend
```

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
ls       justified dir_open / dir_read / dir_close because applications also
         need directory discovery
```

Do not expand an ABI merely to imitate POSIX or GNU utilities.

## Deferred commands

Possible future small utilities should be added only when a real application or
operational need justifies them. Likely candidates include:

```text
free     memory information
UTC/date time display or setting
df       storage capacity/free-space information
```

Their exact form is intentionally deferred until the corresponding ABI behavior
is required.

`cd` and `pwd` are also deferred. MiniShell currently uses absolute logical paths
and has no working-directory model.

## Commands not currently needed

Examples:

```text
grep / head / tail / wc
ps / top / kill / jobs
sudo
chmod / chown
mount / umount
systemctl
```

This is not a permanent prohibition. A command is reconsidered when a concrete
MiniShell use case appears.

## Principle

Keep the shell small. Keep reusable functionality in application-facing services.
Keep ordinary utilities as applications.
