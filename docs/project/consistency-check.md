# MiniShell Architecture Consistency Check

Audit date: 2026-09-06

This check reviews the active Linux reference implementation against four project rules:

1. top-down modular design;
2. clean interfaces and dependency direction;
3. one clear owner for each shared resource/state domain;
4. modules small enough to understand and test independently.

## Summary

| Rule | Result | Notes |
| --- | --- | --- |
| Top-down dependency direction | PASS | Portable applications depend only on the public MiniShell ABI. Services depend on the private backend boundary. |
| Clean interfaces | PASS WITH DEBT | Public ABI is clean; a small amount of POSIX terminal/error behavior remains in `core/main.c` and `core/shell.c`. |
| One owner | PASS WITH DEBT | Service ownership is clear. Linux terminal ownership is split somewhat between shell stdio and the Linux backend. |
| Small understandable modules | PASS WITH DEBT | Most modules are small/cohesive; `filesystem_service.c` and especially `linux_backend.c` should be decomposed. |

There is no reason to redesign the public ABI because of these findings. The remaining debt is internal housekeeping.

## Active dependency direction

```text
portable app
    |
    v
include/minishell/api.h
    |
    v
core/minishell_services/*
    |
    v
core/platform_backend.h / minishell_services_port_t
    |
    v
platform/linux/*
    |
    v
POSIX / Linux
```

Applications do not receive POSIX descriptors, `DIR *`, terminal objects, ESP-IDF types, NuttX types, or board-driver handles.

## Ownership map

| Resource/state | MiniShell owner | Backend/provider role |
| --- | --- | --- |
| Application lifecycle | `app_manager` | platform loader prepares/releases native app |
| Public API table | service composition | backend supplies implementation primitives only |
| App allocations | Memory service | Linux supplies malloc/realloc/free |
| Logical file/directory handles | Filesystem service | Linux supplies POSIX primitives |
| MiniShell storage quota | Filesystem service/resource policy | Linux supplies files below logical root |
| UTC anchor and effective location | Time/Location service | Linux supplies startup UTC and location persistence |
| Logical input queue | Input service | Linux terminal parser produces normalized events |
| Logical display semantics | Display service | Linux maps text operations to terminal behavior |
| Runtime app discovery/loading | app manager + private loader contract | Linux uses `.so`/`dlopen()` |

## Good examples

Portable utilities use the public ABI rather than POSIX. `ls` uses Filesystem directory iteration; `df` uses `space()`; `free` uses Memory `get_info()`; `date` uses Time/Location.

`nano` remains a good modular example:

```text
nano.c          orchestration/input policy
nano_buffer.c   text-buffer state and editing
nano_file.c     persistence through Filesystem ABI
nano_ui.c       rendering through Display ABI
nano_util.c     small string helpers
```

## Housekeeping debt

### H1 — split the Linux backend

`platform/linux/linux_backend.c` contains several independent responsibilities: filesystem primitives, memory primitives, UTC/location, display/input terminal handling, app loading, and platform bootstrap. Split it when convenient without changing the public ABI.

### H2 — remove POSIX details from portable core

`core/shell.c` still interprets a POSIX-style loader error and shell/stdin ownership is partly libc-specific. Introduce platform-neutral internal loader results and clarify terminal ownership later.

### H3 — split Filesystem service internals

`core/minishell_services/filesystem_service.c` is conceptually one owner but contains path normalization, handles, quota accounting, namespace operations, directory iteration, and space reporting. Move private helpers into focused files when useful; do not split ownership merely to reduce line count.

### H4 — legacy Tab5 tree: RESOLVED

The dormant pre-Linux Tab5/ESP-IDF source, old milestone docs, ELF tests, transfer/power experiments, and obsolete build/tooling files were removed from active `main`.

The complete pre-cleanup state is preserved in:

```text
archive/tab5-legacy
```

### H5 — Linux terminal escape parser robustness

The current Linux key parser handles the tested terminal sequences, but an ANSI/CSI sequence split across separate `read()` chunks can be misinterpreted. Add parser state before terminal input becomes more demanding.

## Module-size rule

MiniShell has no rigid source-line limit. The review question is:

> Can one developer explain the module's responsibility, state, inputs/outputs, and failure behavior without also understanding several unrelated subsystems?

Split a module when it owns several independent domains, tests naturally separate into unrelated groups, changes repeatedly cross concerns, or the file is difficult to read as one conceptual unit.

## Gate for future work

Before adding a new service or major feature, review:

```text
1. What application behavior requires it?
2. Which module owns the state/resource?
3. Is the public concept platform-independent?
4. Does the app need a new ABI primitive, or can the existing ABI express it?
5. Is the platform-specific implementation below the private boundary?
6. Can the module be tested independently?
7. Is an existing module becoming too broad and due for decomposition first?
```

The next MiniFT8-driven service work should use this checklist.
