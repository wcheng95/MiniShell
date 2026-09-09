# MiniFT8-V3 RX-1C — Monitor Ownership, Workspace, and Lifecycle

Status: **COMPLETE**

RX-1C replaces the MiniFT8-V2 monitor's hidden mutable singleton buffers with one explicit monitor instance and caller-supplied workspace while preserving the pinned V2 FT8 waterfall byte-for-byte.

RX-1C is an ownership/lifecycle cleanup only. It does not change the FT8 receive algorithm, sample rate, FFT size, window, OSR, candidate policy, decoder, SNR estimator, or UI behavior.

## 1. Scope

Implemented production boundary:

```text
6 kHz mono float PCM
        |
        | exact 960-sample blocks
        v
Ft8Monitor
        |
        v
compact FT8 waterfall
```

RX-1C stops at the waterfall boundary. Candidate search, LDPC/CRC, message decode, hash state, slot framing, MiniShell Audio, and UI remain outside this stage.

## 2. Baseline preserved

The cleaned monitor preserves the pinned MiniFT8-V2 FT8 configuration:

```text
sample rate       6000 Hz
f_min              200 Hz
f_max             2900 Hz
time_osr              2
freq_osr              1
block_size           960 samples
subblock_size        480 samples
nfft                 960
min_bin               32
max_bin              465
num_bins             433
max_blocks            93
block_stride         866 bytes
waterfall capacity 80538 bytes
```

The hard RX-1A waterfall reference remains:

```text
input  : ft8_cq_w1xyz_fn42.wav
blocks : 85
bytes  : 73610
FNV-1a : 18BE1E838FD9C6AF
```

The RX-1C implementation reproduces this fingerprint exactly.

## 3. Ownership before and after

### MiniFT8-V2

The public-looking `monitor_t` object did not actually own all of its mutable DSP state. `monitor.c` also relied on module-static storage for conceptually:

```text
FFT plan arena
waterfall
window
analysis history
FFT time scratch
FFT frequency scratch
module-global ownership flags/pointers
```

Therefore two `monitor_t` values were not independent monitor instances.

### MiniFT8-V3 RX-1C

```text
caller
    |
    +-- allocates one workspace
    |
    `-- Ft8Monitor
          |
          +-- FFT plan slice
          +-- waterfall slice
          +-- window slice
          +-- analysis-history slice
          +-- FFT time-scratch slice
          `-- FFT frequency-scratch slice
```

Rules:

- caller owns allocation/free policy;
- `Ft8Monitor` exclusively owns logical use of the assigned workspace while initialized;
- `Ft8Monitor` never calls MiniShell Memory;
- `ft8_monitor_destroy()` releases logical ownership but never frees caller memory;
- no mutable DSP singleton is used by the cleaned monitor;
- two monitor instances with separate workspaces are independent.

## 4. Monitor contract

Implemented in:

```text
apps/ft8/src/ft8_engine/ft8_monitor.h
apps/ft8/src/ft8_engine/ft8_monitor.c
```

The current interface provides:

```text
ft8_monitor_baseline_config
ft8_monitor_query_requirements
ft8_monitor_init
ft8_monitor_destroy
ft8_monitor_begin_window
ft8_monitor_reset_stream
ft8_monitor_process_block
ft8_monitor_get_waterfall
```

### Workspace query

The caller first asks the monitor for its deterministic storage requirement for a configuration.

The query reports both total storage and important slices/dimensions so RAM cost is visible before initialization.

On the RX-1C Ubuntu 24.04 x86-64/GCC reference build, the baseline monitor requests:

```text
FFT plan             9888 bytes
waterfall           80538 bytes
window               3840 bytes
analysis history     3840 bytes
FFT time scratch     3840 bytes
FFT frequency scratch 3848 bytes
alignment/padding       14 bytes
--------------------------------
total              105808 bytes
```

The exact total may differ slightly on a 32-bit embedded target because pointer size and alignment can affect FFT-plan/padding size. The application must use the query result rather than hard-code `105808`.

This is a measurement/visibility mechanism, not a RAM optimization exercise.

## 5. Lifecycle semantics

Initialization is all-or-safe-to-destroy:

```text
query requirements
      |
caller allocates workspace
      |
monitor_init
      |
process windows
      |
monitor_destroy
      |
caller frees workspace
```

Insufficient or incorrectly aligned workspace fails explicitly.

Two reset operations intentionally have different semantics.

### New decode window

```text
ft8_monitor_begin_window
    reset waterfall count
    reset window diagnostics
    PRESERVE analysis history
```

This preserves V2's normal continuous-stream behavior across decode-window boundaries.

### Stream discontinuity

```text
ft8_monitor_reset_stream
    reset waterfall/window state
    CLEAR analysis history
```

This is for a genuinely new/discontinuous source stream.

The monitor never reads UTC or any clock. Slot identity and boundary ownership remain above it.

## 6. Error/status behavior

RX-1C makes previously silent states observable:

```text
FT8_MONITOR_OK
FT8_MONITOR_ERR_INVALID
FT8_MONITOR_ERR_WORKSPACE
FT8_MONITOR_ERR_NOT_INITIALIZED
FT8_MONITOR_WATERFALL_FULL
```

A full waterfall no longer silently discards input through a void-returning interface.

These statuses are private MiniFT8 domain semantics; they are not MiniShell API result codes.

## 7. FFT dependency

RX-1C keeps the same KissFFT family/implementation used by the pinned V2 baseline under:

```text
apps/ft8/src/ft8_engine/vendor/kissfft/
```

The FFT implementation remains private to `ft8_engine`. Nothing outside the engine should depend on KissFFT types.

The monitor itself includes the neutral `kiss_fftr.h` name, so the monitor source is not coupled to a repository-relative vendor path.

## 8. Tests

### Ownership/lifecycle unit test

```text
tests/ft8_monitor_rx1c_test.c
```

Covers:

```text
exact derived FT8 dimensions
enforcement of the 6 kHz baseline monitor contract
insufficient workspace
misaligned workspace
two-instance independence
new-window history preservation
stream-reset history clearing
full-waterfall status
safe destroy/inert state
```

### Pinned V2 golden test

```text
tests/ft8_monitor_rx1c_reference.c
.github/workflows/rx1c-reference.yml
```

CI checks out exactly:

```text
wcheng95/Mini-FT8
5bd3ef98f72388a850bebad04bd7300b90edb63c
```

only to supply the pinned RX-1A 6 kHz golden WAV.

The cleaned monitor must produce:

```text
blocks=85
active_bytes=73610
fnv=18BE1E838FD9C6AF
```

The normal Linux test suite also runs the ownership/lifecycle test without requiring the external reference checkout.

## 9. Regression found by the golden boundary

The first cleaned implementation produced the correct dimensions and active-byte count but the wrong waterfall fingerprint:

```text
EFBF7CFDB972316C
```

A diagnostic build using the exact pinned V2 KissFFT produced the same wrong fingerprint, proving the FFT implementation was not the cause.

The difference was a seemingly harmless reassociation of float operations in Hann-window construction.

V2 behavior is effectively:

```c
float hann = x * x;
window = fft_norm * hann;
```

The first RX-1C version used:

```c
window = fft_norm * x * x;
```

The expressions are mathematically equivalent but not guaranteed to be float-bit identical. Restoring the V2 operation grouping restored the exact RX-1A waterfall hash.

This is a useful rule for the remaining structural cleanup:

> Do not rewrite mathematically equivalent DSP expressions casually while behavior is being frozen. The golden boundary, not visual/code similarity, decides whether the algorithm remained unchanged.

## 10. RX-1C exit criteria

```text
[done] explicit monitor instance
[done] caller-supplied/queryable workspace
[done] no mutable monitor DSP singleton
[done] explicit init/failure/full status
[done] new-window versus stream-reset semantics
[done] two independent monitor instances proven
[done] V2 6 kHz monitor dimensions preserved
[done] byte-identical RX-1A FT8 waterfall preserved
[done] no MiniShell/platform/UI/timing dependencies
[done] no candidate/LDPC/message migration mixed into RX-1C
```

## 11. Next stage

Next active stage:

**RX-1D — bring candidate search plus likelihood/LDPC/CRC behind `ft8_engine`, preserving the pinned V2 decode policy and exact RX-1A payload behavior.**

RX-1D remains a boundary/ownership cleanup stage. Candidate ranking, search improvements, deep search, SNR changes, and other decoder-performance work remain deferred.
