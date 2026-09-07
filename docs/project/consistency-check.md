# MiniShell Architecture Consistency Check

Audit date: 2026-09-07

Public ABI direction is clean; current debt is internal.

## Debt

- **H1 — pay now:** split `platform/linux/linux_backend.c` by responsibility without changing public ABI/private service-port semantics.
- **H2 — pay now:** remove POSIX loader result meanings from portable core; keep POSIX translation inside Linux and clarify terminal ownership.
- **H3 — pay now:** split Filesystem private path/handle/quota/namespace helpers while retaining one Filesystem service owner.
- **H4 — resolved:** legacy Tab5 active tree removed; history retained on `archive/tab5-legacy`.
- **H5 — evaluate after H1-H3:** make ANSI/CSI parsing robust when escape sequences split across reads.

## Ownership reminders

Applications depend only on MiniShell public services. Services own logical resources/lifecycle. Platform backends/providers own OS/device/file primitives. Audio channel meaning remains application/source-profile state, not MiniShell state.

## Gate

MiniFT8 DSP/live-radio expansion resumes after H1-H3 are paid and all existing tests remain green.
