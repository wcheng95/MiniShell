# T019 — Linux QMX CAT over MiniShell Serial/CDC

Status: COMPLETE

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

- [x] MiniShell exposes optional raw Serial/CDC byte transport;
- [x] MiniShell service owns serial handle lifecycle and app-end cleanup;
- [x] Linux provider supports explicit `serial:/dev/ttyACM*`-style endpoint;
- [x] no QMX CAT syntax exists below the MiniFT8 application boundary;
- [x] MiniFT8 QMX adapter owns exact CAT strings;
- [x] startup command order is `MD6; FR0; FT0; FA...;`;
- [x] 20m command is exactly `FA00014074000;`;
- [x] all band dial frequencies match existing MiniFT8/log policy;
- [x] no TX-related CAT command is emitted;
- [x] omitting `--cat` preserves T018 bare-`ft8` behavior;
- [x] explicit `--cat` failure is reported and cleans up;
- [x] ADIF/Cabrillo regression remains unchanged;
- [x] Linux full CTest passes;
- [x] portable unit suite passes;
- [x] architecture boundary checks pass;
- [x] real ADV build passes despite the append-only optional API field;
- [x] no unrelated cleanup.

Manual architect acceptance on pc-1/QMX:

- [x] identify the current QMX CDC ACM node;
- [x] tune QMX away from the configured MiniFT8 band;
- [x] run with the actual QMX CDC node using `ft8 --cat serial:<QMX-node>`;
- [x] QMX changes to FT8 mode/VFO policy and selected-band dial frequency;
- [x] no RF transmit/key event occurs;
- [x] live FT8 RX still decodes;
- [x] `q` returns cleanly to `M$>`;
- [x] repeated launch/exit does not leave the tty busy.

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

### Implementation summary

Added an optional raw Serial service at the end of `mini_api_t`, retaining API
generation v3 and checking struct sizes before the new field is consumed.
The resident service owns one public stream handle, renews it on reopen,
validates transfers, passes timeouts through, and closes it at app end or service
reconfiguration. No Serial provider is installed on ADV.

The Linux provider accepts `serial:<absolute tty path>`, saves/restores termios,
uses raw 115200/8N1 without flow control, and performs nonblocking reads/writes
with poll and a monotonic deadline. The small flat `mini_serial_api_t` contains
capabilities plus open/read/write/close; no second nested table is needed.
Close consumes the public/backend handle even if restoration or close reports
an error, matching Linux fd lifetime and avoiding a retry against a reused fd.
This detail is specified in the header and new Serial contract.

`--cat` is optional with no default. `ft8_main` asks `app_controller` to start
control after configuration/UI initialization and before RX. The controller
passes the selected band's canonical dial frequency to the control-only
`radio_control` / `radio_qmx` modules. They open Serial and send exactly
`MD6;`, `FR0;`, `FT0;`, `FA%011lu;`, each with the pinned V2 200 ms timeout.
20m is exactly `MD6;FR0;FT0;FA00014074000;`. No other CAT commands are emitted,
including during cleanup. Error or short write aborts the sequence and closes
control. Later RX/app failure and normal quit also close control.

The existing logging frequency arrays were replaced by
`config_service_band_dial_hz()`. ADIF decimal-MHz and Cabrillo integer-kHz
serialization remain identical, including empty/zero frequency for invalid
indices. No V2 platform code was imported; the reference inspected was
`main/radio_control.cpp` and `main/radio_control_qmx.cpp` at
`491e757ae6b1e4cfd2b9a6ba10f48b35643849e0`.

### Files changed

- Public/resident service: `include/minishell/api.h`,
  `core/minishell_services/{minishell_services.h,services_internal.h,services.c,serial_service.c}`.
- Linux transport/composition:
  `platform/linux/{linux_serial.c,linux_internal.h,linux_services.c}`.
- MiniFT8 entry/controller: `apps/ft8/main/ft8_main.c`,
  `apps/ft8/src/app_controller/{app_controller.c,app_controller.h,app_controller_internal.h}`.
- MiniFT8 control and band/log policy:
  `apps/ft8/src/radio_control/{radio_control.c,radio_control.h,radio_qmx.c,radio_qmx.h}`,
  `apps/ft8/src/config_service/{config_service.c,config_service.h}`,
  `apps/ft8/src/log_service/log_service.c`.
- Build composition: root `CMakeLists.txt`, `platform/adv/main/CMakeLists.txt`,
  `tests/unit/CMakeLists.txt`.
- Tests: `tests/{linux_serial_test.c,linux_ft8_cat.py,ft8_radio_control_test.c,ft8_options_test.c,ft8_log_service_test.c}`,
  `tests/unit/{test_serial.c,test_main.c,test_support.h}`.
- Enforcement: `tests/{architecture_rules.py,app_platform_boundary.py,ft8_platform_boundary.py,serial_protocol_boundary.py}`.
- Documentation: `docs/api/serial-api.md`, `docs/README.md`, and this task packet.

### Invariants preserved

Bare FT8 still has no CAT path and retains T018 ADV presentation/live Linux RX
defaults. Explicit WAV/slot and presentation overrides remain intact. Endpoint
syntax is interpreted only by the Linux provider; the application parser rejects
missing/empty/duplicate CAT options, while malformed transport endpoints fail
clearly during open. No POSIX or tty dependency enters MiniFT8. Protocol literals
remain inside `radio_qmx`; the architecture check rejects them in core/Linux.

No RX Audio conversion, buffering, discontinuity, DSP/oversampling, AutoSeq, TX,
UI, or persisted-setting changes. No live band resynchronization, CAT responses,
discovery, or public ADV Serial provider. ADV links the portable optional service
and app code only; its private CDC handle remains separate. No deviation from
the authorized architecture or receive-only scope.

### Local tests run

```sh
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux -R 'serial|radio_control|linux_ft8|ft8_options|log_service|architecture' --output-on-failure
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
cmake -S tests/unit -B /tmp/T019-build-unit
cmake --build /tmp/T019-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T019-build-unit --output-on-failure
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/ft8_platform_boundary.py .
source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build
git diff --check
```

Focused regressions **15/15 PASS**; Linux full CTest **49/49 PASS**; portable
unit suite **15/15 PASS**; all three standalone architecture checks PASS;
real ADV firmware build PASS; whitespace check PASS. ADV binary size is
`0xb9340`, leaving `0x536cc0` (88%) of the app partition free.

New evidence includes:

- Serial validation, capability/unavailable handling, stale handles, short
  transfers, over-report rejection, timeout forwarding, close-error consumption,
  app-end cleanup and reconfiguration through the old provider.
- Production service/provider PTY round trips including NUL/newline/control
  bytes, non-tty/malformed/missing endpoint rejection, raw 8N1, finite/nonblocking
  read timeout, a saturated write queue timeout, termios restoration, reopen
  and peer disconnect.
- All seven exact CAT frequency strings; no transmit/time/tone commands;
  injected short writes and errors at each command stop immediately and close;
  old/truncated or absent Serial API is rejected safely.
- Actual FT8 process with a saved 40m band emits exactly
  `MD6;FR0;FT0;FA00007074000;` over a PTY, survives repeated close/reopen, cleans
  up after later RX startup failure, reports CAT open failure, and emits nothing
  when CAT is omitted. All audio fixtures are deterministic WAVs.
- Existing byte-for-byte ADIF/Cabrillo assertions and added frequency checks for
  all bands pass. Architecture self-tests also cover forbidden termios/poll
  headers in application code.

### Manual/hardware validation still required

After supervisor review, identify the real QMX CDC node on pc-1 and execute the
manual acceptance procedure above: selected mode/VFO/frequency synchronization,
no RF keying, continued live FT8 RX, and clean repeated quit/reopen. No hardware
CAT command, RF transmission, flashing, PR, or Actions wait was performed.

### Known limitations / risks

Writes acknowledge byte acceptance by the transport, not QMX response or a
physical drain guarantee. A failure may leave a partially applied receive setup
or incomplete command at the device; this task deliberately does not send
recovery/transmit commands or parse responses. The selected band is synchronized
only at startup. Device paths are explicit and host-specific; no cross-process
tty arbitration or reconnect/discovery policy is added. PTY/mocked evidence does
not replace real QMX acceptance. Existing untracked Python cache directories
were present before this task and are excluded from the implementation commit.

### Commit

One implementation commit titled `T019: add Linux Serial transport and receive-only QMX CAT`
on `codex/T019-linux-qmx-cat`; exact pushed SHA returned in chat.

## Supervisor review

Supervisor reviews the actual `main..<commit>` diff, including the public API and
the exact QMX byte sequences, before pc-1 hardware validation.


## Supervisor review — Linux Serial + receive-only QMX CAT

PASS for real pc-1/QMX testing on
`3553a0b087e095ec5b5621a50b70050b22194e54`.

Reviewed both the single implementation delta from
`11b90d1941d871ee788e8068e3421d2f4c901b85` and the full
`main..3553a0b0` branch delta.

Accepted boundaries and implementation:

- `mini_api_t.serial` is appended after existing services; API generation stays v3;
- new applications gate access with `mini_api_t.struct_size` and service
  `struct_size`, so older/short API tables remain safe;
- MiniShell Serial owns only raw byte-stream lifecycle, handles, validation,
  timeouts, and automatic app-end cleanup;
- Linux provider accepts `serial:<absolute tty path>`, uses nonblocking
  poll/deadline I/O, raw 115200/8N1, and restores the previous termios state on close;
- no Linux fd, termios, poll, or tty dependency enters `apps/ft8/`;
- MiniFT8 `radio_control/radio_qmx` owns QMX protocol strings and command ordering;
- the startup sequence is exactly:
  ```text
  MD6;
  FR0;
  FT0;
  FA%011lu;
  ```
- 20 m is exactly `FA00014074000;`;
- all seven FT8 band frequencies now share `config_service_band_dial_hz()`, and
  existing ADIF/Cabrillo formatting remains regression-tested;
- CAT cleanup runs on normal quit and on later RX-start failure;
- omitting `--cat` leaves the accepted T018 bare-`ft8` path unchanged.

Explicit transmit-safety review:

```text
TX;     NOT emitted
RX;     NOT emitted
TA...   NOT emitted
TM...   NOT emitted
```

The production QMX CAT module contains only receive-safe startup synchronization.

Accepted local evidence:

```text
Linux CTest          49/49 PASS
unit suite           15/15 PASS
architecture checks  PASS
real ADV build       PASS
git diff --check     PASS
```

The PTY integration test also proves exact CAT bytes through the real MiniShell
Serial/Linux provider path, repeated close/reopen, cleanup after RX failure, and
no CAT bytes when `--cat` is omitted.

No blocking findings. T019 returns to TESTING for real QMX CDC acceptance only.

Recommended pc-1 validation:

1. prefer the stable `/dev/serial/by-id/...` QMX path if Linux provides one;
2. otherwise use the identified `/dev/ttyACM*` node;
3. manually move QMX off the configured FT8 dial;
4. run `ft8 --cat serial:<QMX-CDC-path>`;
5. verify the radio returns to the selected FT8 dial without transmitting;
6. verify live FT8 decode continues;
7. quit and repeat once to prove clean tty release.


## Final architect acceptance — T019 COMPLETE

Real pc-1/QMX hardware validation passed.

The architect built the reviewed T019 branch, identified the QMX CDC ACM endpoint,
moved QMX away from the selected FT8 dial, and launched MiniFT8 with explicit CAT:

```text
M$> ft8 --cat serial:<QMX-CDC-path>
```

Accepted observed behavior:

```text
QMX CDC open / CAT byte transport              PASS
MD6 / FR0 / FT0 startup synchronization        PASS
selected-band FA frequency synchronization     PASS
no RF key / no transmit                        PASS
live QMX FT8 RX continues decoding             PASS
q returns cleanly to M$>                       PASS
repeat launch/exit releases and reopens CDC    PASS
```

This validates the intended ownership boundary on real hardware:

```text
MiniShell Serial/CDC
    owns raw CDC transport and lifecycle

MiniFT8 radio_qmx
    owns QMX CAT syntax and radio semantics
```

T019 remains receive-safe. No `TX;`, `RX;`, `TA...`, tune, waveform, or
physical transmit behavior is part of this accepted baseline.

T019 is COMPLETE. The next transmitter work may build on this proven CAT transport
and application-owned QMX adapter.

## Architect test result

Record the real QMX CDC node, observed frequency/mode synchronization, no-TX
evidence, live RX coexistence and clean repeated close/reopen here.
