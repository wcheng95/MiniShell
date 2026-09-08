#!/usr/bin/env python3

import errno
import os
import pty
import select
import subprocess
import sys
import tempfile
import time


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


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: linux_minift8.py <minishell> <app-dir>", file=sys.stderr)
        return 2

    minishell = os.path.abspath(sys.argv[1])
    app_dir = os.path.abspath(sys.argv[2])
    master_fd, slave_fd = pty.openpty()

    try:
        with tempfile.TemporaryDirectory(prefix="minishell-minift8-") as root:
            env = os.environ.copy()
            env["MINISHELL_APP_DIR"] = app_dir
            env["MINISHELL_ROOT"] = root

            process = subprocess.Popen(
                [minishell],
                stdin=slave_fd,
                stdout=slave_fd,
                stderr=slave_fd,
                env=env,
                close_fds=True,
            )
            os.close(slave_fd)
            slave_fd = -1

            transcript = bytearray()
            try:
                transcript.extend(read_until(master_fd, b"M$> ", 3.0))
                os.write(master_fd, b"minift8\n")
                transcript.extend(read_until(master_fd, b"R T O S V", 3.0))

                os.write(master_fd, b"o")
                transcript.extend(read_until(master_fd, b"Mode: FT8", 3.0))

                os.write(master_fd, b"1")
                transcript.extend(read_until(master_fd, b"Mode: FT4", 3.0))

                os.write(master_fd, b"5")
                transcript.extend(read_until(master_fd, b"Skip TX1: OFF", 3.0))

                os.write(master_fd, b"3")
                transcript.extend(read_until(master_fd, b"Skip TX1: ON", 3.0))

                os.write(master_fd, b"q")
                transcript.extend(read_until(master_fd, b"M$> ", 3.0))

                station = os.path.join(root, "flash", "minift8", "station.txt")
                temp_station = station + ".tmp"
                with open(station, "r", encoding="utf-8") as handle:
                    saved = handle.read()
                if "mode=1\n" not in saved or "skip_tx1=1\n" not in saved:
                    raise RuntimeError(f"unexpected station.txt contents: {saved!r}")
                if os.path.exists(temp_station):
                    raise RuntimeError("atomic save left station.txt.tmp behind")

                os.write(master_fd, b"minift8\n")
                transcript.extend(read_until(master_fd, b"FT4  20m", 3.0))
                os.write(master_fd, b"o")
                transcript.extend(read_until(master_fd, b"Mode: FT4", 3.0))
                os.write(master_fd, b"5")
                transcript.extend(read_until(master_fd, b"Skip TX1: ON", 3.0))
                os.write(master_fd, b"q")
                transcript.extend(read_until(master_fd, b"M$> ", 3.0))

                os.write(master_fd, b"exit\n")
                return_code = process.wait(timeout=3.0)
                if return_code != 0:
                    raise RuntimeError(f"minishell exited with {return_code}")
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
