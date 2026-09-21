# T045 — Mini-CW Keyer UI / I/O mode cleanup, audio-frozen

Status: READY

## Golden baseline / recovery

Current stable checkpoint:

```text
main: da934b03bce4cc8f908fbc1a40afed501a37196d
recovery: golden/minicw-persistence-clean-audio
```

Hardware truth:

```text
MiniShell -> minicw.elf
persistence   PASS
paddle        clean / no pop
automatic M1  clean / no pop
```

The earlier audio-only recovery point also remains:

```text
golden/minicw-clean-audio
48a40d79c13ed60ef9f8444a060164852d226fcd
```

Any audible regression is an immediate stop/revert condition.

## Objective

Make four bounded Keyer UI/model cleanups:

1. Operation/settings menu moves from **Ctrl** to **Opt**.
2. KeyIn user options become:
   - Paddle-Normal (**PDN**)
   - Paddle-Reverse (**PDR**)
   - SK-Tip (**SKT**)
   - SK-Ring (**SKR**)
   - SK-Both (**SKB**)
3. KeyOut user options become:
   - SK-Normal (**SKN**)
   - SK-Mono (**SKM**)
   - OFF (**OFF**)
4. Normal and Operation screens use the persistent 20-column top line:

```text
HH:MM KIN KOUT WW Vnn
```

Example:

```text
09:37 PDN SKN 20 V80
```

This is an external `minicw.elf` change only. No resident MiniShell change is
expected.

## Absolute audio freeze

Do not modify:

```text
include/minishell/api.h
core/minishell_services/audio_service.c
platform/common/tone_stream.c
platform/common/tone_stream.h
platform/common/tone_sim.c
platform/common/tone_sim.h
platform/adv/adv_audio_speaker.cpp
apps/minicw/src/audio_service/audio_service.c
```

Do not alter:

- Tone API;
- worker priority/stack;
- segment ring;
- 5 ms renderer;
- envelope/DDS/phase;
- startup prime;
- codec/I2S/DMA;
- `esp_codec_dev_write()`;
- busy accounting;
- existing paddle/M1 timing;
- persistence quiet-save policy.

Resident firmware must remain byte-for-byte identical to the golden baseline.

## 1. Operation moves from Ctrl to Opt

Current Keyer settings/Operation menu is toggled by the logical Ctrl key.

Change it so:

- Opt toggles Operation at top level;
- Opt while already in Operation exits Operation;
- Ctrl alone has no Keyer UI action;
- Ctrl+C continues to exit the application exactly as today;
- Alt remains the M1-M5 shortcut overlay;
- Fn/navigation/edit controls remain unchanged.

Do not change the raw MiniShell Input API. Use the existing logical
`MINICW_INPUT_EVENT_OPT`.

## 2. KeyIn model

User-facing options and codes:

| Option | top-line code |
| --- | --- |
| Paddle-Normal | PDN |
| Paddle-Reverse | PDR |
| SK-Tip | SKT |
| SK-Ring | SKR |
| SK-Both | SKB |

Use **Paddle-Reverse** as the canonical human label. Persistence may accept
`Paddle_Reverse` as an alias.

### Existing behavior

PDN, PDR, SKT and SKR retain their current electrical/timing behavior exactly.

### New SK-Both behavior

SKB treats either physical input contact as the same straight key:

```text
key_down = tip_pressed || ring_pressed
```

It must reuse the existing straight-key timing/decoder/audio/KeyOut path.

Behavior:

- first asserted contact begins one straight-key element;
- while either contact remains asserted, the element remains down;
- the element ends only when both contacts are released;
- pressing the second contact while the first is held does not begin a second
  element;
- adaptive SK timing and decode behavior are the same as SKT/SKR;
- sidetone hold/release calls are unchanged.

If SKB physical input cancels automatic TX:

- every currently asserted KeyIn contact involved in that cancel is consumed
  until that contact is released;
- no stale second contact may immediately re-key after cancellation.

Add SKB at the end of the KeyIn enum so existing PDN/PDR/SKT/SKR numeric values
remain stable.

KeyIn cycling order:

```text
PDN -> PDR -> SKT -> SKR -> SKB -> PDN
```

For display WPM:

- PDN/PDR use KeyIn WPM;
- SKT/SKR/SKB use current adaptive SK WPM.

## 3. KeyOut model

User-visible options are only:

```text
SK-Normal (SKN)
SK-Mono   (SKM)
OFF       (OFF)
```

Electrical behavior must reuse the already-tested existing modes:

### SKN

Equivalent to current `KEYER_KEY_OUT_SK`:

- idle: tip released, ring released;
- key down: tip active and ring active;
- element end: both released.

### SKM

Equivalent to current `KEYER_KEY_OUT_SK_M`:

- idle: tip released, ring active;
- key down: tip active, ring active;
- shutdown: both lines released.

### OFF

Both lines always released.

The old Paddle / PaddleR KeyOut choices are no longer user-selectable.

For minimum risk, legacy internal enum constants may remain, but:

- UI cycling must only expose SKN -> SKM -> OFF;
- runtime setter/config application must canonicalize legacy Paddle/PaddleR to
  SKN so the application cannot remain in an obsolete output mode;
- do not rewrite the proven SK/SK-M electrical implementation.

## 4. Persistent 20-column header

Use this header on both:

- normal Keyer screen;
- Operation/settings pages.

Exact format:

```text
HH:MM KIN KOUT WW Vnn
```

Fields:

- HH:MM = UTC from MiniShell Time/Location;
- KIN = PDN/PDR/SKT/SKR/SKB;
- KOUT = SKN/SKM/OFF;
- WW = two-digit displayed WPM;
- Vnn = two-digit volume 00..99.

This is exactly 20 columns.

Examples:

```text
00:05 PDN SKN 19 V80
23:59 SKB SKM 27 V05
--:-- PDR OFF 20 V99
```

If MiniShell UTC is unavailable or `utc_get()` fails, display `--:--`.
UTC availability must remain optional; failure must not prevent `minicw`
startup.

Do not add RTC ownership, UTC setting, or persistence of time.

Tune/status remain in the existing lower status line; they do not replace the
new header.

The current dormant OP-name lookup display must not override the fixed header.
Callsign lookup/table loading is a separate future task.

## Operation menu labels

Use the human-readable long names in Operation pages where the current mode is
shown.

Fit within 20 columns by shortening the field prefix, e.g.:

```text
5 In:Paddle-Normal
5 In:Paddle-Reverse
5 In:SK-Both
6 Out:SK-Normal
6 Out:SK-Mono
6 Out:OFF
```

Do not redesign the rest of the Operation pages.

## Persistence compatibility

T044A persistence remains active and must continue to work.

Canonical serialization after T045:

### KeyIn

```text
Paddle-Normal
Paddle-Reverse
SK-Tip
SK-Ring
SK-Both
```

### KeyOut

```text
SK-Normal
SK-Mono
OFF
```

Backward-compatible parser aliases must include the existing T044A / standalone
Mini-CW values:

KeyIn:

```text
Pdl / Paddle                  -> PDN
Pdl-R / PdlR / PaddleR /
Paddle-R / Paddle_R           -> PDR
SK-T / SKT                    -> SKT
SK-R / SKR                    -> SKR
Paddle_Reverse                -> PDR
```

KeyOut:

```text
SK                             -> SKN
SK-M / SKM / SK-Mono          -> SKM
OFF                            -> OFF
Pdl / Paddle / Pdl-R / PaddleR -> SKN   (legacy removed modes)
```

Existing numeric aliases may remain compatible. Do not introduce a numeric
serialization format.

An existing T044A `/flash/minicw/setting.txt` must load successfully without
manual editing. The next successful settings save may canonicalize it to the new
labels.

## Architecture / scope

Allowed production changes are expected only under:

```text
apps/minicw/
```

plus tests/docs/external ELF packaging if alignment requires it.

No changes to:

- resident MiniShell;
- public API;
- existing `apps/keyer`;
- FT8;
- audio implementation;
- persistence transaction/quiet-save mechanics.

Do not add heap allocation.

## Tests

### Input/UI

Prove:

- Opt opens Operation;
- Opt closes Operation;
- Ctrl alone does nothing;
- Ctrl+C still exits;
- Alt M1-M5 remains unchanged;
- exact 20-character headers for all KeyIn/KeyOut modes;
- `--:--` when UTC unavailable;
- Operation pages retain the same header;
- volume/WPM zero padding;
- SK modes display adaptive SK WPM.

### KeyIn

Prove:

- PDN/PDR behavior unchanged;
- SKT/SKR behavior unchanged;
- SKB tip-only press;
- SKB ring-only press;
- SKB both simultaneous;
- SKB one-contact then second-contact overlap;
- SKB does not end until both release;
- SKB adaptive decode path matches straight-key behavior;
- automatic-TX cancellation consumes every asserted SKB contact until release.

### KeyOut

Prove:

- UI cycle is SKN -> SKM -> OFF only;
- SKN electrical behavior is unchanged current SK;
- SKM electrical behavior is unchanged current SK-M;
- OFF remains released;
- legacy Paddle/PaddleR setter/config values canonicalize to SKN;
- shutdown still releases both wires in every mode.

### Persistence

Prove:

- existing T044A file loads unchanged;
- old labels map to new modes;
- new canonical labels round-trip;
- SKB persists;
- legacy removed KeyOut labels load as SKN;
- persistence quiet-save tests remain unchanged/passing.

### Time

Use injected MiniShell `utc_get()` in host tests:

- midnight;
- ordinary time;
- 23:59;
- failure/unavailable -> `--:--`.

Do not require UTC for application availability.

### Regression

Run:

- full Linux CTest;
- portable units;
- focused Mini-CW tests;
- architecture/boundary checks;
- real ADV firmware build;
- clean external ELF build/import inspection;
- `git diff --check`.

Before handoff explicitly report:

```text
protected audio diff     NONE
resident firmware BIN    identical
resident SRAM delta      0
external ELF size        exact
resident imports         mini_api_get only
```

## Hardware acceptance

Install only the new external `minicw.elf` over the current validated resident
MiniShell firmware.

Test in this order:

1. paddle + M1 audio first — must remain clean/no pop;
2. header exactly resembles `HH:MM PDN SKN 20 V80`;
3. Opt opens/closes Operation; Ctrl no longer does;
4. cycle all five KeyIn modes;
5. verify SKT, SKR and especially SKB physical behavior;
6. cycle only SKN/SKM/OFF KeyOut;
7. verify existing settings survive/reload;
8. exit/relaunch and verify persistence;
9. Tune/straight key remain clean;
10. Ctrl+C returns silent.

Any audio regression is immediate failure. Do not tune audio inside T045.

Recovery:

```text
golden/minicw-persistence-clean-audio
da934b03bce4cc8f908fbc1a40afed501a37196d
```

## Non-goals

Do not:

- load callsign lookup tables;
- add GPS;
- add trainer modes;
- add battery/sleep;
- redesign M1-M5;
- change Tune behavior;
- change persistence save timing;
- change audio;
- change MiniShell resident code.

## Branch

Use:

```text
codex/T045-minicw-ui-io-cleanup
```

No PR and no hardware testing by Codex.

Set T045 to REVIEW, push one bounded implementation commit, and return exact SHA
with tests and binary/audio guard evidence.
