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

Reuse a Linux command name when MiniShell implements the same core idea closely
enough that the name will not surprise a Linux user. This reduces unnecessary
vocabulary and lets existing muscle memory carry over.

Using a familiar name does **not** promise full GNU or POSIX compatibility. Each
MiniShell command implements the smallest useful subset first and adds options
only when a real use case requires them.

The planned editor previously called `med` is now named `nano`.

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
| `ls` | minimal storage inspection for provisioning/recovery |
| `exec` | explicit application launch |
| `repeat` | development/lifecycle diagnostic |
| `put` | bootstrap/provisioning file transfer |
| `get` | bootstrap/recovery file transfer |

Do not add resident commands merely because Linux has them.

`cd` and `pwd`, for example, remain deferred until MiniShell has a deliberate
working-directory model. The current Filesystem ABI is based on absolute logical
paths.

## Application command set

The default is one independently built ELF per command under `apps/<name>/`,
installed as `/sd/apps/<name>.elf`.

### Stage A - minimum useful file environment

This is the complete planned Stage A set:

| Command | Initial MiniShell scope | Status / ABI note |
| --- | --- | --- |
| `cat` | display text files | implemented; kept because it already exists |
| `nano` | open/edit/search/save text files | next major utility |
| `cp` | copy one file to another path | current Filesystem ABI is sufficient |
| `mv` | move/rename one file | requires a deliberate Filesystem ABI extension |
| `rm` | remove one file | requires a deliberate Filesystem ABI extension |
| `mkdir` | create one directory | requires a deliberate Filesystem ABI extension |
| `rmdir` | remove one empty directory | requires a deliberate Filesystem ABI extension |

`cat` is not considered essential; it remains because it is already implemented
and useful as a tiny ABI/application example.

`rm -r` is not planned for Stage A. Recursive deletion adds risk and is not
required for the minimum useful environment.

There is no separate `grep` command in the planned MiniShell set. Text search
belongs inside `nano`, where it directly supports the main interactive text-work
use case.

### Stage B

No Stage B command set is currently planned.

Do not add text-processing utilities merely to resemble Unix. Add another command
only when a concrete MiniShell use case justifies it.

### Stage C - minimal system information

The planned Stage C set is intentionally small:

| Command | Initial MiniShell scope | ABI note |
| --- | --- | --- |
| `free` | show useful memory information | builds on Memory ABI |
| `date` | show/set UTC where supported | builds on Time/Location ABI capabilities |
| `df` | show storage capacity/free space | requires filesystem/storage information support |

No other Linux system-information commands are currently planned.

## Commands intentionally not planned

Examples include:

```text
grep / head / tail / wc     unnecessary for the current MiniShell goals
sha256sum / hexdump          not part of the minimal command set
ps / top / kill / jobs      no Linux-like process model
sudo                         no users/privilege boundary
chmod / chown                no Unix permission model
mount / umount               storage ownership is resident MiniShell policy
systemctl                    no systemd/service-manager model
```

This is not a permanent ban. A command can be reconsidered if a real use case
appears later.

## BusyBox policy

Do not package the command set into a BusyBox-style binary by default.

Use independent applications:

```text
/sd/apps/cat.elf
/sd/apps/nano.elf
/sd/apps/cp.elf
/sd/apps/mv.elf
/sd/apps/rm.elf
/sd/apps/mkdir.elf
/sd/apps/rmdir.elf
/sd/apps/free.elf
/sd/apps/date.elf
/sd/apps/df.elf
```

This preserves independent development, testing, replacement, and learning.
Bundle only if future measurement shows a meaningful benefit.

## ABI growth rule

Command development should drive ABI growth rather than the reverse.

```text
define minimum useful command behavior
        |
        v
check current ABI
        |
        +-- sufficient -> build the app
        |
        `-- missing primitive
                |
                v
        decide whether the primitive is generally useful
                |
                +-- yes -> extend ABI deliberately + tests
                `-- no  -> reconsider command/design
```

The current roadmap already gives concrete examples:

```text
cp       current Filesystem ABI is enough
mv       exposes missing rename/move primitive
rm       exposes missing remove primitive
mkdir    exposes missing create-directory primitive
rmdir    exposes missing remove-directory primitive
df       exposes missing storage-capacity/free-space information
```

Do not expand an ABI merely to imitate Linux.

## Preferred development sequence

```text
finish Task 2 file-transfer validation
        |
        v
nano
        |
        v
cp
        |
        v
Filesystem ABI review
        |
        v
mv / rm / mkdir / rmdir
        |
        v
free / date / df
```

This is intentionally a small command set. MiniShell should become useful without
becoming a miniature Linux distribution.
