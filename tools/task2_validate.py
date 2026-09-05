#!/usr/bin/env python3
"""End-to-end hardware validation for MiniShell Task 2 file transfer."""

from __future__ import annotations

import argparse
import hashlib
import tempfile
from pathlib import Path

import minishell_transfer as mft


def write_pattern(path: Path, size: int, seed: int) -> None:
    remaining = size
    offset = 0
    with path.open("wb") as f:
        while remaining:
            count = min(16 * 1024, remaining)
            block = bytes(((seed + (offset + i) * 37 + ((offset + i) >> 8)) & 0xFF)
                          for i in range(count))
            f.write(block)
            offset += count
            remaining -= count


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as f:
        while True:
            data = f.read(64 * 1024)
            if not data:
                break
            digest.update(data)
    return digest.hexdigest()


def require_same(a: Path, b: Path, label: str) -> None:
    a_hash = sha256(a)
    b_hash = sha256(b)
    if a_hash != b_hash:
        raise RuntimeError(f"{label}: SHA-256 mismatch: {a_hash} != {b_hash}")
    print(f"[PASS] {label}: sha256={a_hash}")


def expect_remote_absent(ser: mft.serial.Serial, remote: str, local_probe: Path) -> None:
    try:
        mft.get_file(ser, remote, local_probe)
    except RuntimeError as exc:
        if str(exc) == "MFT1 ERROR stat -2":
            print(f"[PASS] temporary cleanup: {remote} is absent")
            return
        raise
    raise RuntimeError(f"temporary cleanup failed: {remote} still exists")


def interrupted_put(ser: mft.serial.Serial, local: Path, remote: str) -> None:
    size = local.stat().st_size
    if size <= mft.PUT_BLOCK:
        raise ValueError("interruption payload must exceed one PUT block")
    crc = mft.crc32_file(local)

    mft.send_shell_command(ser, f"put {remote}")
    mft.wait_for_line(ser, lambda line: line == "MFT1 PUT READY")

    ser.write(mft.HEADER.pack(mft.MAGIC, size, crc))
    ser.flush()
    mft.wait_for_line(ser, lambda line: line == "MFT1 DATA READY")

    with local.open("rb") as f:
        first = f.read(mft.PUT_BLOCK)
    if len(first) != mft.PUT_BLOCK:
        raise RuntimeError("could not read first interruption block")

    ser.write(first)
    ser.flush()
    mft.wait_for_line(ser, lambda line: line == "MFT1 NEXT")

    print("[TEST] intentionally stalling upload after first acknowledged block...")
    line = mft.wait_for_line(
        ser,
        lambda text: text == "MFT1 ERROR payload-timeout",
        timeout=8.0,
    )
    if line != "MFT1 ERROR payload-timeout":
        raise RuntimeError(f"unexpected interruption result: {line}")
    print("[PASS] interrupted put timed out cleanly")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate MiniShell Task 2 file transfer on real hardware"
    )
    parser.add_argument("port", help="serial device, for example /dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--remote", default="/sd/task2_validate.bin",
                        help="remote scratch file (default: /sd/task2_validate.bin)")
    parser.add_argument("--large-size", type=int, default=512 * 1024 + 37,
                        help="large replacement payload size in bytes")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    mft.validate_remote(args.remote)
    if args.large_size <= mft.PUT_BLOCK:
        raise SystemExit("--large-size must exceed 1024 bytes")

    try:
        with tempfile.TemporaryDirectory(prefix="minishell-task2-") as tmp_name:
            tmp = Path(tmp_name)
            original = tmp / "original.bin"
            original_back = tmp / "original-back.bin"
            replacement = tmp / "replacement.bin"
            replacement_back = tmp / "replacement-back.bin"
            after_interrupt = tmp / "after-interrupt.bin"
            interrupted = tmp / "interrupted.bin"
            probe = tmp / "unexpected-part.bin"

            write_pattern(original, 8193, 0x21)
            write_pattern(replacement, args.large_size, 0x73)
            write_pattern(interrupted, 8193, 0xC5)

            with mft.serial.Serial(args.port, args.baud,
                                   timeout=0.5, write_timeout=5.0) as ser:
                print("[TEST] round-trip put/get")
                mft.put_file(ser, original, args.remote)
                mft.get_file(ser, args.remote, original_back)
                require_same(original, original_back, "put/get round trip")

                print("[TEST] replace existing destination with larger binary")
                mft.put_file(ser, replacement, args.remote)
                mft.get_file(ser, args.remote, replacement_back)
                require_same(replacement, replacement_back,
                             "existing-file replacement + large transfer")

                print("[TEST] interrupt a replacement before publication")
                interrupted_put(ser, interrupted, args.remote)
                expect_remote_absent(ser, args.remote + ".mft.part", probe)
                mft.get_file(ser, args.remote, after_interrupt)
                require_same(replacement, after_interrupt,
                             "old destination survives interrupted put")

        print()
        print("Task 2 hardware validation: PASS")
        print(f"Remote scratch file remains at {args.remote}")
        return 0
    except (OSError, ValueError, RuntimeError, TimeoutError,
            mft.serial.SerialException) as exc:
        print(f"Task 2 hardware validation: FAIL: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
