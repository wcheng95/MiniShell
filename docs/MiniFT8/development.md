# MiniFT8-V3 Development

MiniFT8 runs as a MiniShell app with independent RX Audio, TX Audio, and Control resources.

Audio V1 transport for MiniFT8 is 12 kHz/S16/two-channel. MiniShell preserves channel order; MiniFT8 source profiles decide ordinary-audio versus I/Q meaning. `tests/kfs16b12k.wav` is the deterministic reference and is streamed unchanged by the Linux WAV provider.

The MiniShell H1-H5 housekeeping audit and final boundary review are complete. No known MiniShell debt blocks MiniFT8 RX work.

## Current milestone: decode RX

The canonical RX architecture and development sequence are in `rx.md`.

The high-level pipeline is:

```text
MiniShell Audio
    -> rx_audio_adapter
    -> rx_frontend
    -> rx_slot_framer
    -> ft8_monitor
    -> candidate finder
    -> candidate decoder
    -> message/result builder
    -> RxBatch
    -> app_controller
    -> RX UI
```

Normal raw audio remains streaming and bounded; the retained decode representation is the whole decode-window waterfall. Raw PCM slot retention/double buffering is optional research functionality, never a normal decoder requirement.

## Locked RX refinements

The RX contract fixes these points:

1. **V2 SNR is the structural-cleanup baseline.** Candidate sync score and SNR are separate values. The chosen V2 SNR estimator is preserved while ownership is cleaned, and may be improved later as a deliberate algorithm change.
2. **Protocol message type is first-class output.** Normal typed FT8 messages keep their decoded type and structured field metadata; V3 must not render text and then re-tokenize it to recover structure.
3. **Free-text CQ exception.** A protocol `FREE_TEXT` message may additionally be classified as a logical CQ when its canonical text exactly matches the validated grammar `CQ <nnn|AAAA> <valid-callsign> [grid]`. The protocol type remains `FREE_TEXT`.
4. **Deep-search station hint.** `ft8_engine` may later accept an explicit optional local-callsign/search context for reply-to-me deep search. This is decoder context only; AutoSeq state, reply decisions, IgnoreList, TX stage, and UI policy stay outside the engine.

The important distinction is:

```text
station identity as decoder/search hint     allowed
station/QSO policy ownership                not allowed
```

## Current task: RX-0

### RX-0A — architecture/source map — complete

`rx.md` records:

- the seven logical RX boundaries;
- the streaming/RAM rule;
- MiniFT8-V2 RX source classification;
- golden-reference policy;
- RX-0 through RX-7 development sequence.

### RX-0B — extract the V3 decoder contract from V2 — in progress

Completed reviews:

```text
V2 tests/tx_e2e/decode_helper.cpp
V2 production decode_monitor_results()
V2 monitor.h / monitor.c
V2 decode.h / decode.c
```

Canonical review artifacts:

```text
rx-decoder-contract.md
rx-v2-production-review.md
rx-monitor-review.md
rx-decode-review.md
```

### Monitor direction

The monitor review locks this direction without changing DSP mathematics:

```text
explicit Ft8Monitor instance
    + explicit/queryable memory requirements
    + caller-supplied workspace
    + no mutable module-global DSP storage
    + explicit init/process status
    + explicit new-window versus stream-discontinuity reset semantics
```

The monitor remains narrowly responsible for:

```text
streaming engine-native PCM
    -> overlapping FFT analysis
    -> compact FT8/FT4 waterfall
```

### Decode-core direction

`decode.h/c` is substantially cleaner than `monitor.c` and should be preserved rather than rewritten.

The conceptual internal boundaries are:

```text
completed waterfall
    -> candidate finder / Costas sync
    -> candidate descriptor
    -> likelihood extraction
    -> likelihood normalization
    -> LDPC/FEC
    -> CRC validation
    -> validated protocol payload
```

Locked decode-core cleanup points:

- no mutable global decoder state was found;
- candidate arrays remain caller-owned and bounded;
- V2 Costas/likelihood/LDPC/CRC mathematics stay unchanged initially;
- production candidate capacity 50, minimum score 5, and LDPC max 25 are explicit decode-profile policy rather than protocol constants;
- the embedded `time_offset=-10..19` search range is preserved first, then may become explicit search policy;
- candidate score is sync score, never SNR;
- dead `freq/time` fields in candidate-decode status should not survive as misleading fields;
- decode outcome should eventually distinguish `OK`, `LDPC_FAIL`, `CRC_FAIL`, and invalid input rather than only boolean + partially written status;
- unused `db_power_sum[]`, `ft8_decode_multi_symbols()`, and disabled historical alternatives should not be copied into clean V3;
- future reply-to-me deep search belongs at the engine decode-pass strategy boundary. The local callsign may influence candidate discovery, prior-assisted likelihood/LDPC decoding, or an additional deep pass; it must not be hard-wired specifically into `ftx_find_candidates()`.

The next and final planned RX-0B core source review is:

```text
message.h / message.c
```

Goals:

- protocol message type and structured field ownership;
- callsign hash-store interface and lifecycle;
- Field Day, DXpedition, nonstandard-call, telemetry, and free-text boundaries;
- decide exactly what `ft8_engine` returns versus what `rx_result_builder` derives;
- verify that normal typed messages never require rendered-text reparsing.

Do not copy/refactor implementation code until the RX-0 reviews are complete. After RX-0 is understood, RX-1 begins source-by-source cleanup of the FT8 core. Structural cleanup remains separate from intentional DSP/algorithm improvement.
