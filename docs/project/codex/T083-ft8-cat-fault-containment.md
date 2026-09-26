# T083 — MiniFT8 QMX CAT fault containment and CDC ownership cleanup

Status: READY

## Architect intent

MiniFT8-V3 has frozen twice in live ADV/QMX operation. The second observed freeze
occurred at a TX boundary. The RT log ended with:

```text
R [20260926 152059][7.074] JR7NFW WO7I 73 7 1100
R [20260926 152059][7.074] YB9GAN AE6CH CM97 12 1981
R [20260926 152059][7.074] JD1BOI WU7W 73 4 1250
R [20260926 152100][7.074] BX6AEN W7WKR CN97 -2 844
R [20260926 152100][7.074] CQ POTA N7PMS DN28 8 1544
T [20260926 152100][7.074] N7PMS AG6AQ CM97 2031
```

The UI then stopped responding to all FT8 keys.

The `T` record is written after MiniFT8 has paused logical RX and prepared the
TX, immediately before QMX CAT key-up/tone operations. This makes the QMX
CDC/CAT path the primary fault domain.

The pinned MiniFT8-V2 reference is:

```text
repository: wcheng95/Mini-FT8
commit:     491e757ae6b1e4cfd2b9a6ba10f48b35643849e0
```

V2 did not exhibit this freeze in long ADV/QMX operation.

T083 must restore the important V2 property:

> A QMX CAT/transport failure may abort a transmission, but it must not own or
> terminate the MiniFT8 application/UI lifecycle.

At the same time, simplify CDC ownership so a wedged blocking USB CDC transfer
cannot wedge the FT8/UI task.

---

# Four design principles

T083 is explicitly governed by these four MiniShell engineering principles.

## 1. Modularity

Keep responsibilities separate:

```text
MiniFT8 app_controller
    owns FT8 TX policy and transport-fault behavior

radio_control/radio_qmx
    owns QMX CAT command semantics

MiniShell Serial
    owns the portable write contract

ADV QMX provider
    owns ESP-IDF CDC/UAC/USB implementation
```

Do not move ESP-IDF/FreeRTOS/CDC details into portable MiniFT8 code.

## 2. Ownership

There must be one clear owner for the QMX CDC device handle and driver calls.

After T083:

```text
ADV CDC owner task
    owns cdc_device
    owns CDC open
    owns CDC close
    owns cdc_acm_host_data_tx_blocking()
```

The FT8/UI task, MiniShell Serial service, and callbacks must never directly
open, close, or transmit through the ESP-IDF CDC handle.

## 3. Decoupling

A radio/USB fault must not unnecessarily control unrelated lifecycles.

In particular:

```text
CAT failure      != FT8 application failure
CAT write stall  != UI/input stall
CDC reconnect    != FT8 main-loop ownership
Audio RX pause   != CDC teardown
decode lifecycle != CAT lifecycle
```

The FT8 UI must continue to poll keys after a CAT timeout/error.

## 4. KISS

Use the smallest mechanism that establishes the ownership above.

Prefer:

- reuse the existing ADV CDC task;
- one bounded fixed request/response path;
- one request in flight;
- fixed-size command storage;
- no heap per command;
- no retry of ambiguous CAT writes;
- no generic USB-serial framework;
- no watchdog task;
- no second CDC worker;
- no automatic reconnect/retry storm.

Do not create complexity to make a failed transmission invisible. Report the
fault and keep the application responsive.

---

# V2 vs V3 findings that define this task

## CAT protocol is not the difference

V2 and current V3 both currently use:

```text
MD6;          200 ms
TX;           200 ms
TAxxxx.xx;     10 ms
RX;           200 ms
```

T083 intentionally removes the redundant per-TX `MD6;`. QMX operating mode is
session/band setup state, not per-transmission state. The accepted V3 ownership
model has MiniFT8 as the only CAT policy owner while FT8 is active, so nothing
inside a normal FT8 session should change QMX mode behind it.

Required command ownership after T083:

```text
CAT initial/band setup:
    MD6;
    FR0;
    FT0;
    FA...........;

each physical TX:
    TX;
    TA....;
    ...
    RX;
```

Do not re-send `MD6;` at every TX boundary.

Both use Espressif:

```text
usb_host_cdc_acm 2.2.0
QMX CDC interface 0
CDC driver core 0
CDC driver priority 4
QMX USB FIFO 91/18/91
```

Do not change CAT strings/timings other than the explicitly authorized removal
of redundant per-TX `MD6;`. Do not change the CDC component version or FIFO
tuning in T083 unless hardware evidence proves one of those is wrong.

## V2 transport path

V2 effectively used:

```text
radio_control_qmx
    -> cat_cdc_send()
    -> cdc_acm_host_data_tx_blocking()
```

It did not add an outer CDC lifetime mutex around each CAT write.

A CAT begin failure disabled CAT for that transmission and left the main UI loop
alive. Tone-write failures did not terminate the application.

## V3 transport path today

V3 uses:

```text
MiniFT8
    -> radio_control
    -> MiniShell Serial
    -> ADV serial:qmx
    -> cdc_mutex
    -> cdc_acm_host_data_tx_blocking()
```

A separate CDC task also takes the same `cdc_mutex` for open/close/reconnect.

A physical-TX transport/recovery failure can currently propagate `false` from
`app_controller_step_tx()`, causing `ft8_main` to leave its normal loop and
enter cleanup.

These are deliberate T083 correction targets.

---

# Required change A — CAT faults must not terminate MiniFT8

Separate **external transport faults** from **internal application invariant
errors**.

## Transport faults

The following are recoverable application-level events:

- QMX CAT begin/key-up failure;
- QMX tone-write failure;
- QMX RX/restore failure;
- live band CAT command-submission failure;
- logical Audio RX stop failure before TX;
- logical Audio RX resume failure after TX.

For these conditions:

```text
report diagnostic
abort/contain current TX as safely as possible
preserve radio uncertainty if RX restoration failed
preserve RX-paused state if Audio resume failed
keep ft8_main loop running
keep UI/input responsive
do not retry the same ambiguous CAT command
do not transmit again in the same slot
```

The application must not return an error merely because one of these external
transport operations failed.

### Internal errors remain distinct

Do not silently convert genuine internal errors into transport faults.

Examples that may remain fatal/error-returning if they represent impossible
internal state:

- corrupt/invalid TX tone plan;
- TX scheduler invariant failure;
- invalid semantic encoder state;
- impossible internal clock/schedule state.

Keep the distinction explicit in code. Do not use one generic `fail_tx()` path
for both external transport failures and internal programming/invariant errors.

## Radio uncertainty

If QMX `RX;` restoration fails, keep the existing radio-control uncertainty
state rather than pretending RX was restored.

Do not force-clear `rx_required`/equivalent state just to permit another TX.

Result:

```text
radio uncertain
    -> future TX remains blocked/not-ready
    -> UI remains alive
    -> operator may quit/restart/reconnect
```

No automatic repeated `RX;` loop is required.

## Audio resume failure

If logical Audio RX restart fails:

- FT8 remains alive;
- RX remains unavailable/paused;
- do not allow another TX that assumes RX recovery succeeded;
- report the fault.

No background retry loop is required in T083.

A restart/reconnect remains an acceptable recovery path.

## Band CAT command submission

A band CAT command-submission failure must not terminate MiniFT8.

It should:

- set the existing failed/unknown-radio-state;
- block physical TX while synchronization is failed;
- keep UI active.

A new explicit operator band change may clear the prior sync-failed state and
attempt one new debounced command submission. Do not continuously retry by itself.

---



### Important write-only CAT limitation

ADV MiniShell exposes QMX CAT as WRITE-only `serial:qmx`. There is no CAT RX
parser/readback path in this architecture. Therefore T083 must not claim that
MiniFT8 verifies the QMX mode/VFO/frequency.

For band changes, the only observable outcomes are:

```text
MD6;/FR0;/FT0;/FA... command sequence submitted successfully
OR
local transport submission failed/timed out
```

A successful write means only that the command bytes were accepted by the local
CDC transport contract. It does not prove the QMX applied them.

Accordingly, use terms such as `CAT command-submission failure` or
`radio state unknown`, not `frequency sync verified/failed`.

---

# Required change B — one CDC owner, remove foreground/lifecycle mutex coupling

Refactor the ADV QMX provider so the existing CDC task is the sole owner of:

```text
cdc_device
cdc_acm_host_open()
cdc_acm_host_close()
cdc_acm_host_data_tx_blocking()
```

The disconnect/error callback may only publish bounded state/notification. It
must not manipulate the device handle.

Remove the need for the current outer `cdc_mutex` between foreground CAT writes
and CDC lifecycle work.

Required ownership after T083:

```text
CDC callback
    -> flag/notify only

CDC owner task
    -> open/reconnect
    -> close/disconnect
    -> execute CAT TX request

serial_open()
    -> observe published CDC-ready state
    -> reserve public serial owner

serial_write()
    -> submit bounded request
    -> wait boundedly for completion
    -> never touch cdc_device

serial_close()
    -> release public serial owner
    -> never close cdc_device directly
```

The exact FreeRTOS primitive is implementation detail, but keep it small.

Preferred shape:

- fixed request mailbox/queue;
- fixed completion mailbox/queue or generation-tagged result;
- at most one CAT request in flight.

Do not add a second CAT worker when the existing CDC task can own the work.

---

# Required change C — contain a wedged CDC driver call away from the UI task

Today the FT8/UI task eventually enters
`cdc_acm_host_data_tx_blocking()` synchronously.

After T083 it must not.

`serial_write()` must copy the QMX command into provider-owned bounded storage,
submit it to the CDC owner task, and wait only for the caller's requested
timeout.

All currently used QMX CAT commands fit comfortably in the CDC driver's existing
64-byte OUT buffer. A fixed 64-byte provider request payload is acceptable and
preferred.

Required behavior:

```text
FT8/UI task
    serial_write(timeout=N)
        -> copy command to fixed provider request
        -> wake CDC owner
        -> wait at most N
        -> return success / timeout / I/O error
```

The CDC owner task may call the pinned driver's blocking TX API.

If that driver call itself wedges beyond the requested deadline:

```text
CDC owner task may be unhealthy/stuck
BUT
FT8/UI task returns timeout
FT8/UI continues
keyboard remains responsive
no second write is issued while the prior request is unresolved
```

This is containment, not magical driver recovery.

## In-flight timeout rules

Because an ambiguous USB write might have reached the radio:

- never retry the same request automatically;
- use a monotonically increasing request/generation ID or equivalent;
- stale late completions must not satisfy a newer request;
- after a caller timeout with the worker still in-flight, subsequent CAT writes
  must fail fast/not-ready until that request resolves or the CDC session is
  recreated;
- never retain a pointer to caller-owned command memory after `serial_write()`
  returns.

For finite MiniFT8 CAT writes, include queue/wakeup time inside the caller's
timeout budget.

Preserve current `MINI_WAIT_FOREVER` overflow protection, but do not optimize
T083 around that path; MiniFT8 uses finite CAT timeouts.

---

# Required change D — preserve V2-style physical UAC continuity

Do **not** tear down/reopen the physical QMX UAC stream for every FT8 TX.

Important current fact:

The ADV provider's `Audio.stop()/start()` is already a logical delivery gate.
When physical UAC is streaming, `rx_stop()` sets `started=false` and the
capture task continues reading/discarding UAC data.

Preserve that design.

Required invariant:

```text
before TX:
    logical MiniShell RX delivery pauses
    stale ring data is invalidated/discarded

during TX:
    physical QMX USB Host remains alive
    physical UAC stream remains alive/drained when already streaming
    CDC remains alive
    no UAC class/device teardown

after TX:
    logical RX delivery resumes
    a clean discontinuity/reset boundary is provided
```

T083 is **not** a request to remove the logical MiniShell Audio stop/start
contract. It is a request to prevent that contract from becoming application
lifecycle coupling.

Audio stop/start transport errors follow Required change A: abort/contain the TX,
report, keep the UI alive.

---

# Required change E — remove redundant per-TX mode command

QMX `MD6;` is part of CAT setup/synchronization, not per-slot key-up.

Change the QMX TX begin sequence from:

```text
MD6;
TX;
```

to:

```text
TX;
```

Keep `MD6;` in the initial/band CAT command sequence:

```text
MD6;
FR0;
FT0;
FA...........;
```

Rationale under the four principles:

- **Modularity:** setup owns persistent radio configuration; TX owns keying/tone.
- **Ownership:** MiniFT8 is the sole CAT policy owner during its session.
- **Decoupling:** a TX boundary should not rewrite unrelated persistent mode
  state.
- **KISS:** one fewer CDC transaction at every TX start.

Do not add a readback requirement; ADV CAT remains WRITE-only.

---

# Required change F — bounded TX diagnostics

Add small, bounded diagnostics sufficient to identify future TX stalls without
logging all 79 tones.

Suggested event vocabulary:

```text
FT8T PREP slot=<n>
FT8T BEGIN_ENTER slot=<n>
FT8T BEGIN_OK slot=<n>
FT8T FIRST_TONE_OK slot=<n>
FT8T END_ENTER slot=<n>
FT8T END_OK slot=<n>
FT8T RX_RESUME slot=<n>
FT8T FAULT stage=<stage> code=<n> slot=<n>
```

Exact formatting may vary, but retain stage identity.

Do not print every successful TA tone.

On ADV, diagnostics should be available on the existing system/debug sink.

The important future interpretation is:

```text
BEGIN_ENTER, no BEGIN_OK
    -> key-up CAT request/transport fault

BEGIN_OK, no FIRST_TONE_OK
    -> first TA path fault

FIRST_TONE_OK, later FAULT tone
    -> in-TX tone write fault

END_ENTER, no END_OK
    -> RX restoration CAT fault

END_OK, no RX_RESUME
    -> logical Audio recovery fault
```

---

# Failure-state policy

Keep the policy intentionally small.

## Successful TX

No behavior change.

## CAT begin/tone failure

```text
abort current TX
attempt one best-effort radio RX restore when required
attempt one logical Audio RX resume
mark TX inactive/pending false
do not AutoSeq-tick as successful
UI continues
```

## CAT RX-restore failure

```text
radio stays uncertain/not-ready
TX inactive
UI continues
future TX blocked by radio state
```

## Audio-resume failure

```text
RX remains paused/unavailable
TX inactive
UI continues
future TX requiring recovered RX is blocked
```

Do not invent an automatic recovery state machine in this task.

---

# Protected boundaries / non-goals

Do not change:

- FT8 DSP, waterfall, candidate search, LDPC, or T081 scheduling;
- T082 RX paging or CQ modifier behavior;
- FT8 CAT command strings;
- QMX CAT timeout values;
- QMX FIFO 91/18/91;
- Espressif CDC/UAC component versions;
- MiniShell public API;
- Linux serial implementation except regression tests if needed;
- TX tone timing mathematics;
- AutoSeq semantic progression after a successful TX;
- persisted station configuration;
- UI layout.

Do not add:

- a generic USB serial subsystem;
- a second CDC driver instance;
- per-command heap allocation;
- a CAT retry queue;
- background repeated `RX;` recovery;
- a watchdog that reboots the device;
- a second CDC/TX worker if the existing CDC owner task can serve requests.

---

# Required software tests

## 1. Application CAT-fault containment

Extend physical-TX/controller tests to inject failures at:

```text
MD6/TX begin
first TA tone
later TA tone
RX end
band CAT command submission
```

For every transport failure verify:

- `app_controller_step_tx()` / `app_controller_step_cat()` keeps the
  application loop alive as appropriate;
- TX is no longer active;
- failed TX does not count as successful physical TX;
- AutoSeq is not ticked as successful;
- no second TX starts in the same slot;
- keyboard/UI loop remains conceptually serviceable;
- diagnostic stage is identifiable.

Test radio-restore failure separately and verify the radio remains not-ready for
another TX.

## 2. Audio fault containment

Inject:

- Audio.stop failure before TX;
- Audio.start failure after TX.

Verify:

- no application termination;
- stop failure prevents key-up;
- resume failure leaves RX unavailable/paused;
- another TX is blocked when RX recovery is required;
- UI remains alive.

## 3. ADV CDC ownership

Production-path test must prove:

- only the CDC owner task manipulates `cdc_device`;
- foreground `serial_write` never calls
  `cdc_acm_host_data_tx_blocking()` directly;
- disconnect callback only flags/notifies;
- no outer foreground/lifecycle `cdc_mutex` remains necessary;
- serial open/close public ownership semantics from T030 still hold.

Architecture/static assertions are acceptable for ownership properties that are
awkward to execute in host tests.

## 4. Bounded write with stuck worker

Create a deterministic provider test where:

- a request is accepted by the CDC owner;
- the simulated driver does not complete;
- foreground `serial_write(..., finite_timeout)` returns by that timeout;
- caller-owned data may go out of scope safely;
- a second write fails fast while the first request remains unresolved;
- a stale completion cannot satisfy a later generation.

This is the key regression for UI decoupling.

## 5. Normal QMX CAT behavior

Preserve exact successful command sequence and counts for:

```text
MD6;
FR0;
FT0;
FA...........;
TX;
TA....;
...
RX;
```

No normal CAT retry.

## 6. UAC continuity

Prove logical Audio stop/start during TX:

- does not uninstall/reinstall USB Host;
- does not uninstall/reinstall CDC;
- does not close/reopen the CDC handle;
- does not stop/restart an already-streaming physical UAC device solely because
  MiniShell RX delivery is paused;
- invalidates/discards stale RX samples;
- resumes with the existing discontinuity semantics.

Use existing T030/T017 tests where possible; extend rather than duplicate.

## 7. Full regression gates

Run:

```text
cmake -S . -B build-linux
cmake --build build-linux -j8
ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T083-unit
cmake --build /tmp/T083-unit -j8
ctest --test-dir /tmp/T083-unit --output-on-failure

python3 tests/app_dependency_boundary.py . ft8
python3 tests/app_platform_boundary.py . ft8
python3 tests/ft8_platform_boundary.py .
python3 tests/architecture_rules.py .

source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build

git diff --check
```

Known unrelated Linux PTY serial timeout flake may be documented only if the
focused serial test passes unchanged.

---

# Hardware acceptance

ADV + QMX acceptance is required.

## H1 — repeated live RX/TX

Run normal MiniFT8 long enough to exercise repeated:

```text
RX -> decode -> TX -> RX
```

Prefer at least 20–30 physical TX cycles if practical.

Verify:

- UI remains responsive throughout;
- no unexplained freeze;
- RX resumes after each successful TX;
- normal FT8 QSOs/beacon behavior remains intact;
- diagnostics show complete successful milestone sequences.

## H2 — transport-fault containment

A naturally occurring CAT failure is sufficient evidence if captured.

If a safe deterministic fault can be induced without risking an unintended
stuck transmitter, verify:

```text
FT8T FAULT ...
UI still responds to keys
q/back still works
application does not enter cleanup automatically
```

Do not deliberately induce a fault that could leave QMX keyed into an antenna.

## H3 — post-session cleanup

After a healthy run:

- quit FT8;
- MiniShell prompt returns;
- normal USB/debug console ownership is restored as currently expected;
- post-FT8 `usbmsc` behavior remains at the accepted T030/T017 baseline.

If a deliberately simulated stuck CDC worker prevents clean USB teardown, that
must be bounded and diagnosed; it must not be confused with normal healthy
cleanup acceptance.

---

# Likely files

Portable policy:

```text
apps/ft8/src/app_controller/app_controller_tx_physical.c
apps/ft8/src/app_controller/app_controller.c
apps/ft8/src/radio_control/radio_control.[ch]    only if state exposure is needed
```

ADV provider:

```text
platform/adv/adv_audio_uac.cpp
tests/adv_qmx_serial_test.py
```

Focused tests/docs:

```text
tests/ft8_physical_tx_test.c
tests/ft8_radio_tx_test.c
tests/architecture_rules.py                     if ownership rule is useful
docs/MiniFT8/README.md
docs/MiniFT8/architecture.md
docs/MiniFT8/development.md
this task packet
```

Keep the actual diff smaller if some listed files are unnecessary.

---

# Implementation order

Use this order so each change has a clear reason:

1. split MiniFT8 internal errors from external CAT/Audio transport faults;
2. make CAT/Audio transport faults non-fatal to the FT8 main loop;
3. add bounded TX-stage diagnostics;
4. make the existing ADV CDC task the sole CDC-handle/driver owner;
5. replace direct foreground CDC TX with one bounded request/response path;
6. remove the now-unnecessary outer CDC lifetime mutex;
7. preserve/prove logical Audio pause with physical UAC continuity;
8. run fault-injection and full regressions;
9. hardware validate repeated RX/TX.

Do not start by adding recovery/watchdog machinery.

---

# Acceptance summary

T083 is accepted only when all of these are true:

1. CAT failure cannot terminate MiniFT8's normal UI loop.
2. QMX `MD6;` is issued only during CAT initial/band setup, not before every
   physical TX.
3. Band CAT command-submission failure cannot terminate MiniFT8.
4. Audio pause/resume failure cannot terminate MiniFT8.
5. The ADV CDC task is the sole owner of the ESP-IDF CDC handle and driver calls.
6. FT8/UI core does not call a potentially wedged CDC driver transfer directly.
7. A simulated stuck CDC TX returns a bounded timeout to MiniFT8 and leaves UI
   execution able to continue.
8. No ambiguous CAT write is automatically retried.
9. A failed RX restore leaves radio state uncertain/not-ready rather than faking
   success.
10. Logical RX pause does not tear down physical UAC/CDC/USB Host.
11. Normal successful QMX RX/TX behavior remains unchanged except for removal
    of the redundant per-TX `MD6;`.
12. T081/T082 behavior remains unchanged.
13. Linux/unit/architecture/ADV build gates pass.
14. Repeated ADV/QMX physical RX/TX does not freeze the FT8 UI.

---

# Codex branch / handoff

Work on:

```text
codex/T083-ft8-cat-fault-containment
```

Start from current `main`.

Read first:

```text
AGENTS.md
docs/README.md
docs/project/codex/T030-adv-qmx-cat.md
docs/project/codex/T081-ft8-rx-decouple.md
docs/MiniFT8/README.md
docs/MiniFT8/architecture.md
docs/MiniFT8/development.md
this task packet
```

Use the pinned V2 repository/commit above as behavioral reference.

Keep one reviewable implementation commit. Do not merge to `main` and do not
open a PR unless asked.

If implementation reveals that the sole-owner CDC task cannot provide bounded
10 ms TA command service without breaking tone timing, stop and report measured
evidence rather than adding another worker or silently weakening the ownership
model.
