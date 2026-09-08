# MiniShell Architecture Consistency Check

Audit date: 2026-09-07

Public ABI direction is clean. The housekeeping debts identified by the Linux architecture audit are now resolved.

## Debt status

- **H1 — resolved:** `platform/linux/linux_backend.c` is now a small composition/bootstrap module. Linux filesystem, time/location, terminal display/input + termios handoff, runtime loading, and common path/error helpers live in focused private modules.
- **H2 — resolved:** portable core no longer interprets POSIX `errno` values for app launch. The private platform boundary exposes MiniShell-internal launch results, while Linux translates its own `errno`/`dlopen()` behavior. Core shell argument splitting also no longer depends on `strtok_r`.
- **H3 — resolved:** `filesystem_service.c` remains the single Filesystem policy/API owner, while private path normalization, logical-handle/generation bookkeeping, and quota/usage scanning are isolated in focused internal modules.
- **H4 — resolved:** legacy Tab5 active tree removed; history retained on `archive/tab5-legacy`.
- **H5 — resolved:** Linux terminal input now preserves incomplete ANSI/CSI and UTF-8 sequences across transport reads using a private byte-stream parser.

## H5 implementation

Terminal ownership remains unchanged:

```text
linux_terminal.c             poll/read/termios/display ownership
linux_terminal_parser.c      pure byte-stream parser state machine
linux_terminal_parser.h      private parser state/feed/reset/flush interface
```

The parser preserves incomplete sequences across reads and handles:

- CSI arrows/Home/End split at any byte boundary;
- Insert/Delete/PageUp/PageDown split at any byte boundary;
- UTF-8 code points split across reads;
- malformed UTF-8 without swallowing a following valid ASCII byte;
- parser-state reset on input flush/application handoff.

Standalone Escape uses a **30 ms Linux-backend ambiguity window**. An incomplete ESC/CSI prefix is held briefly; continuation bytes arriving inside that window complete the sequence, while expiry emits a normal Escape key. A timed-out partial CSI prefix replays its non-ESC suffix as normal input rather than silently dropping bytes.

This remains entirely private to the Linux backend: no public MiniShell ABI growth, no application changes, and no Input-service ownership change.

## Ownership reminders

Applications depend only on MiniShell public services. Resident services own logical resource policy/lifecycle. Platform backends/providers own OS/device/file primitives. The Linux terminal module owns terminal rendering/input transport and termios app handoff. Audio channel meaning remains application/source-profile state, not MiniShell state.

## Verification

- PR #13 (`Housekeeping: pay down H1-H3 architecture debt`) passed the full Linux integration suite and strict unit suite before merge.
- H5 adds `linux_terminal_parser_unit`, which deterministically checks every split boundary for supported CSI sequences plus UTF-8, standalone Escape, partial-CSI flush, malformed input recovery, and reset behavior.
- `linux_input` now also exercises split CSI, split UTF-8, and standalone Escape through a real PTY.
- The complete Linux workflow remained green after H5.

## Current gate

The H1-H5 architecture-audit debt is paid. No known housekeeping item from this audit blocks deterministic MiniFT8 Audio -> DSP work.
