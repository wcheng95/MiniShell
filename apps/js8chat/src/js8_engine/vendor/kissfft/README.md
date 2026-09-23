# Private JS8 KissFFT copy

Copied from MiniShell `e3f6d80` at `apps/ft8/src/ft8_engine/vendor/kissfft/`.
Original file hashes at that commit:

- `kiss_fftr.c`: `d2df73b6c0a73fd2ab03cadce81f00d9a947bb91bc175de9afcee68941a58357`
- `kiss_fft.c`: `12013cff5afe9fbd5a656b72027f6070e73207e3b58e2ef89f8b2a4bedd042db`
- `kiss_fft.h`: `01d7f67d0b5ff291b3753fa05e36f481c4330a06b66a59576bb41d06db51df70`
- `_kiss_fft_guts.h`: `9ec22cc5455fab185f19d0e332aaaae5a8fa236d3e040ebfb0ece57fbc0c886a`
- `kiss_fftr.h`: `ec8e10d4078e02bb0fe627df0894f3e2e81faff76a4704f3c7f9ec4693f84b32`

Original Mark Borgerding copyright and BSD-3-Clause SPDX notices are retained.
This is the same pinned implementation used by the current MiniFT8 monitor;
no FT8 file was modified. The local changes enforce the JS8 no-heap contract:

- float-only private header; no SIMD/fixed-point platform headers;
- allocation helpers require caller workspace / size-query pointers;
- plans reject factors other than 2, 3 and 5; unused generic-radix and in-place
  temporary allocations are removed (the monitor always runs out-of-place);
- unused inverse real FFT and OpenMP path removed;
- exported function names are prefixed `js8_` via the private header, preventing
  collisions with the existing FT8 copy if later linked in the same application.

Forward FFT butterfly/window arithmetic is unchanged. Normal 960/1920 FFT
geometry is supported. No FFT API is exposed outside js8_engine. Subsequent
vendor deduplication is deliberately deferred until after the JS8 milestone.
