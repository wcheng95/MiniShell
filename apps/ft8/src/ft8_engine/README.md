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
```

Current files:

```text
ft8_monitor.[ch]   streaming FFT/waterfall owner and explicit workspace contract
ft8_decoder.[ch]   FT8 candidate search and validated-payload boundary
ft8_ldpc.[ch]      private belief-propagation LDPC implementation
ft8_crc.[ch]       private FT8 CRC-14 implementation
vendor/kissfft/    pinned FFT implementation used by the cleaned monitor
```

Ownership rules:

- no MiniShell API calls inside the DSP/protocol core;
- no Linux, ESP-IDF, NuttX, board, UI, storage, AutoSeq, or TX dependency;
- monitor mutable DSP memory belongs to one explicit `Ft8Monitor` instance using caller-supplied workspace;
- candidate/LDPC/CRC scratch is local to a decode operation; no mutable decoder singleton exists;
- callsign hash knowledge is not yet implemented here; RX-1E will introduce an explicit per-engine `Ft8HashStore`;
- protocol message unpacking/text is not yet implemented here; RX-1F owns that stage.

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
```
