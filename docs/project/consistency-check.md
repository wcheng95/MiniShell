# MiniShell Architecture Consistency Check

Audit date: 2026-09-07

Public ABI direction is clean. The housekeeping debts identified by the Linux architecture audit have now been reviewed as follows.

## Debt status

- **H1 — resolved:** `platform/linux/linux_backend.c` is now a small composition/bootstrap module. Linux filesystem, time/location, terminal display/input + termios handoff, runtime loading, and common path/error helpers live in focused private modules.
- **H2 — resolved:** portable core no longer interprets POSIX `errno` values for app launch. The private platform boundary exposes MiniShell-internal launch results, while Linux translates its own `errno`/`dlopen()` behavior. Core shell argument splitting also no longer depends on `strtok_r`.
- **H3 — resolved:** `filesystem_service.c` remains the single Filesystem policy/API owner, while private path normalization, logical-handle/generation bookkeeping, and quota/usage scanning are isolated in focused internal modules.
- **H4 — resolved:** legacy Tab5 active tree removed; history retained on `archive/tab5-legacy`.
- **H5 — cost evaluation next:** make terminal input parsing robust when ANSI/CSI (and potentially UTF-8) sequences are split across separate reads.

## Ownership reminders

Applications depend only on MiniShell public services. Resident services own logical resource policy/lifecycle. Platform backends/providers own OS/device/file primitives. The Linux terminal module now clearly owns terminal rendering/input transport and termios app handoff. Audio channel meaning remains application/source-profile state, not MiniShell state.

## Verification

PR #13 (`Housekeeping: pay down H1-H3 architecture debt`) passed the full Linux integration suite and strict unit suite before merge. Public MiniShell ABI behavior was intentionally unchanged.

## Current gate

H1-H3 no longer block MiniFT8. Evaluate H5 implementation cost before deciding whether to pay it immediately or resume deterministic MiniFT8 DSP replay first.
