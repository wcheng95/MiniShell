# Mini-CW -> MiniShell migration

Reference Mini-CW repository:

```text
repository: wcheng95/Mini-CW
commit:     3bfbf169b7c2d49a1be3e9a4c80f945edb32033e
release:    MiniCW V1.2 behavior is the current hardware golden reference
```

## Goal

Run Mini-CW as a MiniShell application while preserving Mini-CW behavior and
service structure.

The migration direction is:

```text
Mini-CW behavior/domain services
        stay
        |
replace direct board/ESP-IDF ownership
        |
        v
MiniShell application APIs
        |
        v
Linux / Cardputer ADV providers
```

Do not rewrite Mini-CW to resemble the existing MiniShell Keyer application.

## Keep as Mini-CW application/domain code

Preserve these ownership concepts and behavior unless a later task explicitly
changes them:

- `app_core`
- `keyer_service` / decoder
- `ui_service` / `ui_screen`
- GPS parsing/policy
- storage policy and file formats
- CW/audio scheduling semantics above the platform transport boundary

## Replace platform ownership

Direct Mini-CW platform code must eventually disappear from the application:

```text
board_cardputer_adv/
ui_cardputer_port
platform_hal
raw GPIO
raw UART
raw FATFS/dirent
TinyUSB/MSC platform calls
ESP-IDF/FreeRTOS task/synchronization ownership
M5Cardputer/M5Unified direct application access
```

Target MiniShell mapping:

| Mini-CW responsibility | MiniShell boundary |
| --- | --- |
| display | Display |
| keyboard | Input |
| paddle/KeyOut GPIO | Digital I/O |
| files/profile/logs | Filesystem |
| GPS UART | Serial |
| UTC/RTC/location | Time/Location |
| application allocations | Memory |
| speaker transport | Audio |

## Audio rule

Mini-CW V1.2 is the known-good Cardputer ADV audio reference:

- paddle is clean;
- automatic M1 is clean;
- no observed pop.

Future MiniShell audio work must preserve the actual Mini-CW scheduling/continuous
stream behavior rather than merely approximate it.

Do not treat the failed T039 experiments as the reference implementation.

## Migration stages

### T042 — Keyer-mode platform skeleton

Port Mini-CW Keyer mode and UI state onto existing MiniShell services with audio
temporarily disabled.

Proves:

- external `minicw.elf` packaging;
- Mini-CW service/state architecture under MiniShell;
- Display/Input adaptation;
- paddle/KeyOut via Digital I/O;
- monotonic timing via Time/Location;
- no direct ESP-IDF/FreeRTOS/board imports in the external ELF.

### T043 — known-good continuous audio — COMPLETE

The pinned Mini-CW V1.2 continuous-audio architecture is now ported below a
generic MiniShell Tone capability. Cardputer ADV hardware validation passed:
manual paddle and automatic M1 are both clean and pop-free with live UI, matching
the standalone Mini-CW reference.

### T044 — persistence + system time

Move Mini-CW Keyer/profile storage policy onto MiniShell Filesystem.
Map RTC/UTC needs to MiniShell Time/Location.

Battery and sleep are not Mini-CW application responsibilities after migration.
Use MiniShell's resident/internal `batt` and `sleep` facilities instead.

### T045 — GPS

Run Mini-CW GPS parser/policy over MiniShell Serial. Preserve baud detection,
fix/grid behavior and storage updates.

### T046 — Keyer application parity / cleanup

Complete Mini-CW Keyer-mode parity under MiniShell:

- Keyer UI and controls;
- M1-M5 and repeat;
- clean paddle and automatic CW audio;
- persistence;
- optional GPS-derived behavior from T045 where applicable;
- repeated launch/exit and resource cleanup.

Trainer modes are intentionally not migrated:

- `cw_trainer_service`
- `cw_lesson_mode`
- `cw_word_mode`
- `cw_callsign_mode`
- `cw_plaintext_mode`

USB MSC is also not a Mini-CW application responsibility after migration.
MiniShell owns removable-storage/export workflows separately.

## Packaging target

Final Cardputer ADV application:

```text
/flash/apps/minicw.elf
or
/sd/apps/minicw.elf
```

Preferred resident import set:

```text
mini_api_get
```

No ESP-IDF, FreeRTOS, M5*, FATFS, UART, GPIO or board symbols may be imported by
the external application.

## Coexistence

During migration:

- existing `keyer.elf` remains available;
- MiniFT8 is untouched;
- Mini-CW standalone firmware remains the golden reference;
- `minicw` is a separate application name.

Retiring or replacing the existing Keyer is a later product decision after
Mini-CW **Keyer-mode** parity and hardware acceptance. Full standalone Mini-CW
feature parity is not a migration goal.


## Explicitly out of scope after migration

The following standalone-firmware responsibilities do not move into `minicw`:

```text
battery display/control
deep sleep
USB MSC mode
CW trainer modes
lesson mode
word mode
callsign mode
plaintext mode
```

MiniShell owns system-level utilities such as battery/sleep. Trainer features are
not required for the MiniShell CW application.
