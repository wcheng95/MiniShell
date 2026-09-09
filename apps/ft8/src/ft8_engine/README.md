# MiniFT8 `ft8_engine`

`ft8_engine` is the platform-independent FT8 protocol/DSP core used by the MiniFT8 application.

Current implemented receive path:

```text
6 kHz mono float
    -> ft8_monitor
       compact waterfall
    -> ft8_decoder
       candidate search
       likelihood extraction
       BP-LDPC
       CRC-14
    -> validated 10-byte FT8 payload

persistent protocol knowledge
    -> ft8_hash_store
       22 / 12 / 10-bit callsign-hash resolution
       explicit once-per-slot aging
       V2-compatible trim/eviction behavior
```

Current files:

```text
ft8_monitor.[ch]      streaming FFT/waterfall owner and explicit workspace contract
ft8_decoder.[ch]      FT8 candidate search and validated-payload boundary
ft8_ldpc.[ch]         private belief-propagation LDPC implementation
ft8_crc.[ch]          private FT8 CRC-14 implementation
ft8_hash_store.[ch]   explicit caller-owned persistent callsign-hash knowledge
vendor/kissfft/       pinned FFT implementation used by the cleaned monitor
```

Ownership rules:

- no MiniShell API calls inside the DSP/protocol core;
- no Linux, ESP-IDF, NuttX, board, UI, storage, AutoSeq, or TX dependency;
- monitor mutable DSP memory belongs to one explicit `Ft8Monitor` instance using caller-supplied workspace;
- candidate/LDPC/CRC scratch is local to a decode operation; no mutable decoder singleton exists;
- callsign-hash knowledge belongs to one explicit `Ft8HashStore` instance; no global/static hash table exists in the cleaned module;
- `Ft8HashStore` persists across slots and is aged explicitly once per slot by its future engine/slot owner;
- protocol message unpacking/text is not yet implemented here; RX-1F owns that stage and will consume `Ft8HashStore` directly/contextually.

The current hash-store baseline preserves the pinned V2 production policy:

```text
capacity                     128 entries
callsign storage             11 chars + NUL
entry size                   16 bytes
canonical stored hash        22 bits
lookup widths                22 / 12 / 10 bits
bucket                       (top10 * 23) % 128
full-table trim target       78 entries
count after next insertion   79 entries
age                          uint8, saturating, refresh on hit/save
```

Structural cleanup uses the pinned MiniFT8-V2 baseline at:

```text
5bd3ef98f72388a850bebad04bd7300b90edb63c
```

Hard current RX regressions:

```text
active FT8 waterfall FNV-1a-64  18BE1E838FD9C6AF
validated FT8 payload             000000206016500A1988
```

Candidate score/order remain diagnostics rather than permanent golden identity. Exact validated payload bytes are the protocol-message identity at the RX-1D boundary.

Canonical design/implementation records:

```text
docs/MiniFT8/rx-1b-design.md
docs/MiniFT8/rx-1c-monitor.md
docs/MiniFT8/rx-1d-decoder.md
docs/MiniFT8/rx-1e-hash-store.md
```
