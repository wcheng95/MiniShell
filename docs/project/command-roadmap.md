# MiniShell Command Roadmap

This document records shell/application command placement and priorities; it does not define the public ABI.

## Current resident shell

```text
help
status
apps
run <app> [...]
<app> [...]
exit
```

Resident commands should remain limited to MiniShell lifecycle/discovery/platform control.

## Portable applications

Current portable/domain apps include MiniFT8 and the file/system utilities (`cat`, `cp`, `date`, `df`, `free`, `ls`, `mkdir`, `mv`, `nano`, `rm`, `rmdir`).

Prefer a portable app whenever behavior can be expressed through public MiniShell services. Prefer a resident command only when it fundamentally controls MiniShell itself or an intentionally platform-specific operation.

Audio and Control are services, not resident commands. Diagnostic tools, if needed later, should normally be small ordinary apps rather than shell growth.

## Housekeeping gate

Before expanding commands or MiniFT8 DSP/radio work:

```text
split Linux backend
remove POSIX loader semantics from portable core
split Filesystem private helpers
then evaluate stateful ANSI/CSI parser cost
```

New ABI work remains application-driven.
