# MiniShell Applications

This directory contains runtime-loaded applications that are intentionally kept
outside resident MiniShell.

Placement rule:

- resident MiniShell owns runtime, recovery, hardware, and bootstrap facilities;
- ordinary user commands and tools belong here as independently built `.elf`
  applications.

Each application should use only the public MiniShell ABI under `include/minishell`
unless it is explicitly documented as a trusted platform-specific application.

Current applications:

```text
cat    simple text-file display utility
```

Applications are built separately from resident MiniShell and copied or uploaded
to `/sd/apps/<name>.elf`.
