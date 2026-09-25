#!/usr/bin/env python3
"""Run production FT8 + Serial against a PTY; never require radio hardware."""
import os
from pathlib import Path
import pty
import select
import subprocess
import sys
import tempfile
import wave

from linux_ft8_config_load import read_until


def main():
    minishell, apps = map(os.path.abspath, sys.argv[1:])
    with tempfile.TemporaryDirectory(prefix="ft8-cat-") as temp:
        flash = Path(temp) / "flash"
        (flash / "ft8").mkdir(parents=True)
        (flash / "ft8" / "setting.txt").write_text("callsign=AG6AQ\ngrid=CM97\nband=1\n")
        with wave.open(str(flash / "ui.wav"), "wb") as fixture:
            fixture.setparams((2, 2, 12000, 0, "NONE", "not compressed"))
            fixture.writeframes(b"\0" * 4)
        console, terminal = pty.openpty()
        cat, tty = pty.openpty()
        process = None
        try:
            endpoint = "serial:" + os.ttyname(tty)
            env = dict(os.environ, MINISHELL_ROOT=temp, MINISHELL_APP_DIR=apps)
            process = subprocess.Popen([minishell], stdin=terminal, stdout=terminal,
                                       stderr=terminal, env=env, close_fds=True)
            read_until(console, b"M$> ")
            for cycle in range(3):
                rx = "/flash/absent.wav" if cycle == 1 else "/flash/ui.wav"
                os.write(console, f"ft8 --rx {rx} --rx-slot 12345 --cat {endpoint}\n".encode())
                assert read_until(cat, b"FA00007074000;") == b"MD6;FR0;FT0;FA00007074000;"
                if cycle == 1:
                    output = read_until(console, b"M$> ")
                    assert b"failed to start RX audio" in output, output
                else:
                    read_until(console, b"RX 40 ")
                    os.write(console, b"q")
                    read_until(console, b"M$> ")
                assert not select.select([cat], [], [], 0.05)[0], "unexpected extra CAT bytes"

            for invalid in ("serial:relative", "serial:/dev/minishell-no-such-tty"):
                os.write(console, f"ft8 --rx /flash/ui.wav --rx-slot 12345 --cat {invalid}\n".encode())
                output = read_until(console, b"M$> ")
                assert b"CAT open/synchronization failed" in output, output
                assert b"app: ft8 returned 12" in output, output

            os.write(console, b"ft8 --rx /flash/ui.wav --rx-slot 12345\n")
            read_until(console, b"RX 40 ")
            os.write(console, b"q")
            read_until(console, b"M$> ")
            assert not select.select([cat], [], [], 0.05)[0], "CAT sent when omitted"
            os.write(console, b"exit\n")
            assert process.wait(timeout=5) == 0
        finally:
            if process is not None and process.poll() is None:
                process.kill()
                process.wait()
            for fd in (console, terminal, cat, tty):
                os.close(fd)
    print("FT8 CAT PTY: selected band, startup, no-CAT, failures and repeated cleanup PASS")


if __name__ == "__main__":
    main()
