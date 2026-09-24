#!/usr/bin/env python3
"""Stdlib 12 kHz AFSK fixtures; protocol/DDS follows rtty_encode.py at
wcheng95/rtty_decoder 9f50ca6f0387946f7656207767fad9d3ade6d5ab.
No playback or runtime dependency. Default stops match its two MARK bits.
"""
import argparse
import math
import wave

LETTERS = "\0E\nA SIU\rDRJNFCKTZLWHYPQOBG\0MXV\0"
FIGURES = "\0""3\n- '87\r$4\a,!:(5\")2#6019?&\0./;\0"

def symbols(text):
    result = [31]
    figures = False
    for c in text.upper():
        if c in " \r\n":
            result.append(LETTERS.index(c))
        elif c in LETTERS and c != "\0":
            if figures:
                result.append(31)
                figures = False
            result.append(LETTERS.index(c))
        elif c in FIGURES and c != "\0":
            if not figures:
                result.append(27)
                figures = True
            result.append(FIGURES.index(c))
    return result

def samples(text, mark=1000, stop=2.0):
    bits = [1] * (2 * round(0.5 * 45.45))
    for code in symbols(text):
        bits += [0, 0]
        for k in range(5):
            bits += [(code >> k) & 1] * 2
        bits += [1] * round(stop * 2)
    bits += [1] * (2 * round(0.3 * 45.45))
    phase = 0.0
    for n in range(math.ceil(len(bits) * 12000 / 90.9)):
        bit = bits[min(int(n * 90.9 / 12000), len(bits)-1)]
        phase = (phase + 2 * math.pi * (mark if bit else mark-170) / 12000) % (2*math.pi)
        yield 0.5 * math.sin(phase)

def write(path, text, mark=1000, width=3, channels=2, stop=2.0):
    with wave.open(str(path), "wb") as wav:
        wav.setparams((channels, width, 12000, 0, "NONE", "not compressed"))
        data = bytearray()
        for value in samples(text, mark, stop):
            data += round(value*((1 << (width*8-1))-1)).to_bytes(width,"little",signed=True)*channels
            if len(data) >= 4096:
                wav.writeframesraw(data)
                data.clear()
        wav.writeframes(data)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path")
    parser.add_argument("--message", default="THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG\r\n")
    parser.add_argument("--mark", type=float, default=1000)
    parser.add_argument("--width", type=int, choices=(2,3), default=3)
    parser.add_argument("--channels", type=int, choices=(1,2), default=2)
    args = parser.parse_args()
    write(args.path, args.message, args.mark, args.width, args.channels)
