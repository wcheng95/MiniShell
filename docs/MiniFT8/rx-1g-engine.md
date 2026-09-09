# MiniFT8-V3 RX-1G — Pure `Ft8Engine` Assembly

Status: **COMPLETE**

RX-1G assembles the already-clean RX-1C through RX-1F pieces behind one explicit MiniFT8-owned FT8 engine lifecycle.

The stage is deliberately an assembly/regression checkpoint, not a DSP or protocol change.

## 1. Completed boundary

```text
6 kHz mono float
    -> Ft8Engine
       -> Ft8Monitor
          compact waterfall
       -> candidate search
       -> likelihood / BP-LDPC / CRC-14
       -> validated 10-byte payload
       -> typed protocol codec
          <-> Ft8HashStore
       -> exact-payload dedupe
    -> Ft8ProtocolSlot
```

The application-facing RX frontend, MiniShell Audio, slot framer, result builder, UI, AutoSeq, TX, and ADIF remain outside this stage.

## 2. New engine edge

RX-1G adds:

```text
apps/ft8/src/ft8_engine/ft8_engine.h
apps/ft8/src/ft8_engine/ft8_engine.c
```

The public lifecycle inside MiniFT8 is:

```text
ft8_engine_baseline_config()
ft8_engine_query_requirements()
ft8_engine_init()
ft8_engine_begin_window(slot_id)
ft8_engine_process_block(960 samples)
ft8_engine_finalize_window()
ft8_engine_reset_stream()
ft8_engine_destroy()
```

`Ft8Engine` owns:

```text
monitor instance/state
candidate array
Ft8HashStore
current window/slot state
RX-1D decoder policy
```

The caller owns the engine object and supplies the queried DSP workspace. The engine does not allocate memory and does not call MiniShell.

## 3. Baseline policy remains unchanged

```text
sample rate            6000 Hz
monitor block           960 samples
f_min                    200 Hz
f_max                   2900 Hz
time_osr                    2
freq_osr                    1
candidate capacity          50
minimum sync score           5
max LDPC iterations         25
```

No decoder mathematics, FFT policy, candidate search, LDPC, CRC, message unpacking, or hash behavior was intentionally changed.

## 4. Workspace visibility

`Ft8EngineRequirements` exposes both the caller-supplied workspace and fixed state classes:

```text
workspace_bytes
alignment
monitor_workspace_bytes
engine_instance_bytes
candidate_storage_bytes
hash_store_bytes
```

In RX-1G the external workspace requirement is the existing monitor workspace. Candidate storage and the compact hash store live in the caller-owned `Ft8Engine` instance and are reported separately rather than hidden in globals.

## 5. Window and stream semantics

Normal window lifecycle:

```text
begin_window(slot N)
    -> process complete 960-sample blocks
    -> finalize_window()
    -> Ft8ProtocolSlot(slot N)
```

Starting another window while one is active is an explicit state error.

`reset_stream()` means sample/DSP continuity was lost:

```text
reset monitor stream/history
cancel active decode window
preserve Ft8HashStore knowledge
```

This preserves the RX-1B distinction between a normal new decode window and a transport discontinuity.

## 6. Hash-store aging ownership

RX-1G establishes the once-per-slot aging owner.

When a new window begins after a completed window:

```text
slot N calls learned/refreshed -> age 0 during N
begin slot N+1             -> age store once
```

The first window does not perform a meaningless initial age step. An aborted window caused by `reset_stream()` is not counted as a completed slot for aging.

The hash store remains persistent across normal windows and stream resets.

## 7. Finalize behavior

`ft8_engine_finalize_window()`:

1. obtains the completed waterfall;
2. runs the configured candidate search;
3. attempts RX-1D LDPC/CRC decode on every candidate;
4. passes every valid payload to the RX-1F typed codec;
5. resolves/saves callsigns through the engine-owned `Ft8HashStore`;
6. deduplicates messages by exact 10-byte payload;
7. returns a caller-backed `Ft8ProtocolSlot`.

A successful slot with no valid messages returns `FT8_ENGINE_NO_MESSAGES`; this is normal data-plane behavior, not a platform failure.

Caller output capacity exhaustion is explicit as `FT8_ENGINE_ERR_OUTPUT_FULL`.

## 8. Unit coverage

`tests/ft8_engine_rx1g_test.c` covers:

```text
requirements query
insufficient-workspace rejection
explicit initialized/window state rules
begin/process/finalize lifecycle
normal no-message window
stream reset
safe destroy
separate workspaces for two simultaneous engine instances
separate per-engine hash stores
```

The normal Linux suite passes with RX-1G included.

## 9. Pure cleaned golden regression

`tests/ft8_engine_rx1g_reference.c` drives the pinned RX-1A 6 kHz WAV only through the new engine edge.

Reference source:

```text
MiniFT8-V2 commit
5bd3ef98f72388a850bebad04bd7300b90edb63c

tests/tx_e2e/golden/ft8_cq_w1xyz_fn42.wav
```

Required output:

```text
message_count = 1
payload       = 000000206016500A1988
type          = STANDARD
parse_status  = OK
text          = CQ W1XYZ FN42
```

The dedicated `RX-1G Reference` CI workflow passes this regression.

This is the first regression proving the complete cleaned protocol core as one owner:

```text
6 kHz PCM
    -> monitor
    -> waterfall
    -> candidate / LDPC / CRC
    -> exact payload
    -> protocol decode
    -> Ft8ProtocolSlot
```

## 10. Intentionally deferred

RX-1G does not add:

```text
MiniShell Audio
12 kHz -> 6 kHz frontend
slot/sample framing
SNR redesign
station-aware CQ/to-me classification
FREE_TEXT CQ application classification
UI
AutoSeq
TX
ADIF
ADV production composition of the RX engine
```

## 11. Exit decision

RX-1G is complete because:

```text
[done] one explicit Ft8Engine owner exists
[done] lifecycle and state errors are explicit
[done] workspace remains caller supplied
[done] candidate/hash state are per-engine, not global
[done] hash aging has an explicit once-per-completed-slot owner
[done] stream reset preserves protocol knowledge
[done] exact-payload dedupe is inside the engine path
[done] pure 6 kHz PCM -> typed protocol slot golden passes
[done] prior RX-1C/RX-1D/Linux regressions remain green
```

Next stage: **RX-2 — pure MiniShell-independent host FT8 decoder/use harness around the completed engine boundary.**
