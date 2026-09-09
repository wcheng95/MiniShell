#!/usr/bin/env python3

import errno
import os
import pty
import re
import select
import struct
import subprocess
import sys
import tempfile
import time
import wave


def read_until(fd: int, needle: bytes, timeout: float) -> bytes:
    deadline = time.monotonic() + timeout
    data = bytearray()
    while needle not in data:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError(f"timed out waiting for {needle!r}; got {bytes(data)!r}")
        readable, _, _ = select.select([fd], [], [], remaining)
        if not readable:
            continue
        try:
            chunk = os.read(fd, 4096)
        except OSError as exc:
            if exc.errno == errno.EIO:
                break
            raise
        if not chunk:
            break
        data.extend(chunk)
    if needle not in data:
        raise TimeoutError(f"stream ended waiting for {needle!r}; got {bytes(data)!r}")
    return bytes(data)


def create_fixture(src: str, dst: str) -> None:
    with wave.open(src, "rb") as wav:
        if (wav.getframerate(), wav.getnchannels(), wav.getsampwidth()) != (6000, 1, 2):
            raise RuntimeError("unexpected RX-7 golden WAV format")
        raw = wav.readframes(wav.getnframes())
    samples = list(struct.unpack("<" + "h" * (len(raw) // 2), raw))
    slot_samples = 90000
    if len(samples) > slot_samples:
        raise RuntimeError("golden WAV exceeds one FT8 slot")
    samples.extend([0] * (slot_samples - len(samples)))

    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with wave.open(dst, "wb") as wav:
        wav.setnchannels(2)
        wav.setsampwidth(2)
        wav.setframerate(12000)
        out = bytearray()
        for sample in samples:
            frame = struct.pack("<hh", sample, sample)
            out += frame
            out += frame
        wav.writeframes(out)


def main() -> int:
    if len(sys.argv) != 4:
        print("usage: linux_ft8_rx7.py <minishell> <app-dir> <v2-golden-wav>", file=sys.stderr)
        return 2

    minishell = os.path.abspath(sys.argv[1])
    app_dir = os.path.abspath(sys.argv[2])
    golden = os.path.abspath(sys.argv[3])
    master_fd, slave_fd = pty.openpty()

    try:
        with tempfile.TemporaryDirectory(prefix="minishell-rx7-") as root:
            create_fixture(golden, os.path.join(root, "flash", "rx7.wav"))
            env = os.environ.copy()
            env["MINISHELL_APP_DIR"] = app_dir
            env["MINISHELL_ROOT"] = root
            process = subprocess.Popen(
                [minishell], stdin=slave_fd, stdout=slave_fd, stderr=slave_fd,
                env=env, close_fds=True,
            )
            os.close(slave_fd)
            slave_fd = -1
            transcript = bytearray()
            try:
                transcript.extend(read_until(master_fd, b"M$> ", 3.0))
                os.write(master_fd,
                         b"ft8 --profile adv --rx /flash/rx7.wav --rx-slot 12345\n")
                transcript.extend(read_until(master_fd, b"CQ W1XYZ FN42", 12.0))

                plain = bytes(transcript)
                if b"1 CQ W1XYZ FN42" not in plain:
                    raise RuntimeError("decoded CQ did not reach RX line 1")
                if re.search(rb"RX 20 [0-9]{2}:[0-9]{2}:[0-9]{2} 1/1 [0-9A-E]", plain) is None:
                    raise RuntimeError("locked ADV RX top line was not rendered")

                os.write(master_fd, b"q")
                transcript.extend(read_until(master_fd, b"M$> ", 3.0))
                os.write(master_fd, b"exit\n")
                if process.wait(timeout=3.0) != 0:
                    raise RuntimeError("MiniShell exited non-zero")
            except Exception:
                print(transcript.decode("utf-8", errors="replace"), end="")
                process.kill()
                process.wait(timeout=3.0)
                raise

            print(transcript.decode("utf-8", errors="replace"), end="")
    finally:
        if slave_fd >= 0:
            os.close(slave_fd)
        os.close(master_fd)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
