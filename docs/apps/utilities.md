# Portable Utility Applications

This document covers the small general-purpose MiniShell applications that share the same portability model and do not need separate architecture documents.

All of these applications:

- run as ordinary MiniShell applications rather than resident shell commands;
- use only `include/minishell/api.h`;
- use absolute MiniShell logical paths such as `/sd/file.txt` and `/flash/config.txt`;
- return to `M$>` when complete;
- are rebuilt for each target while keeping their application-facing source platform-independent.

## Current utilities

| App | Purpose | Primary MiniShell service |
| --- | --- | --- |
| `hello` | minimal runtime/API smoke application | System |
| `cat` | print a text/binary file to System output | Filesystem + System |
| `cp` | copy one regular file | Filesystem |
| `date` | show or re-anchor MiniShell UTC | Time/Location |
| `df` | show MiniShell-visible storage usage | Filesystem |
| `free` | show MiniShell-visible memory usage | Memory |
| `ls` | enumerate a directory | Filesystem |
| `mkdir` | create one directory | Filesystem |
| `mv` | rename/move one regular file, replacing an existing regular-file destination | Filesystem |
| `rm` | remove one regular file | Filesystem |
| `rmdir` | remove one empty directory | Filesystem |

## Design role

These utilities are useful because they exercise the same public API used by larger applications. They are intentionally not privileged shortcuts inside the shell.

Examples:

```text
ls      -> dir_open / dir_read / dir_close
free    -> memory.get_info
df      -> filesystem.space
date    -> time_location.utc_get / utc_set
mv      -> filesystem.rename replacement semantics
```

The replacement behavior of `rename()` is also used by MiniFT8 for safe configuration saves, showing why utility and domain applications should consume the same general-purpose primitives.

A new utility should remain small and should not cause API growth unless it exposes a capability that is independently useful to real applications.

## Scope

MiniShell does not aim to reproduce GNU coreutils. Features such as recursive copy/remove, permissions, ownership, wildcards, pipes, and a working-directory model are added only when a concrete MiniShell application requirement justifies them.
