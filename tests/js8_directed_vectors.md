# T058 standard directed RX vectors

Normative source: JS8Call-improved/JS8Call-improved, tag **v3.0.3**:

- [JS8_Main/Varicode.cpp](https://github.com/JS8Call-improved/JS8Call-improved/blob/v3.0.3/JS8_Main/Varicode.cpp)
  SHA-256 `2b4a877e7dae1a3fdd9141f4dd8af422185977d2e9363ad23ef6bc1918eaf53a`
- [JS8_Main/Varicode.h](https://github.com/JS8Call-improved/JS8Call-improved/blob/v3.0.3/JS8_Main/Varicode.h)
  SHA-256 `dd7b8bf50466d0d2dc42b8c9ec05a72126363b3cd02c7492c5e2dcc9c9abb99d`

Offline regeneration:

```sh
python3 tests/js8_directed_oracle.py /path/to/Varicode.cpp /path/to/Varicode.h > tests/js8_directed_vectors.h
```

The generator verifies both hashes and extracts the complete `basecalls` and
`directed_cmds` tables from source (after removing line comments). It orders the
ASCII command keys to reproduce QMap::key's first match: code 0 uses ` SNR?`,
code 12 ` QUERY MSGS`, and code 31 one space. Leading spaces remain in fixtures.

The independent Python callsign formulas transcribe upstream `packCallsign` and
`unpackCallsign`, including their permutations, mixed radices, prefix transforms,
trimming, and portable handling. Special basecalls return immediately, ignoring
portable; the 28-bit maximum still uses generic decoding, with no extra validation.
Primitive fixtures include all 54 special values, adjacent generic values, 28-bit
edges, 3DA0 and 3X expansions, leading/trailing trimming and internal spaces.

Directed fixture bits follow `packDirectedMessage`/`unpackDirectedMessage`:
`[3][28][28][5],[2][6]` plus independent tail LAST. No production MiniShell function
is called to generate the checked-in header. The eleven frame fixtures are:

1. AG6AQ -> K1ABC, free text, no number
2. AG6AQ -> K1ABC, ACK
3. AG6AQ -> K1ABC, 73
4. AG6AQ/P -> K1ABC, free text
5. AG6AQ -> K1ABC/P, free text
6. AG6AQ/P -> K1ABC/P, ACK
7. <....> -> K1ABC, ACK
8. AG6AQ -> K1ABC, SNR, -12 (raw number 19)
9. AG6AQ -> K1ABC, HEARTBEAT SNR, +05 (raw number 36)
10. AG6AQ -> K1ABC, FB, +32 (raw number 63)
11. AG6AQ -> @ALLCALL, free text, for decode compatibility only

Fixture 10 deliberately supplies the arbitrary RX wire value 63. Upstream TX
`packNum` clamps to a smaller range; RX preserves number6-31, including +32.
Zero means absent; raw 31 means present integer zero. This is not `unpackCmd`'s
alternate compound-command byte representation, which is outside T058.

The C tests vary every command/number/portable combination and all eight tail flags,
require class DIRECTED, check malformed bits with unchanged outputs, and cover
`formatSNR`'s -60..60 range including signed zero and empty out-of-range output.

The host WAV regression uses these fixed payloads and expected diagnostics. Its
fixture mode invokes the existing T053 PHY encoder to create tones for synthetic
audio; that is an integration fixture, not an independent PHY oracle or a new
application TX packer. Eleven WAVs must each recover exactly one expected payload
and the fixed diagnostic suffix. Normal tests need neither Qt nor upstream files.
