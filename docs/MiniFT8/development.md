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
    -> protocol message codec
    -> Ft8ProtocolSlot
    -> rx_result_builder
    -> RxBatch
    -> app_controller
    -> RX UI
```

Normal raw audio remains streaming and bounded; the retained decode representation is the whole decode-window waterfall. Raw PCM slot retention/double buffering is optional research functionality, never a normal decoder requirement.

## Locked RX refinements

1. **V2 SNR is the structural-cleanup baseline.** Candidate sync score and SNR are separate values. The chosen V2 SNR estimator is preserved while ownership is cleaned, and may be improved later as a deliberate algorithm change.
2. **Protocol message type is first-class output.** Normal typed FT8 messages keep their decoded type and structured field metadata; V3 must not render text and then re-tokenize it to recover structure.
3. **Free-text CQ exception.** A protocol `FREE_TEXT` message may additionally be classified as a logical CQ when its canonical text exactly matches the validated grammar `CQ <nnn|AAAA> <valid-callsign> [grid]`. The protocol type remains `FREE_TEXT`.
4. **Deep-search station hint.** `ft8_engine` may later accept explicit station-aware decoder/search context such as the local callsign. That context may influence candidate search or prior-assisted decoding, but AutoSeq state, reply decisions, IgnoreList, TX stage, and UI policy remain outside the engine.
5. **Hashed callsigns are explicit engine state.** An instance-owned/context-aware `Ft8HashStore` persists across slots and is aged explicitly; it is not MiniShell state and does not require a global table.

Locked distinction:

```text
station identity as decoder/search hint     allowed
station/QSO policy ownership                not allowed
```

## RX-0 — architecture and source review — complete

### RX-0A — architecture/source map

`rx.md` records:

- the seven logical RX/DSP boundaries;
- the separate station-aware `rx_result_builder` boundary;
- the streaming/RAM rule;
- MiniFT8-V2 RX source classification;
- golden-reference policy;
- RX-0 through RX-7 development sequence.

### RX-0B — V2 decoder-contract extraction

Completed reviews:

```text
V2 tests/tx_e2e/decode_helper.cpp
V2 production decode_monitor_results()
V2 monitor.h / monitor.c
V2 decode.h / decode.c
V2 message.h / message.c
```

Canonical review artifacts:

```text
rx-decoder-contract.md
rx-v2-production-review.md
rx-monitor-review.md
rx-decode-review.md
rx-message-review.md
```

### Monitor direction

```text
explicit Ft8Monitor instance
    + explicit/queryable memory requirements
    + caller-supplied workspace
    + no mutable module-global DSP storage
    + explicit init/process status
    + explicit new-window versus stream-discontinuity reset semantics
```

The monitor owns only:

```text
streaming engine-native PCM
    -> overlapping FFT analysis
    -> compact FT8/FT4 waterfall
```

### Decode-core direction

`decode.h/c` is largely a pure bounded algorithm core and should be preserved rather than rewritten:

```text
completed waterfall
    -> Costas candidate finder
    -> likelihood extraction
    -> normalization
    -> LDPC/FEC
    -> CRC validation
    -> validated 77-bit payload
```

V2 Costas/likelihood/LDPC/CRC mathematics stay unchanged initially. Production candidate capacity 50, minimum score 5, LDPC max 25, and the current time-search range are explicit V2 profile policy, not permanent protocol constants.

Future deep search is an engine decode-pass strategy. It is not hard-wired specifically into candidate finding and may later use station-aware priors during candidate discovery, likelihood processing, or LDPC recovery.

### Message-codec direction

The protocol codec ends at a typed protocol result:

```text
CRC-valid payload
    -> protocol type classification
    -> structured message-specific unpack
    -> Ft8ProtocolMessage
```

Rendered text is convenience output derived from typed protocol data, never the canonical source of structure.

The generic V2 three-field/offset representation is not sufficient as the V3 domain model. Field Day and DXpedition already contain richer structure internally and should expose it directly.

Known codec gaps are recorded rather than hidden:

- type 0.6 `CONTESTING` exists in the enum but is not currently mapped by `ftx_message_get_type()`;
- `EU_VHF`, `ARRL_RTTY`, and `WWROF` are recognized types without generic structured decode handlers;
- known protocol type and structured-unpack support are therefore separate facts;
- output buffer capacity safety needs cleanup;
- exact payload comparison is canonical duplicate identity; the CRC-derived quick hash is not collision-free identity.

The callsign hash callback concept is retained, but V3 removes the global-storage requirement by adding explicit context/ownership through `Ft8HashStore`.

TX-specific message parsing/encoding cleanup is deferred to the TX milestone.

## Current task: RX-1 — clean FT8 decode core

RX-1 starts implementation, but still makes **no deliberate DSP/algorithm change**.

Recommended order:

```text
RX-1A  establish golden tests/fixtures
RX-1B  define MiniFT8-owned internal decoder/result types
RX-1C  clean monitor memory ownership/lifecycle
RX-1D  bring candidate + likelihood + LDPC + CRC core across
RX-1E  implement explicit Ft8HashStore
RX-1F  implement typed protocol message codec
RX-1G  pure cleaned decoder golden regression
```

### RX-1A — first implementation step

Before moving decoder code, freeze tests for the boundaries we intend to clean:

```text
PCM -> V2 monitor -> exact waterfall bytes/hash
waterfall -> V2 candidate/decode -> payloads
payload -> V2 message codec -> type + supported decoded semantics
```

Use committed V2 golden FT8/FT4 WAVs plus independent/real audio where appropriate.

Hard invariants during structural cleanup:

```text
same monitor waterfall for same engine-native PCM
same valid protocol payloads
same supported protocol message types/semantics
same callsign-hash resolution behavior
no duplicate exact payloads
```

Known V2 gaps/fixes are tested separately and never smuggled into a structural commit.

Do not change sample rate, resampling, OSR, SNR algorithm, candidate math, likelihood math, LDPC math, or deep-search behavior in RX-1 structural work.