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
        print("usage: linux_ft8.py <minishell> <app-dir>", file=sys.stderr)
        return 2

    minishell = os.path.abspath(sys.argv[1])
    app_dir = os.path.abspath(sys.argv[2])
    master_fd, slave_fd = pty.openpty()

    try:
        with tempfile.TemporaryDirectory(prefix="minishell-ft8-") as root:
            station = os.path.join(root, "flash", "ft8", "station.txt")
            os.makedirs(os.path.dirname(station), exist_ok=True)
            with open(station, "w", encoding="utf-8") as handle:
                handle.write(
                    "# MiniFT8-V3 station.txt\n"
                    "callsign=ag6aq\n"
                    "grid=cm97\n"
                    "profile=0\n"
                    "band=3\n"
                    "skip_tx1=0\n"
                    "max_retry=3\n"
                )

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

                # Default DESKTOP launch still exercises live config mutation.
                os.write(master_fd, b"ft8\n")
                transcript.extend(read_until(master_fd, b"R T O S V", 3.0))
                os.write(master_fd, b"o")
                transcript.extend(read_until(master_fd, b"Protocol: FT8", 3.0))
                os.write(master_fd, b"1")
                transcript.extend(read_until(master_fd, b"Protocol: FT8", 3.0))
                os.write(master_fd, b"5")
                transcript.extend(read_until(master_fd, b"Skip TX1: OFF", 3.0))
                os.write(master_fd, b"3")
                transcript.extend(read_until(master_fd, b"Skip TX1: ON", 3.0))
                os.write(master_fd, b"q")
                transcript.extend(read_until(master_fd, b"M$> ", 3.0))

                with open(station, "r", encoding="utf-8") as handle:
                    saved = handle.read()
                if (
                    "callsign=AG6AQ\n" not in saved
                    or "grid=CM97\n" not in saved
                    or "profile=0\n" not in saved
                    or "band=3\n" not in saved
                    or "skip_tx1=1\n" not in saved
                ):
                    raise RuntimeError(f"unexpected station.txt contents: {saved!r}")
                if "mode=" in saved or "mode0_" in saved or "presentation=" in saved:
                    raise RuntimeError(f"unexpected persisted state: {saved!r}")
                if os.path.exists(station + ".tmp"):
                    raise RuntimeError("atomic save left station.txt.tmp behind")

                os.write(master_fd, b"ft8 --profile desktop\n")
                transcript.extend(read_until(master_fd, b"R T O S V", 3.0))
                os.write(master_fd, b"v")
                transcript.extend(read_until(master_fd, b"System Info", 3.0))

                # V -> 1 Memory reads the real MiniShell Memory API while ft8 is alive.
                os.write(master_fd, b"1")
                memory_view = read_until(master_fd, b"RX: OFF", 3.0)
                if b"Heap free:" not in memory_view or b"App alloc:" not in memory_view:
                    raise RuntimeError(f"Memory view missing runtime fields: {memory_view!r}")
                transcript.extend(memory_view)
                os.write(master_fd, b"`")
                transcript.extend(read_until(master_fd, b"System Info", 3.0))

                os.write(master_fd, b"5")
                transcript.extend(read_until(master_fd, b"Presentation: DESKTOP", 3.0))
                os.write(master_fd, b"q")
                transcript.extend(read_until(master_fd, b"M$> ", 3.0))

                # ADV uses the locked 20-character top row; profile name is no longer there.
                os.write(master_fd, b"ft8 --profile adv\n")
                transcript.extend(read_until(master_fd, b"RX 20 ", 3.0))
                os.write(master_fd, b"o")
                transcript.extend(read_until(master_fd, b"Protocol: FT8", 3.0))
                os.write(master_fd, b"5")
                transcript.extend(read_until(master_fd, b"Skip TX1: ON", 3.0))
                os.write(master_fd, b"v")
                transcript.extend(read_until(master_fd, b"System Info", 3.0))
                os.write(master_fd, b"5")
                adv_system = read_until(master_fd, b"UI: text 20x7", 3.0)
                if b"Presentation: ADV" not in adv_system:
                    raise RuntimeError(f"ADV system view missing presentation label: {adv_system!r}")
                transcript.extend(adv_system)
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
