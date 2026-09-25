# T070 — ADV LCD backlight PWM hardware fix

Status: CANCELLED

## Architect decision — 2026-09-24

The architect dropped the brightness feature after the first ADV hardware failure. **Do not implement T070. No implementation is required.** T071 removes the merged T069 brightness code and documentation while preserving the accepted `startup=` behavior.

## Original architect intent

T069 startup sequencing is hardware-accepted, but `brightness=` did not change
the real Cardputer ADV LCD backlight. Fix only the ADV brightness hardware path.

Do not redesign resident settings or startup behavior.

## Objective

Make the already-accepted resident setting:

```text
brightness=1..100
```

produce an observable and monotonic LCD backlight change on real Cardputer ADV
hardware, while preserving all T069 parsing/boot semantics and the public API.

## Current hardware evidence

T069 ADV validation on 2026-09-24:

```text
startup=...      PASS
brightness=...   FAIL / no observable effect
```

The current T069 implementation routes ADV brightness to:

```cpp
M5.Display.setBrightness(...)
```

Unit tests only mocked that method, so they proved the call/mapping but not the
real ESP32-S3 LEDC/backlight hardware.

Cardputer ADV hardware and the pinned M5GFX implementation identify LCD
backlight as:

```text
GPIO38
LEDC channel 7
256 Hz
ST7789V2 backlight
```

The ADV backend pins:

```text
M5GFX      0.2.17
M5Unified  0.2.11
ESP-IDF    5.5.x reference family
```

Before coding, inspect the exact locally resolved M5GFX 0.2.17 backlight setup
and duty mapping used by the ADV build. Do not assume a newer upstream version.

## Source of truth

Read:

```text
AGENTS.md
docs/README.md
docs/architecture/configuration.md
docs/project/codex/T069-resident-startup-brightness.md
platform/adv/README.md
platform/adv/adv_display.cpp
platform/adv/adv_backend.c
platform/adv/main/idf_component.yml
```

Also inspect the actual M5GFX 0.2.17 source resolved by the ADV build, especially
its Cardputer/CardputerADV autodetect and `Light_PWM` implementation.

## Architectural constraints

- Preserve `MINISHELL_API_VERSION 3`; do not edit `include/minishell/api.h`.
- Preserve T069 resident parser and startup sequencing unless a tiny test-only
  adjustment is required.
- Brightness remains a private ADV backend concern.
- Do not add a public Display brightness capability.
- Do not call `M5.begin()`; MiniShell deliberately initializes only
  `M5.Display.begin()` to avoid claiming audio/I2S resources.
- Do not change keyboard, audio, USB, WebFS, filesystem, RTC/GPS, or app behavior.
- Keep LCD backlight ownership local to `adv_display`.
- No heap allocation for brightness control.
- Do not introduce an interactive brightness command in this task.

## Implementation direction

The real-hardware failure means the fix must exercise the actual backlight PWM,
not merely call or mock the same high-level method again.

Preferred approach:

1. After `M5.Display.begin()` has successfully initialized the panel, establish
   explicit ADV backlight PWM control for GPIO38 using the ESP-IDF LEDC driver,
   matching the pinned M5GFX/CardputerADV configuration.
2. Keep one small backend-owned function that accepts `1..100`.
3. Convert percentage to a monotonic PWM duty. Preserve the pinned M5GFX
   Cardputer behavior/minimum-offset semantics where practical so low nonzero
   values remain lit rather than behaving like OFF.
4. Apply the configured duty when T069 invokes the existing private
   `minishell_platform_display_brightness()` hook.
5. `brightness=100` must produce full backlight.
6. Values outside 1..100 remain ignored, as in T069.

If a different direct ESP-IDF mechanism is cleaner after inspecting the pinned
component source, use it, but document why. The key requirement is that the
real GPIO38 PWM changes independently of whether the M5GFX wrapper later changes
internals.

Do not duplicate panel drawing or reinitialize the display.

## Important hardware note

GPIO38 is also associated with the ADV RGB LED power-enable path. Brightness PWM
therefore may affect RGB behavior. T070 does not add RGB support, but the fix must
not accidentally force GPIO38 permanently high or otherwise defeat LCD
brightness control merely to preserve hypothetical RGB behavior.

## Non-goals

- resident settings parser changes;
- startup sequencing changes;
- startup aliases;
- relative paths / cd / pwd;
- cp/mv directory semantics;
- command history;
- pathname completion / Tab choices;
- clear command;
- display sleep/dimmer policy;
- RGB LED feature work;
- application UI brightness controls;
- M5 library upgrades.

## Acceptance criteria

- [ ] T069 startup tests and behavior remain unchanged.
- [ ] Public MiniShell API blob is unchanged.
- [ ] ADV build uses explicit backend-owned real backlight PWM control.
- [ ] `brightness=1` maps to a low nonzero duty.
- [ ] `brightness=25`, `50`, `75`, `100` are strictly monotonic.
- [ ] `brightness=100` maps to full backlight duty.
- [ ] Invalid percentages are ignored.
- [ ] Display initialization still succeeds without `M5.begin()`.
- [ ] Existing ADV display color/scrollback tests still pass.
- [ ] Full Linux CTest passes.
- [ ] Real ADV firmware builds successfully.
- [ ] Real ADV hardware visibly changes brightness across at least low / middle /
      full values after reboot.

## Automated tests

Add focused tests for the pure percentage-to-duty mapping and any new ADV
backlight state/guard logic. Do not satisfy this task only by extending an
`M5.Display.setBrightness()` mock.

At minimum run:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
ctest --test-dir build-linux --output-on-failure
idf.py -C platform/adv build
git diff --check
```

Also run focused ADV display and T069 resident/startup tests.

## Manual / hardware validation

Architect/tester validates on Cardputer ADV with no startup app required.

Use:

```text
brightness=1
startup=
```

reboot and observe a clearly dim display.

Then repeat with:

```text
brightness=50
startup=
```

and:

```text
brightness=100
startup=
```

Expected:

```text
1 < 50 < 100 visibly
100 = normal/full brightness
```

Then restore a real startup line, for example:

```text
brightness=50
startup=ft8;b
```

and confirm T069 startup still behaves normally.

## Codex branch / handoff

Work on:

```text
codex/T070-adv-backlight-pwm
```

Start from current `main`.

Before coding, read `AGENTS.md`, `docs/README.md`, T069, and this packet.

Keep T070 to one reviewable implementation commit. Do not merge to `main` and
do not open a PR unless asked.

## Codex implementation notes

### Implementation summary

### Files changed

### Invariants preserved

### Local tests run

### Manual/hardware validation still required

### Known limitations / risks

### Commit

## Supervisor review

Supervisor fills this after reviewing the actual `main..<commit>` diff and
local test evidence.

## Architect test result

Record ADV brightness validation here.