# T009 — Define Audio TX blocking contract and measure ADV latency

Status: COMPLETE

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

- [x] Audio TX timeout/partial-progress semantics are documented.
- [x] service unit tests enforce timeout forwarding, partial progress, timeout, and over-report rejection.
- [x] Keyer sidetone's 20 ms request/partial-write behavior remains covered.
- [x] current ADV codec/I2S implementation path is inspected from local resolved dependencies and recorded.
- [x] external `audio_tx_probe.elf` uses public MiniShell APIs only.
- [x] probe measures both 20 ms and nonblocking phases.
- [x] probe builds locally with allowed import table.
- [x] no speaker-provider or Keyer scheduling redesign is mixed in.
- [x] architecture checks pass.
- [x] normal local suites recorded.
- [ ] ADV hardware measurement is recorded before COMPLETE.
- [x] evidence clearly states whether current ADV provider honors the documented timeout contract.
- [ ] next design choice, if any, is based on that evidence.

## Codex implementation notes

### Implementation summary

Documented the intended public TX wait/partial-progress contract, strengthened
service and Keyer tests, and built an external public-API-only diagnostic probe.
Inspected the actual locally resolved ADV codec/I2S sources. No provider,
service pacing, Keyer loop/block-size, public-layout, or API-version changes.
No task deviations. Status REVIEW; hardware data remains pending, so this is
not a COMPLETE or hardware-latency claim.

### Audio TX contract

Documentation defines zero-initialized output, count bounds, nonblocking/finite/
unbounded transport wait budgets, partial accepted progress as OK, no-progress
budget exhaustion as TIMEOUT, and truthful progress on failures. Service tests
exercise exact 20/0/FOREVER/7 ms forwarding, partial progress, timeout followed by
successful same-stream use, and rejection of over-reporting. Keyer tests require
exactly 20 ms on each call, 48 frames completed in three partial calls, and one-call
termination for TIMEOUT, IO, or zero-progress OK (mapped to IO).

### Resolved ADV codec/I2S implementation evidence

Local build metadata: `platform/adv/build/project_description.json` resolves
`espressif__esp_codec_dev` to
`/home/wei/projects/MiniShell/platform/adv/managed_components/espressif__esp_codec_dev`
and IDF to `/home/wei/projects/esp-idf`. `platform/adv/dependencies.lock` and the
component's `idf_component.yml:10` identify **esp_codec_dev 1.6.2**. The IDF lock
entry and `git -C /home/wei/projects/esp-idf describe --tags --always` identify
**v5.5.4**.

Inspected call chain:

1. `platform/adv/adv_audio_speaker.cpp`, `speaker_write()`: explicitly discards
   `timeout_ms`, calls `esp_codec_dev_write()` for the entire mono S16 byte count.
   `prepare_codec()` selects `audio_codec_new_i2s_data()` with the ADV I2S handles.
2. Resolved component `esp_codec_dev.c:317`, `esp_codec_dev_write()`: optional
   software-volume processing, then `data_if->write(data_if, data, len)` at line 332.
3. Resolved component `interface/audio_codec_data_if.h:30`: write callback has
   only interface/data/size arguments, no timeout or accepted-byte output.
4. Resolved component `platform/audio_codec_data_i2s.c:720`, `_i2s_data_write()`:
   the IDF >=5 path calls `i2s_channel_write(..., DEFAULT_WAIT_TIMEOUT)` at line 739.
   Line 25 defines that timeout as **1000 ms**. The older-IDF alternative uses
   `i2s_write(..., portMAX_DELAY)` but is NOT the resolved 5.5.4 build path.
5. `/home/wei/projects/esp-idf/components/esp_driver_i2s/i2s_common.c:1347`,
   `i2s_channel_write()`: uses `pdMS_TO_TICKS(timeout_ms)` for the channel binary
   semaphore (line 1361) and DMA queue waits (line 1370), with the timeout reused
   on each queue acquisition rather than a single elapsed-call deadline.

The resolved codec path therefore does **not** honor the 20 ms/WAIT_NONE public
budgets: it substitutes 1000 ms. This is source-level noncompliance, not a claim
that hardware was observed stalling for 1000 ms. Its public write API and data
callback cannot forward MiniShell's per-call timeout unchanged without changing
or replacing/bypassing that codec data path. The adapter also hides accepted-byte
counts and maps driver failure generically; T009 does not repair it. No worker,
direct-I2S, or scheduling redesign is selected here.

### Probe design

Public `speaker`, 48000 Hz/S16/mono, 48 silent frames per call. Two independently
opened phases request 20 ms and WAIT_NONE, each with 100 warm-up calls then 5000
measured calls. Fixed stack/static storage, no heap, no platform access. Timing
surrounds only the public write call. Reports calls/accepted frames, OK/TIMEOUT/
other, partial and zero-OK counts, min/integer mean/max, and all five requested
strict-greater-than thresholds. TIMEOUT continues; other errors report a partial
phase and abort/close. No latency pass/fail criterion is introduced.

Integer formatting/division is self-contained. Initial import inspection exposed
compiler-generated memcpy/memset; static constants and explicit volatile stats
initialization removed those imports. Final ELF imports only mini_api_get.
The host regression checks exact statistics across both phases and fatal-error
cleanup. It is synthetic test evidence, not an ADV measurement.

### Files changed

- `docs/api/audio-api.md`: intended TX write contract.
- `tests/unit/test_audio.c`: forwarding, partial progress, timeout, bounds tests.
- `tests/keyer_k5_sidetone_test.c`: exact timeout, full partial-write completion,
  and bounded error/zero-progress handling.
- `platform/adv/elf_apps/audio_tx_probe/`: external project/CMake, pinned component
  manifest, defaults, generated-file ignores, public probe, host regression,
  and build/install/run README. No production registry entry.
- This task packet: dependency evidence, commands, test results and REVIEW status.

### Local tests/build results

Base: `e95d7c0d5b3880e1ab4c3fe6b9dfe175f7141f9b`.

- `git status --short`: clean at start on existing T009 branch.
- `cmake -S . -B build-linux`: PASS.
- `cmake --build build-linux -j"$(nproc)"`: PASS.
- `ctest --test-dir build-linux -R 'audio|keyer_k5' --output-on-failure`: 2/3 PASS;
  only documented `linux_audio` stale expected-output failure. Keyer K5 lives
  in the separate unit suite and passes there.
- `python3 tests/app_dependency_boundary.py . ft8`: PASS.
- `python3 tests/app_dependency_boundary.py . keyer`: PASS.
- `python3 tests/ft8_platform_boundary.py .`: PASS.
- `cmake -S tests/unit -B /tmp/T009-build-unit`: PASS.
- `cmake --build /tmp/T009-build-unit -j"$(nproc)"`: PASS.
- `ctest --test-dir /tmp/T009-build-unit --output-on-failure`: PASS, 14/14.
- External `idf.py ... elf` command below: PASS after permitting initial component
  registry access (sandbox network resolution initially failed).
- Final readelf relocation/dynamic-symbol inspection: PASS, exactly one named
  undefined symbol and one `R_XTENSA_JMP_SLOT`, both `mini_api_get`.
- Host probe statistics/cleanup compile/run command below: PASS.
- `git diff --check`: PASS.
- `ctest --test-dir build-linux --output-on-failure`: 26/28 PASS, only accepted
  `linux_audio` and `linux_ft8` baseline failures. Neither was changed.

ELF artifact: `platform/adv/elf_apps/audio_tx_probe/build/audio_tx_probe.app.elf`,
3256 bytes. SHA-256:
`138ee9b5ed156018b778a2cfa7ed0718818aa598184cbda405ccff403730dbca`.
The generated binary/dependencies are not committed; rebuild with commands below.

### Probe build/install/run commands

Build on the inspected development machine:

```bash
cd /home/wei/projects/MiniShell
source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv/elf_apps/audio_tx_probe elf
xtensa-esp32s3-elf-readelf -rW platform/adv/elf_apps/audio_tx_probe/build/audio_tx_probe.app.elf
xtensa-esp32s3-elf-readelf --dyn-syms -W platform/adv/elf_apps/audio_tx_probe/build/audio_tx_probe.app.elf
```

The `.rela.plt` table must have exactly one `R_XTENSA_JMP_SLOT`: `mini_api_get`.
It must also be the only named undefined dynamic symbol. The ESP ELF packaging
can produce a readelf “no .dynamic section in the dynamic segment” diagnostic;
inspect the displayed relocation/dynamic-symbol tables, not that diagnostic alone.
The build pins `espressif/elf_loader` 1.3.3; first resolution may need registry access.

Host statistics/cleanup regression:

```bash
cc -std=c11 -Wall -Wextra -Werror -Wpedantic -Iinclude \
  platform/adv/elf_apps/audio_tx_probe/probe_host_test.c -o /tmp/T009-probe-host-test
/tmp/T009-probe-host-test
```

Install with the ADV microSD mounted on this host (enter its actual mount path):

```bash
cd /home/wei/projects/MiniShell
read -r -p 'ADV microSD mount path: ' T009_SD_MOUNT
mountpoint -q "$T009_SD_MOUNT" && \
  mkdir -p "$T009_SD_MOUNT/apps" && \
  cp platform/adv/elf_apps/audio_tx_probe/build/audio_tx_probe.app.elf \
     "$T009_SD_MOUNT/apps/audio_tx_probe.elf" && \
  sync "$T009_SD_MOUNT/apps/audio_tx_probe.elf"
```

Safely eject the card and insert it into ADV running the current MiniShell
speaker-capable firmware. No probe firmware flashing is needed. In MiniShell:

```text
ls /sd/apps
cp /sd/apps/audio_tx_probe.elf /flash/apps/audio_tx_probe.elf
audio_tx_probe
```

The explicit copy updates `/flash/apps`, which takes precedence over `/sd/apps`.
The file name is `audio_tx_probe.elf` on device; the build artifact is
`audio_tx_probe.app.elf`. Retain both complete phase reports and any error lines,
then paste them into the T009 architect hardware-result section. The probe returns
to MiniShell. After collecting evidence it can be removed with:

```text
rm /flash/apps/audio_tx_probe.elf
rm /sd/apps/audio_tx_probe.elf
```

### Hardware measurement still required

Architect/user must install and run the ELF on Cardputer ADV and supply both
complete phase reports. No ADV device was used here. Supervisor may move this
task to TESTING after diff review; do not merge to main or mark COMPLETE until
hardware output is supplied and reviewed. Keep the hardware-measurement and
next-design-choice acceptance items pending.

### Known limitations / risks

The existing ADV timeout noncompliance remains deliberately unfixed. Timing
measures the synchronous public call and includes ordinary scheduling overhead;
no stricter Keyer responsiveness budget is assumed. A stuck provider call cannot
be interrupted by this synchronous probe. Copying the probe to the SD card and
running it are user hardware steps, not actions performed by this implementation.
The next bounded provider fix, if any, belongs to the architect/supervisor after
reviewing source evidence and measured output.

### Commit

One bounded implementation commit on `codex/T009-audio-tx-latency` containing
this report; its pushed SHA is returned in the handoff. No PR or Actions wait.
Branch deletion remains deferred until acceptance and merge.

## Supervisor review

PASS. Reviewed commit `0e5135cc7beb6f0fdbaf54222fcb6e1a8138fcb5` against `main`. The change is evidence-only: it documents the generic TX wait/partial-progress contract, strengthens service/Keyer regressions, adds a public-API-only external ADV probe, and does not alter the speaker provider or Keyer scheduling.

Resolved dependency evidence shows the current ADV path is source-level noncompliant with the documented timeout contract: `speaker_write()` discards MiniShell `timeout_ms`; `esp_codec_dev_write()` reaches the I2S data callback, whose resolved IDF 5.5.x path calls `i2s_channel_write(..., 1000 ms)`. Thus both Keyer's 20 ms request and `MINI_WAIT_NONE` are replaced by a fixed 1000 ms transport wait budget. This establishes contract noncompliance but not normal observed hardware latency.

Local build/tests/import inspection are accepted. T009 is now TESTING pending the two-phase Cardputer ADV probe output. Do not merge to `main` until that output is reviewed.

## Architect hardware result

Cardputer ADV hardware probe PASS. External ELF loaded from `/sd/apps/audio_tx_probe.elf` using ELF loader 1.3.3 and returned cleanly to MiniShell.

Exact measured phase reports:

```text
phase A timeout_ms=20
calls=5000
frames=240000
OK=5000
TIMEOUT=0
other=0
partial=0
zero_OK=0
min_us=9
mean_us=998
max_us=2487
>1000us=2000
>2000us=2000
>5000us=0
>10000us=0
>20000us=0

phase B timeout_ms=0 (MINI_WAIT_NONE)
calls=5000
frames=240000
OK=5000
TIMEOUT=0
other=0
partial=0
zero_OK=0
min_us=9
mean_us=998
max_us=2487
>1000us=2000
>2000us=2000
>5000us=0
>10000us=0
>20000us=0
```

Observed runtime conclusion:

- Normal 48-frame / 48 kHz Keyer-shaped writes pace at about 1 ms average.
- Worst observed call was 2487 us; no call exceeded 5 ms in either 5000-call phase.
- Phase A and B are identical, so `MINI_WAIT_NONE` has no observable nonblocking effect.
- Combined with source inspection showing MiniShell's caller timeout is discarded and the resolved codec/I2S path substitutes a fixed 1000 ms wait, the current ADV provider is contract-noncompliant even though its normal measured latency is small.
- No Keyer scheduling failure was demonstrated by this measurement.
- The next bounded fix should be provider-side timeout compliance, not a speculative Keyer worker/scheduling redesign.

Additional device logs emitted `i2s_channel_disable(...): the channel has not been enabled yet` during codec open/close. These occurred outside the measured write intervals and did not prevent successful execution; treat them as separate cleanup-noise evidence, not T009 latency failure.

Architect acceptance: COMPLETE. Hardware evidence is sufficient to close F12 measurement/contract-definition work and authorize a separate provider-compliance task.