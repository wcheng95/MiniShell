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
- `cw_trainer_service` and lesson/word/callsign/plaintext modes
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
| battery/deep sleep | Power capability to be added |
| USB MSC enable/disable | storage/system capability to be added if still required |

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

### T043 — known-good continuous audio

Port the Mini-CW V1.2 continuous audio architecture to the MiniShell Audio
boundary. This is a hardware-critical task and must compare actual task,
segment, codec, I2S and DMA behavior against the pinned reference.

Acceptance requires clean paddle and automatic M1 with live UI.

### T044 — persistence + system time/power

Move Mini-CW profile/keyer/trainer storage policy onto MiniShell Filesystem.
Map RTC/UTC to Time/Location. Add the minimal generic Power API needed for battery
percentage and deep sleep.

### T045 — GPS

Run Mini-CW GPS parser/policy over MiniShell Serial. Preserve baud detection,
fix/grid behavior and storage updates.

### T046 — trainer modes

Enable Lessons, Words, Callsigns and Plaintext with preserved Mini-CW behavior,
random generation semantics and storage.

### T047 — USB storage / full parity

Add only the minimal MiniShell capability required for Mini-CW USB-drive mode,
then perform full Mini-CW V1.2 behavior parity validation.

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

Retiring or replacing the existing Keyer is a later product decision after full
Mini-CW parity and hardware acceptance.
