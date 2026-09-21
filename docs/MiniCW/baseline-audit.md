# MiniShell + Mini-CW ownership baseline audit

Audit date: 2026-09-21

Audited production implementation:

```text
T045 implementation: a699602f5ad789b3d89fc4059430e41ee80a020f
T045 review head:    73e2aeb646eaa2662d5834f7efd574b1184fb897
```

Hardware acceptance at this point:

```text
persistence       PASS
T045 UI/I/O       PASS
paddle audio      clean / no pop
automatic M1      clean / no pop
Opt Operation     PASS
SKB / SKN/SKM/OFF PASS
fixed UTC header  PASS
```

## Result

**PASS.** The MiniShell / `minicw` ownership boundary is suitable to freeze as
the post-migration baseline.

The migration is complete for the intended Keyer-only scope. Future work is
optional feature work, not migration/parity work.

## Ownership contract

### Mini-CW application owns domain behavior

`minicw.elf` owns:

- Keyer state and CW timing semantics;
- paddle / straight-key interpretation;
- KeyIn / KeyOut user modes;
- automatic text TX, M1-M5, repeat and Tune policy;
- Morse-domain vocabulary and decoding;
- Keyer UI / Operation pages;
- application settings schema, parsing and persistence policy;
- callsign recognition / base-call parsing domain logic;
- future callsign-table and log semantics.

It does **not** own board drivers.

### MiniShell owns platform resources

Mini-CW obtains platform capabilities only through `mini_api_t`.

MiniShell owns:

- Display;
- Input;
- Filesystem and file handles;
- Time/Location;
- Digital I/O handles and electrical mode;
- Audio/Tone handles;
- foreground application lifecycle and cleanup;
- public handle validation and resource reclamation.

MiniShell does not know CW concepts such as dit/dah, WPM, M1, callsigns,
paddle mode or Keyer log format.

### ADV owns hardware implementation

ADV owns:

- Cardputer keyboard/display implementation;
- physical GPIO driver access;
- ES8311 / I2S / DMA;
- continuous Tone worker implementation;
- flash/FAT filesystem backend;
- RTC/I2C;
- ELF loading and executable-memory management.

No ADV/ESP-IDF/FreeRTOS/M5/FATFS symbol is directly imported by `minicw.elf`.

## Application API boundary

`apps/minicw/main/minicw_main.c` enters through:

```c
return minicw_run(mini_api_get());
```

`apps/minicw/src/port/minicw_port.c` is the sole MiniShell API owner inside
the application.

Domain modules do not include `minishell/api.h` directly.

The T045 external ELF inspection reports exactly one resident import:

```text
mini_api_get
```

This is the desired ABI boundary.

## Module boundary

Current application dependency policy:

```text
main
  -> port

app_core
  -> keyer_service
  -> ui_service
  -> audio_service
  -> storage_service
  -> port/runtime

keyer_service
  -> audio_service
  -> port/runtime

ui_service
  -> keyer_service
  -> audio_service
  -> port/runtime

audio_service
  -> port/runtime

storage_service
  -> keyer_service types
  -> port/runtime

port
  -> app_core
  -> MiniShell API
```

`tests/app_dependency_boundary.py` enforces these local dependencies.

`tests/app_platform_boundary.py` additionally rejects:

- platform/ESP/FreeRTOS/M5/native headers;
- native platform calls such as GPIO/I2S/FATFS/POSIX file access;
- direct MiniShell API access outside the port;
- heap allocation in every Mini-CW module.

## Audio ownership

Mini-CW retains CW-domain behavior:

```text
dit / dah duration
hold / release intent
pitch / volume selection
CW pattern lookup
busy-aware Keyer policy
```

MiniShell Tone exposes only generic transport semantics:

```text
open
configure
enqueue duration
hold
stop
busy
close
```

Resident Tone has no knowledge of:

- dits/dahs;
- WPM;
- paddle modes;
- M1-M5;
- callsigns;
- logging.

The hardware-validated continuous worker remains below the API boundary.

## Filesystem ownership

Mini-CW storage code owns schema and policy but never FATFS/POSIX access.

All current settings I/O goes through private port wrappers over MiniShell
Filesystem.

Current application-owned path:

```text
/flash/minicw/setting.txt
```

The application uses transactional temp-write/sync/close/rename policy and
defers runtime saves to a Keyer/audio quiet point.

MiniShell owns actual file handles and closes leaked handles at app end.

Future callsign/log files must follow the same rule.

## Time ownership

Mini-CW reads UTC only through MiniShell Time/Location.

It does not:

- probe or drive the RTC;
- set system time;
- persist RTC state.

On ADV the resident Time/Location service reads the RTC only when establishing
its anchor; normal `utc_get()` calls use resident monotonic arithmetic.

GPS is not part of the Mini-CW MiniShell scope.

## Digital I/O ownership

Mini-CW selects the logical interface lines required by its Cardputer Keyer
profile:

```text
KeyIn:  13 / 15
KeyOut: 3 / 6
```

This is application hardware configuration, not driver ownership.

MiniShell owns:

- opening the line;
- pull-up/open-drain mode;
- read/write handle validity;
- closing/reclaiming lines.

ADV owns the physical GPIO driver.

## Lifecycle / cleanup ownership

Normal flow:

```text
MiniShell app_begin
    -> load minicw ELF
    -> minicw acquires services
    -> minicw runs
    -> minicw stops CW / closes Tone / releases lines
    -> ELF returns
MiniShell app_end
    -> closes any remaining Serial
    -> closes remaining Digital I/O
    -> closes remaining Audio/Tone
    -> closes remaining Filesystem handles
    -> reclaims application Memory
    -> resets Input ownership
    -> ELF loader deinitializes/frees image
    -> shell resumes
```

The application performs orderly cleanup for electrical/audio safety.
MiniShell remains the final resource owner and backstop for abnormal return.

## Allocation policy

Current Mini-CW application modules are all no-heap by architecture rule.

This becomes part of the baseline contract.

Future optional features must not reintroduce the standalone firmware's
`malloc/realloc/free` behavior.

In particular, standalone V1.2 callsign-table loading dynamically grows its
table; the MiniShell version must use a bounded/static design instead.

## Resource evidence at T045

```text
protected audio diff    NONE
resident firmware BIN   identical
resident SRAM delta     0
external minicw ELF     43,024 bytes
resident import         mini_api_get only
```

T045 software evidence:

```text
Linux CTest             84/84 in Codex final run
portable units          26/26
architecture checks     PASS
ADV build               PASS
clean external ELF      PASS
diff check              PASS
```

## Known unrelated test-harness issue

`linux_serial_unit` line 67 can be timing-sensitive on Linux PTYs.

The Serial API allows a finite-timeout write either to:

- make progress and return `MINI_OK` with a nonzero count; or
- make no progress before the deadline and return `MINI_ERR_TIMEOUT`.

The current test sometimes assumes the second outcome after a preceding
nonblocking fill. This is unrelated to Mini-CW and the audited boundary. The
latest local rerun passed, but the assertion should be cleaned up separately as
host-test housekeeping.

No Mini-CW or resident audio change is justified by that test behavior.

## Scope frozen out

The following standalone Mini-CW responsibilities are intentionally not part of
the MiniShell `minicw` application:

- GPS;
- trainer / lesson / word / callsign-training / plaintext modes;
- battery;
- deep sleep;
- USB MSC;
- direct RTC ownership.

## Baseline rule for future features

After this audit, any optional Mini-CW feature must satisfy all of:

1. external `minicw.elf` only unless a genuinely generic MiniShell capability
   is missing;
2. only `mini_api_get` as resident ELF import;
3. no platform/native/board access in application code;
4. no heap allocation;
5. no change to the validated Tone implementation for non-audio features;
6. filesystem writes deferred away from active CW/audio;
7. MiniShell remains final owner/reclaimer of resources;
8. paddle and automatic M1 acoustic regression test remains mandatory.

## Optional post-baseline features

The two currently desired optional features are:

1. callsign -> operator-name lookup from an application-owned CSV;
2. Keyer logging.

These are feature additions after the migration baseline, not parity
requirements.
