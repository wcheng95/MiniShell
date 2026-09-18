# FT8 TX reference vectors

`vectors.h` contains fixed 10-byte payloads and all 79 FT8 tone indices generated
from `wcheng95/Mini-FT8` commit
`491e757ae6b1e4cfd2b9a6ba10f48b35643849e0`. CTest reads only this checked-in file;
it needs neither the reference repository nor a network connection.

The first six cases reproduce the FT8 text cases in the pinned
`tests/tx_e2e/test_l1_encoder.cpp`. The first, second, third, and fifth also match
the FT8 message selections in `tests/tx_e2e/gen_golden.cpp` and
`tests/tx_e2e/golden/MANIFEST.txt`. No WAV was generated or changed.

Additional vectors cover SOTA/POTA/QRP/FD CQ, explicit free text, Field Day
exchanges on both sides of the 16/17-transmitter type boundary, 32 transmitters,
standard `/P` and `/R` suffix bits, the V2 `3DA0`/`3X` standard-prefix mappings,
and report/free-text boundary values. All 25 cases are compared against directly
constructed `AutoSeqTxIntent` values in `../ft8_tx_encoder_test.c`.

`generate.py` is a one-time reproducibility tool. It extracts the exact pinned
`components/ft8_lib/ft8/{message,text,encode,constants,crc}.[ch]`, `debug.h`, and
`common/stpcpy_compat.[ch]` into a temporary directory. It builds an isolated C
oracle against those unmodified V2 files, invokes `ftx_message_encode()` (or
`ftx_message_encode_free()` for explicit free text), then `ft8_encode()`. It does
not link V3 sources or use V3 output to create expectations. Reference debug
format warnings on LP64 hosts are inherited from the unchanged V2 source.

Reproduce from the MiniShell repository root with a local checkout containing
the pinned Git object:

```bash
PYTHONDONTWRITEBYTECODE=1 python3 tests/ft8_tx_vectors/generate.py /path/to/Mini-FT8 > /tmp/T021-vectors.h
cmp tests/ft8_tx_vectors/vectors.h /tmp/T021-vectors.h
sha256sum tests/ft8_tx_vectors/vectors.h
```

Expected SHA-256:

```text
0561eac9414eb91829f7e91168d0d1b50221542cae17197d33c7ba5afe6c3a51
```

Production ports only the required FT8 message packing/channel algorithms and
83-by-12-byte LDPC generator table. FT4 and the V2 library are not vendored into
MiniShell. Existing V3 CRC, protocol alphabets/section table, and decoder are
reused; RX decoder parity checks are unchanged.
