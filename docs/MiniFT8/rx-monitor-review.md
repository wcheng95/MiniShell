# MiniFT8-V3 RX — V2 `monitor.h` / `monitor.c` Review

## Purpose

This document continues RX-0B by reviewing MiniFT8-V2:

```text
components/ft8_lib/common/monitor.h
components/ft8_lib/common/monitor.c
```

The goal is to preserve V2 monitor mathematics and decode behavior while replacing hidden singleton storage with explicit, inspectable ownership suitable for Linux and Cardputer-class targets.

No implementation is moved or refactored in this review.

## What the V2 monitor does

The monitor is the streaming PCM-to-waterfall stage:

```text
engine-native PCM blocks
    -> overlapping analysis frame
    -> Hann window
    -> real FFT
    -> selected frequency bins
    -> dB magnitude
    -> compact waterfall storage
```

The completed waterfall is later consumed by candidate search and candidate decode.

The current public API looks instance-oriented:

```c
monitor_t mon;
monitor_init(&mon, &cfg);
monitor_reset(&mon);
monitor_process(&mon, frame);
monitor_free(&mon);
```

and `monitor_t` contains configuration-derived values, pointers, the waterfall descriptor, and an FFT plan.

However, the implementation is not actually instance-independent.

## Major ownership problem: apparent object, hidden singleton

The expensive mutable storage is held in file-static/global state rather than owned by the supplied `monitor_t` instance.

Current hidden state includes conceptually:

```text
window_buf
last_frame_buf
waterfall_mag_buf
fft_work_buf
fft_work_is_static
waterfall_is_static

waterfall_static_buf[]
fft_work_static[]
window_static[]
last_frame_static[]
timedata[]
freqdata[]
```

Consequences:

- two `monitor_t` values are not two independent monitors;
- concurrent monitors are unsafe;
- sequential monitor lifetimes can interfere through shared module state;
- ownership cannot be understood from `monitor_t` alone;
- memory requirements are hidden behind implementation constants and fallback `malloc()`;
- future normal/deep/alternate algorithm comparison becomes harder than necessary.

V3 rule:

> A monitor instance must have explicit access to every mutable buffer/state object it uses. No mutable singleton DSP storage may sit behind `monitor_t`.

## Baseline V2 RAM shape

For the current normal FT8 configuration:

```text
sample rate = 6000 Hz
time_osr    = 2
freq_osr    = 1
f_min       = 200 Hz
f_max       = 2900 Hz
nfft        = 960
num_bins    = 433
max_blocks  = 93
```

The magnitude waterfall contains:

```text
93 x 2 x 1 x 433 = 80,538 uint8 elements
```

or about 78.7 KiB.

Other large V2 buffers are approximately:

```text
FFT plan work arena       12,288 bytes static allowance
window                     3,840 bytes
last analysis frame        3,840 bytes
FFT time-domain scratch    3,840 bytes
FFT frequency scratch      3,848 bytes
```

So the visible baseline monitor-related storage is roughly 108 kB before small structures/overhead.

This is exactly why workspace requirements must be queryable before allocation.

### OSR/sample-rate implications

The current `MONITOR_NFFT_MAX = 960` is an implementation ceiling caused by static arrays, not an FT8 protocol rule.

Examples:

```text
6 kHz, freq_osr=1 -> nfft  960
12 kHz, freq_osr=1 -> nfft 1920
6 kHz, freq_osr=2 -> nfft 1920
```

For the same 200-2900 Hz range, changing 6 kHz to 12 kHz at `freq_osr=1` does not inherently double the compact waterfall width because the base FT8 frequency-bin spacing is still tied to the symbol period. It does increase FFT/history/scratch requirements.

OSR directly changes waterfall RAM. At the V2 FT8 frequency range and 93-block capacity:

```text
time_osr=2, freq_osr=1 ->  80,538 bytes
time_osr=4, freq_osr=1 -> 161,076 bytes
time_osr=2, freq_osr=2 -> 161,076 bytes
time_osr=4, freq_osr=2 -> 322,152 bytes
```

These costs matter on Cardputer-class hardware and should be visible before an algorithm profile is selected.

## Initialization/lifecycle problems

### `monitor_init()` cannot report failure

The function returns `void`, but initialization can fail because of FFT-plan setup or allocation.

A caller therefore cannot distinguish:

```text
valid monitor
partially initialized monitor
failed monitor
```

V3 requirement:

> Initialization must return explicit success/failure and leave the object in a safely destructible state on every failure path.

### Object is not initialized before failure paths

V2 callers commonly use:

```c
monitor_t mon;
monitor_init(&mon, &cfg);
```

without first zeroing `mon`.

Some `monitor_init()` failure paths return before all fields are valid; another failure path calls `monitor_free()` while parts of `monitor_t` may not yet have been initialized.

This is unsafe lifecycle behavior even if the common embedded configuration normally avoids it.

V3 should initialize the complete object to a known inert state before acquiring any resource.

### Mixed static/dynamic ownership

V2 prefers fixed static buffers and silently falls back to `malloc()` when a request exceeds them.

That solves a practical V2 heap-fragmentation problem, but makes ownership and RAM policy implicit.

V3 should not have a hidden rule of:

```text
static if small enough
malloc if larger
```

inside the DSP core.

Allocation policy should be explicit outside the monitor.

## Preferred V3 workspace model

The preferred direction is a caller-supplied workspace model.

Conceptually:

```text
Ft8MonitorConfig
      |
      v
query requirements
      |
      +--> persistent bytes
      +--> scratch bytes
      `--> waterfall bytes

caller / ft8_engine owner
      |
      +--> obtains memory once
      |    Linux: normal allocation is easy
      |    embedded: fixed/preallocated region is possible
      |
      v
Ft8Monitor init(config, workspace)
```

The exact C API is intentionally not frozen yet.

Important ownership distinction:

- the caller owns the raw memory allocation;
- the `Ft8Monitor` owns the **logical use/lifetime** of its assigned slices while initialized;
- the monitor performs no MiniShell calls and knows nothing about the platform allocator.

This allows a MiniShell-facing application edge to obtain memory using the appropriate platform service while `ft8_engine` receives only ordinary pointer/size storage.

Benefits:

- no hidden heap fragmentation;
- exact RAM requirements are inspectable;
- static embedded allocation is possible;
- Linux tests can deliberately allocate exact-size buffers and test failure cases;
- alternate monitor instances can be created without global collisions;
- future algorithm comparison can make RAM cost part of the comparison.

## Persistent state versus scratch state

It is useful to distinguish memory by lifetime.

### Persistent for a monitor/decode window

```text
immutable configuration / derived dimensions
FFT plan state + plan work memory
Hann window
analysis-history / last-frame buffer
waterfall storage + descriptor
current waterfall block count
monitor diagnostics such as max magnitude
```

### Scratch during FFT processing

```text
windowed time-domain FFT input
frequency-domain FFT output
```

V1 may simply assign all of this from one workspace for simplicity. Keeping persistent versus scratch requirements visible leaves open a later RAM optimization where serialized monitor instances share scratch memory without sharing persistent state.

## Reset semantics: one V2 behavior, two V3 operations

This review found an important subtlety.

`monitor_init()` clears `last_frame` once. `monitor_reset()` later resets only:

```text
wf.num_blocks = 0
max_mag       = -120 dB
```

It does **not** clear `last_frame`.

In V2 live RX, the same monitor is reset after each decode/slot transition, so analysis history intentionally or effectively continues across decode-window boundaries. The first overlapping FFT of a new window may therefore include samples from immediately before that window.

We should preserve that baseline behavior during structural cleanup, but make the semantics explicit.

Conceptually V3 needs two operations:

```text
begin_new_decode_window
    clear waterfall count/diagnostics
    preserve analysis history

reset_stream / discontinuity
    clear waterfall count/diagnostics
    clear analysis history
```

Examples of a true stream discontinuity include opening a new unrelated WAV, reconnecting/restarting a source, or deliberately abandoning continuity.

The exact function names are deferred.

## Monitor input contract

V2 `monitor_process()` expects exactly one `block_size` frame, where:

```text
block_size = sample_rate x protocol_symbol_period
```

For FT8 at 6 kHz:

```text
block_size = 6000 x 0.160 = 960 samples
```

Within that block, `time_osr` controls the analysis shift. With `time_osr=2`, the monitor performs two overlapping FFT analyses while consuming the 960 new samples.

This DSP block framing is a monitor concern. Transport read sizes are not.

For structural cleanup we should initially preserve the exact-block input behavior. Whether a later higher-level `ft8_monitor_feed(samples, count)` accepts arbitrary chunk sizes and owns a small accumulator is a separate interface improvement; it must not be mixed with the first mathematical-preservation refactor.

## Capacity and finalization are different responsibilities

V2 computes waterfall capacity from:

```text
protocol slot time / symbol period
```

which gives 93 blocks for FT8.

But the surrounding V2 audio pipeline decides when enough blocks have arrived to trigger decode. Therefore the monitor's `max_blocks` is storage capacity, not authority over UTC slot boundaries or decode scheduling.

V3 ownership should be explicit:

```text
rx_slot_framer / RX pipeline
    owns UTC/sample slot identity
    decides when a decode window begins/finalizes

Ft8Monitor
    owns analysis state
    stores up to an explicit waterfall capacity
    never reads a clock
```

A later API may make waterfall capacity an explicit configuration value instead of deriving it internally from slot duration. During structural cleanup, the V2-compatible capacity can remain 93 so behavior/RAM is easy to compare.

## What stays mathematically unchanged in RX-1 cleanup

The first cleanup must preserve:

```text
symbol-period-derived block size
subblock shift calculation
nfft calculation
Hann window formula
FFT normalization
frequency-bin selection
FFT bin/sub-bin ordering
dB magnitude calculation
uint8 waterfall quantization/clamping
waterfall layout/order
analysis-history behavior across normal window reset
```

Do not combine any of these with sample-rate, filter, OSR, window, FFT, or candidate-search improvements.

## Process/full-capacity behavior

V2 `monitor_process()` silently returns when the waterfall is full and otherwise returns no status.

V3 should make processing state observable enough to distinguish at least:

```text
block accepted
waterfall capacity reached / block not stored
invalid/not-initialized state
```

The exact status enum/API can be chosen during implementation.

Silent data loss is a poor contract even when the caller normally checks capacity first.

## Configuration validation

V2 assumes valid configuration. V3 initialization should explicitly reject invalid combinations such as:

```text
sample_rate <= 0
time_osr <= 0
freq_osr <= 0
f_min < 0
f_max <= f_min
analysis bins outside FFT/Nyquist range
block_size/subblock relationships that cannot be represented correctly
insufficient supplied workspace
unsupported protocol
```

Validation is structural safety, not an algorithm change.

## `WATERFALL_USE_PHASE` / resynthesis path

The repository contains an optional `WATERFALL_USE_PHASE` branch and `monitor_resynth()`, but the feature flag is currently commented out and no active use was found in the normal V2 tree.

When enabled, the waterfall element expands from a compact magnitude byte to magnitude+phase floats, creating a very different RAM profile. The optional path also carries separate inverse-FFT workspace/lifecycle code.

V3 baseline decision:

> Do not bring the phase/resynthesis path into the first clean monitor implementation unless a current golden behavior requires it.

Keep the V2 code as a reference. Revisit phase or time-domain subtraction later as an explicit enhanced-decoder/research feature. The normal Cardputer-capable magnitude-only monitor should not pay its RAM cost.

## Recommended clean ownership

Conceptually one clean monitor instance should look like:

```text
Ft8Monitor
    immutable/derived config
        protocol
        sample rate
        f_min/f_max
        time_osr/freq_osr
        block/subblock/nfft/bin dimensions

    persistent DSP state
        FFT plan
        Hann window
        analysis history

    waterfall state
        storage pointer/capacity
        dimensions/layout
        num_blocks

    diagnostics
        max magnitude

    assigned scratch
        FFT input
        FFT output
```

It should **not** own:

```text
MiniShell Audio
WAV/UAC/device transport
channel meaning or ordinary-audio/IQ selection
AGC/source conditioning
UTC/RTC
slot identity
wall-clock sleeps
decode-trigger policy
candidate search
LDPC/CRC
SNR policy
UI waterfall rendering
AutoSeq/TX/logging
```

The monitor is narrowly:

> streaming engine-native PCM -> compact FT8/FT4 waterfall.

## Golden tests for the monitor cleanup

Before/while RX-1 rewrites ownership, add monitor-level golden coverage.

### 1. Derived configuration

For known FT8/FT4 configs verify:

```text
block_size
subblock_size
nfft
min_bin/max_bin
num_bins
block_stride
waterfall capacity
reported workspace bytes
```

### 2. Byte-identical waterfall regression

For fixed known PCM and unchanged config:

```text
V2 monitor
    -> waterfall bytes/hash

clean V3 monitor
    -> same waterfall bytes/hash
```

During structural cleanup this can be an exact hard golden because the monitor mathematics is intentionally unchanged.

This test localizes monitor regressions before candidate search/LDPC/message decode are involved.

### 3. Two-instance independence

Create two monitors with separate workspaces and interleave processing. Their results must equal two independent serial runs.

This directly proves that hidden singleton state is gone.

### 4. Reset semantics

Test separately:

```text
new decode window -> history preserved
stream discontinuity -> history cleared
```

### 5. Failure/lifecycle tests

Verify:

```text
invalid config fails cleanly
insufficient workspace fails cleanly
failed init is safe to destroy
full waterfall is reported
repeated destroy/reset behavior is defined
```

## Review conclusion

`monitor.h/c` contains useful, straightforward FT8/FT4 STFT/waterfall mathematics, but its current memory/lifecycle design is strongly shaped by MiniFT8-V2's ESP32 heap-fragmentation constraints.

Classification remains:

```text
monitor math       KEEP first
monitor ownership  CLEAN substantially
phase/resynth path DEFER from baseline
```

The preferred V3 direction is:

```text
explicit Ft8Monitor instance
    + explicit configuration
    + caller-supplied/queryable workspace
    + explicit persistent/scratch/waterfall memory
    + explicit reset semantics
    + explicit init/process status
    + no mutable module-global DSP state
```

This preserves Cardputer RAM discipline while making Linux development, testing, multiple decoder instances, and later algorithm comparison much cleaner.

## RX-0B status after this review

Completed:

```text
decode_helper.cpp
production decode_monitor_results()
monitor.h / monitor.c
```

Next review:

```text
decode.h / decode.c
```

That review should focus on candidate representation, Costas sync scoring/search, likelihood extraction, LDPC/CRC boundaries, hidden state, and what future deep-search context belongs at the candidate-search boundary.
