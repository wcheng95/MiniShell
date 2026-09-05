#!/usr/bin/env python3
"""Host-side MiniShell MFT1 file-transfer helper."""

from __future__ import annotations

import argparse
import binascii
import os
import struct
import sys
import time
from pathlib import Path

try:
    import serial  # type: ignore
except ImportError:
    print("pyserial is required: python3 -m pip install pyserial", file=sys.stderr)
    raise SystemExit(2)

MAGIC = b"MFT1"
HEADER = struct.Struct("<4sQI")
FILE_CHUNK = 16 * 1024
PUT_BLOCK = 1024
HANDSHAKE_TIMEOUT = 10.0


def validate_remote(path: str) -> None:
    if not path.startswith("/") or any(ch.isspace() for ch in path):
        raise ValueError("remote path must be absolute and contain no whitespace")


def crc32_file(path: Path) -> int:
    crc = 0
    with path.open("rb") as f:
        while True:
            data = f.read(FILE_CHUNK)
            if not data:
                break
            crc = binascii.crc32(data, crc)
    return crc & 0xFFFFFFFF


def wait_for_line(ser: serial.Serial, predicate, timeout: float = HANDSHAKE_TIMEOUT) -> str:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        line = raw.decode("ascii", errors="replace").strip()
        if predicate(line):
            return line
        if line.startswith("MFT1 ERROR"):
            raise RuntimeError(line)
    raise TimeoutError("timed out waiting for MiniShell transfer response")


def send_shell_command(ser: serial.Serial, command: str) -> None:
    ser.reset_input_buffer()
    ser.write(command.encode("ascii") + b"\n")
    ser.flush()


def put_file(ser: serial.Serial, local: Path, remote: str) -> None:
    validate_remote(remote)
    if not local.is_file():
        raise FileNotFoundError(local)

    size = local.stat().st_size
    if size > 0xFFFFFFFF:
        raise ValueError("MFT1 V1 limits files to 4 GiB - 1")
    crc = crc32_file(local)

    send_shell_command(ser, f"put {remote}")
    wait_for_line(ser, lambda line: line == "MFT1 PUT READY")

    ser.write(HEADER.pack(MAGIC, size, crc))
    ser.flush()
    wait_for_line(ser, lambda line: line == "MFT1 DATA READY")

    remaining = size
    with local.open("rb") as f:
        while remaining:
            data = f.read(min(PUT_BLOCK, remaining))
            if not data:
                raise RuntimeError("local file became shorter during transfer")
            ser.write(data)
            ser.flush()
            remaining -= len(data)
            if remaining:
                wait_for_line(ser, lambda line: line == "MFT1 NEXT")

    line = wait_for_line(ser, lambda text: text.startswith("MFT1 OK "))
    fields = line.split()
    if len(fields) != 4:
        raise RuntimeError(f"malformed completion response: {line}")
    remote_size = int(fields[2], 10)
    remote_crc = int(fields[3], 16)
    if remote_size != size or remote_crc != crc:
        raise RuntimeError("device completion metadata does not match local file")

    print(f"put: {local} -> {remote} ({size} bytes, crc32={crc:08x})")


def get_file(ser: serial.Serial, remote: str, local: Path) -> None:
    validate_remote(remote)
    send_shell_command(ser, f"get {remote}")

    line = wait_for_line(ser, lambda text: text.startswith("MFT1 GET "))
    fields = line.split()
    if len(fields) != 4:
        raise RuntimeError(f"malformed metadata response: {line}")
    size = int(fields[2], 10)
    expected_crc = int(fields[3], 16)

    temporary = local.with_name(local.name + ".mft.part")
    crc = 0
    remaining = size
    try:
        with temporary.open("wb") as f:
            while remaining:
                chunk = ser.read(min(FILE_CHUNK, remaining))
                if not chunk:
                    raise TimeoutError(f"transfer stopped with {remaining} bytes remaining")
                f.write(chunk)
                crc = binascii.crc32(chunk, crc)
                remaining -= len(chunk)
            f.flush()
            os.fsync(f.fileno())

        crc &= 0xFFFFFFFF
        if crc != expected_crc:
            raise RuntimeError(f"CRC mismatch: expected {expected_crc:08x}, got {crc:08x}")

        wait_for_line(ser, lambda text: text == "MFT1 OK")
        os.replace(temporary, local)
    except Exception:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass
        raise

    print(f"get: {remote} -> {local} ({size} bytes, crc32={expected_crc:08x})")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Transfer files to/from MiniShell over USB Serial/JTAG")
    parser.add_argument("port", help="serial device, for example /dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200,
                        help="serial baud setting (USB Serial/JTAG ignores the physical baud; default: 115200)")

    sub = parser.add_subparsers(dest="operation", required=True)

    put = sub.add_parser("put", help="copy a host file to MiniShell")
    put.add_argument("local", type=Path)
    put.add_argument("remote")

    get = sub.add_parser("get", help="copy a MiniShell file to the host")
    get.add_argument("remote")
    get.add_argument("local", type=Path)

    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        with serial.Serial(args.port, args.baud, timeout=0.5, write_timeout=5.0) as ser:
            if args.operation == "put":
                put_file(ser, args.local, args.remote)
            else:
                get_file(ser, args.remote, args.local)
    except (OSError, ValueError, RuntimeError, TimeoutError, serial.SerialException) as exc:
        print(f"transfer failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
