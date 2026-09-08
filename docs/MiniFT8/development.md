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

Normal raw audio remains streaming and bounded; the whole-slot retained representation is the waterfall. Raw PCM slot retention/double buffering is optional research functionality, never a normal decoder requirement.

## Current task: RX-0

RX-0 has two parts:

### RX-0A — freeze architecture and source map

- define the seven logical RX boundaries;
- lock the streaming/RAM rule;
- inventory MiniFT8-V2 RX sources;
- classify V2 files as KEEP/CLEAN/REWRITE/DROP;
- define golden-reference policy.

`rx.md` is the canonical RX-0A artifact.

### RX-0B — extract the V3 decoder contract from V2

Start from MiniFT8-V2 `tests/tx_e2e/decode_helper.cpp`, because it exposes the simplest useful decoder chain:

```text
PCM
    -> monitor
    -> candidate search
    -> candidate decode
    -> message decode
```

Do not copy code yet. Review ownership and interfaces first, then establish the V3 engine contract and golden cases.

After RX-0 is understood, RX-1 begins source-by-source cleanup of the FT8 core. Structural cleanup is kept separate from any intentional DSP/algorithm improvement.
