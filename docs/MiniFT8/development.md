# MiniFT8-V3 Development

## Current phase

MiniFT8-V3 is now developed as a MiniShell application rather than as a standalone Linux/ESP-IDF application with its own platform layer.

Current milestone:

```text
MiniShell launch
    -> MiniFT8 30x8 UI
    -> UI actions
    -> config/scheduler state
    -> MiniShell Filesystem persistence
    -> clean app exit
    -> M$>
```

This deliberately proves lifecycle, ownership, UI, and persistence before adding DSP or radio complexity.

## Tests

Two MiniFT8-specific tests are part of the MiniShell reference suite.

### `minift8_ui_smoke`

Pure UI-state test. It drives `UiInput` directly and checks `UiFrame`/`AppAction` behavior without a platform or MiniShell backend.

It covers:

- initial RX frame;
- O screen structure;
- mode action generation;
- TX submenu and scheduler controls;
- S screen grouping;
- V/System platform-neutral presentation.

### `linux_minift8`

PTY integration test through the real MiniShell runtime:

```text
M$> MiniFT8
    -> O
    -> FT8 -> FT4
    -> TX submenu
    -> Skip TX1 ON
    -> q
M$>
```

The test then verifies:

- `/flash/MiniFT8/Station.txt` exists;
- mode and Skip TX1 were persisted;
- `Station.txt.tmp` is absent after save;
- relaunch loads FT4/Skip TX1 state;
- exit returns to the MiniShell prompt.

## Development rule

For each new capability:

```text
1. define required MiniFT8 behavior
2. identify the MiniFT8 owner
3. check existing MiniShell ABI
4. if sufficient, use it
5. if insufficient, define the smallest reusable MiniShell primitive
6. add MiniShell service/unit tests
7. add MiniFT8 module/integration tests
8. keep platform implementation below MiniShell
```

Do not add broad generic services speculatively.

## Next major vertical slice

The intended next application-driven slice is live/replayed FT8 RX:

```text
QMX live 48 kHz audio or deterministic WAV source
        -> MiniShell Audio ABI/provider
        -> app_controller
        -> ft8_engine
        -> decoded messages
        -> UiModel
        -> RX screen
```

The exact Audio ABI should be designed from this flow before implementation. No Radio/CAT API is required merely to decode audio, so Radio should wait until its first concrete requirement.

## Timing direction

For framed digital audio modes:

- authoritative UTC identifies the slot/frame boundary;
- audio sample count is the preferred progression clock inside the slot;
- OS scheduling/ticks are execution mechanics, not protocol timing.

For deterministic WAV replay, replay-start UTC plus samples consumed can provide deterministic virtual UTC progression.

## Migration policy

Mini-FT8 V2 remains a behavioral and algorithmic reference. Proven code may be migrated, but every reused block must fit the current MiniFT8/MiniShell ownership boundaries.

Especially review `ft8_engine`/ft8_lib structure rather than treating old source-file boundaries as architectural requirements.

## Focused code-reading targets

For the current integrated slice, the most useful files to understand are:

1. `apps/MiniFT8/main/minift8_main.c` — application lifecycle and orchestration boundary.
2. `apps/MiniFT8/main/minishell_ui_adapter.c` — how platform-independent MiniFT8 UI types meet the MiniShell ABI.
3. `apps/MiniFT8/src/storage_service/storage_service.c` — distinction between MiniFT8 file policy and MiniShell filesystem ownership.
