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

## Normal Huffman DATA RX (T059)

Normal legacy DATA uses application prefix `10`. Its third bit is the first
Huffman content bit, so both `100` and `101` envelopes select this decoder.
The JS8-owned pure Huffman module uses the exact 44-entry v3.0.3 `hufftable` to
produce uppercase/punctuation text fragments, without escapes or JSC support.
`11` remains DATA_COMPRESSED and returns a distinct compressed status from the
Huffman module; T060 routes that class separately to JSC.

`packHuffMessage` adds a code only when the resulting application length is
strictly less than 72. It then appends one zero sentinel followed by enough ones
to reach 72 bits. RX finds the final zero in application bits 2..71, drops it and
all trailing ones, and decodes only the preceding content after the two-bit prefix.
The three PHY transmission flags are excluded. An incomplete final code stops
decoding and returns the text decoded so far, matching upstream `huffDecode`.

At most 69 content bits and a shortest code of two bits yield 34 output characters;
the caller-owned buffer holds 35 bytes including NUL. Missing data-area sentinel
is an explicit BAD_PADDING error with output unchanged, instead of relying on Qt
negative-length container behavior. An empty fragment with a valid sentinel is
accepted. No replacement character is invented for an incomplete suffix.

A DATA fragment is not yet a conversation message. Association with a directed
header and FIRST/LAST reassembly remain future work. No production Huffman TX
packer, compressed text decoder, or automatic action is introduced.

For host WebSDR experiments, `js8_decode` validates the entire 12 kHz mono S16 RIFF
container and processes only its first 93 complete 960-sample blocks at 6 kHz,
with continuous phase-0 decimation from input sample zero. It never pads a partial
block or reads the whole recording into memory. `blocks=` reports processed blocks;
`ignored_engine_samples=` includes every decimated sample beyond those blocks,
both complete later blocks and the incomplete tail. This is not a sliding-window
or multi-slot decoder; a signal occurring only after the first window is ignored.

## Normal JSC DATA_COMPRESSED RX (T060)

The pure `js8_jsc` module now decodes Normal `11` DATA_COMPRESSED frames using the
same last-zero sentinel/trailing-one padding rule as Huffman. Tail PHY flags stay
metadata. Four-bit nibbles >=7 extend a base-9 codeword; a terminal nibble <7 forms
the dictionary index with the upstream dense-code base, and its optional separator
bit appends one space. Partial trailing nibbles, unterminated words, and out-of-range
indices stop with the previously decoded prefix, matching defined v3.0.3 RX behavior.
Missing padding and resource failures are explicit errors, without partial output.

A caller-supplied read callback owns JSC1 access; the engine performs no file,
MiniShell or platform I/O and uses no heap. Each lookup reads only needed offsets,
lengths and the selected string, without loading the complete index/dictionary.
The result holds Latin-1 bytes in a 384-byte text buffer (at most 14 entries of
26 bytes plus separators). The full map strings are used, including `@ALLCALL`
and `ROSIDS` despite upstream Tuple.size quirks.

The pinned A_2_1 payload independently decodes to **`MSG ID 416`**. The real WAV
and independent oracle agree exactly. Host diagnostics append
`codec=jsc data="MSG ID 416"`; quotes, backslashes and control bytes are escaped.
The host uses the repository dictionary by default and accepts `JS8_JSC_DICT` as
a path override. A missing/corrupt resource yields `data_error=resource` and exit 1
if a JSC frame needs it; non-JSC decoding remains available. T059 long-WAV policy
is unchanged. Resource identity and structural validation are documented in
`jsc-dictionary.md`.

This is an unassociated text fragment, not a reconstructed conversation message.
TX JSC compression, prefix/list lookup, codec selection and reassembly remain pending.

## Aligned multi-slot WAV host mode (T061)

The existing `js8_decode capture.wav` invocation keeps the T060 first-window
behavior and output. Explicit `js8_decode --all-slots capture.wav` decodes every
complete aligned 15-second slot. The caller must align PCM sample zero to a JS8
Normal boundary; the tool does not infer alignment or use wall-clock/UTC time.

At 12 kHz each slot starts at `slot_index * 180000` input samples. Its first
178560 samples feed phase-0 2:1 decimation into exactly 93 x 960 engine samples;
the final 1440 input samples are skipped. The even 180000-sample stride preserves
the same kept samples as continuous phase-0 decimation. A partial final slot is
ignored, with its exact input-sample count reported. At least one full slot is
required in this mode, while default mode still accepts shorter windows.

Each multi-slot result starts with `slot=N slot_s=N*15`; candidate diagnostics
and per-slot summaries also identify the slot. `slot_s` is elapsed time from
aligned PCM sample zero, not UTC. Per-slot `ignored_engine_samples=720` describes
the skipped full-slot tail; final `slots=` / `trailing_input_samples=` describes
the capture. Default output remains untagged.

The host allocates one monitor workspace, resets its stream per slot, and reuses
one lazily opened JSC resource. Exact-payload dedupe resets each slot, so repeated
heartbeats/text in different slots remain visible. Entire-RIFF validation still
precedes DSP, including malformed trailing data. No overlap, sliding-window
search, automatic alignment, message association or reassembly is implemented.
This host file-seeking contract does not prescribe an embedded/live audio design.

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

## Directed free-text receive state (T062)

The pure `js8_reassembly` module accepts normalized standard DIRECTED headers and
successfully decoded DATA text. FIRST belongs to the first logical frame,
normally the DIRECTED free-text header (command 31); LAST belongs to the final
logical frame, normally the last DATA fragment. FIRST|LAST on a free-text header
completes an empty message. ACK, 73, other commands, and `<....>` placeholders do
not open text streams.

Four caller-owned contexts each contain a 1024-byte temporary text buffer,
including NUL. Both Huffman DATA and JSC DATA_COMPRESSED append identical decoded
bytes with no whitespace changes. No codec, DSP, resource, heap, platform, or
clock access occurs inside this layer. This is temporary RF state; persistent
conversation history and UI remain separate and unimplemented.

Association chooses the closest latest frequency within inclusive +/-10 Hz,
with the lowest context index winning ties. The host computes integer milli-Hz
from candidate bin/sub-bin fields, without rounding a floating-point diagnostic.
A match updates the stored frequency. FIRST replaces a matching context,
otherwise uses the first free context or evicts the oldest last-slot context
(lowest index on ties).

DATA at or before the last accepted slot is a duplicate/stale event and does not
append. A gap between consecutive 15-second slots permanently marks the stream
incomplete; LAST then drops it without a valid completed message. An unfinished
stream expires on a new semantic event more than six slots (90 seconds) after
its last fragment. No forced completion at 60 seconds occurs. Buffer overflow
explicitly drops the affected stream instead of truncating it.

The optional host invocation is:

```sh
./build-linux/js8_decode --all-slots --messages aligned.wav
```

It keeps all raw frame output and adds completed lines such as:

```text
message from=AG6AQ to=K1ABC first_slot=0 last_slot=1 hz=1000.000 text="HELLO WORLD"
```

Text uses the existing JSC diagnostic escaping rules. Stable stderr diagnostics
identify orphan/duplicate/gap/overflow events and expired/replaced/evicted
context bit masks. With `--messages` absent, default and ordinary `--all-slots`
output are unchanged. The T061 sample-zero alignment contract still applies.
Per-slot raw payload dedupe runs before reassembly, so identical raw payloads
in separate slots remain eligible to append. Same-slot semantic duplicate
handling is based on stream progression, not global payload identity.

Compound association, buffered/query commands, conversation state, local-station
filtering, auto-replies, TX and live MiniShell integration remain out of scope.
No WAV end-of-file or elapsed timeout fabricates LAST.

## Normalized RX activity (T063)

The pure activity model now represents HB, CQ/CQ FIELD, compound, directed,
Huffman/JSC DATA, and completed T062 MESSAGE observations. The optional host
append-only JSONL logger consumes those snapshots without changing RX/reassembly
behavior. Exact audio milli-Hz belongs to the decoder; optional dial frequency
and aligned UTC start are caller metadata. No RF frequency or absolute UTC is
invented when metadata is absent.

See [activity-log.md](activity-log.md) for `js8-activity-v1`, CLI options, escaping,
ordering, and failure semantics. This is an RX observation log, not ADIF/QSO
logging. TX, live audio/CAT, networking, databases and UI remain deferred.

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
