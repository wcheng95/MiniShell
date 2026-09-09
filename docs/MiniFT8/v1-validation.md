# V1 Cross-Backend/Profile Validation

## Status

**COMPLETE**

V1 is the checkpoint that validates MiniFT8 across two MiniShell backends and two application presentation profiles before RX-1B resumes.

The goal is not pixel-identical output or identical backend implementation. The goal is that the same MiniFT8 application logic behaves consistently above the MiniShell API while each backend owns its platform details below that boundary.

## Required matrix

| MiniShell backend | MiniFT8 presentation | Result |
| --- | --- | --- |
| Linux | DESKTOP | PASS |
| Linux | ADV | PASS |
| ADV | ADV | PASS |
| ADV | DESKTOP | not required |

## Evidence

### Linux + DESKTOP

Automated `linux_ft8` integration coverage verifies:

- normal `ft8` launch defaults to DESKTOP on Linux;
- foreground application launch/exit returns cleanly to `M$>`;
- O-screen navigation and configuration mutation work;
- `Skip TX1` persists through `/flash/ft8/station.txt`;
- obsolete protocol-mode state is not persisted;
- presentation identity is not persisted in station configuration;
- explicit `ft8 --profile desktop` reports `Presentation: DESKTOP`.

`ft8_ui_smoke` verifies the DESKTOP presentation contract as 30 columns x 8 rows with a contextual footer.

### Linux + ADV

The same Linux `ft8.so` is launched with:

```text
M$> ft8 --profile adv
```

Automated coverage verifies:

- the same persisted MiniFT8 configuration is observed;
- the same O-screen navigation is used;
- the same `O -> 5 -> 3` logical input sequence produces `APP_ACTION_SET_SKIP_TX1`;
- System Info reports `Presentation: ADV` and `UI: text 20x7`;
- clean foreground exit returns to `M$>`.

`ft8_ui_smoke` independently verifies the ADV presentation contract as 20 columns x 7 rows with no footer.

### ADV + ADV

Real Cardputer ADV hardware validation verifies:

- `apps` discovers compiled-in runtime app `ft8`;
- `ft8` launches normally using the ADV presentation;
- System Info reports `Presentation: ADV` and `UI: text 20x7`;
- configuration changes survive exit and relaunch through `/flash/ft8/station.txt`;
- `q` returns cleanly to the resident MiniShell prompt;
- repeated `ft8` launch/exit cycles are stable;
- foreground applications run on the MiniShell-managed ADV application task rather than borrowing the resident `app_main` stack.

Observed ADV memory baseline after the dedicated application-task change:

```text
heap free       approximately 282 KiB
largest block   approximately 228 KiB
```

Running and exiting additional `ft8` cycles left these values effectively unchanged, with no meaningful application-memory leak observed.

## Platform-boundary guard

`tests/ft8_platform_boundary.py` scans MiniFT8 C/C++ source/header files and fails CI if `apps/ft8/` introduces direct platform-layer dependencies such as:

- ESP-IDF / ESP headers;
- FreeRTOS or driver headers;
- Linux/POSIX platform headers;
- M5/Cardputer headers;
- private MiniShell backend headers;
- explicit platform-selection macros.

This complements normal compilation: Linux and ADV intentionally compose the same MiniFT8 source modules differently, but application source remains above the MiniShell API.

## Intentional differences

These differences are expected and do not violate V1:

```text
Linux application packaging      runtime ft8.so
ADV application packaging        compiled-in static registry

Linux DESKTOP presentation       30 x 8, footer
ADV presentation                 20 x 7, no footer

Linux backend execution          host process/runtime loader
ADV backend execution            MiniShell-managed FreeRTOS app task

Linux Audio                      WAV/reference provider available
ADV Audio                        not implemented yet
```

Presentation selection is application/composition policy. It is not derived from backend identity inside shared MiniFT8 logic and is not stored in `station.txt`.

## V1 conclusion

The checkpoint demonstrates both directions of the intended architecture:

```text
same MiniFT8 core + different presentation policy
and
same MiniFT8 ADV presentation + different MiniShell backend
```

Therefore the P1/P2/V1 detour is closed. MiniFT8 development may resume at **RX-1B — top-down RX module/interface design**.
