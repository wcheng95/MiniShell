# MiniShell Architecture Consistency Check

Audit date: 2026-09-07

The public API direction is clean. The housekeeping debts identified by the Linux architecture audit are resolved, and a final MiniFT8/MiniShell boundary review found no structural blocker in the service/application boundary.

## Debt status

- **H1 — resolved:** `platform/linux/linux_backend.c` is now a small composition/bootstrap module. Linux filesystem, time/location, terminal display/input + termios handoff, runtime loading, and common path/error helpers live in focused private modules.
- **H2 — resolved:** portable core no longer interprets POSIX `errno` values for app launch. The private platform boundary exposes MiniShell-internal launch results, while Linux translates its own `errno`/`dlopen()` behavior. Core shell argument splitting also no longer depends on `strtok_r`.
- **H3 — resolved:** `filesystem_service.c` remains the single Filesystem policy/API owner, while private path normalization, logical-handle/generation bookkeeping, and quota/usage scanning are isolated in focused internal modules.
- **H4 — resolved:** inactive legacy platform code was removed from the maintained tree.
- **H5 — resolved:** Linux terminal input preserves incomplete ANSI/CSI and UTF-8 sequences across transport reads using a private byte-stream parser.

## Final boundary review

The intended dependency direction is:

```text
MiniFT8 domain/application modules
        |
        v
MiniShell public API
        |
        v
portable MiniShell services
        |
        v
private backend/provider hooks
        |
        v
Linux / ESP-IDF / hardware / mocks
```

The review confirmed:

- MiniFT8 has no direct Linux, POSIX, ESP-IDF, USB, ALSA, UART, I2S, GPIO, or board-driver dependency.
- MiniShell-facing MiniFT8 modules may use the MiniShell API directly; MiniShell is the platform abstraction and should not be hidden behind a duplicate generic HAL.
- Pure MiniFT8 domain modules such as `ui_shell`, `qso_scheduler`, and future `ft8_engine` use MiniFT8-owned/standard-C types rather than platform types.
- `app_controller` remains the MiniFT8 domain coordinator; `minift8_main` owns only lifecycle/top-level call sequencing.
- `storage_service` owns MiniFT8 file policy while MiniShell Filesystem owns paths/handles/quota/backend semantics.
- Audio transport and channel meaning are separate: MiniShell transports ordered frames; MiniFT8 source/profile logic decides ordinary audio versus I/Q.
- RX Audio, TX Audio, and Control are independent resources. The old prototype UI `Radio`/single-`Audio Path` vocabulary was removed and replaced by `RX Audio`, `TX Audio`, and `Control`.
- MiniShell Control remains conceptual/not yet implemented; no CAT syntax or FT8 control semantics have entered the public API.
- Mocks/providers remain below MiniShell and cannot bypass the MiniFT8 decoder/scheduler when those components are under test.

No second HAL is recommended inside MiniFT8. Platform-facing application modules are allowed to consume MiniShell services; domain engines should not.

## H5 implementation

Terminal ownership remains unchanged:

```text
linux_terminal.c             poll/read/termios/display ownership
linux_terminal_parser.c      pure byte-stream parser state machine
linux_terminal_parser.h      private parser state/feed/reset/flush interface
```

The parser preserves incomplete sequences across reads and handles CSI keys, UTF-8, malformed-input recovery, application handoff/reset, and standalone Escape. Standalone Escape uses a **30 ms Linux-backend ambiguity window**; the policy remains completely below the Input API.

## Verification

- PR #13 (`Housekeeping: pay down H1-H3 architecture debt`) passed the full Linux integration suite and strict unit suite before merge.
- H5 adds deterministic parser split-boundary tests plus real PTY integration coverage.
- The final boundary cleanup strengthened `minift8_ui_smoke` to assert the independent `RX Audio` / `TX Audio` / `Control` vocabulary.
- The API terminology cleanup renamed `MINISHELL_ABI_VERSION` / `abi_version`, migrated `docs/abi/` to `docs/api/`, and removed the premature append-only compatibility policy.
- The complete Linux integration and platform-neutral unit workflow passes after the API rename.

## Current direction

The maintained backends are Linux and Cardputer ADV.

```text
Linux backend + DESKTOP profile
Linux backend + ADV profile
ADV backend   + ADV profile
```

The Linux + ADV-profile versus ADV + ADV-profile comparison remains the main cross-platform architectural check. MiniFT8 RX integration has since advanced through RX-7, and the architecture cleanup gate C0-C4 is complete.
