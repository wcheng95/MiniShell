# MiniShell Command Roadmap

## Goal

MiniShell uses Linux as the naming and behavior reference for ordinary shell
commands, but it does not try to reproduce a full GNU/Linux userland.

The governing criteria are:

```text
familiar
minimal
useful
understandable
```

A Linux command name should be reused when MiniShell implements the same core
idea closely enough that the name will not surprise a Linux user. This reduces
unnecessary vocabulary and lets existing muscle memory carry over.

Examples:

```text
cat
cp
mv
rm
mkdir
nano
grep
head
tail
wc
sha256sum
```

The planned editor previously called `med` is now named `nano`.

## Compatibility philosophy

Using a Linux command name does **not** mean MiniShell promises full GNU or POSIX
compatibility.

Each MiniShell command should implement the smallest useful subset first:

- preserve the familiar core behavior;
- add options only when a real use case requires them;
- reject unsupported options rather than silently interpreting them differently;
- document the MiniShell subset in the command's README/help text;
- keep exit status simple: `0` for success, non-zero for failure unless a command
  later needs a more specific documented convention.

If MiniShell behavior would be fundamentally different from the familiar Linux
command, use a different name instead of borrowing a misleading name.

## Placement rule

The resident-vs-application rule remains unchanged.

Resident commands exist when MiniShell itself needs the capability for shell
state, bootstrap, provisioning, recovery, diagnostics, or hardware ownership.
Ordinary user utilities are independent ELF applications.

### Resident baseline

Current resident commands:

| Command | Reason it is resident |
| --- | --- |
| `help` | shell control/diagnostics |
| `status` | platform/runtime diagnostics |
| `ls` | minimal storage inspection needed for provisioning/recovery |
| `exec` | explicit application launch |
| `repeat` | development/lifecycle diagnostic |
| `put` | bootstrap/provisioning file transfer |
| `get` | bootstrap/recovery file transfer |

Possible later resident shell-state commands:

```text
cd
pwd
```

These should be added together only after MiniShell defines a coherent current
working directory model. The application Filesystem ABI currently uses absolute
logical paths, so `cd` should not be added as a cosmetic shell feature without a
clear rule for how applications see or resolve relative paths.

Other trusted system-control commands such as `reboot` may be added later when a
real requirement exists.

## Application command roadmap

The default is one independently built ELF per command under `apps/<name>/`,
installed as `/sd/apps/<name>.elf`.

### Stage A - essential file work

These are the highest-value commands for a small self-hosting environment:

| Command | Initial MiniShell scope | Status |
| --- | --- | --- |
| `cat` | display text files | implemented |
| `nano` | open/edit/save text files; simple interactive editor | next editor |
| `cp` | copy one file to another path | planned |
| `mv` | rename/move a file where supported | planned |
| `rm` | remove one file; no recursive deletion initially | planned |
| `mkdir` | create one directory | planned; requires Filesystem ABI support |
| `rmdir` | remove an empty directory | planned; requires Filesystem ABI support |
| `sha256sum` | compute SHA-256 for one or more files | planned |
| `hexdump` | inspect binary files in a familiar hex+ASCII form | planned |

`rm -r` is intentionally deferred. Recursive deletion is powerful and not needed
for the first useful tool set.

### Stage B - text inspection/search

Add these after Stage A is comfortable:

```text
head
tail
wc
grep
```

Initial versions should stay small. For example, `grep` needs ordinary literal
or simple pattern search before it needs a large GNU option surface.

### Stage C - lightweight system information

Useful candidates that can be built from existing or small future services:

```text
free       memory information via Memory ABI
uptime     elapsed time via monotonic Time/Location ABI
date       UTC read/set when the platform time capability is available
df         storage information after the Filesystem/service model exposes it
```

### Stage D - add only when another feature makes them useful

Examples:

```text
echo
printf
sort
uniq
find
tar
gzip
```

`echo` and `printf` become much more useful if MiniShell later gains redirection
or pipelines. `find`, archive, and compression tools should wait for demonstrated
need rather than expanding the userland by habit.

Network commands such as `ping`, `curl`, or `wget` belong even later, after a
network service/ABI exists.

## Commands that do not currently fit MiniShell

Do not copy Linux commands whose underlying system concept is absent.

Examples:

```text
ps / top / kill / jobs     no Linux-like process model
sudo                       no users/privilege boundary
chmod / chown              no Unix permission model
mount / umount             storage ownership is resident MiniShell policy
systemctl                  no systemd/service-manager model
```

If MiniShell later grows the corresponding concept, reconsider them then.

## BusyBox policy

Do not package these commands into a BusyBox-style binary by default.

Start with:

```text
/sd/apps/cat.elf
/sd/apps/nano.elf
/sd/apps/cp.elf
/sd/apps/rm.elf
...
```

This preserves independent development, testing, replacement, and learning.
Only introduce a bundled `minitools.elf` if measurements later show a meaningful
space/load/build benefit.

## Development order

For each command:

```text
define the minimum useful behavior
        |
        v
check whether current ABI supports it cleanly
        |
        +-- no -> decide whether the missing capability deserves an ABI extension
        |
        v
implement as a small standalone app
        |
        v
host/unit test command logic where practical
        |
        v
build ELF
        |
        v
install with resident `put`
        |
        v
exercise on real MiniShell
```

Do not expand an ABI merely to imitate Linux. An ABI grows only when a useful
MiniShell application exposes a genuine missing primitive.

## Near-term sequence

The current preferred sequence is:

```text
finish Task 2 file-transfer validation
        |
        v
nano
        |
        v
cp / mv / rm
        |
        v
mkdir / rmdir   (with deliberate Filesystem ABI review)
        |
        v
sha256sum / hexdump
        |
        v
head / tail / wc / grep
```

This sequence makes MiniShell increasingly useful for managing its own files and
applications while keeping the command set compact.
