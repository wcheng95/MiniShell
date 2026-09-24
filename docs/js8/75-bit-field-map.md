# JS8 Normal 75-bit field map

This is the compact wire-field reference for the MiniShell JS8 Normal implementation.
The frozen interoperability reference is **JS8Call-improved v3.0.3**.

## Physical payload and FEC envelope

A decoded JS8 Normal PHY payload is **75 bits**:

```text
bit 0                                                        bit 74
 |                                                              |
 +---------------- 72 application bits ----------------+---------+
 |                    bits 0..71                       | 72..74  |
 +-----------------------------------------------------+---------+
                                                       TX flags
```

The coding chain is:

```text
72 application bits
+ 3 transmission flags
= 75 payload bits
+ 12 CRC bits
= 87 information bits
+ 87 LDPC parity bits
= 174 coded bits
```

The 12-character physical diagnostic representation is only bits 0..71 encoded
as twelve 6-bit alphabet symbols. It is not decoded application text.

## Transmission flags — bits 72..74

The three tail bits are independent of application class.

```text
bit 72  DATA   value 4
bit 73  LAST   value 2
bit 74  FIRST  value 1
```

Therefore the numeric flag field is:

```text
flags = bit72*4 + bit73*2 + bit74
```

All bitwise combinations are possible.

| Value | Meaning |
| ---: | --- |
| 0 | none |
| 1 | FIRST |
| 2 | LAST |
| 3 | FIRST + LAST |
| 4 | DATA |
| 5 | DATA + FIRST |
| 6 | DATA + LAST |
| 7 | DATA + FIRST + LAST |

These flags are **not** the application message type.

## Application class — leading application bits

| Leading bits | Application class | Field map |
| --- | --- | --- |
| `000` | HEARTBEAT / CQ family | compound/beacon layout |
| `001` | COMPOUND | compound layout |
| `010` | COMPOUND_DIRECTED | compound layout |
| `011` | DIRECTED | standard directed layout |
| `10` | DATA | Huffman fragment |
| `11` | DATA_COMPRESSED | JSC fragment |

For DATA and DATA_COMPRESSED, bit 2 belongs to the encoded text stream; the
class selector is only two bits.

---

## 000 — HEARTBEAT / CQ

```text
bits  0..2   000
bits  3..52  callsign50
bits 53..68  extra16
bits 69..71  bits3
bits 72..74  transmission flags
```

`callsign50` is the JS8Call compound callsign encoding.

For class `000`:

```text
extra16 bit 15 = 0   -> heartbeat
extra16 bit 15 = 1   -> CQ family
extra16 bits 0..14   -> four-character grid encoding
bits3                -> heartbeat/CQ subtype
```

All heartbeat subtypes are presented as `HB`.

CQ subtype map:

| bits3 | Meaning |
| ---: | --- |
| 0 | CQ CQ CQ |
| 1 | CQ DX |
| 2 | CQ QRP |
| 3 | CQ CONTEST |
| 4 | CQ FIELD |
| 5 | CQ FD |
| 6 | CQ CQ |
| 7 | CQ |

---

## 001 — COMPOUND

```text
bits  0..2   001
bits  3..52  callsign50
bits 53..68  extra16
bits 69..71  bits3
bits 72..74  transmission flags
```

For plain COMPOUND, `extra16 <= 32400` is interpreted as a grid. Larger values
remain raw/unresolved.

---

## 010 — COMPOUND_DIRECTED

```text
bits  0..2   010
bits  3..52  callsign50
bits 53..68  extra16
bits 69..71  bits3
bits 72..74  transmission flags
```

MiniShell currently preserves `extra16` and `bits3` as raw compound-directed
content. Association with another frame is an application/reassembly concern,
not a different PHY layout.

---

## 011 — DIRECTED

```text
bits  0..2   011
bits  3..30  from callsign28
bits 31..58  to callsign28
bits 59..63  command5
bit      64  from-portable flag
bit      65  to-portable flag
bits 66..71  number6
bits 72..74  transmission flags
```

### Portable flags

Bits 64 and 65 are independent `/P` indicators for ordinary decoded callsigns:

```text
bit 64 = 0  from callsign unchanged
bit 64 = 1  append /P to ordinary from callsign

bit 65 = 0  to callsign unchanged
bit 65 = 1  append /P to ordinary to callsign
```

Examples:

```text
AG6AQ    -> N6HAN
AG6AQ/P  -> N6HAN
AG6AQ    -> N6HAN/P
AG6AQ/P  -> N6HAN/P
```

Upstream special/basecall-table entries such as symbolic/group names ignore these
portable flags when rendered; the raw flag bits still exist in the frame.

### number6

Bits 66..71 carry a six-bit optional signed number.

```text
raw number6 = 0   -> number absent

raw number6 = 1   -> -30
raw number6 = 2   -> -29
...
raw number6 = 30  -> -1
raw number6 = 31  ->  0
raw number6 = 32  -> +1
...
raw number6 = 63  -> +32
```

Equivalent rule:

```text
if number6 == 0:
    absent
else:
    number = number6 - 31
```

So the representable numeric value range is **-30 .. +32**.

The field structurally exists for every DIRECTED command. Commands such as
`SNR` and `HEARTBEAT SNR` make especially direct use of it, but command
semantics decide whether the number is meaningful.

Example:

```text
command5 = 29        HEARTBEAT SNR
number6  = 13

13 - 31 = -18

=> HEARTBEAT SNR -18
```

### DIRECTED command5 map

| Code | Command | Code | Command |
| ---: | --- | ---: | --- |
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
| 15 | ` GRID` | 31 | one space = free text |

Important UI/protocol cases:

```text
14  ACK
28  73
25  SNR
29  HEARTBEAT SNR
31  free-text directed header
```

The portable bits append `/P` to ordinary decoded callsigns. Upstream special
base-call entries ignore those flags.

---

## 10 — DATA / Huffman fragment

```text
bits  0..1   10
bits  2..K-1 Huffman content bits
bit      K   0 sentinel
bits K+1..71 1 padding
bits 72..74  transmission flags
```

where `K` is variable.

RX finds the **last zero** in bits 2..71, removes that zero and all following
one-padding, then Huffman-decodes the preceding bits after the `10` prefix.

Consequences:

- both apparent three-bit prefixes `100` and `101` are DATA;
- bit 2 is text content, not a subtype bit;
- an empty fragment is valid if the sentinel is present;
- missing sentinel is malformed padding;
- a DATA fragment is not by itself a complete chat message.

---

## 11 — DATA_COMPRESSED / JSC fragment

```text
bits  0..1   11
bits  2..K-1 JSC dense-code bitstream
bit      K   0 sentinel
bits K+1..71 1 padding
bits 72..74  transmission flags
```

JSC uses the same last-zero / trailing-one framing rule as Huffman.

The content is decoded as variable-length dense dictionary codewords:

- four-bit nibbles `>= 7` extend a base-9 codeword;
- a terminal nibble `< 7` completes the dictionary index;
- an optional separator bit appends a space.

The dictionary/resource lookup is an application decoding concern; the 75-bit
PHY layout is unchanged.

---

## Reassembly view

A conversation message can span several independent 75-bit PHY payloads.

Typical directed free-text sequence:

```text
DIRECTED (011)
  from=<sender>
  to=<recipient>
  command5=31  free text
  FIRST as appropriate

DATA / DATA_COMPRESSED
  text fragment
  DATA/FIRST/LAST flags as transmitted

DATA / DATA_COMPRESSED
  more text
  LAST closes the message

=> application MESSAGE event
```

The reconstructed `MESSAGE` is **not another over-the-air 75-bit frame type**.
It is a local semantic object produced by receive reassembly.

## UI-oriented classification

For MiniShell JS8Chat UI design, the PHY/application classes naturally divide as:

| Wire class | Typical UI role |
| --- | --- |
| HEARTBEAT | station activity / reachability; normally hide from chat transcript |
| CQ family | active-caller/station list; optionally visible outside chat |
| COMPOUND | callsign/grid support; normally hidden as protocol machinery |
| COMPOUND_DIRECTED | protocol/reassembly support; normally hidden |
| DIRECTED automatic commands | query/reachability state; selectively visible |
| DIRECTED free text | conversation header; show when associated with chat |
| DATA / DATA_COMPRESSED | reassembly fragments; hide from normal transcript |
| reconstructed MESSAGE | completed chat text; show |

The full JSONL log should retain every semantic event even when the operator UI
filters it out.

## Summary diagram

```text
75 payload bits
|
+-- 0..71 application
|   |
|   +-- 000  HEARTBEAT/CQ
|   |         callsign50 | extra16 | bits3
|   |
|   +-- 001  COMPOUND
|   |         callsign50 | extra16 | bits3
|   |
|   +-- 010  COMPOUND_DIRECTED
|   |         callsign50 | extra16 | bits3
|   |
|   +-- 011  DIRECTED
|   |         from28 | to28 | command5 | /P | /P | number6
|   |
|   +-- 10   DATA
|   |         Huffman bits | 0 | 111... padding
|   |
|   `-- 11   DATA_COMPRESSED
|             JSC bits | 0 | 111... padding
|
`-- 72..74 transmission flags
      bit72 DATA=4 | bit73 LAST=2 | bit74 FIRST=1

+ CRC-12 -> 87 information bits
+ LDPC parity -> 174 coded bits
```

## Canonical references

- `docs/js8/application-protocol.md`
- `apps/js8chat/src/js8_engine/js8_frame.[ch]`
- `apps/js8chat/src/js8_engine/js8_crc.[ch]`
- `apps/js8chat/src/js8_engine/js8_ldpc.[ch]`
- JS8Call-improved v3.0.3 Varicode / Normal unpacking behavior
