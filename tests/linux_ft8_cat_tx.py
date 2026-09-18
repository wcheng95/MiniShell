#!/usr/bin/env python3
"""Bounded CAT tone through the production CLI and Serial provider, without RF."""
import os
from pathlib import Path
import pty
import select
import subprocess
import sys
import tempfile
import time

from linux_ft8_config_load import read_until


def main():
    minishell, apps = map(os.path.abspath, sys.argv[1:])
    with tempfile.TemporaryDirectory(prefix="ft8-cat-tx-") as temp:
        station = Path(temp) / "flash/ft8/station.txt"
        station.parent.mkdir(parents=True)
        original = "callsign=AG6AQ\ngrid=CM97\nband=3\n"
        station.write_text(original)
        console, terminal = pty.openpty()
        cat, tty = pty.openpty()
        process = None
        try:
            endpoint = "serial:" + os.ttyname(tty)
            env = dict(os.environ, MINISHELL_ROOT=temp, MINISHELL_APP_DIR=apps)
            process = subprocess.Popen([minishell], stdin=terminal, stdout=terminal,
                                       stderr=terminal, env=env, close_fds=True)
            read_until(console, b"M$> ")
            for duration, band, frequency in ((500, 3, "14074000"), (100, 1, "07074000"), (500, 3, "14074000")):
                contents = original.replace("band=3", f"band={band}")
                station.write_text(contents)
                started = time.monotonic()
                os.write(console, f"ft8 --cat {endpoint} --cat-test-tone 1500 --cat-test-ms {duration}\n".encode())
                assert read_until(cat, b"TA1500.00;") == f"MD6;FR0;FT0;FA000{frequency};MD6;TX;TA1500.00;".encode()
                tone_seen = time.monotonic()
                assert not select.select([cat], [], [], duration / 2000)[0], "RX before requested hold"
                assert read_until(cat, b"RX;") == b"RX;"
                elapsed = time.monotonic() - tone_seen
                assert duration / 1000 - 0.03 <= elapsed < duration / 1000 + 1, elapsed
                output = read_until(console, b"M$> ")
                assert b"CAT tone test complete" in output, output
                assert time.monotonic() - started < duration / 1000 + 2
                assert not select.select([cat], [], [], 0.02)[0], "unexpected CAT bytes"
                assert station.read_text() == contents
                assert set(station.parent.iterdir()) == {station}, "diagnostic persisted FT8 state"

            for tone, duration in ((299, 500), (2701, 500), (1500, 99), (1500, 2001)):
                os.write(console, f"ft8 --cat {endpoint} --cat-test-tone {tone} --cat-test-ms {duration}\n".encode())
                assert b"usage:" in read_until(console, b"M$> ")
                assert not select.select([cat], [], [], 0.02)[0], "invalid diagnostic keyed TX"
            os.write(console, b"exit\n")
            assert process.wait(timeout=5) == 0
        finally:
            if process is not None and process.poll() is None:
                process.kill()
                process.wait()
            for fd in (console, terminal, cat, tty):
                os.close(fd)
    print("CAT tone PTY: exact bytes, selected band, bounded hold, clean reopen, no persistence PASS")


if __name__ == "__main__":
    main()
