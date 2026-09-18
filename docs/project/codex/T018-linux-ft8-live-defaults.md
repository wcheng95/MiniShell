# T018 — Linux MiniFT8 live QMX defaults

Status: REVIEW

## Architect intent

MiniFT8 development has returned to Linux after T017 proved the Cardputer ADV
RAM/USB-host feasibility risk. The first real MiniFT8-V3 QSO will happen on Linux/QMX.

As the first Linux completion step, make the normal operator command:

```text
M$> ft8
```

start the same working live configuration that is currently entered explicitly:

```text
M$> ft8 --profile adv --rx alsa:hw:2,0
```

This is a default-composition change only. Do not start Control/CAT or TX work in
this task.

## Objective

On the normal Linux build, bare `ft8` defaults to:

```text
presentation  ADV
RX endpoint   alsa:hw:2,0
```

while explicit command-line options continue to override those defaults.

The existing Linux ALSA provider remains the QMX UAC transport implementation.

## Current context

T017 is COMPLETE and main is the accepted baseline.

Linux/QMX live RX is already proven:

```text
QMX ALSA card 2, device 0
endpoint       alsa:hw:2,0
native         48000 Hz / S24_3LE / stereo
MiniShell      12000 Hz / S16 / stereo
```

Current application behavior:

- portable `ft8_main.c` defaults presentation to DESKTOP through
  `FT8_DEFAULT_PRESENTATION`;
- RX has no generic compile-time default endpoint;
- Linux operator currently types
  `ft8 --profile adv --rx alsa:hw:2,0`;
- ADV already demonstrates composition-time defaults without teaching FT8 domain
  logic about the platform;
- explicit `--profile` and `--rx` are established public CLI overrides;
- deterministic WAV/`--rx-slot` tests must remain hardware-independent.

The ALSA card index is the current pc-1/QMX baseline for this task. Do not add USB
VID/PID discovery, ALSA card-name discovery, udev policy, or generic device selection
yet. A later task may replace the literal endpoint if operator experience requires it.

## Source of truth

Read before editing:

```text
AGENTS.md
docs/README.md
docs/project/progress.md
docs/MiniFT8/README.md
docs/MiniFT8/development.md
docs/MiniFT8/architecture.md

apps/ft8/main/ft8_main.c
platform/linux/linux_audio_wav.c
CMakeLists.txt
tests/linux_ft8.py
```

Pinned V2 behavior remains:

```text
repository: wcheng95/Mini-FT8
commit:     491e757ae6b1e4cfd2b9a6ba10f48b35643849e0
```

No V2 source needs to be copied for this task.

## Architectural constraints

1. Keep MiniFT8 application/domain code platform-independent.
2. Do not put Linux, ALSA, QMX VID/PID, or `hw:2,0` policy into FT8 DSP,
   AutoSeq, UI, RX frontend/framer, or controller domain modules.
3. A small **generic application default hook** in `ft8_main.c` is allowed if
   Linux composition supplies the actual endpoint.
4. Prefer composition-time configuration in the Linux build, analogous in spirit
   to the existing ADV default composition.
5. Explicit CLI values win:
   - `--profile desktop` must override the new ADV presentation default;
   - `--profile adv` remains valid;
   - explicit `--rx <endpoint>` must override the live default;
   - deterministic WAV + `--rx-slot` behavior must remain unchanged.
6. Preserve the existing rule that `--rx-slot` is deterministic fixture timing;
   do not silently reinterpret it as a live-QMX timing option.
7. Do not change the FT8 engine oversampling profile. Linux remains at its current
   engine baseline; this task is about presentation and RX endpoint defaults only.
8. Do not change ALSA conversion, buffering, discontinuity behavior, timing, or the
   Audio public API.
9. Do not change ADV behavior.
10. No public MiniShell API expansion.

## Implementation scope

Expected bounded shape:

### 1. Generic FT8 RX default hook

Add the smallest generic mechanism so composition can supply a default RX endpoint
when the user did not pass `--rx`.

Requirements:

- portable/unconfigured fallback remains no default RX endpoint;
- explicit `--rx` wins;
- do not hardcode `alsa:`, QMX, or Linux inside shared FT8 application logic;
- preserve existing option validation, especially deterministic `--rx-slot`.

A compile-time `FT8_DEFAULT_RX_ENDPOINT`-style hook is acceptable.

### 2. Linux composition defaults

Configure the normal Linux `ft8.so` build so that:

```text
FT8_DEFAULT_PRESENTATION = ADV
FT8 default RX endpoint  = alsa:hw:2,0
```

Use target/source compile composition rather than platform branching inside FT8
domain modules.

### 3. Regression tests

Existing Linux tests currently use bare `ft8` for some hardware-independent UI
checks. Those tests must not become dependent on a physically connected QMX.

Update tests narrowly so deterministic test launches explicitly select their RX
fixture (and presentation where the test specifically needs DESKTOP behavior).

Add a regression that proves the Linux build composition contains the intended bare
defaults without requiring ALSA hardware in CI.

Also prove explicit overrides remain available.

### 4. Documentation

Update the T018 packet with implementation/test evidence. Do not promote the new
bare command into canonical current-state docs until supervisor review + architect
live validation pass.

## Non-goals

Do not implement:

- MiniShell Control/CAT;
- QMX CDC command transmission;
- Audio TX/UAC OUT;
- FT8 waveform generation;
- physical TX;
- AutoSeq changes;
- logging changes;
- ALSA/udev QMX auto-discovery;
- stable card-name or VID/PID discovery;
- new CLI options such as `--no-rx`;
- ADV changes;
- `freq_osr` changes;
- RX DSP/timing changes;
- UI redesign.

## Acceptance criteria

Software/review gate:

- [x] bare Linux `ft8` composition defaults to ADV presentation;
- [x] bare Linux `ft8` composition defaults RX to `alsa:hw:2,0`;
- [x] shared FT8 logic contains no Linux/ALSA/QMX platform identity;
- [x] `--profile desktop` overrides the presentation default;
- [x] explicit `--rx /flash/kfs.wav --rx-slot 12345` overrides live RX and remains deterministic;
- [x] existing explicit live command retains option selection (live hardware validation below):
      `ft8 --profile adv --rx alsa:hw:2,0`;
- [x] Linux tests do not require QMX hardware;
- [x] Linux full CTest is green;
- [x] portable unit suite is green;
- [x] architecture boundary checks are green;
- [x] ADV build remains green if shared `ft8_main.c` is touched;
- [x] no unrelated cleanup.

Manual architect acceptance on pc-1/QMX:

- [ ] with QMX connected, bare `M$> ft8` enters ADV 20x7 presentation;
- [ ] bare `ft8` opens the live QMX UAC/ALSA stream and decodes on-air FT8;
- [ ] `q` returns cleanly to `M$>`;
- [ ] explicit deterministic WAV launch still works;
- [ ] explicit `--profile desktop` still selects DESKTOP when desired.

## Automated tests

Use the established local gates:

```bash
git status --short

cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T018-build-unit
cmake --build /tmp/T018-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T018-build-unit --output-on-failure

PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/ft8_platform_boundary.py .

# Shared ft8_main.c regression only; no ADV hardware test in T018.
source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build

git diff --check
```

No GitHub Actions wait.

## Manual / hardware validation

After supervisor review:

1. connect/tune QMX on pc-1 as for the already-proven Linux live-RX path;
2. launch MiniShell;
3. at `M$>`, run only:
   ```text
   ft8
   ```
4. confirm ADV presentation;
5. confirm real live FT8 decodes;
6. quit cleanly;
7. run an explicit WAV fixture launch to prove override still works;
8. optionally run `ft8 --profile desktop --rx alsa:hw:2,0` to prove presentation
   override independently from RX selection.

## Branch workflow

Use:

```text
codex/T018-linux-ft8-live-defaults
```

Codex:

1. read this task and canonical docs;
2. implement only the bounded default-composition change;
3. update deterministic Linux tests so no physical QMX is required;
4. run all required local gates;
5. set Status to REVIEW;
6. record exact files, commands/results, and any deviations;
7. commit and push one reviewable commit;
8. return commit SHA;
9. no PR;
10. no Actions wait.

## Codex implementation notes

### Implementation summary

Linux `ft8.so` now composes ADV presentation and `alsa:hw:2,0` as defaults.
The generic RX endpoint hook falls back to NULL in unconfigured builds and is
applied only after parsing and validation. Explicit `--rx` overrides it without
weakening duplicate-option validation. `--rx-slot` still requires an explicit
`--rx`; a live default cannot silently satisfy the fixture-timing requirement.

### Files changed

- `apps/ft8/main/ft8_main.c`: generic `FT8_DEFAULT_RX_ENDPOINT` fallback and
  post-validation default selection.
- `CMakeLists.txt`: Linux target defaults and two parser regression targets.
- `tests/ft8_options_test.c`: execute the production parser with actual Linux
  target compile definitions and separately without composition definitions.
  Test bare defaults, explicit profiles/endpoints, fixture option order, and
  invalid/duplicate options without initializing any transport. Unused app entry
  code is discarded at link time; no production test hook or API was added.
- `tests/linux_ft8.py`: use explicit short canonical WAV fixtures for UI checks
  and explicit DESKTOP where required; exercise the default ADV presentation
  during the existing full kfs decode/queue regression.
- `tests/linux_ft8_config_load.py`: supply the short WAV explicitly so the
  successful station-creation case needs no QMX. Load-failure assertions remain.
- This task packet: REVIEW status, software checklist and handoff evidence.

### Invariants preserved

No platform identity entered shared FT8 source. Linux engine time_osr=2/freq_osr=2,
ALSA transport/conversion/buffering, fixture timing, AutoSeq, logging and public
APIs are unchanged. ADV composition still supplies its existing explicit
`uac:qmx` argument and ADV presentation, with its existing freq_osr=1 profile.
The UI-only fixtures contain one silent 12 kHz/S16/stereo frame and reach EOF,
preserving the existing RX: OFF memory-view assertion without a physical device.
Canonical current-state documentation was not promoted before live acceptance.
No deviations from the bounded task.

### Local tests run

```sh
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux -R 'ft8_options|linux_ft8' --output-on-failure
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
cmake -S tests/unit -B /tmp/T018-build-unit
cmake --build /tmp/T018-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T018-build-unit --output-on-failure
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/ft8_platform_boundary.py .
source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build
git diff --check
```

Focused tests **4/4 PASS**; Linux full CTest **47/47 PASS** (including the
previously configured pinned golden engine tests at freq_osr=1 and 2); unit
suite **14/14 PASS**; all three standalone architecture checks PASS; real ADV
build PASS; whitespace check PASS. The new Linux parser target inherits the
`ft8` target's actual definitions, also confirmed in generated compiler flags;
the portable parser target has no default overrides. ADV firmware size is
`0xb8e40`, with `0x5371c0` (88%) app-partition space free.

### Manual/hardware validation still required

After supervisor review, perform the pc-1/QMX bare-command live decode and clean
quit checks above, plus explicit WAV and DESKTOP overrides. No live QMX or ADV
hardware testing, flashing, PR creation, or GitHub Actions wait was performed.

### Known limitations / risks

The live default uses the approved host-specific ALSA card index 2, device 0.
Hosts with a different enumeration must provide an explicit RX endpoint. There
is no discovery or fallback to WAV when the selected live device cannot open.
Automated coverage proves composition, parsing and deterministic application
behavior; it does not establish on-air decode for the new bare command.

### Commit

One commit titled `T018: default Linux FT8 to live QMX and ADV presentation` on
`codex/T018-linux-ft8-live-defaults`; the pushed SHA is returned in chat.

## Supervisor review

Supervisor reviews the actual `main..<commit>` diff before live pc-1 validation.

## Architect test result

Record bare-`ft8` Linux/QMX live validation and final acceptance here.
