# T059 Normal Huffman RX vectors

Normative source: JS8Call-improved/JS8Call-improved **v3.0.3**:

- [JS8_Main/Varicode.cpp](https://github.com/JS8Call-improved/JS8Call-improved/blob/v3.0.3/JS8_Main/Varicode.cpp),
  SHA-256 `2b4a877e7dae1a3fdd9141f4dd8af422185977d2e9363ad23ef6bc1918eaf53a`
- [JS8_Main/Varicode.h](https://github.com/JS8Call-improved/JS8Call-improved/blob/v3.0.3/JS8_Main/Varicode.h),
  SHA-256 `dd7b8bf50466d0d2dc42b8c9ec05a72126363b3cd02c7492c5e2dcc9c9abb99d`

Regenerate offline:

```sh
python3 tests/js8_huffman_oracle.py /path/to/Varicode.cpp /path/to/Varicode.h > tests/js8_huffman_vectors.h
```

The oracle verifies both source hashes and extracts all 44 `hufftable` entries.
It transcribes `huffEncode`, `packHuffMessage`, Normal `unpackDataMessage` padding,
and `huffDecode`'s sorted QMap scan (including its lack of an inner-loop break).
Prefix-free codewords make production's bounded greedy matching equivalent.
No MiniShell production code is imported or invoked to generate these vectors.

The encoder oracle forces the Huffman `10` branch. Upstream `packDataMessage`
compares Huffman and JSC character counts and may select compressed `11` instead;
that selection and JSC are outside T059. Encoding here exists only for offline
fixtures, not as a production TX API. Valid-character checks uppercase input as
upstream does, while actual encoding matches input as supplied; vectors use the
exact uppercase/punctuation table.

Each fixed record contains source input, exact 72 application bits, consumed
character count, expected decoded text, unpadded bit count, and full 75 bits with
varied tail flags. Cases include HELLO, HELLO WORLD, CQ FIELD, ACK, 73, AG6AQ,
K1ABC/P, THE QUICK, 40 input spaces (34 consumed), 24 input Es (23 consumed,
69 content bits), quoted HELLO, and empty input. A separate decoder-only vector
uses HE followed by incomplete `1111` before the sentinel; input is NULL and
consumed is -1 because no `packHuffMessage` call emits that incomplete code.
Expected output is HE, preserving upstream stop-at-incomplete-suffix behavior.

Tests decode every exact table code, verify prefix freedom, test every proper
code prefix both alone and following E, all sentinel positions 2..71, both
100/101 application prefixes, and all eight tail flags. The maximum is proven:
72 minus two prefix bits minus at least one sentinel = 69 content bits;
shortest code is two bits, so at most 34 characters plus NUL fit in 35 bytes.
The production module also has a static assertion for this bound.

Deliberate malformed-input safety difference: `10` followed by 70 ones has no
sentinel in its data area and returns BAD_PADDING, leaving output unchanged.
Upstream finds only the selector zero, then Qt `mid(1, -1)` can expose the entire
70-bit area; that is not a frame `packHuffMessage` can emit. Empty content with
its sentinel at application bit 2 remains valid. Incomplete code suffixes are
accepted for upstream decode compatibility, not rejected as malformed padding.

Thirteen host WAV tests use fixed payloads and expected text. The existing PHY
encoder generates waveform tones only; it is not the independent application
oracle. Quotation marks are escaped in stable host diagnostics, and fragment
spaces remain intact. Normal CTest requires neither Qt nor upstream files.
