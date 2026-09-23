# T057 compound/beacon vectors

Normative source: [JS8Call-improved v3.0.3 Varicode.cpp](https://github.com/JS8Call-improved/JS8Call-improved/blob/v3.0.3/JS8_Main/Varicode.cpp)
and [Varicode.h](https://github.com/JS8Call-improved/JS8Call-improved/blob/v3.0.3/JS8_Main/Varicode.h).

SHA-256:

- `.cpp`: `2b4a877e7dae1a3fdd9141f4dd8af422185977d2e9363ad23ef6bc1918eaf53a`
- `.h`: `dd7b8bf50466d0d2dc42b8c9ec05a72126363b3cd02c7492c5e2dcc9c9abb99d`

Regenerate offline (no Qt, no MiniShell production code):

```sh
python3 tests/js8_compound_oracle.py /path/to/Varicode.cpp /path/to/Varicode.h > tests/js8_compound_vectors.h
```

The generator verifies both source hashes and transcribes the exact formulas from
`packAlphaNumeric50`, `unpackAlphaNumeric50`, `grid2deg`, `packGrid`, `unpackGrid`,
and `deg2grid`. It uses upstream weighted mixed-radix packing and geographic
conversion, independently of production's reverse-radix loop and reduced integer
grid conversion. The header contains fixed input bits and expected fields, used
without any upstream checkout or regeneration in CTest.

`packCompoundFrame` emits `[3][50][11],[5][3]`: the two extra fragments are the
contiguous 16-bit extra field. The vectors append transmission LAST (`010`),
which content tests subsequently vary over all eight values. They cover:

| Class | Callsign | Extra | bits3 | Meaning in this task |
| --- | --- | --- | --- | --- |
| HEARTBEAT | AG6AQ | 27127 | 0 | HB, CM97 |
| HEARTBEAT | AG6AQ | 59895 | 4 | CQ FIELD, CM97 |
| HEARTBEAT | KN4CRD | 65535 | 7 | CQ, no grid |
| COMPOUND | KN4CRD/P | 23883 | 0 | EM73 |
| COMPOUND | VE3/LB9YHX | 32767 | 5 | no grid |
| COMPOUND_DIRECTED | KN4CRD/P | 32442 | 6 | raw fields only |

The heartbeat high extra bit and subtype constants come from
`packHeartbeatMessage`, `unpackHeartbeatMessage`, `cqs`, and `hbs`.
Command interpretation from upstream `unpackCompoundMessage` is deliberately
excluded per T057; extras above 32400 remain raw.

Primitive fixtures include both slash separator positions, internal spaces,
first-character `@`, an empty decoded callsign, eleven visible slashes, and the
50-bit maximum. The final first-character modulus is 39 even when unused high
values exceed the mixed-radix range; upstream accepts these without validation.
Grid 32400 wraps to RA90, just like grid 0; 32401 and above are empty.

The existing T053 tone vectors also exercise host HB, plain compound, and raw
compound-directed diagnostics. Applying these independent formulas to their
payloads yields `000000000 / RA90`, `DA/IXRO81 / CB51`, and
`462/MSW/VXG / extra=43690 / bits3=5` respectively. These arbitrary PHY vectors
are not required to contain conventional valid callsigns.
