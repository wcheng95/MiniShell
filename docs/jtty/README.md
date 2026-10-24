# JTTY

MiniShell JTTY is a planned narrow-band keyboard-to-keyboard application based on the standard JTTY mode implemented by WSJT-X.

JTTY combines free-running 1.888-second frames, 4-GFSK, CRC protection, tail-biting convolutional coding, structured contest/source atoms, and arbitrary TEXT5 text. Unlike FT8/JS8-style slotted operation, a JTTY transmission may start at any time and may contain one or more consecutive frames.

Current implementation direction:

- **first target:** rpi3 + QMX, RX only;
- **portable core:** designed so a later Cardputer ADV compile-in app does not require a rewrite;
- **RX source support:** all current standard JTTY source types;
- **TX:** deferred; likely simple free text first, with broader standard-JTTY TX added only when useful;
- **interoperability:** remain fully compatible with standard WSJT-X JTTY stations;
- **application semantics:** no custom on-air JChat protocol is required.

Canonical planning documents:

```text
docs/jtty/README.md
docs/jtty/rx-implementation-plan.md
docs/jtty/conversational-operation.md
```

## Protocol summary

A JTTY transmission contains one or more 1.888-second frames.

Each frame carries:

```text
32-bit source word
 1-bit reserved field = 0
 1-bit EOM flag
----------------------
34-bit source payload

+ CRC-12
----------------------
46 bits

tail-biting convolutional code
rate 1/2, K = 10
----------------------
92 coded bits

2 coded bits / 4-FSK symbol
----------------------
46 data symbols

+ 13 synchronization symbols
----------------------
59 total symbols
```

At 31.25 baud:

```text
symbol period = 32 ms
frame length  = 59 * 32 ms
              = 1.888 s
```

### Waveform

```text
Modulation:       4-GFSK
Symbol rate:      31.25 baud
Tone spacing:     31.25 Hz
Modulation index: h = 1
Gaussian BT:      2.0
Reference Fs:     12000 Hz
Reference Nsps:   384
Occupied BW:      about 127 Hz (99% power)
```

For lowest tone `f0`:

```text
tone 0 = f0
tone 1 = f0 + 31.25 Hz
tone 2 = f0 + 62.50 Hz
tone 3 = f0 + 93.75 Hz
```

### Synchronization

Each frame starts with:

```text
0 2 2 3 0 0 3 2 1 3 1 2 0
```

Frame layout:

```text
| 13 sync symbols | 46 encoded data symbols |
```

### Forward error correction

```text
34 source bits
      |
    CRC-12
      |
46 information bits
      |
tail-biting convolutional encoder
rate 1/2, K=10
      |
92 coded bits
      |
Gray dibit-to-tone mapping
      |
46 4-FSK symbols
```

Convolutional-code parameters:

```text
memory ν:           9
constraint length: 10
states:            512

G0 = 1167 octal
G1 = 1545 octal
```

CRC polynomial:

```text
0x80F
```

Dibit-to-tone mapping:

```text
00 -> 0
01 -> 1
11 -> 2
10 -> 3
```

Tail biting preloads the encoder state from the final nine information bits, so the final state equals the initial state without adding termination bits.

## Source encoding

Every frame carries a 32-bit source grammar word followed by:

```text
bit 33 = reserved, always 0
bit 34 = EOM
```

`EOM=1` identifies the final frame of a logical message.

The current source grammar includes:

- compact callsign/action atoms;
- STRUCT30 contest/exchange/control atoms;
- TEXT5 arbitrary text.

### TEXT5

TEXT5 carries five 6-bit characters per frame.

The 64-character alphabet is:

```text
0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ +-./?!"#$%,&*()_'=[]{}<>|:;
```

Arbitrary text therefore has a simple fallback rate of approximately:

```text
5 characters / 1.888 s ~= 2.65 characters/s
```

Structured source atoms can represent common callsigns and exchanges much more compactly.

Examples that can fit in one frame include:

```text
CQ K1ABC CQ
WB9XYZ
WB9XYZ TU
599 123
599 MA
599 FN42
1D EMA
```

Real on-air testing with WSJT-X has already observed structured CALL traffic such as:

```text
LC5DGO TU
```

which is one reason MiniShell RX should decode all standard source types rather than TEXT5 alone.

## Receiver validity chain

A frame is accepted only after:

```text
4-GFSK detection
      |
sync / timing / frequency estimation
      |
tail-biting convolutional decode
      |
CRC-12
      |
reserved bit == 0
      |
source grammar validation
      |
source rendering / message assembly
```

The source grammar therefore adds another validity filter after FEC and CRC.

## MiniShell architecture direction

Keep protocol layers independent:

```text
QMX / Audio service
      |
12 kHz S16 mono
      |
sync / demod
      |
46 x 4 soft tone metrics
      |
TBCC / WAVA
      |
CRC + 34-bit payload
      |
full source decoder
      |
EOM-aware message assembler
      |
neutral decoded-message object
      |
UI
```

Primary design rules remain:

1. **Modular**
2. **Ownership**
3. **Decouple**
4. **KISS**

The rpi3 implementation should favor clarity and correctness over premature CPU/RAM optimization, while preserving an architecture suitable for ADV.

See [RX implementation plan](rx-implementation-plan.md) for the staged implementation plan.

## Conversational direction

JTTY itself is contest-oriented and does not define a JS8Call-style directed-message protocol. MiniShell does not need to change the wire format.

If conversational operation is later implemented, preserve standard JTTY interoperability and use ordinary standard text/source messages. A normal WSJT-X operator must be able to read and answer the exchange.

See [Conversational operation](conversational-operation.md).

## Reference implementation

The protocol is documented and implemented in the public WSJT-X source tree under:

```text
lib/jtty/
```

Important upstream files include:

```text
jtty_design.md
jtty_source_encoding.txt
jtty_fec_mod.f90
jtty_tbcc_code_profile.f90
tbcc.f90
genjtty.f90
gen_jttywave.f90
jtty_mdecode.f90
subtract_jtty.f90
```

Initial study used the WSJT-X v3.2.0-rc1 JTTY source; current on-air experimentation uses the newer WSJT-X beta series.

Upstream:

- https://github.com/WSJTX/wsjtx
- https://wsjtx.github.io/wsjtx/

## Implementation policy

MiniShell should independently implement the documented protocol rather than porting the complete WSJT-X implementation.

WSJT-X is GPL-licensed. Protocol facts and independently written code should remain clearly separated from copied upstream implementation code. If upstream implementation code is incorporated directly, its license requirements apply.

## Current status

**Phase: RX implementation planning / protocol study**

Established decisions:

- JTTY is inside MiniShell's five-mode portable-radio scope.
- rpi3 + QMX is the first implementation target.
- first implementation is RX-only;
- canonical DSP boundary is 12 kHz S16 mono;
- first live search window is 1250–1750 Hz audio;
- RX should support CALL, STRUCT30, and TEXT5;
- frame decoding and logical-message assembly are separate;
- bounded decode work only;
- ADV compile-in remains a later target if practical.
