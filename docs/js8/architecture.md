# JS8Chat v0.1 Architecture

Status: **FROZEN for v0.1 implementation**

This document is the canonical architecture contract for the MiniShell built-in JS8Chat v0.1 application.

## Interoperability reference

The frozen wire/protocol reference is:

```text
JS8Call-improved
release: v3.0.3
tag:     v3.0.3
```

JS8Chat v0.1 should be bit/wire compatible with JS8Call 3.0.3 for the subset implemented here.

Current JS8Call-improved `master` is a secondary compatibility check only. Changes after v3.0.3 do not automatically change the JS8Chat v0.1 architecture.

## Product boundary

JS8Chat is a compile-in MiniShell application providing an MCU-oriented JS8 endpoint for keyboard chat and simple portable operation.

v0.1 includes:

- JS8 Normal mode;
- multi-signal decode using the existing Mini-FT8 approach;
- heartbeat TX/RX;
- automatic heartbeat acknowledgement;
- heard/reachable station activity;
- `CQ` and `CQ FIELD`;
- standard and compound callsigns;
- directed free-text TX/RX;
- multiple local peer conversations;
- multi-frame FIRST/LAST reassembly;
- compact `ACK` and `73`;
- Huffman TX/RX;
- JSC TX/RX using upstream JS8Call lookup/compression semantics;
- six-line chat/activity UI;
- simple QSO logging.

Deferred beyond v0.1:

- group operation, including `@POTA` and `@SOTA`;
- relays;
- store-and-forward inbox / `MSG` services;
- APRS/APRS-IS;
- PSKReporter/internet spotting;
- TCP/UDP/JSON remote APIs;
- the general automatic query suite;
- additional JS8 speeds.

## MiniShell integration and module split

JS8Chat is not a standalone repository/product boundary. It is built into MiniShell, like FT8.

```text
MiniShell
|
+-- JS8Chat app
|   |
|   +-- js8_engine
|   |   +-- symbol/framing PHY
|   |   +-- CRC-12
|   |   +-- LDPC(174,87)
|   |   +-- Costas sync
|   |   +-- 8-FSK modulation/demodulation
|   |   +-- candidate search / multi-decode
|   |
|   +-- js8_protocol
|   |   +-- JS8Call v3.0.3 application frames
|   |   +-- callsign packing
|   |   +-- compound callsign handling
|   |   +-- HB / CQ / directed / continuation
|   |   +-- Huffman / JSC
|   |
|   +-- js8chat_core
|       +-- station activity
|       +-- conversation state
|       +-- RX reassembly contexts
|       +-- TX scheduler
|       +-- UI-independent application state
|
+-- shared MiniShell services
    +-- audio / USB-UAC
    +-- display / keyboard
    +-- accurate UTC time
    +-- GPS / RTC
    +-- filesystem / logging
    +-- radio / platform services
```

JS8Chat does not own hardware drivers. MiniShell owns platform services. The JS8 core should remain host-testable, but the deployed app is compiled into MiniShell.

## Time ownership

MiniShell is expected to have GPS support and therefore provide accurate UTC.

JS8Chat consumes MiniShell time and schedules JS8 Normal 15-second slots from that source.

The JS8 decoder may estimate/report received time offset/drift as part of DSP, but v0.1 does not require the desktop application's full manual per-peer clock-shift UI.

## Callsigns

v0.1 supports both:

```text
standard callsigns
compound / portable callsigns
```

This includes portable forms such as `/P` and compound prefix/suffix forms supported by JS8Call 3.0.3.

Compound callsigns may require the upstream multi-frame compound/compound-directed representation. Implement that behavior as defined by the v3.0.3 reference.

Group callsigns are not part of v0.1 except where a protocol-internal form is required for heartbeat interoperability.

## Conversation model

JS8 directed messages are addressed by callsign and do not establish a protocol-level connection/channel.

Therefore:

```text
conversation != RF channel
conversation != audio offset
```

JS8Chat keeps multiple local peer conversation contexts. One is selected for display/reply at a time; other conversations continue to receive and accumulate messages.

Suggested per-peer state:

```text
callsign
last_rx_offset
last_rx_time
last_snr
unread
conversation/history
optional QSO state
```

Temporary RF reassembly state is separate from persistent conversation state.

## Frequency-offset policy

Directed messages can be decoded anywhere in the receiver passband. A conversation is not bound to a peer's last RX offset.

v0.1 policy:

```text
HB / HB ACK     choose a clear offset in the 500-1000 Hz HB region
CQ / CQ FIELD   use current local TX offset
directed chat   use current local TX offset
peer RX offset  remember for display / optional later QSY
```

Selecting a peer does not automatically change the local TX offset.

An optional future UI action may jump to the peer's last-heard offset, similar to desktop JS8Call.

## Receive reassembly

Continuation frames do not repeat full addressing information.

Maintain a small bounded set of temporary RX contexts, keyed primarily by RF/audio offset plus timing/addressing context.

Each context tracks enough state to associate FIRST/intermediate/LAST frames with the directed message that opened it.

When complete, append the resulting message to the peer's conversation history.

## TX scheduler

There is one transmitter and all outgoing work is serialized.

Priority policy:

```text
1. active user-directed chat TX / queued user chat
2. finish receiving an active multi-frame directed message to us
3. automatic heartbeat ACK
4. periodic CQ / heartbeat
```

While a directed multi-frame message to us is still open, automatic replies should be suppressed/deferred until LAST or receive-context timeout.

Heartbeat/CQ automation must never preempt active chat traffic.

## Text codecs

v0.1 supports:

```text
TX: Huffman + JSC
RX: Huffman + JSC
```

For outgoing data, build both candidates and choose the one that consumes more source text, matching JS8Call 3.0.3 behavior.

JSC lookup/compression semantics must follow upstream JS8Call rather than introducing a different algorithm.

## JSC storage

The current packed receive dictionary is about 1.83 MiB.

The full TX+RX resource is expected to remain only a few MiB and may be stored:

```text
1. dedicated read-only internal-flash partition
2. compiled-in read-only resource
3. microSD
```

Do not freeze the final ADV flash partition layout until the TX lookup structure is extracted and measured.

## UI model

The existing MiniFT8-style 20x7 display model is retained:

```text
1 top status line
6 body lines
```

Idle/monitoring:
- body shows active calls;
- up to three callsigns per line when they fit;
- left/right browses active calls/pages.

Chat:
- six lines show the selected conversation;
- up/down scrolls message history;
- messages to other peers set unread/new state;
- a shortcut recalls active calls/conversations, preferably as a temporary two-line overlay.

Multiple-chat behavior is an application/UI feature, not a wire-protocol feature.

## QSO logging

v0.1 records enough for a useful local QSO record:

```text
UTC
band/frequency
callsign
SNR
optional exchange/reference
```

ADIF export and network upload are later work.

## Architecture freeze rule

The following are now implementation/validation tasks, not architecture blockers:

- exact bit-packing golden vectors;
- CRC/LDPC vectors;
- JSC TX packed size;
- JSC TX lookup/consume-length equivalence checks;
- DSP sensitivity/performance;
- receive-context collision tests;
- ADV timing benchmarks;
- on-air interoperability tests.

Changes to this frozen architecture should require an explicit v0.1 design change, not be inferred from future JS8Call master development.
