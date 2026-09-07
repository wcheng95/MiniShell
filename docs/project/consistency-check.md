# MiniShell Architecture Consistency Check

Audit date: 2026-09-07

MiniShell is checked against four project rules: top-down modular design, clean interfaces/dependency direction, one clear owner for each shared resource/state domain, and modules small enough to understand/test independently.

## Current result

Public ABI direction is clean and does not need redesign. The remaining debt is internal housekeeping.

## Ownership map

| Resource/state | MiniShell owner | Backend/provider role |
| --- | --- | --- |
| Application lifecycle | `app_manager` | platform loader prepares/releases native app |
| Public API table | service composition | backend supplies implementation primitives |
| App allocations | Memory service | platform supplies allocator primitives |
| Logical file/directory handles | Filesystem service | platform supplies file primitives |
| MiniShell storage quota | Filesystem service/resource policy | platform supplies storage below logical roots |
| UTC/effective location | Time/Location service | platform supplies UTC/location persistence |
| Logical input queue | Input service | terminal/device parser produces normalized events |
| Logical display semantics | Display service | platform renders text/display operations |
| Audio stream lifecycle | Audio service | provider supplies file/device frame transport |
| Audio channel meaning | application/source profile | provider preserves channel order only |
| Runtime app loading | app manager + private loader contract | Linux uses `.so`/`dlopen()` |

## Housekeeping debt

### H1 — split the Linux backend

`platform/linux/linux_backend.c` mixes filesystem, memory, UTC/location, terminal display/input, app loading, and bootstrap responsibilities.

Status: **active housekeeping target**.

### H2 — remove POSIX details from portable core

The shell currently interprets POSIX-style loader errors. Application launch should return platform-neutral internal MiniShell loader results, with POSIX translation kept inside the Linux backend. Terminal ownership should also be explicit.

Status: **active housekeeping target**.

### H3 — split Filesystem service internals

`filesystem_service.c` is correctly the single service owner but mixes path normalization, handle bookkeeping, quota accounting, namespace operations, directory iteration, and space reporting. Move private helpers into focused internal modules without changing ownership or public ABI.

Status: **active housekeeping target**.

### H4 — legacy Tab5 tree

**RESOLVED.** Historical pre-Linux work is preserved on `archive/tab5-legacy`; obsolete active files were removed from `main`.

### H5 — Linux terminal escape parser robustness

ANSI/CSI sequences split across separate `read()` chunks can still be misinterpreted.

Status: **deferred until H1-H3 are complete; then evaluate implementation cost**.

## Module-size rule

There is no rigid source-line limit. Split a module when understanding one responsibility requires understanding several unrelated subsystems, tests naturally separate into unrelated groups, or unrelated changes repeatedly collide in one file.

## Gate for future work

Before adding a service or major feature, ask:

```text
1. What application behavior requires it?
2. Who owns the state/resource?
3. Is the public concept platform-independent?
4. Can the existing ABI express it?
5. Is platform-specific implementation below the private boundary?
6. Can it be tested independently?
7. Is an existing module due for decomposition first?
```

MiniFT8 DSP/live-radio expansion resumes after the active housekeeping targets are paid and the retained test suite remains green.
