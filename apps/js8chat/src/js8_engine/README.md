# JS8 Normal PHY core

T052/T053 own CRC-12, LDPC(174,87) and direct-binary 79-tone framing. T054 adds
`js8_monitor` and `js8_decoder`, adapted from MiniShell's MiniFT8 monitor/decoder
at `e3f6d80`. JS8Call-improved v3.0.3 supplies wire semantics, not the desktop
receive architecture. No FT8 headers, types or link dependencies are used.

The monitor consumes exactly 960 finite normalized float samples at 6 kHz per
call. Query storage, allocate aligned caller-owned workspace, initialize, then
process blocks and get a linear byte waterfall view. Workspace includes only
FFT plan, window, rolling analysis history, scratch and the compact waterfall;
there is no full-slot raw PCM buffer. The copied private FFT and its no-heap
adaptations are documented in `vendor/kissfft/README.md`.

The baseline is 200..2900 Hz, 2x time and 2x frequency oversampling, 93 blocks.
Dimensions use the same derivation and Hann/window arithmetic as MiniFT8.
`reset_window` rewinds count/diagnostics and clears analysis history but retains
waterfall bytes. `reset_stream` additionally clears those bytes. Both follow
current FT8 source semantics (older RX-1C prose describes an earlier lifecycle).
No UTC/slot-framer API is introduced. Destroy makes the instance inert and
leaves caller storage ownership unchanged.

A view's `mag` always points to the allocation's first stored block;
`first_block` labels that block. A negative logical block never requires a
pointer before the allocation. Views must remain immutable and backed by
`num_blocks * block_stride` live bytes during search/decode; reset or further
processing invalidates previously captured views. No snapshot is allocated.

Candidate search is synchronous and uses MiniFT8's direct neighborhood score,
min-heap retention and descending-score sort: -10..19 block offsets, all
oversampling lanes, all valid eight-tone frequency starts, up to 50 entries.
The original `4 2 5 6 1 3 0` sync sequence is evaluated at 0, 36 and 72.
The 75 possible comparisons are evaluated directly; no score cache or
resumable scheduling state is needed in this stage. Default minimum score 5
and capacity 50 are initial search policy, not protocol constants.

`js8_decoder_extract_likelihood` exposes raw max-log dB differences for focused
mapping tests. Each tone is direct binary; 7..35 and 43..71 yield parity then
information LLRs. Byte magnitudes mean `byte * 0.5 - 120` dB. Absent time rows
are erasures, including for negatively labelled views; there is no FT8 UTC
preroll-overwrite policy. Decode applies the MiniFT8 variance normalization,
rejects singular/no-evidence variance, then calls T052 BP and CRC. Only OK
returns validated unpacked 75-bit payloads. Candidate scores and lattice
coordinates are diagnostics, not identity or UTC estimates (FFT history adds
analysis delay). No message parsing, tracking, subtraction or soft combining.

`tests/js8_rx_test.c` synthesizes continuous-phase, amplitude-0.5 PCM one block
at a time directly from the third checked-in upstream tone vector. The frame
starts at sample 3000 (500 ms), at base 1000 Hz and 6.25 Hz spacing. It does
not use the production channel encoder to create tones and never reads a WAV.
Tests also isolate lifecycle, negative logical indexing, oversampling lanes,
all direct bit mappings, all three sync groups, capacity/pruning and distinct
invalid/LDPC/CRC failures. `js8_no_heap_test.py` checks compiled library symbols
in addition to source boundary enforcement.

Host measurements and exact build/test commands are recorded in T054. These
are baseline resource visibility, not ADV tuning or noisy-signal sensitivity
evidence. Real WAV reception remains T055 work.
