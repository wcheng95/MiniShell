# T010 — Make ADV Audio TX honor caller timeout

Status: REVIEW

## Objective

Resolve the provider noncompliance established by T009 without changing Keyer scheduling.

T009 proved:

- MiniShell Audio TX now defines caller-owned wait-budget semantics.
- Keyer sidetone requests 48 mono S16 frames at 48 kHz with `timeout_ms=20`.
- Cardputer ADV `speaker_write()` discards that timeout.
- resolved `esp_codec_dev 1.6.2` ultimately substitutes a fixed 1000 ms wait in `i2s_channel_write()`.
- real ADV measurements show normal writes are ~1 ms average / 2.487 ms max, but `MINI_WAIT_NONE` behaves identically to 20 ms because caller timeout is ignored.

T010 fixes only the ADV provider contract.

## Target design

Keep ES8311 / codec ownership for:

```text
codec creation/open
hardware volume
mute/unmute
start/stop/abort/close lifecycle
```

For PCM transport in `speaker_write()`, use the already-owned:

```c
s_i2s_tx
```

directly through `i2s_channel_write()`, passing the MiniShell caller's `timeout_ms`.

This is preferred over a worker/thread redesign because T009 measured normal latency as already small; the defect is timeout/progress semantics at the provider boundary.

## Critical semantic check before implementation

The resolved `esp_codec_dev_write()` path may contain optional software-volume processing before invoking its data interface.

Before replacing it for data transfer, inspect the locally resolved:

```text
platform/adv/managed_components/espressif__esp_codec_dev
version 1.6.2
```

and prove whether the current ADV configuration created in:

```text
platform/adv/adv_audio_speaker.cpp
prepare_codec()
```

has any active software transform that would alter PCM samples in `esp_codec_dev_write()`.

Record:

- the relevant config fields/defaults;
- whether software volume/gain/other PCM processing is active for our `esp_codec_dev_cfg_t`;
- why direct I2S transport preserves current observable speaker amplitude/data semantics.

If direct I2S would bypass an active transform, STOP and report the architecture conflict. Do not silently change audio semantics.

## Provider write contract

Current endpoint remains:

```text
speaker
48000 Hz
S16
mono
```

`speaker_write()` must:

1. validate handle, started state, pointers, frame count exactly as today;
2. set `*out_frames = 0` before transport;
3. compute requested bytes safely;
4. call:

```c
i2s_channel_write(s_i2s_tx, frames, requested_bytes,
                  &bytes_written, timeout_ms)
```

or the exact equivalent required by IDF 5.5.4;
5. map transport progress/result into the MiniShell contract below.

### Progress mapping

Frame size is 2 bytes for this fixed endpoint.

If `bytes_written` is not frame-aligned:

```text
return MINI_ERR_IO
out_frames = 0
```

Record this as a defensive impossible/driver-contract guard.

If one or more complete frames were accepted:

```text
out_frames = bytes_written / 2
return MINI_OK
```

even if the underlying I2S call reports timeout after partial progress. MiniShell defines accepted partial progress as success so the caller can submit the remainder.

If no frames were accepted:

```text
ESP_OK              -> MINI_ERR_IO   # zero-progress success is invalid here
ESP_ERR_TIMEOUT     -> MINI_ERR_TIMEOUT
other error         -> MINI_ERR_IO
```

Do not claim frames that were not accepted.

### Timeout mapping

Pass through:

```text
MINI_WAIT_NONE     == 0
finite milliseconds unchanged
MINI_WAIT_FOREVER  == 0xFFFFFFFF
```

Verify against the resolved IDF 5.5.4 `i2s_channel_write()` implementation that these values produce the intended wait behavior for this target/configuration. Record the exact evidence.

Do not add an arbitrary provider-side clamp.

## Lifecycle / codec ownership

Do not redesign:

- `speaker_open()`
- `prepare_i2s()`
- `prepare_codec()`
- codec hardware volume
- mute/unmute behavior
- `speaker_start()`
- `speaker_stop()`
- `speaker_abort()`
- `speaker_close()`

except for a tiny testability extraction if absolutely necessary.

Do not change DMA geometry in T010:

```text
dma_desc_num = 4
dma_frame_num = 120
```

Do not address the existing `i2s_channel_disable(): channel has not been enabled yet` diagnostic noise in this task. It is separate cleanup behavior.

## Tests

### 1. Provider mapping host test

Add a focused host-testable mapping helper if necessary, but do not introduce a public API.

Prove:

- exact finite timeout is forwarded;
- zero timeout is forwarded;
- `MINI_WAIT_FOREVER` is forwarded;
- full ESP_OK write -> full MINI_OK progress;
- partial ESP_OK -> partial MINI_OK;
- zero-progress ESP_ERR_TIMEOUT -> MINI_ERR_TIMEOUT;
- partial-progress ESP_ERR_TIMEOUT -> MINI_OK with partial frames;
- zero-progress other error -> MINI_ERR_IO;
- odd/non-frame-aligned byte progress -> MINI_ERR_IO with zero public frames;
- zero-progress ESP_OK -> MINI_ERR_IO.

Prefer a small private pure mapping helper over large ESP-IDF mocking.

### 2. Existing service / Keyer tests

Keep passing unchanged:

```text
tests/unit/test_audio.c
tests/keyer_k5_sidetone_test.c
```

No Keyer code change is expected.

### 3. ADV firmware build

Required locally:

```bash
cd platform/adv
idf.py build
```

This task must prove the real provider compiles against resolved IDF 5.5.4 and esp_codec_dev 1.6.2.

### 4. Repeat T009 hardware probe

Reuse the existing external:

```text
platform/adv/elf_apps/audio_tx_probe/
```

Do not create a new probe.

After supervisor diff review, architect flashes the T010 firmware and reruns:

```text
audio_tx_probe
```

Capture both phases.

Expected interpretation, not a hard-coded exact result:

#### Phase A — timeout_ms=20

Normal 48-frame writes should remain operational. No regression in ordinary speaker transport is expected.

#### Phase B — MINI_WAIT_NONE

It must no longer behave as a hidden 1000 ms blocking call.

Because this phase repeatedly writes 1 ms blocks as fast as possible, legal outcomes include:

- immediate full progress while DMA has room;
- partial progress;
- zero-progress `MINI_ERR_TIMEOUT` when no capacity is immediately available.

The probe should show nonblocking behavior rather than silently pacing every call at ~1 ms.

No arbitrary latency threshold beyond the timeout contract is introduced.

## Production scope

Expected production change:

```text
platform/adv/adv_audio_speaker.cpp
```

Expected tests/build metadata changes only as needed.

Do not change:

```text
apps/keyer/
core/minishell_services/audio_service.c
include/minishell/api.h
platform/linux/
MiniFT8
```

The Audio TX public contract was established by T009 and is not redesigned here.

## Architecture

Keep dependency direction:

```text
Keyer
  -> public MiniShell Audio TX
       -> portable Audio service
            -> ADV speaker provider
                 -> ES8311 control + I2S transport
```

No Keyer concepts below the application boundary.

## Local build/test gate

Run locally:

```bash
git status --short

cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"

ctest --test-dir build-linux -R 'audio|keyer_k5' --output-on-failure

python3 tests/app_dependency_boundary.py . ft8
python3 tests/app_dependency_boundary.py . keyer
python3 tests/ft8_platform_boundary.py .

cmake -S tests/unit -B /tmp/T010-build-unit
cmake --build /tmp/T010-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T010-build-unit --output-on-failure

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build

git diff --check

ctest --test-dir build-linux --output-on-failure
```

Record exact results. The two accepted Linux baseline failures may remain.

No GitHub Actions wait.

## Branch workflow

Use:

```text
codex/T010-adv-audio-tx-timeout
```

Codex handoff:

1. implement only T010;
2. record the esp_codec_dev software-transform check;
3. run local host + ADV build gates;
4. set task to `Status: REVIEW`;
5. commit and push;
6. return commit SHA;
7. no PR; no Actions wait.

Supervisor reviews `main..<SHA>`.

If clean, status becomes `TESTING` pending one ADV flash + rerun of the existing `audio_tx_probe`.

Do not merge to main until hardware output confirms the provider still operates and WAIT_NONE is no longer silently replaced by the fixed codec wait.

After acceptance/merge, delete local and remote T010 branch.

## Acceptance criteria

- [x] current codec software-processing semantics inspected and recorded;
- [x] direct I2S transport proven not to change current PCM semantics, or task reports conflict instead;
- [x] ADV speaker forwards caller timeout to I2S;
- [x] partial accepted bytes map truthfully to frames;
- [x] zero-progress timeout maps to MINI_ERR_TIMEOUT;
- [x] zero-progress/odd-progress error cases fail safely;
- [x] no Keyer scheduling/block-size changes;
- [x] no Audio public API changes;
- [x] existing service/Keyer tests pass;
- [x] focused provider mapping tests pass;
- [x] real ADV firmware builds;
- [x] architecture checks pass;
- [x] full local suite recorded;
- [ ] repeated ADV phase A remains functional;
- [ ] repeated ADV phase B demonstrates nonblocking timeout behavior;
- [x] no unrelated cleanup.

## Codex implementation notes

### Implementation summary

Replaced only speaker PCM transfer with the already-owned I2S TX channel,
forwarding caller timeout unchanged and mapping actual byte progress. Codec
creation/control/volume/mute/lifecycle, DMA geometry, Keyer, public Audio API,
and portable service behavior remain unchanged. Added a small private callback
helper to test the exact forwarding/mapping used by production without mocking
ESP-IDF. No redesign was introduced.

The required direct-call design removes the codec's fixed 1000 ms wait. Inspection
also found inherited IDF timeout limitations described below: this implementation
must not be interpreted as proving strict whole-call deadline or true infinite
wait semantics for every possible request. Supervisor review must consider that
evidence before declaring the entire public timeout contract satisfied.

### esp_codec_dev software-transform check

Resolved versions: **esp_codec_dev 1.6.2**, **ESP-IDF v5.5.4**, confirmed by
`platform/adv/dependencies.lock`, component `idf_component.yml`, and resolved
`platform/adv/build/project_description.json`. Component root:
`/home/wei/projects/MiniShell/platform/adv/managed_components/espressif__esp_codec_dev`.

Evidence from those local sources:

- `platform/adv/adv_audio_speaker.cpp::prepare_codec()` zero-initializes
  `esp_codec_dev_cfg_t`, then sets `codec_if` to the ES8311 codec, `data_if` to
  the I2S data interface, and `dev_type` to IN_OUT. ES8311 configuration uses
  BOTH mode, existing I2C/GPIO control, PA pin NC and `use_mclk=false`; default
  hardware-gain fields are zero. No custom software-volume handler is installed.
- `esp_codec_dev.c:128` (`esp_codec_dev_new`) allocates zeroed device state with
  calloc: `sw_vol` starts NULL. Defaults include the volume curve and
  disable-when-closed; these do not create a PCM transform.
- `esp_codec_dev.c:201-209` (`esp_codec_dev_open`) creates software volume only
  when codec is NULL or `codec->set_vol` is NULL. Our ES8311 has a non-NULL callback:
  `device/es8311/es8311.c:737` assigns `es8311_set_vol`.
- `esp_codec_dev.c:358-377` (`esp_codec_dev_set_out_vol`) dispatches to hardware
  `codec->set_vol` when `sw_vol` is NULL. `es8311_set_vol` at
  `device/es8311/es8311.c:350-362` subtracts hardware-gain compensation and writes
  ES8311_DAC_REG32 through codec control. Gain defaults (5 V PA/3.3 V DAC when
  zero) are applied in this hardware-register calculation, not to PCM bytes.
- `esp_codec_dev.c:317-333` (`esp_codec_dev_write`) has only one possible PCM
  transform: `dev->sw_vol->process`, guarded by non-NULL `sw_vol`. It is inactive
  for this configuration. The following `_i2s_data_write` at
  `platform/audio_codec_data_i2s.c:720-744` passes the byte buffer unchanged to
  `i2s_channel_write` on IDF >=5. There is no additional active PCM conversion.

Therefore direct transport does not bypass active software volume/gain/other
PCM processing. Existing ES8311 volume 80 and mute/unmute remain hardware control
operations, unchanged. This proof depends on the inspected configuration/version;
a future software-volume handler or different codec requires reevaluation.

### Provider timeout/progress mapping

The existing handle/state/pointer/frame-count validation remains in speaker_write,
including its original `0x3fffffff` bound. Requested byte multiplication is safe
for this S16 endpoint. A private transport callback invokes:
`i2s_channel_write(s_i2s_tx, frames, requested_bytes, &written, timeout_ms)`.

The shared tested helper initializes public progress to zero. Odd or over-reported
byte counts return IO/zero defensively. Any positive complete-frame progress
returns OK with the accepted count, including native timeout/other-error results.
With no progress, native ESP_ERR_TIMEOUT maps to TIMEOUT; ESP_OK and other errors
map to IO. There is no retry loop, timeout clamp, or invented pacing policy.

**Resolved IDF timeout evidence and limitation:**

- `/home/wei/projects/esp-idf/components/esp_driver_i2s/i2s_common.c:1347-1395`
  passes `pdMS_TO_TICKS(timeout_ms)` to the channel binary semaphore at line 1361
  and to each DMA queue receive at line 1370. There is no aggregate deadline or
  special UINT32_MAX handling in this function. Semaphore failure maps to native
  INVALID_STATE; queue exhaustion maps to native TIMEOUT.
- `platform/adv/build/config/sdkconfig.h` sets CONFIG_FREERTOS_HZ=100.
  IDF `components/freertos/config/include/freertos/FreeRTOSConfig.h:92` uses that
  as configTICK_RATE_HZ. `FreeRTOS-Kernel/include/freertos/projdefs.h:46` multiplies
  in TickType_t before dividing by 1000. Xtensa `portmacro.h:99-100` uses 32-bit
  TickType_t and portMAX_DELAY=0xffffffff.
- Reused the actual ADV speaker compiler command from build/compile_commands.json
  for `/tmp/T010-tick-check.cpp` static assertions: 0 ms -> 0 ticks, 20 ms -> 2
  ticks, UINT32_MAX -> **4294967 ticks**, not portMAX_DELAY. Compilation passed.
  Thus WAIT_NONE does not intentionally wait; the normal 20 ms request reaches
  I2S unchanged, quantized to two ticks. Passing FOREVER unchanged follows T010's
  explicit instruction but is about 11.93 hours per wait in this IDF configuration,
  not truly indefinite. Finite budgets may also be reused across acquisitions.

These are measured source/compiler findings, not silently fixed by a provider
clamp, IDF patch, new worker, or public-contract change. Exact forwarding is
implemented and tested; true unbounded/whole-call-budget semantics cannot be
claimed for this underlying API. Hardware phase A/B validation remains pending.

### Files changed

- `platform/adv/adv_audio_speaker.cpp`: direct I2S transport adapter and helper call.
- `platform/adv/adv_audio_tx_write.h`: private host-testable byte/result mapping.
- `tests/adv_audio_tx_timeout_test.c`: 40 forwarding/progress cases.
- `CMakeLists.txt`: register focused host test.
- This task packet: evidence, local results and REVIEW status.

### Tests added

`adv_audio_tx_timeout_unit` covers each native result/progress case under 20 ms,
WAIT_NONE, WAIT_FOREVER and 7 ms requests. Assertions verify identical callback
handle/buffer/byte count, unchanged timeout, one transport call, full/partial OK,
zero/partial timeout, zero/partial other error, odd progress, zero-progress OK,
and over-report rejection. Existing service and Keyer tests were not modified.

### Local tests/build results

Base: `fa88688ef87f87b95460c00189361415a21b07b9`.

- `git status --short`: initially clean on existing T010 branch.
- `cmake -S . -B build-linux`: PASS.
- `cmake --build build-linux -j"$(nproc)"`: PASS.
- `ctest --test-dir build-linux -R 'audio|keyer_k5' --output-on-failure`: 3/4 PASS;
  new mapping test passes; only documented linux_audio baseline failure.
- `python3 tests/app_dependency_boundary.py . ft8`: PASS.
- `python3 tests/app_dependency_boundary.py . keyer`: PASS.
- `python3 tests/ft8_platform_boundary.py .`: PASS.
- `cmake -S tests/unit -B /tmp/T010-build-unit`: PASS.
- `cmake --build /tmp/T010-build-unit -j"$(nproc)"`: PASS.
- `ctest --test-dir /tmp/T010-build-unit --output-on-failure`: PASS, 14/14,
  including unchanged Audio and Keyer K5 regressions.
- `source /home/wei/projects/esp-idf/export.sh` then
  `idf.py -C platform/adv build`: PASS against the real resolved dependencies.
  Firmware `platform/adv/build/minishell_adv.bin`: 0xa3a00 bytes, 89% app partition
  free. SHA-256:
  `e63cdd41b5434e0919ec016c2026a2a8d786de06907cce4c04a7d5f66403a974`.
- Actual Xtensa compiler static-assert check of resolved tick conversion: PASS
  (IDF system headers emit existing include_next pedantic warnings).
- `git diff --check`: PASS.
- `ctest --test-dir build-linux --output-on-failure`: 27/29 PASS. Only accepted
  linux_audio output-substring and linux_ft8 queue-order failures remain.

No PR, GitHub Actions wait, or hardware flashing performed.

### Hardware validation still required

After supervisor review, architect flashes this firmware and reruns the existing
T009 external `audio_tx_probe` (no probe changes needed). From repository root:

```bash
source /home/wei/projects/esp-idf/export.sh
read -r -p 'ADV serial port: ' T010_ADV_PORT
idf.py -C platform/adv -p "$T010_ADV_PORT" flash monitor
```

In MiniShell, run `audio_tx_probe` and retain both complete phase reports.
Phase A must remain operational; phase B should demonstrate immediate capacity/
partial/timeout behavior instead of hidden 1000 ms waits. Do not merge until
those results are supplied and reviewed. No arbitrary latency threshold added.

### Known limitations / risks

Exact pass-through removes the codec timeout substitution but inherits the IDF
quantization/repeated-budget/FOREVER conversion limitations above. No stronger
compliance claim is made. ESP_ERR_INVALID_STATE with zero progress maps to IO
per the task, including a failed I2S channel-semaphore acquisition. Odd progress
is a defensive impossible/driver-contract guard. DMA geometry and existing
channel-disable diagnostic noise remain unchanged. Hardware amplitude and normal
transport still require the specified post-review probe evidence.

### Commit

One bounded implementation commit on `codex/T010-adv-audio-tx-timeout`, containing
this report; pushed SHA returned in the handoff. Supervisor review and hardware
TESTING precede merge and later branch deletion.

## Supervisor review

Supervisor reviews the actual `main..<commit>` diff and local evidence. If accepted, move to TESTING for the existing ADV probe.

## Architect hardware result

Record the post-fix Cardputer ADV `audio_tx_probe` output here before T010 is COMPLETE.
