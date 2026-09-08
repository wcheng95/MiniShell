# MiniShell Architecture Consistency Check

Audit date: 2026-09-07

Public ABI direction is clean. The housekeeping debts identified by the Linux architecture audit have now been reviewed as follows.

## Debt status

- **H1 — resolved:** `platform/linux/linux_backend.c` is now a small composition/bootstrap module. Linux filesystem, time/location, terminal display/input + termios handoff, runtime loading, and common path/error helpers live in focused private modules.
- **H2 — resolved:** portable core no longer interprets POSIX `errno` values for app launch. The private platform boundary exposes MiniShell-internal launch results, while Linux translates its own `errno`/`dlopen()` behavior. Core shell argument splitting also no longer depends on `strtok_r`.
- **H3 — resolved:** `filesystem_service.c` remains the single Filesystem policy/API owner, while private path normalization, logical-handle/generation bookkeeping, and quota/usage scanning are isolated in focused internal modules.
- **H4 — resolved:** legacy Tab5 active tree removed; history retained on `archive/tab5-legacy`.
- **H5 — evaluated:** terminal input parsing should preserve incomplete ANSI/CSI and UTF-8 sequences across reads. The work is localized to the Linux terminal backend and requires no public ABI or resident Input-service change.

## H5 cost assessment

Implementation cost is **low-to-moderate** after H1 because terminal ownership is now isolated.

Recommended shape:

```text
linux_terminal.c             transport/poll/termios ownership
linux_terminal_parser.c      small pure byte-stream state machine
linux_terminal_parser.h      private parser state/feed/flush interface
focused parser tests         deterministic split-boundary cases
PTY integration test         verifies real split writes through MiniShell
```

The parser needs only a small pending-byte buffer plus state/deadline handling. It should cover:

- CSI arrows/Home/End split at any byte boundary;
- Insert/Delete/PageUp/PageDown sequences split at any byte boundary;
- UTF-8 code points split across reads;
- standalone Escape without turning it permanently into a sequence prefix;
- parser-state reset on input flush/application handoff.

The only subtle policy is standalone `Escape`: after receiving an incomplete leading ESC, the terminal backend needs a short ambiguity deadline. More bytes arriving before that deadline continue the sequence; expiry emits a normal Escape key. Nonblocking callers can retain pending state across polls and flush it when the deadline expires.

This is an internal Linux robustness improvement: no MiniShell ABI growth, no application changes, and no ownership redesign.

## Ownership reminders

Applications depend only on MiniShell public services. Resident services own logical resource policy/lifecycle. Platform backends/providers own OS/device/file primitives. The Linux terminal module now clearly owns terminal rendering/input transport and termios app handoff. Audio channel meaning remains application/source-profile state, not MiniShell state.

## Verification

PR #13 (`Housekeeping: pay down H1-H3 architecture debt`) passed the full Linux integration suite and strict unit suite before merge. Public MiniShell ABI behavior was intentionally unchanged.

## Current gate

H1-H3 no longer block MiniFT8. H5 is small enough to implement without architectural risk, but it is a robustness improvement rather than a prerequisite for deterministic FT8 audio/DSP work.
