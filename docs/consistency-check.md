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

There is no reason to redesign the public ABI because of these findings. The debt is internal housekeeping.

## Active dependency direction

The current root CMake build follows:

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

`core/main.c` is a small composition/orchestration entry point. `core/app_manager.c` owns the foreground application lifecycle. `core/minishell_services/services.c` composes the public service table and coordinates per-app service lifecycle.

## Ownership map

| Resource/state | MiniShell owner | Backend/provider role |
| --- | --- | --- |
| Application lifecycle | `app_manager` | platform loader prepares/releases native app |
| Public API table | service composition | backend supplies implementation primitives only |
| App allocations | Memory service | Linux supplies malloc/realloc/free |
| Logical file handles | Filesystem service | Linux supplies POSIX file operations |
| Logical directory handles | Filesystem service | Linux supplies POSIX directory operations |
| MiniShell storage quota | Filesystem service/resource policy | Linux supplies files below logical root |
| UTC anchor and effective location | Time/Location service | Linux supplies startup UTC and location persistence |
| Logical input queue | Input service | Linux terminal parser produces normalized events |
| Logical display semantics | Display service | Linux maps text operations to terminal/ANSI behavior |
| Runtime app discovery/loading | app manager + private platform loader contract | Linux uses `.so`/`dlopen()` |

This is the intended meaning of one-owner on Linux: Linux may physically own hardware/resources, but applications have one MiniShell gateway and one MiniShell module owns the application-visible semantics.

## Good examples

### Portable applications

The current utilities use the public ABI rather than POSIX. `ls` uses Filesystem directory iteration; `df` uses Filesystem `space()`; `free` uses Memory `get_info()`; `date` uses Time/Location.

`nano` is a good modular example:

```text
nano.c          orchestration/input policy
nano_buffer.c   text-buffer state and editing
nano_file.c     persistence through Filesystem ABI
nano_ui.c       rendering through Display ABI
nano_util.c     small string helpers
```

No nano module owns platform terminal or filesystem implementation details.

### Portable service ownership

Memory owns allocation bookkeeping and cleanup. Filesystem owns logical handles/path rules/quota semantics. Input owns the normalized event queue. This prevents backends from leaking platform ownership into applications.

## Housekeeping debt

### H1 — split the Linux backend

`platform/linux/linux_backend.c` is approximately 34 KB and currently contains several independent responsibilities:

```text
filesystem primitives
memory primitives
UTC/location persistence
display/ANSI rendering
terminal input parsing + termios handoff
application discovery/loading
platform path/bootstrap logic
```

This violates the project's understandability goal even though the private interface is clean.

Preferred decomposition, without changing the public ABI:

```text
linux_platform.c       composition/init
linux_memory.c         allocator primitives
linux_fs.c             POSIX filesystem primitives
linux_time.c           monotonic/UTC/location persistence
linux_terminal.c       terminal display/input + foreground handoff
linux_app_loader.c     discovery/dlopen lifecycle
resource_config.c      existing resource policy configuration
platform_info.c        existing identity
```

### H2 — remove POSIX details from portable core

`core/shell.c` currently includes `errno.h` and interprets `-ENOENT` from the private app-loader path. `core/main.c` and the shell also directly use libc stdin/stdout for shell I/O.

This works on the Linux reference target, but the long-term core should not require POSIX error values or Linux-terminal ownership assumptions.

Future cleanup should introduce a small platform-neutral internal loader result and clarify one terminal/shell I/O owner. This is an internal refactor; applications and the public ABI should not change.

### H3 — split Filesystem service internals

`core/minishell_services/filesystem_service.c` is approximately 24 KB. It is conceptually cohesive but contains enough independent mechanisms to make review harder:

```text
path normalization
file/directory handle tables
storage usage/quota accounting
file operations
namespace operations
directory iteration
space reporting
```

Keep one Filesystem service owner, but move private helpers into focused files such as `fs_path.c`, `fs_handles.c`, and `fs_quota.c` when convenient. Do not split ownership merely to reduce line count.

### H4 — dormant pre-Linux source tree

The repository still contains earlier ESP-IDF/Tab5 code (`main/`, old `core/minishell_*` modules, and `platform/minishell_platform_tab5/`) that is not part of the current root Linux build.

Git already preserves history, so this should eventually be removed from the active tree or deliberately archived once we are certain nothing is still needed as a porting reference. Until then it must be treated as historical, not canonical.

### H5 — Linux terminal escape parser robustness

The current Linux key parser handles the tested terminal sequences, but an ANSI/CSI sequence split across separate `read()` chunks can be misinterpreted. A stateful parser should be added before terminal input becomes more demanding.

## Module-size rule

MiniShell deliberately has no rigid source-line limit. The review question is:

> Can one developer explain the module's responsibility, state, inputs/outputs, and failure behavior without also understanding several unrelated subsystems?

Split a module when either:

- it owns several independent domains;
- tests naturally separate into unrelated groups;
- a change in one concern repeatedly risks another;
- the file becomes difficult to read as one conceptual unit.

A large module that has one coherent state machine may remain one module. A smaller file mixing unrelated ownership should still be split.

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
