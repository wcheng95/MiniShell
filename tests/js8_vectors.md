# T052 JS8 CRC/LDPC reference vectors

Normative source: [JS8Call-improved v3.0.3 JS8_Mode/JS8.cpp](https://github.com/JS8Call-improved/JS8Call-improved/blob/v3.0.3/JS8_Mode/JS8.cpp).
The source credits `(C) 2025 Allan Bazinet <w6baz@arrl.net>`.

Downloaded source SHA-256:
`b72787f6e6473919872103997ef5dc9847810ff3e7783124a3961a2da2a86bf1`.

`js8_reference_oracle.py` checks that digest, extracts the upstream `parity`,
`CRC12` (Boost augmented CRC) and `bpdecode174` definitions verbatim, and runs
a standalone C++17 helper. Its encoder packs the payload into 11 zero-initialized
bytes and uses the upstream parity loop. It neither compiles nor calls MiniShell
code. Boost and the upstream file are only required to regenerate vectors:

```sh
python3 tests/js8_reference_oracle.py /tmp/T052-JS8.cpp > /tmp/T052-vectors.h
cmp tests/js8_golden_vectors.h /tmp/T052-vectors.h
```

The three 75-bit payloads are all zero, alternating `i % 2`, and the top bit
of successive uint32 LCG states (`state = state * 1664525 + 1013904223`, seed
`0x0528abcd`, advance before sampling). The fixed header records all input,
information and codeword bits plus numeric CRCs 42, 3508 and 1822 respectively.
CRC bits are information positions 75..86, MSB first.

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
