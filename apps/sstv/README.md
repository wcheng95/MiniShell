# MiniShell SSTV

SSTV is an external MiniShell application. The current T067 implementation is a
Linux receive-only bring-up for the project's single SSTV mode, Robot 36.

Current command:

```text
sstv <input.wav> <output.bmp>
```

Input contract:

- RIFF/WAVE integer PCM;
- 12000 Hz exactly;
- 16-bit or 24-bit;
- mono or stereo (stereo is averaged to mono);
- ordinary real audio.

The portable decoder boundary is always 12 kHz signed-16-bit mono. The WAV adapter,
BMP sink, and MiniShell API calls remain outside the pure decoder core.

Robot 36 RX is streaming and bounded-memory. It performs standard VIS acquisition,
frequency demodulation, fractional line/pixel timing, line-sync tracking, and
Y/alternating-Cr/Cb reconstruction. It keeps only bounded DSP and line/component
buffers; it does not allocate a 320x240 framebuffer or buffer the complete WAV.

Output is an uncompressed top-down 320x240 24-bit BMP so rows can be written in the
same order they are decoded.

Not implemented in T067:

- live WebSDR/QMX receive;
- SSTV transmit;
- CAT/PTT;
- Cardputer ADV packaging or display;
- SSTV modes other than Robot 36.

The eventual ADV deployment remains external:

```text
/flash/apps/sstv.elf
or
/sd/apps/sstv.elf
```

Canonical architecture: `docs/SSTV/architecture.md`.
