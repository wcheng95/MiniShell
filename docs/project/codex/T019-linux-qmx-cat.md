# T019 — Linux QMX CAT over MiniShell Serial/CDC

Status: READY

## Architect intent

MiniFT8 development is back on Linux and the first MiniFT8-V3 QSO will happen on
Linux/QMX.

Architecture decision for CAT:

```text
MiniShell owns CDC/serial transport and resource lifecycle.
MiniFT8 owns radio semantics and CAT command strings.
```

In particular, MiniShell must not know QMX CAT commands such as `MD6;`,
`FA...;`, `TX;`, `RX;`, or `TA...;`. Those belong to a MiniFT8 radio
adapter.

T019 is the first safe CAT slice: add the transport boundary and prove
receive-safe QMX frequency/mode synchronization. Do **not** key the transmitter
in this task.

## Objective

Add an application-visible MiniShell Serial/CDC byte-stream service on Linux,
then add MiniFT8-owned QMX CAT control that can open an explicit CAT endpoint and
send the receive-safe startup synchronization sequence:

```text
MD6;
FR0;
FT0;
FA...........;
```

The frequency must correspond to the currently selected MiniFT8 band.

Hardware acceptance is a Linux/pc-1/QMX proof that MiniFT8 can change/synchronize
QMX mode/VFO/frequency over CDC while live FT8 RX continues to work.

## Current context

Accepted baseline:

```text
T017  ADV QMX USB-host RX                    COMPLETE
T018  Linux bare ft8 -> ADV + live QMX RX    COMPLETE
```

Current Linux operator baseline:

```text
M$> ft8
```

which uses:

```text
presentation  ADV
RX Audio      alsa:hw:2,0
```

QMX CAT behavior reference is the pinned MiniFT8-V2 implementation:

```text
wcheng95/Mini-FT8
491e757ae6b1e4cfd2b9a6ba10f48b35643849e0
main/radio_control.cpp
main/radio_control_qmx.cpp
```

V2 QMX RX-safe synchronization is:

```text
MD6;
FR0;
FT0;
FA%011d;
```

T019 deliberately excludes:

```text
TX;
RX;
TA....;
TM......;
```

The existing public MiniShell API has no serial/CDC byte-stream service.
The architecture on this branch has been amended to make CAT application-owned
above a MiniShell-owned Serial/CDC transport.

## Source of truth

Read before editing:

```text
AGENTS.md
docs/README.md
docs/project/progress.md
docs/MiniFT8/README.md
docs/MiniFT8/development.md
docs/MiniFT8/architecture.md

include/minishell/api.h
core/minishell_services/minishell_services.h
core/minishell_services/services.c
core/minishell_services/audio_service.c

platform/linux/linux_services.c
platform/linux/linux_internal.h

apps/ft8/main/ft8_main.c
apps/ft8/src/app_controller/
apps/ft8/src/config_service/
apps/ft8/src/log_service/

tests/
```

Pinned reference:

```text
MiniFT8-V2 commit
491e757ae6b1e4cfd2b9a6ba10f48b35643849e0
```

## Architectural constraints

### 1. Transport/protocol boundary

Hard boundary:

```text
MiniShell Serial/CDC
    owns:
      endpoint open/close
      raw byte read/write
      timeout behavior
      app-lifecycle cleanup
      Linux tty mechanics

MiniFT8 radio_qmx
    owns:
      QMX CAT syntax
      command ordering
      QMX mode/VFO/frequency semantics
```

No QMX command string or QMX mode/tone meaning may appear in MiniShell core,
service, or Linux backend code.

### 2. Public API shape

Add a small optional Serial service to `mini_api_t`, appended at the end.

A suitable V1 shape is:

```c
#define MINI_SERIAL_CAP_READ   (1ull << 0)
#define MINI_SERIAL_CAP_WRITE  (1ull << 1)

typedef uint32_t mini_serial_t;
#define MINI_SERIAL_INVALID ((mini_serial_t)0u)

typedef struct {
    uint32_t struct_size;
    mini_result_t (*open)(const char *endpoint, mini_serial_t *out_serial);
    mini_result_t (*read)(mini_serial_t serial, void *buffer,
                          uint32_t size, uint32_t *out_read,
                          uint32_t timeout_ms);
    mini_result_t (*write)(mini_serial_t serial, const void *buffer,
                           uint32_t size, uint32_t *out_written,
                           uint32_t timeout_ms);
    mini_result_t (*close)(mini_serial_t serial);
} mini_serial_api_t;

typedef struct {
    uint32_t struct_size;
    uint64_t capabilities;
    ...;
} mini_serial_service_api_t;
```

Exact naming may be simplified, but keep the semantics small and raw-byte based.

This is an append-only optional service. Keep `MINISHELL_API_VERSION` at the
current value unless implementation evidence shows that the existing struct-size
compatibility pattern cannot safely support the append.

### 3. MiniShell service ownership

The resident service layer owns public handles and automatic foreground-app cleanup,
following the existing Audio/Filesystem/Digital-I/O patterns.

For this first implementation one open serial stream at a time is sufficient.
Do not build a generalized multi-port manager unless needed by the tests.

### 4. Linux transport

Implement a Linux serial/CDC provider.

For the first slice, an explicit endpoint form such as:

```text
serial:/dev/ttyACM0
```

is sufficient.

Requirements:

- recognize only the intended serial endpoint syntax;
- use raw byte transport suitable for USB CDC/ACM;
- nonblocking/poll-based timeout behavior is preferred;
- configure a sane raw 8N1 tty state; QMX USB CDC does not require application
  awareness of physical UART baud timing;
- no QMX VID/PID discovery, udev enumeration, or stable by-id selection in T019;
- no raw Linux fd escapes into the application.

### 5. MiniFT8 radio ownership

Add a small MiniFT8 control-only radio layer, for example:

```text
apps/ft8/src/radio_control/
    radio_control.c/.h
    radio_qmx.c/.h
```

It is not a monolithic Radio object and must not own RX Audio or TX Audio.

The QMX adapter owns exact CAT strings and writes them through MiniShell Serial.

### 6. CAT CLI for this slice

Add an explicit application option:

```text
--cat serial:/dev/ttyACM0
```

No CAT endpoint default is required yet. Bare `ft8` must continue to work
exactly as T018 established when `--cat` is omitted.

Explicit CAT configuration is intentional for T019 so deterministic tests and
operators without QMX CDC remain unaffected. A later accepted task may compose a
default endpoint once pc-1 device naming is confirmed.

### 7. Frequency ownership

The selected MiniFT8 band must map to one canonical dial frequency:

```text
80m   3.573 MHz
40m   7.074 MHz
30m  10.136 MHz
20m  14.074 MHz
17m  18.100 MHz
15m  21.074 MHz
10m  28.074 MHz
```

Do not introduce a second drifting band-plan table just for CAT.

Move/centralize the existing band frequency policy into a reusable MiniFT8-owned
function (config/band-plan ownership is acceptable) and keep current ADIF/Cabrillo
frequency output byte-for-byte equivalent.

### 8. Startup behavior

When `--cat <endpoint>` is provided:

1. create/open the MiniFT8 QMX radio control path;
2. derive selected-band dial frequency;
3. send, in order:

```text
MD6;
FR0;
FT0;
FA%011u;
```

4. fail clearly if CAT open/write fails;
5. then continue normal live RX;
6. close the control transport during normal FT8 cleanup.

Do not send TX-related commands.

Band-change live resynchronization is **not required in T019**. This task proves
the ownership and startup path only.

## Implementation scope

### A. MiniShell Serial public service

Expected files include:

```text
include/minishell/api.h
core/minishell_services/minishell_services.h
core/minishell_services/services_internal.h
core/minishell_services/services.c
core/minishell_services/serial_service.c
CMakeLists.txt
```

Implement:

- optional API exposure;
- public-handle validation;
- read/write argument validation;
- timeout pass-through;
- app-end automatic close;
- unavailable-service behavior consistent with existing services.

### B. Linux serial provider

Add a focused Linux provider, e.g.:

```text
platform/linux/linux_serial.c
platform/linux/linux_internal.h
platform/linux/linux_services.c
```

Do not combine it into Audio.

### C. MiniFT8 QMX CAT layer

Add application-owned control modules and wire them through `app_controller`.

Required production behavior:

- no CAT path when `--cat` omitted;
- exact startup QMX command order when present;
- selected-band frequency formatting uses 11 decimal digits after `FA`;
- control cleanup on all app exits/failures.

### D. Band frequency single source

Centralize the FT8 dial-frequency mapping and preserve existing log output.

### E. Tests

Add deterministic tests with no radio required.

At minimum:

1. **Serial service unit**
   - open/read/write/close validation;
   - bad handle behavior;
   - timeout argument pass-through;
   - app-end cleanup.

2. **Linux serial provider PTY test**
   - create a pseudo-terminal;
   - open its slave through the production serial endpoint form;
   - write bytes through MiniShell/backend path;
   - verify exact bytes on the PTY master;
   - if read is implemented, verify reverse direction too.

3. **QMX CAT unit**
   - mock MiniShell Serial;
   - verify exact sequence:
     `MD6;`, `FR0;`, `FT0;`, `FA00014074000;` for 20m;
   - verify all supported band frequencies;
   - verify short/error write propagation;
   - assert no `TX;`, `RX;`, `TA`, or `TM` command is emitted.

4. **FT8 option/application regression**
   - bare `ft8` still requires no CAT;
   - explicit `--cat` is accepted;
   - duplicate/empty/malformed CAT option is rejected appropriately;
   - existing WAV and live RX tests remain hardware-independent.

5. **Architecture boundary**
   - no Linux/POSIX/termios dependency under `apps/ft8/`;
   - no QMX CAT command strings under MiniShell service/backend code.

## Non-goals

Do not implement:

- `TX;`;
- `RX;` as a transmit-lifecycle command;
- `TA....;`;
- QMX tone stepping;
- FT8 TX waveform generation;
- MiniShell Audio TX;
- physical RF transmission;
- AutoSeq TX integration;
- tune mode;
- CAT time synchronization (`TM`);
- live band-change resync;
- CAT command-response parsing;
- QMX VID/PID discovery;
- udev/by-id discovery;
- ADV Serial/CDC public provider;
- reuse of T017's ADV private CDC handle;
- UI redesign;
- new persistent station settings;
- a generic MiniShell radio/Control service.

## Acceptance criteria

Software/review gate:

- [ ] MiniShell exposes optional raw Serial/CDC byte transport;
- [ ] MiniShell service owns serial handle lifecycle and app-end cleanup;
- [ ] Linux provider supports explicit `serial:/dev/ttyACM*`-style endpoint;
- [ ] no QMX CAT syntax exists below the MiniFT8 application boundary;
- [ ] MiniFT8 QMX adapter owns exact CAT strings;
- [ ] startup command order is `MD6; FR0; FT0; FA...;`;
- [ ] 20m command is exactly `FA00014074000;`;
- [ ] all band dial frequencies match existing MiniFT8/log policy;
- [ ] no TX-related CAT command is emitted;
- [ ] omitting `--cat` preserves T018 bare-`ft8` behavior;
- [ ] explicit `--cat` failure is reported and cleans up;
- [ ] ADIF/Cabrillo regression remains unchanged;
- [ ] Linux full CTest passes;
- [ ] portable unit suite passes;
- [ ] architecture boundary checks pass;
- [ ] real ADV build passes despite the append-only optional API field;
- [ ] no unrelated cleanup.

Manual architect acceptance on pc-1/QMX:

- [ ] identify the current QMX CDC ACM node;
- [ ] tune QMX away from the configured MiniFT8 band;
- [ ] run, for the actual node:
      `ft8 --cat serial:/dev/ttyACM0`;
- [ ] QMX changes to FT8 mode/VFO policy and selected-band dial frequency;
- [ ] no RF transmit/key event occurs;
- [ ] live FT8 RX still decodes;
- [ ] `q` returns cleanly to `M$>`;
- [ ] repeated launch/exit does not leave the tty busy.

## Automated tests

Run:

```bash
git status --short

cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T019-build-unit
cmake --build /tmp/T019-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T019-build-unit --output-on-failure

PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/ft8_platform_boundary.py .

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build

git diff --check
```

No GitHub Actions wait.

## Manual / hardware validation

After supervisor review, on pc-1:

1. determine QMX CDC node:
   ```bash
   ls -l /dev/serial/by-id/ 2>/dev/null || true
   ls -l /dev/ttyACM* 2>/dev/null || true
   ```
2. manually move QMX off the configured MiniFT8 dial frequency;
3. launch MiniShell;
4. run `ft8 --cat serial:<actual-QMX-node>`;
5. confirm QMX returns to the selected MiniFT8 dial frequency without keying TX;
6. confirm RX decoding continues;
7. quit and repeat once to prove clean CDC release.

## Branch workflow

Use:

```text
codex/T019-linux-qmx-cat
```

Codex:

1. read the amended architecture and this task;
2. implement only the receive-safe CAT slice;
3. run all required local gates;
4. set Status to REVIEW;
5. record exact files, commands/results and deviations;
6. commit and push one reviewable implementation commit;
7. return commit SHA;
8. no PR;
9. no Actions wait.

## Codex implementation notes

Codex fills this section before handoff.

### Implementation summary

### Files changed

### Invariants preserved

### Local tests run

### Manual/hardware validation still required

### Known limitations / risks

### Commit

## Supervisor review

Supervisor reviews the actual `main..<commit>` diff, including the public API and
the exact QMX byte sequences, before pc-1 hardware validation.

## Architect test result

Record the real QMX CDC node, observed frequency/mode synchronization, no-TX
evidence, live RX coexistence and clean repeated close/reopen here.
