# MiniFT8-V3 Architecture

## 1. Invariant

MiniFT8-V3 depends on MiniShell concepts, never directly on a platform implementation.

```text
MiniFT8 application
        |
        v
MiniShell ABI
        |
        v
MiniShell service semantics
        |
        v
platform backend
```

Linux is the current reference implementation, but Linux behavior must not leak into the application core.

## 2. Control-hub model

`app_controller` is the application-level coordinator. Other logical MiniFT8 modules do not coordinate one another behind its back.

```text
                    app_controller
                    /     |      \
                   v      v       v
             ui_shell  config  qso_scheduler
                   |
                   v
          explicit AppAction / UiModel

app_controller
     |
     v
storage_service
     |
     v
MiniShell Filesystem ABI
```

The application edge translates between MiniShell Display/Input and MiniFT8 `UiFrame`/`UiInput`.

## 3. Current modules and ownership

| Module | Owns |
| --- | --- |
| `minift8_main` | application lifecycle/orchestration loop |
| `minishell_ui_adapter` | translation between MiniShell Display/Input and MiniFT8 UI types |
| `app_controller` | active/requested mode and cross-module sequencing |
| `ui_shell` | Screen/Submenu navigation, UI rendering, UI-local selection state |
| `config_service` | parsed/persisted MiniFT8 configuration values |
| `qso_scheduler` | current scheduler-owned prototype settings (`Skip TX1`, `Max Retry`) |
| `storage_service` | MiniFT8 file policy and safe text persistence |

MiniShell remains the owner of application-visible filesystem handles, display semantics, input events, time, memory, and future platform resources.

## 4. Current data flow

Input:

```text
MiniShell Input
    -> minishell_ui_adapter
    -> UiInput
    -> ui_shell
    -> AppAction
    -> app_controller
    -> owning MiniFT8 module
```

Presentation:

```text
owning MiniFT8 modules
    -> app_controller
    -> UiModel
    -> ui_shell
    -> UiFrame
    -> minishell_ui_adapter
    -> MiniShell Display
```

Persistence:

```text
config change
    -> app_controller
    -> config_service serialize
    -> storage_service
    -> MiniShell Filesystem
```

## 5. Storage boundary

`storage_service` does not own the filesystem. It owns MiniFT8 file policy:

- MiniFT8 data directory;
- Station configuration naming;
- complete text reads/writes;
- safe temporary-file save sequence.

MiniShell Filesystem owns:

- logical paths;
- file handles;
- namespace behavior;
- quota policy;
- backend/native file operations.

This distinction is important: `/flash/MiniFT8/Station.txt` is MiniFT8 policy; how `/flash` maps to Linux, NuttX, FATFS, or another backend is MiniShell policy.

## 6. UI boundary

The UI uses a fixed logical frame for this milestone:

```text
30 columns x 8 rows
```

`ui_shell` does not know terminal dimensions, ANSI sequences, ncurses, touch hardware, or keyboard scan codes. It sees only `UiInput` and produces `UiFrame`.

The adapter requires MiniShell text Display >= 30x8 and key Input. Unsupported platforms fail cleanly at application startup rather than bypassing MiniShell.

## 7. Mode ownership

`Mode` means operating protocol/application behavior:

```text
FT8 / FT4 / RTTY / CW / future
```

Current canonical mode is owned by `app_controller`. `ui_shell` receives it through `UiModel`; it does not own duplicate application mode state.

Future mode-specific engines remain subordinate to the same controller. FT8-specific DSP belongs in `ft8_engine`, not in Display, Audio, Radio, or `ui_shell`.

## 8. Future service boundary

The next real vertical slice is expected to establish:

```text
QMX / file audio
      |
MiniShell Audio ABI
      |
app_controller
      |
ft8_engine
      |
UiModel -> decoded RX text
```

Radio/CAT should be added only when a real control requirement appears. The same rule applies to logging, GPS-specific behavior, power, and other capabilities.

## 9. Design rules

1. Start from required MiniFT8 behavior, then ask whether the existing MiniShell ABI expresses it.
2. Add a MiniShell primitive only when it is generally reusable and justified by a real application requirement.
3. Keep one authoritative owner for mutable state/resource policy.
4. Keep platform implementation below MiniShell.
5. Prefer synchronous explicit calls until concurrency is actually required.
6. Keep modules small enough to explain and test independently.
7. Reuse proven V2 code for behavior/algorithms, but do not inherit old structural coupling automatically.
8. C/C++ choice remains pragmatic; boundaries and ownership matter more than language purity.
