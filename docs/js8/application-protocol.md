# JS8Chat Application Protocol Scope

This document defines the intended **v0.1 interoperability subset** for the MiniShell built-in JS8Chat app. It deliberately separates the JS8 modem/DSP from the JS8Call application protocol.

## Status

The v0.1 application architecture is **FROZEN for implementation**. No serious architectural blocker is currently known.

Implementation is deferred until development time is available; the MiniShell app is parked by schedule, not by an unresolved design problem.

Important assumptions already validated by related work:

- Mini-FT8 already provides multi-signal decoding, so JS8Chat does not need a new multi-candidate receiver architecture from scratch;
- JS8 Normal is close enough to the existing FT8 work that substantial DSP reuse is expected;
- the JSC resources are small enough to be practical on Cardputer ADV internal flash, with microSD still available as an alternative;
- JS8Chat is intended to be fully open source and compatible with the licensing of the upstream JS8Call/JSC material.

## Goal

The first JS8Chat application should do four useful things:

1. advertise that this station is alive;
2. establish likely two-way reachability through heartbeat acknowledgements;
3. call or answer `CQ`, including `CQ FIELD` for portable operation;
4. exchange directed free text with multiple peers, while showing/replying to one selected conversation at a time.

The intended operator flow is:

```text
MONITOR
  |
  +-- periodic heartbeat ------------------------------+
  |                                                    |
  +-- receive heartbeat -> update station -> auto ACK  |
  |                                                    |
  +-- receive ACK to our heartbeat -> mark REACHABLE   |
  |                                                    |
  +-- CQ / CQ FIELD TX or RX ---------------------------+
  |                                                    |
  +-- select station / answer CQ -> directed chat -----+
  |                                                    |
  +-- receive directed chat -> store by peer ----------+
  |                                                    |
  +-- switch selected peer -> display/reply ------------+
```

## Compatibility target

- Normal-speed JS8 first.
- RF interoperability with **JS8Call-improved v3.0.3** is the frozen target.
- The `v3.0.3` source/tag is the normative implementation reference when written documentation and implementation disagree.
- Current JS8Call-improved master is a secondary compatibility check only.
- Additional JS8 speeds can be added after Normal-mode interoperability is proven.

## Physical frame and protocol envelope

The 75-bit PHY payload contains 72 application bits followed by three transmission
flags. **Physical transmission bits are not application FrameType.** The physical
12-character representation encodes the 72 application bits; it is not decoded
message text. In host diagnostics, legacy `type=` and `tx_raw=` both report the
raw transmission field.

The pinned v3.0.3 [Varicode enums](https://github.com/JS8Call-improved/JS8Call-improved/blob/v3.0.3/JS8_Main/Varicode.h)
and [Normal data unpacking](https://github.com/JS8Call-improved/JS8Call-improved/blob/v3.0.3/JS8_Main/Varicode.cpp)
define these independent mappings:

| Application prefix | Application class |
| --- | --- |
| `000` | HEARTBEAT |
| `001` | COMPOUND |
| `010` | COMPOUND_DIRECTED |
| `011` | DIRECTED |
| `100`, `101` | DATA |
| `110`, `111` | DATA_COMPRESSED |

For the two data families, the third prefix bit belongs to the payload.
The tail flags are `FIRST=1`, `LAST=2`, and `DATA=4`; zero means none and all
bitwise combinations are possible. The DATA transmission flag is distinct from
the DATA application class. Upstream [DecodedText dispatch](https://github.com/JS8Call-improved/JS8Call-improved/blob/v3.0.3/JS8_Mode/DecodedText.cpp)
uses that flag to select a different data unpacker. T056 only reports it while
classifying the Normal prefix; it does not implement that unpacker or another mode.

The T055 payload starts with `111` and ends with `010`: physical frame
`vTA7BWh1Y7++` therefore has application class **DATA_COMPRESSED** and transmission
flag **LAST**. Envelope classification performs no content decoding or reassembly
and does not replace LDPC/CRC validation.

## Shared compound and beacon content (T057)

Normal HEARTBEAT, COMPOUND, and COMPOUND_DIRECTED share these 72 application bits:

| Bits | Field |
| --- | --- |
| 0..2 | Application class |
| 3..52 | callsign50 |
| 53..68 | extra16 |
| 69..71 | bits3 |

The JS8-owned pure compound decoder owns callsign50 unpacking. It follows the
v3.0.3 mixed radices `[39][38][38][2][38][38][38][2][38][38][38]`, using alphabet
`0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ /@`; the radix-2 positions are space/slash
separators. All spaces are removed without extra callsign validity filtering.
The raw decoder returns callsign, extra16 and bits3 for all three classes.
COMPOUND_DIRECTED stays raw, with no command interpretation.

For HEARTBEAT, extra16 bit 15 selects HB (0) or CQ (1), and its low 15 bits carry
the four-character grid. All eight HB subtypes name `HB`; CQ bits3 maps as follows:

| bits3 | CQ name |
| --- | --- |
| 0 | CQ CQ CQ |
| 1 | CQ DX |
| 2 | CQ QRP |
| 3 | CQ CONTEST |
| 4 | CQ FIELD |
| 5 | CQ FD |
| 6 | CQ CQ |
| 7 | CQ |

Plain COMPOUND uses extra16 directly as a grid only when it is at most 32400;
otherwise it preserves the raw value and reports no grid. Upstream `unpackGrid`
includes 32400 (RA90); larger values, including 32767, produce an empty grid.
No command-range extras are interpreted. Transmission flags remain independent
of this content decoding. CQ FIELD is a calling variant, not group operation.

## Standard directed RX content (T058)

Standard DIRECTED frames carry these 72 application bits:

| Bits | Field |
| --- | --- |
| 0..2 | `011` DIRECTED |
| 3..30 | from callsign28 |
| 31..58 | to callsign28 |
| 59..63 | command5 |
| 64 | portable from |
| 65 | portable to |
| 66..71 | number6 |

The final three PHY transmission flags remain independent. The pure JS8 directed
parser owns callsign28 decoding, including the complete v3.0.3 `basecalls` table
at `262177560 + 1..54`. These special names (including `<....>` and group names)
ignore portable flags when rendered; raw flags remain exposed. Generic callsigns
are trimmed, expand `3D0` to `3DA0` and `Q[A-Z]` to `3X[A-Z]`, and append `/P`
for the corresponding portable flag. `<....>` stays an unresolved placeholder.

Canonical command strings preserve spaces, matching v3.0.3 `directed_cmds.key()`:

| Code | String | Code | String |
| --- | --- | --- | --- |
| 0 | ` SNR?` | 16 | ` INFO?` |
| 1 | ` DIT DIT` | 17 | ` INFO` |
| 2 | ` NACK` | 18 | ` FB` |
| 3 | ` HEARING?` | 19 | ` HW CPY?` |
| 4 | ` GRID?` | 20 | ` SK` |
| 5 | `>` | 21 | ` RR` |
| 6 | ` STATUS?` | 22 | ` QSL?` |
| 7 | ` STATUS` | 23 | ` QSL` |
| 8 | ` HEARING` | 24 | ` CMD` |
| 9 | ` MSG` | 25 | ` SNR` |
| 10 | ` MSG TO:` | 26 | ` NO` |
| 11 | ` QUERY` | 27 | ` YES` |
| 12 | ` QUERY MSGS` | 28 | ` 73` |
| 13 | ` QUERY CALL` | 29 | ` HEARTBEAT SNR` |
| 14 | ` ACK` | 30 | ` AGN?` |
| 15 | ` GRID` | 31 | one space (free text) |

ACK (14), 73 (28), free text (31), and SNR (25/29) are explicitly identified.
Number6 zero means absent; every other value decodes to `number6 - 31`, giving
-30 through +32, including present zero. This applies to every command, not only
SNR. The optional SNR formatter emits signed two-digit text (`-08`, `+05`, `+00`)
for -60..60 and an empty string outside that upstream range.

Decode support does not imply automatic action support. No compound-directed
association, continuation text, reassembly, group-operation policy, auto-reply,
relay/store-forward behavior, or TX packing is implemented here.

## Required application frame classes

v0.1 requires the frame/application forms needed for:

- **Heartbeat**
- **CQ / CQ FIELD**
- **Directed messages**
- **Data / continuation frames**

**Compound callsigns are required in v0.1**, including portable/prefix/suffix forms supported by JS8Call 3.0.3. Group-directed operation remains deferred.

## Heartbeat

A valid received heartbeat should update a compact station record containing at least:

```text
callsign
grid, if present
SNR
RX audio-frequency offset
last-heard time
last-heartbeat time
last-ACK time / reachability state
CQ state / expiry, if applicable
```

### Automatic heartbeat ACK

Automatic ACK is a **required v0.1 feature**.

Reason: a received heartbeat only proves that we can hear the other station. If we ACK it, the other station can know that it is heard. When our own heartbeat is ACKed, we have evidence that the transmit direction also works. That makes heartbeat/ACK history a practical channel-availability indicator before starting chat.

Policy:

```text
receive valid HB
    -> record/update station
    -> schedule ACK
    -> do not preempt active directed chat
    -> transmit ACK at the next safe opportunity
```

Duplicate/rate suppression should prevent a misconfigured station from causing excessive ACK traffic. This is policy, not a change to the wire format.

## Reachability state

Suggested UI semantics:

```text
HEARD      = we decoded that station recently
REACHABLE  = that station ACKed one of our recent heartbeats
CQ         = station recently transmitted a CQ-family call
```

`REACHABLE` is intentionally stronger than `HEARD`, but it is still only evidence from recent RF conditions, not a guaranteed connection.

## CQ and portable operation

v0.1 supports TX/RX of:

```text
CQ
CQ FIELD
```

`CQ FIELD` is the initial portable/POTA/SOTA calling mechanism. Park or summit references are exchanged after a directed conversation begins rather than requiring `@POTA` or `@SOTA` group support.

Group operation, including `@POTA` and `@SOTA`, is therefore **not required for v0.1**. Relay support is also deferred.

## Directed chat

JS8 directed messaging is addressed by callsign; it does not establish a protocol-level connection or channel. Therefore JS8Chat treats a "chat" as a **local application/UI conversation context**, normally keyed by peer callsign.

For normal directed chat, JS8Chat needs to encode/decode the upstream directed-message header and free-text command, then attach subsequent data frames to the correct temporary receive stream until the LAST indication closes that message.

Conceptually:

```text
DIRECTED
    from = AG6AQ
    to   = W6XYZ
    cmd  = free text

DATA
    text fragment

DATA
    text fragment
    LAST
```

No TCP-like connection establishment or disconnect is required.

Compact `ACK` and `73` commands are also part of v0.1 because the directed-command machinery is already required and these provide efficient acknowledgement and QSO termination.

### Multiple conversations

Multiple local one-to-one conversations are a **required v0.1 application feature**.

Example:

```text
W6AAA -> AG6AQ: HELLO WEI
K6BBB -> AG6AQ: GM WEI
```

JS8Chat stores both conversations. The operator can select W6AAA, reply, then switch to K6BBB and reply. There is no need to terminate one conversation before replying to another because there is no protocol-level chat channel to tear down.

Suggested per-peer application state:

```text
callsign
last RX offset
last RX time
unread flag
conversation/history pointer
optional QSO state
```

A small fixed or bounded number of active conversation contexts is sufficient; exact capacity is an implementation choice rather than an on-air protocol constraint.

## TX frequency-offset policy

Directed conversations are callsign-addressed and are not frequency channels.

v0.1 uses:

```text
HB / HB ACK     clear offset in the 500-1000 Hz HB region
CQ / CQ FIELD   current local TX offset
directed chat   current local TX offset
peer RX offset  remembered for display / optional later QSY
```

Selecting a peer does not automatically retune the local TX offset.

## Scheduler policy

There is one transmitter; outgoing work is serialized.

```text
1. user-directed chat TX
2. finish receiving an active multi-frame directed message to us
3. automatic heartbeat ACK
4. periodic CQ / heartbeat
```

Automatic replies are deferred while a directed multi-frame receive context to us remains open. Chat traffic always has priority over heartbeat/CQ automation.

## Platform/time ownership

MiniShell owns audio, keyboard/display, filesystem/logging, radio/platform services, GPS, RTC and UTC time.

GPS-backed MiniShell time is expected to keep slot timing accurate. JS8Chat uses that time source for JS8 Normal scheduling. The decoder may estimate received timing offset, but v0.1 does not require cloning the full desktop manual drift-control UI.

## Receive stream state

Multi-frame receive reassembly is separate from the persistent per-peer conversation model. Continuation data does not repeat all addressing information, so the receiver needs temporary RF stream state to associate continuation frames with the directed message that started them.

The natural discriminator is primarily RX audio-frequency offset plus timing/addressing context.

An MCU implementation can use a very small fixed table, for example 4-8 temporary contexts:

```text
frequency
destination/source context
last-frame time
message buffer state
FIRST/LAST state
```

When a complete directed message is reassembled, it is appended to the corresponding per-peer conversation history.

Exact collision rules must be validated against upstream captures and test vectors before freezing the structure.

## Text codecs

JS8Call can use both Huffman and JSC compressed data.

v0.1 policy:

- **TX:** support both Huffman and JSC.
- **RX:** support both Huffman and JSC.
- For each transmit data frame, generate both candidates and use whichever consumes more input text, matching upstream JS8Call behavior.
- Implement JSC compression/decompression using the upstream JSC algorithm and lookup semantics rather than inventing a different hash/trie scheme for v0.1.
- JSC dictionary/search resources may live in Cardputer ADV internal flash or be compiled into the firmware image if the final packed size permits; microSD remains an optional alternative.
- Do not freeze flash partition sizes until the packed TX lookup structures have been measured.

See `jsc-dictionary.md`.

## UI model

The display follows the existing MiniFT8-style 20x7 model: one top status line plus six body lines.

While idle/monitoring, the six body lines show active calls, up to three callsigns per line when they fit. Left/right scrolls through active calls.

During chat, all six body lines show the **currently selected peer conversation**. Up/down scrolls that conversation's message history.

Other peer conversations remain active in the background. Incoming messages for a non-selected peer set an unread/new indication rather than forcing the UI away from the current conversation.

A shortcut recalls the active-call/conversation browser, preferably as a temporary two-line overlay leaving four message lines visible. The operator can select another peer and switch the six-line chat view to that conversation.

Thus multiple-chat support is primarily a UI/application-state decision, not a new JS8 protocol feature.

## Simple QSO log

v0.1 should retain enough information to record a completed contact:

```text
UTC
band/frequency
callsign
SNR
optional exchange/reference
```

ADIF export and network upload are separate later work.

## Explicitly out of scope for v0.1

- `@POTA`, `@SOTA`, and general group/net operation;
- store-and-forward inbox / `MSG` services;
- relays and relay paths;
- APRS / APRS-IS;
- PSKReporter and internet spotting;
- JS8Call TCP/UDP/JSON remote-control APIs;
- automatic query command suite (`GRID?`, `INFO?`, `HEARING?`, etc.);
- large persistent activity database;
- desktop logbook integration;
- full JS8Call UI behavior;
- additional JS8 speeds until Normal-mode interoperability is proven.

## Remaining validation work

These are implementation checks, not known blockers:

- build application-layer golden vectors against JS8Call-improved v3.0.3;
- verify exact HB/ACK/CQ/directed/FIRST/LAST bit packing;
- RX JSC dictionary equivalence is verified; verify the TX `prefix[]` / `list[]` lookup ordering against JS8Call-improved v3.0.3;
- measure the final packed JSC TX+RX resource size;
- benchmark JS8 decode/turnaround timing on the target MCU;
- validate simultaneous/interleaved multi-frame receive-context handling;
- perform on-air interoperability tests against desktop JS8Call.

The design rule is: **implement only what another JS8Call operator must see on the air for heartbeat, reachability, calling, and direct conversation to work normally.**
