# T009 — Define Audio TX blocking contract and measure ADV latency

Status: READY

## Objective

Resolve the evidence gap in T001 F12 before choosing any Keyer/speaker scheduling redesign.

T009 has two outputs:

1. define the MiniShell Audio TX `write()` timeout/partial-progress contract;
2. measure the current Cardputer ADV public Audio TX path using the exact Keyer sidetone write shape.

This task is evidence-first. Do **not** redesign Keyer scheduling or add a speaker worker in T009.

## Finding being addressed

T001 F12 observed:

- Keyer samples paddles, advances its engine, applies KeyOut, then synchronously calls sidetone Audio TX;
- sidetone writes 48 mono S16 frames at 48 kHz (1 ms of audio) and passes `timeout_ms=20`;
- the ADV speaker provider currently discards `timeout_ms` and calls `esp_codec_dev_write()`;
- therefore the public timeout argument is not currently an enforceable bound at that provider edge;
- host mocks prove functionality, not real ADV write latency.

No missed-key claim has been established. T009 measures before choosing a design change.

## Ownership

Keep:

```text
keyer_engine
    owns CW timing/state only

app_controller
    owns loop/pacing and KeyIn -> engine -> KeyOut -> sidetone order

sidetone
    owns tone synthesis + Audio TX stream usage

MiniShell Audio
    owns generic TX lifecycle + timeout contract

ADV speaker provider
    owns ES8311/I2S transport implementation
```

Do not put CW timing semantics into MiniShell/ADV.

## Public Audio TX write contract

Update `docs/api/audio-api.md` to state the intended semantics of:

```c
write(stream, frames, frame_count, out_frames, timeout_ms)
```

Contract:

- `out_frames` is always initialized to zero by the public service before provider call.
- `out_frames <= frame_count`.
- `MINI_WAIT_NONE` means the provider must not intentionally wait for additional transport capacity.
- finite `timeout_ms` is the caller's maximum permitted **transport wait budget** for that write call.
- `MINI_WAIT_FOREVER` permits unbounded waiting.
- normal scheduler/interrupt/call overhead is not a hard-real-time guarantee, but a provider must not knowingly replace a finite timeout with an unbounded blocking operation.
- partial progress is legal:
  - if at least one frame is accepted, return `MINI_OK` with the accepted count, even if fewer than requested;
  - caller may call again for the remainder.
- if no frame can be accepted within a finite wait budget, return `MINI_ERR_TIMEOUT` with `out_frames=0`.
- other transport failures return their normal error with `out_frames` describing no uncommitted progress; providers must not claim frames they did not accept.

Do not change public struct layout or API version.

## Public service tests

Extend `tests/unit/test_audio.c` so fake TX verifies:

1. `timeout_ms` is forwarded unchanged to the backend;
2. partial `MINI_OK` progress is preserved;
3. `MINI_ERR_TIMEOUT` with zero progress is preserved;
4. public service rejects backend `out_frames > frame_count` as `MINI_ERR_IO`;
5. stream remains usable after a timeout.

Do not add service-level pacing policy.

## Keyer contract regression

Extend `tests/keyer_k5_sidetone_test.c` only as needed to make the Keyer dependency explicit:

- each sidetone write passes `SIDETONE_WRITE_TIMEOUT_MS == 20`;
- partial writes are completed by repeated calls;
- timeout/error is returned to controller; sidetone does not spin indefinitely on zero progress.

Do not change the 48-frame block size or Keyer loop in T009.

## Inspect the actual ADV transport

On the local development machine, inspect the installed ESP-IDF / esp_codec_dev sources used by the ADV build.

Record in this task:

- exact esp_codec_dev component version if discoverable;
- implementation path from `esp_codec_dev_write()` to its data interface;
- whether that implementation ultimately uses `i2s_channel_write()` or another primitive;
- the timeout/wait value used there;
- whether the current MiniShell `timeout_ms` can be forwarded through that API without bypassing/replacing the codec data path.

Do not infer this from memory. Record file paths and relevant function names from the locally resolved build dependencies.

If the installed dependency source is unavailable locally, say so and rely on the hardware measurement; do not invent internals.

## ADV measurement probe

Add a small **external diagnostic ELF**, not a normal production app:

```text
platform/adv/elf_apps/audio_tx_probe/
```

It must import only `mini_api_get`, like the existing external ELF policy.

The probe uses public MiniShell APIs only. No ESP-IDF, codec, I2S, GPIO, or private MiniShell calls.

### Probe stream

Open:

```text
endpoint      speaker
rate          48000 Hz
format        S16
channels      1
block         48 frames
```

Use silent samples so the test is not audibly annoying.

### Measurement phases

Measure elapsed time around each **public** `audio->tx->write()` call with MiniShell `monotonic_us()`.

Run at least two phases:

```text
A: timeout_ms = 20          # exact current Keyer request
B: timeout_ms = MINI_WAIT_NONE
```

Recommended default: at least 5000 writes per phase after a short warm-up.

For each phase report:

- total calls;
- total frames accepted;
- counts by result: OK / TIMEOUT / other;
- partial-write count;
- zero-progress OK count;
- min elapsed us;
- integer mean elapsed us;
- max elapsed us;
- count > 1000 us;
- count > 2000 us;
- count > 5000 us;
- count > 10000 us;
- count > 20000 us.

A percentile histogram is optional; do not add heap merely for percentiles.

The probe must stop/abort/close cleanly on errors and return to MiniShell.

### Build evidence

Add an external-ELF build target/project consistent with the existing ADV external apps.

Local build must prove:

- ELF builds;
- external import table contains exactly the allowed `mini_api_get` jump-slot import;
- no private/platform imports.

Do not add the probe to normal application discovery/registry.

## Measurement interpretation

T009 does not pre-judge the result.

Record actual ADV hardware output before marking T009 COMPLETE.

Interpretation rules:

- If phase A shows ordinary ~1 ms pacing and no large stalls, that is evidence the current path is normally responsive, but source-level timeout compliance still depends on the inspected underlying implementation.
- If phase A has calls materially beyond the 20 ms requested budget, F12 is directly demonstrated.
- If `MINI_WAIT_NONE` still blocks approximately one or more audio-buffer periods, the ADV provider does not implement nonblocking semantics.
- If the underlying codec path uses an unbounded wait internally, the provider is contract-noncompliant even if normal measured latency is small.
- Do not choose worker/thread/direct-I2S redesign in T009. Record the evidence and let the next task select the smallest correct provider fix, if needed.

## No arbitrary Keyer latency threshold

Do not invent a new 1 ms / 2 ms / 5 ms product pass/fail requirement in T009.

The existing architectural fact is:

```text
Keyer loop target ~1 ms granularity
sidetone block duration = 1 ms
requested Audio TX wait budget = 20 ms
```

T009 reports measured distributions and timeout-contract compliance. A stricter Keyer responsiveness budget, if needed, is a later architect decision based on evidence.

## Production scope

Expected production/document changes are small:

```text
docs/api/audio-api.md
tests/unit/test_audio.c
tests/keyer_k5_sidetone_test.c        # only if needed
platform/adv/elf_apps/audio_tx_probe/ # diagnostic external ELF
```

Do not change these production behaviors in T009 unless needed only to correct a contract test bug:

```text
platform/adv/adv_audio_speaker.cpp
apps/keyer/src/app_controller/
apps/keyer/src/sidetone/ block size/pacing
core/minishell_services/audio_service.c
```

If measurement cannot be produced without changing the provider, stop and report why instead of redesigning it.

## Hardware procedure

Codex prepares the probe and gives exact build/install/run commands in this task.

The architect/user will run the probe on Cardputer ADV and paste the output.

T009 remains `TESTING` after supervisor diff review until this measurement is recorded.

No GitHub Actions wait.

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

cmake -S tests/unit -B /tmp/T009-build-unit
cmake --build /tmp/T009-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T009-build-unit --output-on-failure

# build the ADV external probe using its documented local command
# inspect its relocation/import table

git diff --check

ctest --test-dir build-linux --output-on-failure
```

The two known baseline failures may remain and must not be changed here.

## Branch workflow

Use:

```text
codex/T009-audio-tx-latency
```

Codex handoff:

1. set task to `Status: REVIEW`;
2. include dependency-source inspection evidence;
3. include exact probe build/install/run commands;
4. include all local tests/build results;
5. commit and push;
6. return commit SHA;
7. no PR; no Actions wait.

Supervisor then reviews `main..<SHA>`.

If code is clean, supervisor sets T009 to `TESTING`; do **not** merge to `main` until the ADV hardware measurement has been supplied and reviewed.

After acceptance/merge, delete local and remote T009 branch.

## Acceptance criteria

- [ ] Audio TX timeout/partial-progress semantics are documented.
- [ ] service unit tests enforce timeout forwarding, partial progress, timeout, and over-report rejection.
- [ ] Keyer sidetone's 20 ms request/partial-write behavior remains covered.
- [ ] current ADV codec/I2S implementation path is inspected from local resolved dependencies and recorded.
- [ ] external `audio_tx_probe.elf` uses public MiniShell APIs only.
- [ ] probe measures both 20 ms and nonblocking phases.
- [ ] probe builds locally with allowed import table.
- [ ] no speaker-provider or Keyer scheduling redesign is mixed in.
- [ ] architecture checks pass.
- [ ] normal local suites recorded.
- [ ] ADV hardware measurement is recorded before COMPLETE.
- [ ] evidence clearly states whether current ADV provider honors the documented timeout contract.
- [ ] next design choice, if any, is based on that evidence.

## Codex implementation notes

### Implementation summary

### Audio TX contract

### Resolved ADV codec/I2S implementation evidence

### Probe design

### Files changed

### Local tests/build results

### Probe build/install/run commands

### Hardware measurement still required

### Known limitations / risks

### Commit

## Supervisor review

Supervisor reviews the actual `main..<commit>` diff and local evidence. If accepted, status becomes TESTING pending ADV probe output.

## Architect hardware result

Record the exact Cardputer ADV probe output and the conclusion about timeout/nonblocking compliance here before T009 is COMPLETE.
