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

The RX contract now also fixes these points:

1. **V2 SNR is the structural-cleanup baseline.** Candidate sync score and SNR are separate values. The chosen V2 SNR estimator is preserved while ownership is cleaned, and may be improved later as a deliberate algorithm change.
2. **Protocol message type is first-class output.** Normal typed FT8 messages keep their decoded type and structured field metadata; V3 must not render text and then re-tokenize it to recover structure.
3. **Free-text CQ exception.** A protocol `FREE_TEXT` message may additionally be classified as a logical CQ when its canonical text exactly matches the validated grammar `CQ <nnn|AAAA> <valid-callsign> [grid]`. The protocol type remains `FREE_TEXT`.
4. **Deep-search station hint.** `ft8_engine` may later accept an explicit optional local-callsign/search context for reply-to-me deep search. This is decoder search context only; AutoSeq state, reply decisions, IgnoreList, TX stage, and UI policy stay outside the engine.

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
```

Canonical review artifacts:

```text
rx-decoder-contract.md
rx-v2-production-review.md
```

The production review confirms that `ft8_engine` should produce protocol-level decoded-slot results. Its ordinary decode semantics remain protocol-level, while future deep-search algorithms may receive explicit station-aware search hints. Station-aware logical transformation, CQ/to-me classification, IgnoreList, AutoSeq, TX arming, RTC correction policy, logging, presentation sorting, and UI handoff remain outside the engine.

The current RX-0B source review is:

```text
monitor.h / monitor.c
```

Goals of that review:

- separate explicit monitor state from hidden static/singleton storage;
- make FFT/workspace/waterfall ownership visible;
- preserve incremental streaming processing;
- distinguish normal slot/decode-window reset from stream-discontinuity reset;
- make initialization failure and memory requirements explicit;
- identify V2's 6 kHz / 960-point implementation constraints without treating them as permanent protocol requirements;
- preserve mathematics during structural cleanup.

Do not copy/refactor implementation code until this review is complete. After RX-0 is understood, RX-1 begins source-by-source cleanup of the FT8 core. Structural cleanup remains separate from intentional DSP/algorithm improvement.
