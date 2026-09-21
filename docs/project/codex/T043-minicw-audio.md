# T043 — Mini-CW known-good continuous audio under MiniShell

Status: READY

## Objective

Make the T042 Mini-CW Keyer application produce the same clean sidetone behavior
as standalone Mini-CW V1.2 on Cardputer ADV.

Golden reference:

```text
repository: wcheng95/Mini-CW
commit:     3bfbf169b7c2d49a1be3e9a4c80f945edb32033e
hardware:   standalone MiniCW_V1_2.bin
result:     paddle clean, automatic M1 clean, no audible pop
```

T042 hardware foundation is accepted.

Do **not** reuse the failed T039 audio worker design as the implementation
reference. T043 must preserve the actual pinned Mini-CW continuous-audio behavior.

## Architecture

Keep Mini-CW application/domain calls above MiniShell:

```text
apps/minicw
    keyer_service
        -> audio_service_play_dit/dah()
        -> audio_service_tone_on/off()
        -> audio_service_stop_all()
        -> audio_service_is_busy()
```

Replace T042's silent private audio seam with a small MiniShell Audio extension.

The external application must still import only:

```text
mini_api_get
```

The dedicated worker/task, segment queue, codec/I2S ownership and hardware access
belong resident/platform-side below MiniShell.

## Public Audio extension

Add an optional generic continuous-tone capability to MiniShell Audio.

Exact naming/layout is an implementation choice, but it must support at least:

- open/acquire speaker tone owner;
- configure pitch 300-999 Hz;
- configure level/volume 0-99;
- enqueue one finite tone duration;
- start indefinite tone hold;
- release hold;
- stop/preempt all tone work;
- query busy state;
- close/release.

The app-facing interface must not expose CW letters, dits, dahs or Keyer-specific
concepts.

The existing PCM RX/TX API and T009/T010 timeout/progress semantics must remain
unchanged.

Applications/providers without the new capability remain source-compatible via
`struct_size` + capability checks.

## Resident ADV implementation — source of truth

Use the pinned Mini-CW V1.2 implementation as the algorithmic reference:

```text
components/audio_service/audio_service.c
components/audio_service/audio_output_port.c
components/board_cardputer_adv/board_audio.cpp
```

Preserve the important known-good behavior, including:

- dedicated continuous audio task;
- task priority 5;
- 6144-byte task stack unless exact porting proves a different resident stack
  accounting requirement;
- 64-entry bounded segment ring;
- 5 ms PCM chunks;
- 48 kHz S16 mono;
- continuous zero PCM while idle;
- codec remains enabled/unmuted between CW elements/gaps;
- prime DMA with eight zero chunks before initial unmute;
- Mini-CW's exact attack/release envelope law and phase behavior;
- finite tone segments;
- indefinite hold segment;
- preempt/flush semantics;
- busy tracking based on keyed samples committed toward DMA, including the
  generation guard used by Mini-CW;
- Mini-CW's mutex/critical ownership semantics;
- 4 x 120 I2S DMA geometry;
- Mini-CW's `esp_codec_dev_write()` board-audio transport path;
- ES8311 setup and volume behavior equivalent to the pinned reference.

Do not replace the 64-entry ring with a smaller queue merely because Keyer usually
submits one element at a time.

Do not substitute the T039 4-slot mailbox/renderer.

Do not invent a different envelope.

Where exact source cannot be reused because of MiniShell ownership/lifecycle,
preserve behavior and document the adaptation line-by-line in the task handoff.

## Mini-CW application audio wrapper

Replace T042's silent `apps/minicw/src/audio_service/audio_service.c` seam with
a wrapper that preserves the Mini-CW public/domain API currently used by Keyer
mode:

- `audio_service_init`
- volume/pitch getters/setters;
- `audio_service_tone_on/off`;
- `audio_service_play_dit/dah`;
- `audio_service_stop_all`;
- `audio_service_is_busy`;
- Morse pattern lookup used by Keyer/UI validation.

The wrapper should translate those calls into the MiniShell tone capability.
Do not move Keyer-domain behavior into MiniShell.

Feedback/trainer/text playback APIs that are not required by Keyer-only scope may
remain absent or private stubs if they are not referenced.

## Linux behavior

Provide a host-testable/Linux implementation sufficient for application tests.

Preferred:

- a portable simulated tone backend for timing/busy semantics;
- no Linux real-time audio thread is required unless it materially simplifies the
  implementation.

Do not regress existing Linux PCM Audio tests.

## Lifecycle and ownership

Tone ownership must be exclusive with ordinary speaker PCM TX on ADV.

Required:

- app begin starts with no retained tone owner;
- open failure leaves speaker resources recoverable;
- close/preempt is bounded from the app's point of view;
- app exit leaves speaker silent;
- repeated `minicw` launch/exit does not accumulate tasks/queues/codec state;
- a provider failure becomes a MiniShell error, not stale continuing sound.

The external app must own no FreeRTOS/ESP-IDF task, semaphore, codec or I2S object.

## Tests

### Reference behavior tests

Port/retain deterministic tests for the actual Mini-CW audio algorithm where
possible:

- 5 ms chunk generation;
- exact attack/release envelope checkpoints;
- phase continuity;
- idle zero PCM;
- finite dit/dah duration;
- hold on/off;
- preempt during attack/release;
- stop-all flush;
- 64-entry ring behavior;
- busy accounting/generation guard;
- eight-zero-chunk startup prime;
- continuous writes through simulated foreground/UI stalls.

### Service/API tests

Prove:

- optional capability discovery via `struct_size` and capability bit;
- old Audio objects/providers still work;
- ordinary PCM RX/TX semantics unchanged;
- tone and ordinary PCM speaker ownership exclude each other;
- invalid handles/configuration rejected;
- busy state truthful enough for Mini-CW Keyer scheduling;
- app cleanup releases retained tone owner.

### Mini-CW integration tests

Prove:

- T042 Keyer timing tests remain;
- paddle dit/dah calls map to finite resident tone work;
- straight key/Tune maps to hold/release;
- physical preemption calls stop/preempt correctly;
- automatic M1 uses the same Mini-CW Keyer timing and the new resident audio path;
- no current `apps/keyer` code is reused.

### Existing gates

Run:

- full Linux CTest;
- portable units;
- architecture/dependency/platform checks;
- real ADV firmware build;
- clean external `minicw.elf` build;
- external import inspection;
- `git diff --check`.

## Resource evidence

Record exact before/after:

- ADV firmware size;
- resident `.iram0.text`, `.dram0.data`, `.dram0.bss`;
- worker stack;
- TCB/semaphore/mutex/ring bytes;
- codec/I2S allocations if statically knowable;
- `minicw.elf` size/sections/imports;
- any dynamic heap impact or hardware free-heap measurements if available.

No PSRAM is available.

## Hardware acceptance — decisive gate

Use the same Cardputer ADV and compare directly with standalone
`MiniCW_V1_2.bin`.

### H1 — paddle

Under MiniShell -> `minicw`:

- paddle sidetone sounds clean;
- no occasional pop;
- response/rhythm feels the same as standalone Mini-CW V1.2;
- decoded UI remains live.

### H2 — automatic M1

- M1 automatic CW sounds clean;
- no character-to-character pop;
- no startup/ending click regression;
- UI remains live;
- rhythm matches standalone Mini-CW reference.

### H3 — Tune / straight key / preemption

- continuous tone starts/stops cleanly;
- physical takeover does not leave stale queued tone;
- mute/volume/pitch work.

### H4 — lifecycle

- Ctrl+C returns to MiniShell with speaker silent;
- repeated launch/exit works;
- FT8/shell Audio remains usable afterward.

T043 is **not accepted** unless both paddle and automatic M1 are pop-free.

If T043 still pops, stop and compare the ported resident worker against the pinned
Mini-CW source at the exact PCM/queue/task/codec level before any new design.

## Non-goals

Do not:

- modify Mini-CW Keyer timing/UI semantics;
- port trainer modes;
- port GPS/persistence yet;
- change existing `apps/keyer`;
- change FT8 behavior;
- add app-owned RTOS dependencies;
- reuse T039's reduced queue/worker implementation;
- optimize the known-good Mini-CW algorithm before hardware parity.

## Branch

Use:

```text
codex/T043-minicw-audio
```

Implement only T043, set status to REVIEW, push, and return exact SHA.

No PR and no hardware testing by Codex.
