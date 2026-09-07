# MiniShell Architecture Consistency Check

Audit date: 2026-09-07

Public ABI direction is clean. Remaining debt is internal.

## Ownership map

| Resource/state | MiniShell owner | Backend/provider role |
| --- | --- | --- |
| App lifecycle | `app_manager` | platform loader |
| API table | service composition | backend primitives |
| Allocations | Memory service | allocator |
| File/directory handles + quota | Filesystem service | file primitives/storage |
| UTC/location | Time/Location service | clock/persistence |
| Input queue | Input service | event producer/parser |
| Display semantics | Display service | renderer |
| Audio stream lifecycle | Audio service | file/device transport provider |
| Audio channel meaning | application/source profile | preserve order only |
| Runtime app loading | app manager/private loader contract | Linux `.so`/`dlopen()` |

## Active debt

### H1 — split Linux backend

`platform/linux/linux_backend.c` mixes filesystem, memory, time/location, terminal display/input, app loading, and bootstrap.

Status: **pay now**.

### H2 — remove POSIX details from portable core

Shell/application launch should use platform-neutral internal loader results; POSIX `errno` meanings belong inside Linux. Clarify terminal ownership while splitting the backend.

Status: **pay now**.

### H3 — split Filesystem private helpers

Keep one Filesystem service owner, but move path/handle/quota/namespace helper concerns into focused internal modules.

Status: **pay now**.

### H4 — legacy Tab5 tree

**Resolved.** Historical code is on `archive/tab5-legacy`.

### H5 — split-read ANSI/CSI parser

The current parser can misinterpret an escape sequence split across reads.

Status: **evaluate cost after H1-H3**.

## Gate

MiniFT8 DSP/live-radio expansion resumes after H1-H3 are paid and all retained tests remain green.
