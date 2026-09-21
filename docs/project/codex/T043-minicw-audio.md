# T043 — Mini-CW known-good continuous audio under MiniShell

Status: REVIEW

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

## Implementation handoff

Baseline: `8d7ce15828a8fee176d9b7cf00dbb06573a4f8b0` (hardware-accepted T042).
Branch: `codex/T043-minicw-audio`. Commit reference: the single implementation
commit containing this handoff; exact SHA returned after push.

### Implementation summary / files changed

- `include/minishell/api.h`: optional `MINI_AUDIO_CAP_TONE`, configuration/handle
  types and a tail-appended tone table. API version remains 3; existing PCM
  tables/signatures/layout prefixes and timeout/progress behavior are unchanged.
- `core/minishell_services/{audio_service.c,minishell_services.h}`: optional
  private provider table, validation, fresh public handles, PCM-TX/tone exclusion
  and application cleanup. RX remains independent. Old providers can omit tone.
- `platform/common/tone_stream.{c,h}`: pinned Mini-CW DDS, envelope, 64-entry ring,
  cursor, flush and committed-sample generation accounting. No CW text/letters,
  trainer, application scheduler or hardware calls enter this generic module.
- `platform/adv/adv_audio_speaker.cpp`: reference continuous worker and codec
  transport, startup prime, recoverable lifecycle and fail-silent I/O handling.
  Existing ordinary `speaker_write()` is byte-for-byte unchanged. The only added
  ordinary-open action is reaping an exceptionally delayed retired tone owner.
- `platform/common/tone_sim.{c,h}` and Linux service preparation: silent,
  monotonic-clock-driven simulation of the same renderer for Linux tests.
- Mini-CW's private audio wrapper/port maps existing domain calls onto tone;
  no Keyer timing/UI source changes. Capability absence retains T042's silent
  timing fallback; an advertised provider's operation failure propagates to the
  existing Mini-CW error/cleanup path rather than silently falling back.
- Build registration, Audio API contract, Mini-CW README, focused tests and this
  packet updated. `apps/keyer/**`, `apps/ft8/**`, USB/UAC ownership, Wi-Fi,
  foreground stack and ordinary PCM implementations are unchanged.

### Pinned-source mapping and adaptation audit

Reference paths below are at
`3bfbf169b7c2d49a1be3e9a4c80f945edb32033e`, verified against the local Mini-CW
checkout. No T039 worker/renderer was used.

| Original source lines / operation | Port and exact adaptation |
| --- | --- |
| `audio_service.c:34–69`, stream/DDS constants | 48,000 Hz, 240-frame/5 ms chunks, 5 ms edges, amplitude 12,000, 64 segments, 257-entry quarter-wave Q15 LUT, 64-bit phase and 10-bit interpolation retained. Stack 6,144 and priority 5 live in the ADV worker. |
| `:77–93`, segment definition | Same enum, duration, attack and release fields; 12 bytes/entry on Xtensa. No replacement mailbox. |
| `:143–180`, globals/ownership | Pitch, LUT, phase, ring, cursor, flush/hold and busy-generation state retained in the resident renderer. Mutex/spinlock callbacks replace direct RTOS access in the portable renderer. Unused Morse-text source and WPM/Farnsworth state stay out of the generic backend. |
| `:219–366`, DDS/envelope math | Copied functions unchanged. This includes `llroundl`, runtime sine-table construction, integer lookup/truncation, short-tone edge splitting, release indexing and amplitude scaling. |
| `:383–408`, conversions/locks | Sample conversion unchanged. Lock wrappers now call injected segment-mutex functions. ADV commands retain 20 ms mutex acquisition, and the worker uses an indefinite segment-mutex wait as in the reference. |
| `:436–503`, generation and ring | Same add/subtract/clear generation guard and bounded push/pop/clear. Only `portENTER/EXIT_CRITICAL` spellings become injected callbacks. |
| `:510–538`, preempt/enqueue | Same ring flush, hold reset, busy clear/increment-generation and flush request. Removed only inactive text-source state. Public admission validates 1–60,000 ms and reports a full ring as `MINI_ERR_NO_SPACE` rather than silently dropping a tone. The duration bound also bounds the full-ring sample counter. |
| `:555–637`, cursor gain/release/refill | Copied unchanged apart from `portMAX_DELAY` being represented by `UINT32_MAX` at the private lock seam. |
| `:691–705`, startup prime | ADV writes eight zero chunks while muted, then unmutes. Every write and unmute result is checked; a failed prime does not proceed as a successful owner. |
| `:708–725`, per-chunk control | Same phase-increment calculation and pending-flush processing. Pitch read is placed under the segment mutex to synchronize generic `configure`; no numeric calculation changes. Only unused text expansion is removed. |
| `:727–763`, per-sample rendering | Exact copied loop. Phase advances for keyed samples, including shaped release, and is retained without advancement through idle/silence. It is never reset at each element. Hold gain position freezes after attack, while tone phase continues. |
| `:765–781`, PCM write/commit | Same order: render, synchronous codec write, generation-checked busy decrement. Unlike the standalone retry loop, failed transport latches `MINI_ERR_IO`, mutes and retires; failed mute also disables TX I2S. No successful busy commit is reported for a failed write. |
| `:785–821`, initialization/task | Same LUT initialization and dedicated `xTaskCreate`, no affinity override, priority 5, 6,144-byte stack. Allocation/startup failures unwind. Startup/done semaphores and an atomic closing/error state are new solely for MiniShell application lifetime. |
| `:824–853`, pitch/volume | Generic configure validates 300–999 Hz / 0–99 and uses ES8311 output volume. Pitch-only changes do not rewrite unchanged codec volume or reset phase. Mini-CW getters/local defaults remain app-owned. |
| `:884–947`, feedback/hold/dit/dah | Mini-CW feedback preempts and enqueues 50 ms. Each dit/dah becomes exactly one finite duration; hold-on clears pending work and releases the old cursor before hold; hold-off flushes and releases only when held. No letters or element names cross the public API. |
| `:1039–1056`, stop/busy | Same preempt and keyed-outstanding-or-hold interpretation; unused text-active term omitted. Stop clears busy immediately while the old cursor's release tail is still being emitted. |
| `audio_output_port.c` / `board_audio.cpp` | Uses the existing ADV shared I2C bus and exact matching BCLK41/DOUT42/WS43/DIN46/no-MCLK mapping, ES8311 BOTH, 48 kHz S16 mono and 4×120 DMA. Tone startup adds the reference input gain 30 dB and three 480-frame ADC-discard reads. Tone PCM calls `esp_codec_dev_write(s_codec, pcm, 480)`; ordinary PCM stays direct I2S. |
| Standalone process lifetime | Added close: request release, emit the tail plus eight zero chunks to drain DMA, mute, signal worker retirement, then release codec/interfaces/I2S and all synchronization allocations. Join waits up to 3 seconds. Timeout retains explicit private ownership for deterministic later tone/PCM-open reaping; no running task/resource is freed. |

`tests/tone_reference.json` fingerprints the pinned source, not a second DSP
implementation. `tone_reference_test.py` proves 18 copied functions and the
sample loop match, normalizing only whitespace/comments and synchronization
adapter spellings. The semantic changes above are the complete intended worker
adaptations; there is no envelope/queue/task/DMA optimization.

### Tests run and results

```sh
cmake -S . -B build-linux
cmake --build build-linux -j8
ctest --test-dir build-linux --output-on-failure
# PASS: 82/82
cmake -S tests/unit -B /tmp/T043-unit
cmake --build /tmp/T043-unit -j8
ctest --test-dir /tmp/T043-unit --output-on-failure
# PASS: 24/24
ctest --test-dir build-linux -R 'tone|minicw' --output-on-failure
# PASS: 8/8
ctest --test-dir build-linux -R 'architecture|boundary' --output-on-failure
# PASS: 11/11
python3 tests/app_dependency_boundary.py . minicw
python3 tests/app_platform_boundary.py . minicw
# PASS
python3 tests/tone_reference_test.py .
python3 tests/adv_tone_worker_test.py .
# PASS

source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build
# PASS: real ESP32-S3 ADV firmware
idf.py -C platform/adv/elf_apps/minicw fullclean
idf.py -C platform/adv/elf_apps/minicw elf
# PASS: clean 1073-step external build
xtensa-esp32s3-elf-readelf -rW platform/adv/elf_apps/minicw/build/minicw.app.elf
python3 tests/minicw_elf_inspect.py platform/adv/elf_apps/minicw/build/minicw.app.elf
# PASS: sole import mini_api_get, 518 mapped relocations, packed alignment valid
xtensa-esp32s3-elf-size -A platform/adv/build/minishell_adv.elf
xtensa-esp32s3-elf-size -A platform/adv/elf_apps/minicw/build/minicw.app.elf
git diff --check
# PASS
```

The first full Linux run had one failure in the unchanged serial PTY test's
filled-output-queue timeout assertion while builds were running. Full reruns
passed 82/82; no serial code or test assertion was changed. As in T042,
`readelf` prints the upstream packaging's removed-`.dynamic` diagnostic while
printing relocations; the independent ELF inspector passes.

Coverage added/retained:

- exact 5 ms chunks and envelope checkpoints, short-tone edges, phase retention,
  idle zeros, finite duration, hold/release, preemption during attack/release,
  full 64-entry ring, stop flush and stale in-flight commit generation rejection;
- threaded host compilation of the **actual ADV worker**, eight-zero startup
  prime before unmute, continuing writes while the foreground sleeps, unchanged
  direct-I2S PCM path, three repeated lifetimes, each semaphore allocation
  failure, task/hardware/prime/write/volume/mute failures, bounded-close timeout
  and later reap/reopen;
- optional/truncated capability discovery, old PCM-only providers, invalid
  configuration/handles, truthful finite/hold busy state, mutual PCM TX exclusion,
  stale handles and app-end cleanup;
- all T042 physical timing/decoder cases retained, repeated through an observed
  resident command seam (one 63 ms dit / 189 ms dah, hold transitions and physical
  preempt stop). Existing UI, M1/repeat, Tune, delayed TX and cleanup traces also
  repeat through the actual simulated renderer and new public wrapper.

### Resource evidence

ESP-IDF v5.5.4 / Xtensa GCC 14.2.0, no PSRAM. Baseline firmware was built before
editing and compared with the final real build:

| Resident resource | Baseline | T043 | Delta |
| --- | ---: | ---: | ---: |
| Firmware BIN bytes | 1,375,776 | 1,381,280 | +5,504 |
| `.iram0.text` | 63,959 | 63,959 | 0 |
| `.dram0.data` | 27,000 | 27,016 | +16 |
| `.dram0.bss` | 38,864 | 40,336 | +1,472 |
| Static internal SRAM delta | — | — | **+1,488** |

The 64-entry ring is **768 resident BSS bytes**; the sine LUT is 514 bytes,
already included in the BSS delta above. No RTOS queue allocation replaces it.

Dynamic worker resource sizes, measured by compiling `sizeof` probes with the
actual ADV `compile_commands.json` flags and inspecting the resulting Xtensa
objects with `nm -S`:

| Allocation | Requested bytes |
| --- | ---: |
| Worker stack, unchanged from golden reference | 6,144 |
| TCB (`StaticTask_t` size in this FreeRTOS build) | 340 |
| Segment mutex | 84 |
| Startup binary semaphore | 84 |
| Retirement binary semaphore | 84 |
| Worker/synchronization subtotal | **6,736** |
| RX + TX DMA sample buffers: 2×4×120×2 | 1,920 |
| Eight 12-byte DMA descriptors | 96 |
| Two descriptor-pointer and two buffer-pointer arrays | 64 |
| Known DMA subtotal | **2,080** |

Codec object `sizeof` probes using each component's actual compilation flags:
`codec_dev_t` 48, `i2s_data_t` 88, `i2s_data_keep_t` 12, `i2c_ctrl_t` 32,
`audio_codec_gpio_if_t` 12 and `audio_codec_es8311_t` 96: **288 bytes** for these
objects. This is a known subset, not a claim about total driver heap usage:
I2S/GDMA channel/control structures, internal driver queues/locks, codec
reference bookkeeping, I2C device bookkeeping, allocator headers/alignment and
transient SDK work are additional. The shared I2C bus is retained as before.
Worker PCM (480 bytes) lives on its 6,144-byte stack; the startup ADC discard
buffer (960 bytes) uses the existing 16 KiB foreground stack. TCB freeing follows
FreeRTOS idle-task retirement. No hardware free-heap/high-water reading was taken.

| External `minicw.app.elf` | T042 | T043 | Delta |
| --- | ---: | ---: | ---: |
| File bytes | 34,724 | 38,820 | +4,096 |
| `.text` | 23,432 | 24,052 | +620 |
| `.rodata` | 1,272 | 1,272 | 0 |
| `.data` | 1,176 | 1,176 | 0 |
| `.bss` | 2,732 | 2,740 | +8 |
| Loader text + data allocation, excluding bookkeeping | 28,612 | 29,240 | +628 |

Other final ELF sections: `.hash` 40, `.dynsym` 80, `.dynstr` 38,
`.rela.dyn` 6,216, `.rela.plt` 12, `.eh_frame` 44, `.got` 4; `size -A` total
35,674 bytes. File growth includes linker segment/file alignment. Resident import
remains **only `mini_api_get`**. No app task, semaphore, codec, I2S object, PCM
buffering worker, ESP-IDF/FreeRTOS dependency or libm import enters the ELF.

### Hardware/manual validation still required / known risks

No hardware testing was performed. H1–H4 remain pending after supervisor review:
paddle **and** automatic M1 must both be pop-free against standalone
`MiniCW_V1_2.bin`, with live UI, correct Tune/straight/preemption/volume/mute,
Ctrl+C silence, repeated launch/exit and FT8/shell Audio reuse. Software parity
and passing host tests do not establish acoustic parity or real-time scheduling
on ADV. Dynamic free-heap/stack high-water and full driver-allocation totals
remain unmeasured on device. If either paddle or M1 still pops, compare exact
PCM/queue/task/codec behavior with the pinned source before any redesign.
