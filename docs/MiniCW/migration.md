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

Do not rewrite Mini-CW to resemble the former MiniShell `apps/keyer` application; that superseded implementation has been removed.

## Keep as Mini-CW application/domain code

Preserve these ownership concepts and behavior unless a later task explicitly
changes them:

- `app_core`
- `keyer_service` / decoder
- `ui_service` / `ui_screen`
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

### T044A — persistence — COMPLETE

Mini-CW Keyer-mode persistence now uses MiniShell Filesystem. Hardware validation passed with settings persistence working and paddle/M1 audio remaining clean. The resident T043 audio path remained unchanged.

### T044B — UTC/time — SUBSUMED BY T045

The final Keyer header reads UTC through MiniShell Time/Location. Mini-CW does
not own RTC/system-clock setting or time persistence.

Battery and sleep are not Mini-CW application responsibilities after migration.
Use MiniShell's resident/internal `batt` and `sleep` facilities instead.

### T045 — Keyer UI / I/O cleanup — COMPLETE

Operation moved to Opt, KeyIn/KeyOut modes were simplified, SK-Both was added, and the fixed `HH:MM KIN KOUT WW Vnn` top line is hardware-validated with clean paddle/M1 audio.

### Post-T045 ownership audit — COMPLETE

The MiniShell / `minicw` boundary passed the final ownership audit. See:

```text
docs/MiniCW/baseline-audit.md
```

The intended Keyer-only migration is complete. GPS and generic "final parity"
work are removed from the roadmap.

Feature follow-ons from the audited baseline are also complete:

- T046 full callsign -> operator-name lookup;
- T047 color UI and 2-pixel separator;
- T048 compact daily transcript logging and safe note mode.

The Mini-CW Keyer-mode application is therefore in operational field shape for
this milestone. Trainer modes remain intentionally out of scope. USB MSC remains
a MiniShell system responsibility. Full standalone Mini-CW feature parity is not
a migration goal.


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
GPS
```

MiniShell owns system-level utilities such as battery/sleep. Trainer features are
not required for the MiniShell CW application.
