#!/usr/bin/env python3
"""Exercise station-load policy through the real FT8 controller and filesystem."""

import errno
import os
from pathlib import Path
import pty
import select
import subprocess
import sys
import tempfile
import time
import wave


DEFAULT_STATION = (
    b"# MiniFT8-V3 station.txt\ncallsign=\ngrid=\nprofile=0\nband=3\n"
    b"skip_tx1=0\nmax_retry=3\ncq_type=0\ncq_ft=\nfree_text=\nfd_exchange=\nrxtx_log=1\noffset_src=0\noffset=1500\n"
)


def read_until(fd, needle):
    deadline = time.monotonic() + 5.0
    data = bytearray()
    while needle not in data:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break
        if not select.select([fd], [], [], remaining)[0]:
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
        raise AssertionError(f"waiting for {needle!r}; got {bytes(data)!r}")
    return bytes(data)


def run_case(minishell, app_dir, name, original):
    with tempfile.TemporaryDirectory(prefix="minishell-ft8-config-") as root:
        station = Path(root) / "flash" / "ft8" / "station.txt"
        station.parent.mkdir(parents=True)
        with wave.open(str(Path(root) / "flash" / "ui.wav"), "wb") as fixture:
            fixture.setparams((2, 2, 12000, 0, "NONE", "not compressed"))
            fixture.writeframes(b"\0" * 4)
        if original is not None:
            station.write_bytes(original)
        master, slave = pty.openpty()
        process = None
        try:
            env = dict(os.environ, MINISHELL_ROOT=root, MINISHELL_APP_DIR=app_dir)
            process = subprocess.Popen(
                [minishell], stdin=slave, stdout=slave, stderr=slave, env=env,
                close_fds=True,
            )
            os.close(slave)
            slave = -1
            read_until(master, b"M$> ")
            os.write(master, b"ft8 --profile desktop --rx /flash/ui.wav --rx-slot 12345\n")
            if original is None:
                read_until(master, b"R T O S V")
                os.write(master, b"q")
                read_until(master, b"M$> ")
            else:
                response = read_until(master, b"M$> ")
                assert b"ft8: failed to initialize storage/configuration" in response, response
                assert b"app: ft8 returned 3" in response, response
            assert station.read_bytes() == (DEFAULT_STATION if original is None else original), name
            assert not station.with_suffix(".txt.tmp").exists(), name
            # A fresh shell command proves control returned after app teardown.
            os.write(master, b"help\n")
            response = read_until(master, b"M$> ")
            assert b"show this help" in response, response
            os.write(master, b"exit\n")
            assert process.wait(timeout=5) == 0, name
        finally:
            if process is not None and process.poll() is None:
                process.kill()
                process.wait()
            os.close(master)
            if slave >= 0:
                os.close(slave)
    print(f"PASS: {name}")


def main():
    if len(sys.argv) != 3:
        print("usage: linux_ft8_config_load.py <minishell> <app-dir>", file=sys.stderr)
        return 2
    minishell, app_dir = map(os.path.abspath, sys.argv[1:])
    run_case(minishell, app_dir, "missing creates established defaults", None)
    run_case(minishell, app_dir, "oversized preserves original", b"#" + b"x" * 2200 + b"\n")
    # Unknown keys are tolerated; an overlong callsign is a definite parse error.
    run_case(minishell, app_dir, "parse failure preserves original", b"callsign=ABCDEFGHIJKLMNOP\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
