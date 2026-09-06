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
        print("usage: linux_nano.py <minishell> <app-dir>", file=sys.stderr)
        return 2

    minishell = os.path.abspath(sys.argv[1])
    app_dir = os.path.abspath(sys.argv[2])
    master_fd, slave_fd = pty.openpty()

    try:
        with tempfile.TemporaryDirectory(prefix="minishell-nano-") as root:
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
                os.write(master_fd, b"nano /sd/note.txt\n")
                transcript.extend(read_until(master_fd, b"New file", 3.0))

                os.write(master_fd, b"abc")
                time.sleep(0.1)
                os.write(master_fd, b"\x0f")  # Ctrl-O: save
                transcript.extend(read_until(master_fd, b"Wrote 3 bytes", 3.0))

                os.write(master_fd, b"\x18")  # Ctrl-X: exit
                transcript.extend(read_until(master_fd, b"M$> ", 3.0))
                os.write(master_fd, b"exit\n")

                return_code = process.wait(timeout=3.0)
                if return_code != 0:
                    raise RuntimeError(f"MiniShell exited with {return_code}")

                note = os.path.join(root, "sd", "note.txt")
                with open(note, "rb") as handle:
                    content = handle.read()
                if content != b"abc":
                    raise RuntimeError(f"nano saved {content!r}, expected b'abc'")
            except Exception:
                print(transcript.decode("utf-8", errors="replace"), end="")
                process.kill()
                process.wait(timeout=3.0)
                raise

    finally:
        if slave_fd >= 0:
            os.close(slave_fd)
        os.close(master_fd)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
