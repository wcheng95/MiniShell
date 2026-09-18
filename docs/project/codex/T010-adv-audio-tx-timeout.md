# T010 — Make ADV Audio TX honor caller timeout

Status: READY

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

- [ ] current codec software-processing semantics inspected and recorded;
- [ ] direct I2S transport proven not to change current PCM semantics, or task reports conflict instead;
- [ ] ADV speaker forwards caller timeout to I2S;
- [ ] partial accepted bytes map truthfully to frames;
- [ ] zero-progress timeout maps to MINI_ERR_TIMEOUT;
- [ ] zero-progress/odd-progress error cases fail safely;
- [ ] no Keyer scheduling/block-size changes;
- [ ] no Audio public API changes;
- [ ] existing service/Keyer tests pass;
- [ ] focused provider mapping tests pass;
- [ ] real ADV firmware builds;
- [ ] architecture checks pass;
- [ ] full local suite recorded;
- [ ] repeated ADV phase A remains functional;
- [ ] repeated ADV phase B demonstrates nonblocking timeout behavior;
- [ ] no unrelated cleanup.

## Codex implementation notes

### Implementation summary

### esp_codec_dev software-transform check

### Provider timeout/progress mapping

### Files changed

### Tests added

### Local tests/build results

### Hardware validation still required

### Known limitations / risks

### Commit

## Supervisor review

Supervisor reviews the actual `main..<commit>` diff and local evidence. If accepted, move to TESTING for the existing ADV probe.

## Architect hardware result

Record the post-fix Cardputer ADV `audio_tx_probe` output here before T010 is COMPLETE.
