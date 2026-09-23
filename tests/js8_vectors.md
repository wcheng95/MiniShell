# T052/T053 JS8 CRC/LDPC/channel reference vectors

Normative source: [JS8Call-improved v3.0.3 JS8_Mode/JS8.cpp](https://github.com/JS8Call-improved/JS8Call-improved/blob/v3.0.3/JS8_Mode/JS8.cpp).
The source credits `(C) 2025 Allan Bazinet <w6baz@arrl.net>`.

Downloaded source SHA-256:
`b72787f6e6473919872103997ef5dc9847810ff3e7783124a3961a2da2a86bf1`.

The channel oracle also pins [JS8_Mode/JS8.h](https://github.com/JS8Call-improved/JS8Call-improved/blob/v3.0.3/JS8_Mode/JS8.h)
with SHA-256 `8927db1d7e8151ff00333b9fe0524b0c907b56e9c0895221e37b6882d7790dd1`.
Normal timing was checked against [JS8_Include/commons.h](https://github.com/JS8Call-improved/JS8Call-improved/blob/v3.0.3/JS8_Include/commons.h),
SHA-256 `d7abc3090fcf5516b64435f78dfbd08dff9b01a56905afbe4d69fa4eb9c16048`:
1920 samples at 12 kHz, 160 ms symbols, 6.25 Hz spacing, 15 s period.
That header is not needed to compile the isolated encoder oracle.

`js8_reference_oracle.py` checks both source/header digests and extracts the
upstream `parity`, `CRC12` (Boost augmented CRC), `bpdecode174`, alphabet,
`JS8::encode()` and `JS8::Costas` definitions verbatim into a standalone C++17
helper. It neither compiles nor calls MiniShell code. The existing CRC/codeword
oracle remains unchanged. For the new tone vectors, a test-only adapter maps
payload bits 0..71 to twelve alphabet characters and bits 72..74 to the type,
then calls the actual extracted `JS8::encode()` with `Costas::Type::ORIGINAL`.
No application text codec is exposed in the production module.

Boost and the two upstream files are only required to regenerate vectors.
For files downloaded as `/tmp/T053-JS8.cpp` and `/tmp/T053-JS8.h`:

```sh
python3 tests/js8_reference_oracle.py /tmp/T053-JS8.cpp > /tmp/T053-vectors.h
cmp tests/js8_golden_vectors.h /tmp/T053-vectors.h
```

The optional second argument specifies the header path; otherwise the helper
replaces the source path's `.cpp` suffix with `.h`. A pinned checkout's
`JS8_Mode/JS8.cpp` therefore also works directly.

The three 75-bit payloads are all zero, alternating `i % 2`, and the top bit
of successive uint32 LCG states (`state = state * 1664525 + 1013904223`, seed
`0x0528abcd`, advance before sampling). The fixed header records all input,
information and codeword bits plus numeric CRCs 42, 3508 and 1822 respectively.
CRC bits are information positions 75..86, MSB first. T053 adds an exact
79-tone array to each record; the T052 payload/CRC/info/codeword values are
unchanged. Each Normal sync group at 0, 36 and 72 is `4 2 5 6 1 3 0`.
Data at 7..35 encodes codeword bits 0..86; data at 43..71 encodes bits 87..173.
Tests require consecutive MSB-first three-bit words to map directly to tone
indices, and verify that the vectors exercise all eight words. Thus an FT8
Gray map cannot pass the direct-mapping regression.

The oracle also requires exact upstream BP recovery for each vector with LLR
magnitude 4 (positive means 1), and again with positions 0, 87 and 173 replaced
by wrong-sign magnitude 0.25. Upstream reports 0 and 3 hard errors respectively.
The C tests lock the same results. Golden vectors are checked in; normal tests
have no upstream checkout, network, Boost or C++ requirement.

The production tables transcribe the upstream 87 generator hex rows into
87 x 11 bytes (87 MSB-first bits and one zero padding bit per row), and copy
`Mn` and `Nm` with their original zero-based indexing and neighbor order.
`js8_ldpc_tables_test.py` locks the SHA-256 of generator bytes, bit-check rows,
check lengths, then check-bit rows, concatenated in that order:
`f7a5608687c1a782bfabde44ebe7ae01d3355c631fc79ad99bc3c86405bd00c9`.
This digest was independently calculated from the downloaded upstream tables.
It also checks graph dimensions, all 522 reciprocal edges, padding, and every
one-hot information word against all parity checks (`H * G^T == 0`).

The C decoder preserves upstream update order, initial decision check, 30 BP
updates, and stalled-syndrome early exit. It fuses the temporary check-message
and tanh arrays; only valid neighbors are used. No clipping or alternative
min-sum algorithm is introduced. As in upstream, extreme LLRs can saturate
`tanhf` and generate nonfinite internal messages; sensitivity and numeric
conditioning are outside T052. Nonfinite *input* LLRs are rejected explicitly.
