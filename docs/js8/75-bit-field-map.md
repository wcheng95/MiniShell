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

COMPOUND is primarily an **identity** frame for a callsign that needs the 50-bit
compound representation, for example a portable/prefix/suffix callsign.

Typical meaning:

```text
COMPOUND
    callsign50
    optional grid in extra16
```

For plain COMPOUND:

- `extra16 <= 32400` is interpreted as a packed four-character grid;
- values above the grid range are not interpreted as a grid by MiniShell and
  remain raw/unresolved;
- upstream `packCompoundMessage()` normally uses this class when it has a
  compound callsign plus optional grid and no directed command.

Example conceptually:

```text
VE6/LB9YH DO30
    -> COMPOUND
       callsign50 = VE6/LB9YH
       extra16    = packed DO30
```

The class does **not** itself mean "from" or "to". Which party this identity
belongs to comes from the surrounding directed-message sequence/context.

---

## 010 — COMPOUND_DIRECTED

```text
bits  0..2   010
bits  3..52  callsign50
bits 53..68  extra16
bits 69..71  bits3
bits 72..74  transmission flags
```

COMPOUND_DIRECTED uses the same physical layout, but `extra16` carries a
**reduced directed command** instead of an ordinary grid.

Conceptually:

```text
COMPOUND_DIRECTED
    callsign50
    command [+ optional SNR number] in extra16
```

This form is useful when a directed exchange involves a callsign that cannot be
represented cleanly by the normal 28-bit DIRECTED callsign fields. JS8Call can
send a compound identity frame and a compound-directed frame as separate logical
frames; the receiver associates them using the surrounding receive context.

The reserved `extra16` ranges are:

```text
0 .. 32400        grid range
32401 .. 32409    reserved gap
32410 .. 32766    user/command range
32767             max/sentinel value

nbasegrid = 32400
nusergrid = 32410
nmaxgrid  = 32767
```

Upstream packing is:

```text
extra16 = 32410 + packed_command
```

For ordinary directed commands, `packed_command` is the reduced command code
stored in the low 7 bits.

For SNR-family commands, upstream uses an 8-bit compact form:

```text
packed_command:

bit 7      = 1          marks SNR-family encoding
bit 6      = 0          SNR
             1          HEARTBEAT SNR
bits 5..0  = number6
```

The six-bit number uses the same compact number convention as standard DIRECTED:

```text
0       = absent
1..63   = -30 .. +32 via value = raw - 31
```

So, for example, a compound callsign followed by an SNR-family command can be
carried without needing the two 28-bit callsign fields of a standard DIRECTED
frame.

Important distinction:

| Class | Main purpose | `extra16` interpretation |
| --- | --- | --- |
| COMPOUND | identify compound callsign | grid / identity metadata |
| COMPOUND_DIRECTED | directed operation involving compound callsign | reduced command, optionally SNR number |

Neither class contains both sender and recipient callsigns in one frame. Full
from/to meaning may require association with adjacent directed/compound frames.

MiniShell currently decodes the shared raw fields for COMPOUND_DIRECTED but does
not yet perform the full upstream association/command interpretation; `extra16`
and `bits3` remain available in the JSONL/debug path.

Current upstream `packCompoundMessage()` supplies `bits3=0` for ordinary
COMPOUND and COMPOUND_DIRECTED message packing, although the shared wire field is
still three bits wide.

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
