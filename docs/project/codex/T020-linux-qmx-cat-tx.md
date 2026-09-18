# T020 — Linux QMX CAT TX primitives

Status: READY

## Architect intent

Use QMX's CAT-controlled transmitter path before implementing Linux Audio TX.

QMX can transmit FT8 by CAT-controlled tone frequency, so the preferred first-QSO
path is:

```text
FT8 symbols
    -> MiniFT8 radio_qmx
    -> TX;
    -> TAxxxx.xx; tone updates
    -> RX;
    -> MiniShell Serial/CDC
    -> QMX
```

This keeps QMX TX independent of MiniShell Audio TX. Audio TX remains useful for
other radios such as QDX and may be implemented later.

T020 adds only the QMX CAT TX primitives and proves them with a short controlled
test tone. It does not yet encode or schedule a real FT8 message.

## Objective

Extend the T019 MiniFT8-owned QMX radio adapter with:

```text
begin_tx()
set_tone_hz()
end_tx()
```

using the pinned MiniFT8-V2 command behavior:

```text
begin_tx:
    MD6;
    TX;

set_tone_hz:
    TA%04d.%02d;

end_tx:
    RX;
```

Provide a bounded diagnostic path that lets the architect key QMX briefly at a
single requested audio-offset tone and return immediately to RX.

No AutoSeq or FT8 symbol scheduling is connected in T020.

## Current context

Accepted Linux baseline:

```text
T018  bare ft8 -> ADV UI + live QMX RX        COMPLETE
T019  MiniShell Serial/CDC + receive CAT      COMPLETE
```

T019 already provides:

```text
radio_control_open_qmx()
    -> MD6;
    -> FR0;
    -> FT0;
    -> FA%011lu;
```

over the MiniShell Serial/CDC service.

Architecture is locked:

```text
MiniShell Serial/CDC
    owns raw bytes and lifecycle

MiniFT8 radio_qmx
    owns QMX CAT command syntax and radio semantics
```

Current V3 has no FT8 TX encoder/symbol schedule yet. T020 must not create one
implicitly.

## Source of truth

Read before editing:

```text
AGENTS.md
docs/MiniFT8/architecture.md
docs/MiniFT8/development.md
docs/project/progress.md
docs/api/serial-api.md

apps/ft8/src/radio_control/
apps/ft8/src/app_controller/
apps/ft8/src/tx_lifecycle/
apps/ft8/src/auto_seq/
include/minishell/api.h
```

Pinned V2 reference:

```text
wcheng95/Mini-FT8
491e757ae6b1e4cfd2b9a6ba10f48b35643849e0

main/radio_control_qmx.cpp
tests/tx_e2e/test_ta_format.cpp
tests/tx_e2e/tx_state_machine.cpp
```

## Pinned V2 behavior to preserve

### TX begin

V2 QMX begin-TX behavior:

```text
MD6;
TX;
```

The mode command is intentionally repeated at TX entry.

Use the established normal CAT command timeout:

```text
200 ms
```

### Tone command

V2 QMX tone command:

```text
TA%04d.%02d;
```

Examples:

```text
1500.00 Hz -> TA1500.00;
1520.83 Hz -> TA1520.83;
1543.75 Hz -> TA1543.75;
```

Formatting rules are mandatory:

1. use floor/truncation for the integer part, not round-to-nearest;
2. clamp integer part to 0..9999;
3. compute fractional hundredths from the non-negative remainder;
4. round fractional hundredths;
5. clamp fraction to 0..99;
6. never emit a negative fraction or three-digit fractional field.

This preserves the V2 fix for invalid forms such as:

```text
TA1521.-17;
TA1234.100;
```

Use V2's short tone-update write timeout:

```text
10 ms
```

T020 does not yet establish the final symbol scheduler, but this timeout choice is
part of the proven reference behavior for later 160 ms FT8 symbols.

### TX end

V2 QMX end-TX behavior:

```text
RX;
```

Use the normal 200 ms CAT timeout.

## Architectural constraints

1. CAT syntax remains under `apps/ft8/src/radio_control/`.
2. No QMX CAT literal may enter MiniShell core or Linux Serial provider.
3. `app_controller` may request radio actions but must not format CAT.
4. AutoSeq remains pure and must not call radio control.
5. T020 must not change RX Audio, FT8 decode, logging, UI, or station persistence.
6. No MiniShell public API change.
7. No Linux Audio TX work.
8. No ADV CAT TX work.
9. No FT8 encoder or symbol scheduler yet.
10. All failure paths that key TX must make a best-effort attempt to return QMX to
    RX before releasing the Serial stream.

## Production API shape

Extend the control-only MiniFT8 radio layer with operations equivalent to:

```c
mini_result_t radio_control_begin_tx(RadioControl *radio);
mini_result_t radio_control_set_tone_hz(RadioControl *radio, float tone_hz);
mini_result_t radio_control_end_tx(RadioControl *radio);
```

Exact signatures may differ if a small explicit state struct is cleaner.

The control layer should track whether TX has successfully begun so cleanup can
perform fail-safe RX restoration.

### Lifecycle rules

- `begin_tx` requires an open QMX control session;
- do not silently treat duplicate begin as success unless state semantics are
  explicit and tested;
- `set_tone_hz` requires active TX;
- `end_tx` sends `RX;` and clears active-TX state only after the transport
  accepts the command;
- `radio_control_close()` must best-effort send `RX;` first if TX is still
  active, then close Serial even if RX restoration reports an error;
- a failed `TX;` must not mark TX active;
- a failed `TA` leaves TX active, so caller/cleanup must still send `RX;`.

## Diagnostic validation path

Add a small MiniFT8 diagnostic mode/tool using the production radio-control module,
not a second CAT implementation.

Preferred operator form:

```text
M$> ft8 --cat serial:<QMX-node> --cat-test-tone 1500 --cat-test-ms 500
```

If keeping this diagnostic behavior inside the production `ft8` CLI would
complicate normal app behavior, a MiniFT8-owned diagnostic app/tool built from the
same `radio_control/radio_qmx` module is acceptable.

Required behavior:

1. open/synchronize CAT exactly as T019;
2. begin TX;
3. set requested tone;
4. hold for the bounded requested duration using MiniShell time/sleep;
5. end TX;
6. close and return cleanly.

Bounds:

```text
tone       300..2700 Hz
duration   100..2000 ms
```

Reject values outside those diagnostic bounds. The production tone formatter still
supports its V2 0..9999 defensive range; the tighter probe range is only an
operator-safety/diagnostic policy.

The diagnostic path must not start AutoSeq, generate FT8 symbols, or write Audio TX.

## Tests

### 1. Exact CAT TX unit tests

Using mocked MiniShell Serial, prove:

```text
begin -> MD6;TX;
tone  -> exact TA...
end   -> RX;
```

Test at least:

```text
300.00
1500.00
1520.8333
1543.75
2700.00
```

and all eight FT8 tone offsets for representative base frequencies using:

```text
spacing = 6.25 Hz
```

Verify no negative fractional field and no fraction >99.

### 2. Failure-state tests

Inject failure/short writes at:

- MD6;
- TX;
- TA;
- RX;

Prove state and cleanup semantics.

Especially:

- failed TX command => not active;
- failed TA after successful TX => active until RX cleanup;
- close while active => attempts RX then closes;
- close still releases Serial if RX restoration fails.

### 3. Diagnostic integration test

Use a PTY and real MiniShell Serial provider.

Verify exact byte stream for e.g. 20 m + 1500 Hz:

```text
MD6;FR0;FT0;FA00014074000;MD6;TX;TA1500.00;RX;
```

Verify bounded runtime and clean tty reopen.

No radio hardware in automated tests.

### 4. Architecture checks

Extend CAT-literal boundary enforcement as necessary while allowing the new
production strings only in MiniFT8 `radio_qmx`.

No `TX;`, `RX;`, or `TA` literal may appear in MiniShell core or platform code.

## Non-goals

Do not implement:

- FT8 message encoding;
- FT8 79-symbol generation;
- 160 ms symbol scheduler;
- AutoSeq physical TX;
- QSO progression driven by real TX completion;
- Audio TX;
- UAC OUT;
- tune UI;
- persistent TX endpoint settings;
- default CAT endpoint;
- ADV CAT TX;
- CAT response parsing.

## Acceptance criteria

Software/review gate:

- [ ] MiniFT8 QMX adapter implements TX begin;
- [ ] MiniFT8 QMX adapter implements tone command;
- [ ] MiniFT8 QMX adapter implements TX end;
- [ ] begin bytes are exactly `MD6;TX;`;
- [ ] end byte sequence is exactly `RX;`;
- [ ] TA formatting matches pinned V2 behavior;
- [ ] V2 fractional rounding/clamp bug fixes are preserved;
- [ ] active-TX lifecycle is explicit and fail-safe;
- [ ] close while active attempts RX restoration before Serial close;
- [ ] diagnostic test path is bounded and uses production CAT code;
- [ ] no Audio TX implementation;
- [ ] no AutoSeq or FT8 encoder integration;
- [ ] T018/T019 live RX/CAT regressions remain green;
- [ ] Linux full CTest passes;
- [ ] portable unit suite passes;
- [ ] architecture checks pass;
- [ ] real ADV build passes;
- [ ] no unrelated cleanup.

Manual architect acceptance on pc-1/QMX:

- [ ] use the real QMX CDC node;
- [ ] connect QMX to a dummy load or otherwise use an appropriate legal low-power
      test setup;
- [ ] run a 1500 Hz CAT tone for approximately 500 ms;
- [ ] QMX enters TX only for the bounded interval;
- [ ] observable RF/tone behavior is correct;
- [ ] QMX returns to RX automatically;
- [ ] normal live FT8 RX works immediately afterward;
- [ ] repeat once to prove CAT handle/TX state cleanup.

## Automated tests

Run:

```bash
git status --short

cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T020-build-unit
cmake --build /tmp/T020-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T020-build-unit --output-on-failure

PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/ft8_platform_boundary.py .

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build

git diff --check
```

No GitHub Actions wait.

## Manual / hardware validation

After supervisor review:

1. use a dummy load or appropriate legal low-power test setup;
2. identify QMX CDC:
   ```bash
   ls -l /dev/serial/by-id/ 2>/dev/null || true
   ls -l /dev/ttyACM* 2>/dev/null || true
   ```
3. run the reviewed diagnostic at 1500 Hz for about 500 ms;
4. confirm TX begins, tone/RF is present as expected, and RX is restored;
5. repeat once;
6. launch bare `ft8` and confirm live decode still works.

## Branch workflow

Use:

```text
codex/T020-linux-qmx-cat-tx
```

Codex:

1. read this task and pinned V2 CAT TX reference;
2. implement only CAT TX primitives + bounded diagnostic;
3. run all local gates;
4. set Status to REVIEW;
5. record exact files/tests and any deviations;
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

Supervisor reviews the actual `main..<commit>` diff, exact CAT bytes, TX state
machine, and fail-safe RX restoration before real RF validation.

## Architect test result

Record the actual CDC endpoint, test setup, bounded tone duration, observed TX/RX
transition, tone/RF result, repeated-cleanup result, and post-test FT8 RX result.
