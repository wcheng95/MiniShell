# MiniShell Applications

This directory contains runtime-loaded applications that are intentionally kept
outside resident MiniShell.

Placement rule:

- resident MiniShell owns runtime, recovery, hardware, and bootstrap facilities;
- ordinary user commands and tools belong here as independently built `.elf`
  applications.

Each application should use only the public MiniShell ABI under `include/minishell`
unless it is explicitly documented as a trusted platform-specific application.

MiniShell uses familiar Linux command names when the behavior is close enough to
be unsurprising, but implements only the minimum useful subset rather than trying
to reproduce full GNU/Linux userland behavior.

Current applications:

```text
cat     simple text-file display utility
nano    interactive text editor
cp      binary-safe file copy utility (Task 5 active)
```

Near-term planned applications:

```text
mv
rm
mkdir
rmdir
free
date
df
```

Applications are built separately from resident MiniShell and copied or uploaded
to `/sd/apps/<name>.elf`.

See [`../docs/command-roadmap.md`](../docs/command-roadmap.md) for the command
selection, naming, placement, and staging policy.
