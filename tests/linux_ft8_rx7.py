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


def read_slot_samples(src: str) -> list[int]:
    with wave.open(src, "rb") as wav:
        if (wav.getframerate(), wav.getnchannels(), wav.getsampwidth()) != (6000, 1, 2):
            raise RuntimeError(f"unexpected FT8 golden WAV format: {src}")
        raw = wav.readframes(wav.getnframes())
    samples = list(struct.unpack("<" + "h" * (len(raw) // 2), raw))
    slot_samples = 90000
    if len(samples) > slot_samples:
        raise RuntimeError("golden WAV exceeds one FT8 slot")
    samples.extend([0] * (slot_samples - len(samples)))
    return samples


def write_12k_stereo(dst: str, samples: list[int]) -> None:
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


def create_fixture(src: str, dst: str) -> None:
    write_12k_stereo(dst, read_slot_samples(src))


def create_sequence(sources: list[str], dst: str) -> None:
    samples: list[int] = []
    for src in sources:
        samples.extend(read_slot_samples(src))
    write_12k_stereo(dst, samples)


def write_station(root: str) -> None:
    path = os.path.join(root, "flash", "ft8", "station.txt")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(
            "# MiniFT8-V3 AS-4 reference station\n"
            "callsign=W1ABC\n"
            "grid=FN42\n"
            "profile=0\n"
            "band=3\n"
            "skip_tx1=0\n"
            "max_retry=3\n"
        )


def current_screen(data: bytes) -> bytes:
    marker = b"\x1b[2J\x1b[H"
    pos = data.rfind(marker)
    return data[pos:] if pos >= 0 else data


def main() -> int:
    if len(sys.argv) != 4:
        print("usage: linux_ft8_rx7.py <minishell> <app-dir> <v2-golden-wav>", file=sys.stderr)
        return 2

    minishell = os.path.abspath(sys.argv[1])
    app_dir = os.path.abspath(sys.argv[2])
    golden = os.path.abspath(sys.argv[3])
    golden_dir = os.path.dirname(golden)
    grid_golden = os.path.join(golden_dir, "ft8_w1abc_k9xyz_fn42.wav")
    report_golden = os.path.join(golden_dir, "ft8_w1abc_k9xyz_neg12.wav")
    rr73_golden = os.path.join(golden_dir, "ft8_w1abc_k9xyz_rr73.wav")
    master_fd, slave_fd = pty.openpty()

    try:
        with tempfile.TemporaryDirectory(prefix="minishell-rx7-") as root:
            create_fixture(golden, os.path.join(root, "flash", "rx7.wav"))
            create_fixture(grid_golden, os.path.join(root, "flash", "as4-grid.wav"))
            create_fixture(report_golden, os.path.join(root, "flash", "as4-report.wav"))
            create_fixture(rr73_golden, os.path.join(root, "flash", "as4-rr73.wav"))
            create_sequence([grid_golden, report_golden],
                            os.path.join(root, "flash", "as4-sequence.wav"))
            write_station(root)

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

                # RX-7 / AS-3 regression: manual CQ selection still works.
                os.write(master_fd,
                         b"ft8 --profile adv --rx /flash/rx7.wav --rx-slot 12345\n")
                transcript.extend(read_until(master_fd, b"CQ W1XYZ FN42", 12.0))

                plain = bytes(transcript)
                if b"1 CQ W1XYZ FN42" not in plain:
                    raise RuntimeError("decoded CQ did not reach RX line 1")
                if re.search(rb"RX 20 [0-9]{2}:[0-9]{2}:[0-9]{2} 1/1 [0-9A-E]", plain) is None:
                    raise RuntimeError("locked ADV RX top line was not rendered")

                os.write(master_fd, b"1")
                transcript.extend(read_until(master_fd, b"CQ W1XYZ FN42", 3.0))
                os.write(master_fd, b"q")
                transcript.extend(read_until(master_fd, b"M$> ", 3.0))

                # AS-4: TX1/grid addressed to W1ABC creates a QSO automatically.
                # No RX line selection is sent before switching to T.
                os.write(master_fd,
                         b"ft8 --profile adv --rx /flash/as4-grid.wav --rx-slot 12345\n")
                transcript.extend(read_until(master_fd, b"W1ABC K9XYZ FN42", 12.0))
                os.write(master_fd, b"t")
                grid_view = read_until(master_fd, b"K9XYZ    RPRT 0/3", 3.0)
                transcript.extend(grid_view)
                os.write(master_fd, b"q")
                transcript.extend(read_until(master_fd, b"M$> ", 3.0))

                # AS-4: a fresh TX2/report also starts automatically. V2 semantics
                # advance the new context to ROGER_REPORT (derived next TX3).
                os.write(master_fd,
                         b"ft8 --profile adv --rx /flash/as4-report.wav --rx-slot 12345\n")
                transcript.extend(read_until(master_fd, b"W1ABC K9XYZ -12", 12.0))
                os.write(master_fd, b"t")
                report_view = read_until(master_fd, b"K9XYZ    RRPT 0/3", 3.0)
                transcript.extend(report_view)
                os.write(master_fd, b"q")
                transcript.extend(read_until(master_fd, b"M$> ", 3.0))

                # Stronger AS-4 progression proof: two consecutive RX slots for
                # the same DX are consumed in order. The second message must
                # advance the existing context, not append a duplicate K9XYZ.
                os.write(master_fd,
                         b"ft8 --profile adv --rx /flash/as4-sequence.wav --rx-slot 12345\n")
                transcript.extend(read_until(master_fd, b"W1ABC K9XYZ -12", 18.0))
                os.write(master_fd, b"t")
                sequence_view = read_until(master_fd, b"\x1b[7;1H                    ", 3.0)
                sequence_screen = current_screen(sequence_view)
                if b"K9XYZ    RRPT 0/3" not in sequence_screen:
                    raise RuntimeError(f"two-slot progression did not reach RRPT: {sequence_screen!r}")
                if sequence_screen.count(b"K9XYZ") != 1:
                    raise RuntimeError(f"two-slot progression duplicated context: {sequence_screen!r}")
                transcript.extend(sequence_view)
                os.write(master_fd, b"q")
                transcript.extend(read_until(master_fd, b"M$> ", 3.0))

                # V2 reincarnation guard: an unknown late RR73 must not create a
                # fresh QSO. Again, no manual selection is sent.
                os.write(master_fd,
                         b"ft8 --profile adv --rx /flash/as4-rr73.wav --rx-slot 12345\n")
                transcript.extend(read_until(master_fd, b"W1ABC K9XYZ RR73", 12.0))
                os.write(master_fd, b"t")
                empty_view = read_until(master_fd, b"\x1b[7;1H                    ", 3.0)
                tx_screen = current_screen(empty_view)
                if (b"K9XYZ" in tx_screen or b"RPLY" in tx_screen or
                        b"RPRT" in tx_screen or b"RRPT" in tx_screen):
                    raise RuntimeError(f"late RR73 incorrectly created a QSO: {tx_screen!r}")
                transcript.extend(empty_view)
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
