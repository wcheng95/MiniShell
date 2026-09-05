# Resident MiniShell vs Runtime Application

## Purpose

MiniShell and runtime-loaded applications are intentionally developed on opposite
sides of a stable ABI. New functionality therefore needs an explicit placement
decision rather than automatically becoming either a shell built-in or an ELF
application.

## Primary rule

Keep a function resident in MiniShell when MiniShell needs it to manage,
provision, diagnose, recover, or own the application environment itself.

Make ordinary user/domain functionality a runtime-loaded application when
MiniShell can remain a healthy application environment without that function.

A useful question is:

> If this feature disappeared, would MiniShell still be a usable and recoverable
> application environment?

If yes, it probably belongs in an application. If no, it probably belongs in the
resident runtime.

## Resident responsibilities

Typical resident functionality includes:

```text
shell and command dispatch
application loader / lifecycle
stable ABI services
platform and shared-hardware ownership
platform/service diagnostics
bootstrap and recovery facilities
file transfer / provisioning
trusted system configuration
```

Resident does not mean monolithic. A resident command should normally delegate
to a small MiniShell module rather than embedding its implementation in the shell
parser.

For example:

```text
shell `put` / `get`
        |
        v
resident file-transfer module
        |
        +-- console transport ownership
        `-- filesystem service
```

The shell remains a control plane, not the implementation home for every built-in
facility.

## Runtime application responsibilities

Ordinary functionality should normally remain independently built and loaded:

```text
med.elf
calculator.elf
radio applications
analysis tools
games
future MiniFT8
```

These applications consume MiniShell services and can evolve independently of
the resident runtime.

## Why file transfer is resident

File transfer is a bootstrap/recovery capability. Requiring
`file_transfer.elf` in order to copy `file_transfer.elf` or another application
onto an empty/repaired SD card would create the wrong dependency direction.

Transfer also temporarily owns the serial byte stream as a protocol transport:

```text
normal shell mode
USB Serial/JTAG -> shell line input/output

transfer mode
USB Serial/JTAG -> resident transfer protocol -> filesystem
```

That ownership handoff is a MiniShell responsibility.

## BusyBox-style packaging

Do not begin with a BusyBox-style all-tools binary.

MiniShell currently values:

```text
clear ownership
small independently understandable programs
independent builds/releases
simple testing
load only what is needed
```

The default remains one ELF per ordinary application/tool. If later measurement
shows that many tiny programs waste meaningful flash/SD space or create excessive
maintenance overhead, a related group may be bundled into a `minitools.elf`
style application. That is an optimization, not the base architecture.

## Placement categories

| Category | Examples | Default placement |
| --- | --- | --- |
| Runtime/bootstrap/recovery | loader, file transfer, essential diagnostics | resident MiniShell |
| Shared hardware/service owner | filesystem backend, input routing, time service | resident MiniShell |
| Ordinary standalone tool | editor, calculator, radio utility | separate `.elf` |
| Large domain application | MiniFT8 | separate `.elf` |
| Many closely related tiny tools | future text/file utilities | separate `.elf` first; bundle only if measurements justify it |

## Boundary rule

Placement must not be used as an excuse to blur module boundaries.

Resident functionality should still have:

- one clear owner;
- a small internal interface;
- platform-private code below the platform boundary;
- tests appropriate to its semantics;
- no unnecessary expansion of the public application ABI.

Runtime applications should continue to use the public MiniShell ABI rather than
reaching around it to resident internals.
