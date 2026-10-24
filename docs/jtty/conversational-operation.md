# JTTY conversational operation — decision (2026-10-23)

Status: **design decision / not implemented**.

## Goal

Use standard JTTY for conversational HF QSOs, particularly exchanging SOTA/POTA references, without inventing a new on-air message type or imposing an automatic QSO sequence. The attractive feature is JTTY's free-running 1.888-second frames, rather than fixed 15-second operating slots.

This document specifies an **optional application-level convention**, not a modification to JTTY framing, modulation, FEC, or source encoding. Preserve interoperability with standard JTTY/WSJT-X stations.

## Operating convention

When an operator chooses to follow the convention, a free-text message has this form:

```text
<destination> <sender> [arbitrary free text]
```

- Field 1 is a destination callsign or CQ variant, such as `CQ`, `CQPOTA`, or `CQDX`.
- Field 2 is the transmitting station's callsign.
- Everything after field 2 is **unrestricted free text**; it has no mandatory grammar.
- No other fields or message order are required. This convention does not restrict what other standard JTTY users may transmit.
- `CQPOTA` is an application text convention, **not a new native JTTY action code**.

Example exchange:

```text
CQPOTA AG6AQ US-0189
AG6AQ K1ABC -5 US-3473
K1ABC AG6AQ R+10
AG6AQ K1ABC 73
K1ABC AG6AQ 73
```

Either operator can continue the conversation naturally. References like `US-0189` are passed as text, not protocol-specific objects. SOTA references can be passed likewise.

## Scope of a potential MiniShell JTTY application

- **RX:** accept/decode **all standard JTTY source message types**, not only TEXT5 or messages using this convention. Render unfamiliar or nonconforming messages without rejecting them.
- **TX:** free text is sufficient as an initial scope. Native structured CQ or broader structured TX may be added later if useful.
- **Message handling:** use standard JTTY end-of-message framing to assemble logical text; do not equate one frame with one message.
- **UI:** allow manual transmission and continuation; no mandatory state machine, directed-message protocol, automatic QSO sequencing, or custom JChat wire extension.
- **Logging:** preserve the full RX/TX conversation as text, CW-style.
- **Extraction:** callsigns, SNR, SOTA/POTA references and exchange details may be *tentatively inferred* for UI/logging or entered/corrected manually. Failed or ambiguous extraction never invalidates a message or overwrites the raw record.

Keep JTTY modem/source codec, optional application parsing, and logging separate. Application interpretation must not alter standard JTTY encoding or decoder validity.

## UI direction under discussion

A likely field-use model is:

```text
RX activity screen
  - free-running scrolling history
  - newest at bottom
  - scroll back several pages
  - older retained messages remain selectable

select/click a message
        ↓
focused TX/QSO screen
  - stay on the focused screen after TX
  - show new RX from the engaged station
  - macros assist composition
  - Enter sends
```

This UI is intentionally not finalized yet.

## Implementation implications

The current engineering priority is RX-only. The PHY/FEC/source decoder and reference-vector work remain useful regardless of final UI.

If interactive TX is added later, prefer full standard RX plus simple standard-JTTY TX before building contest-style workflow or any custom conversational protocol.

Non-goals:

- custom on-air message types;
- native SOTA/POTA bit fields;
- forced report/acknowledgment/73 sequence;
- guaranteed automatic extraction;
- mandatory structured CQ TX.
