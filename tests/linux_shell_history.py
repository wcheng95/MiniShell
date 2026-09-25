"""Exercise the real resident editor with ANSI keys and inspect terminal leases."""
import fcntl
import os
from pathlib import Path
import pty
import struct
import subprocess
import sys
import tempfile
import termios
import time

from linux_input import read_until

executable, input_probe, hello, cat = map(os.path.abspath, sys.argv[1:5])
UP, DOWN, LEFT, RIGHT = b"\x1b[A", b"\x1b[B", b"\x1b[D", b"\x1b[C"
HOME, END, DELETE = b"\x1b[H", b"\x1b[F", b"\x1b[3~"
with tempfile.TemporaryDirectory(prefix="minishell-history-") as temporary:
    config = Path(temporary) / "flash/minishell"
    config.mkdir(parents=True)
    (config / "setting.txt").write_text("startup=startup-only\n")
    (config / "alias.txt").write_text("h=pwd\n")
    app_dir = Path(temporary) / "apps"
    app_dir.mkdir()
    (app_dir / "input_probe.so").symlink_to(input_probe)
    (app_dir / "hello.so").symlink_to(hello)
    (app_dir / "cat.so").symlink_to(cat)
    (Path(temporary) / "sd").mkdir()
    (Path(temporary) / "sd/partial").write_text("NO-NEWLINE")
    fixtures = Path(temporary) / "flash/ft8"
    fixtures.mkdir()
    (fixtures / "setting.txt").write_text("EXPANDED-FILE\n")
    (fixtures / "RT260925.txt").touch()
    (fixtures / "RT260926.txt").touch()
    (fixtures / "RxTxLog.txt").touch()
    env = dict(os.environ, MINISHELL_ROOT=temporary, MINISHELL_APP_DIR=str(app_dir))
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 20, 0, 0))
    original = termios.tcgetattr(slave)
    process = subprocess.Popen([executable], stdin=slave, stdout=slave, stderr=slave, env=env)
    try:
        read_until(master, b"M$> ", 3)

        def submit(keys, expected=None):
            os.write(master, keys + b"\n")
            output = read_until(master, b"M$> ", 3)
            if expected is not None:
                assert expected in output, output
            return output

        # Startup is excluded, so empty-history Up leaves only the typed command.
        submit(UP + b"first", b"first: command not found")
        submit(UP + LEFT + b"X" + RIGHT + b"\x7f", b"firsX: command not found")
        submit(UP + HOME + DELETE + END + b"Z", b"irsXZ: command not found")
        submit(b"draft" + LEFT + UP + DOWN + b"X", b"drafXt: command not found")
        submit(UP + DOWN + b"empty", b"empty: command not found")
        # Page keys do not navigate history; punctuation is ordinary text.
        submit(b",/;." + b"\x1b[5~\x1b[6~", b"app: ,/;. launch failed")
        submit(b"h", b"/\r\n")
        os.write(master, UP)
        read_until(master, b"\x1b[Kh\r\x1b[5C", 3)  # Recall submitted alias, not expansion.
        submit(b"Z", b"hZ: command not found")
        for i in range(11):
            submit(f"item{i}".encode(), f"item{i}: command not found".encode())
        submit(b"   ")  # Whitespace does not evict item1.
        submit(UP * 15, b"item1: command not found")
        # Navigation clamps, edits never mutate the original entry.
        submit(UP + b"X", b"item1X: command not found")
        submit(UP * 2, b"item1: command not found")
        # Full 255-character payload remains editable at a narrow terminal width.
        long_name = b"a" * 255
        submit(long_name + b"discarded", b"app: " + long_name + b" launch failed")
        submit(UP + HOME + DELETE + b"b" + END, b"app: b" + b"a" * 254 + b" launch failed")
        # Ctrl-C cancels the draft without leaving the terminal raw on process exit.
        submit(b"cancel-me\x03")

        os.write(master, b"run input_probe\n")
        read_until(master, b"input_probe: READY", 3)
        app_mode = termios.tcgetattr(slave)
        assert app_mode[3] & termios.ISIG == original[3] & termios.ISIG
        assert not app_mode[3] & (termios.ICANON | termios.ECHO)
        os.write(master, b"x")
        read_until(master, b"input_probe: PASS", 3)
        # read_until may consume the following prompt; synchronize via shell mode.
        deadline = time.monotonic() + 3
        while termios.tcgetattr(slave)[3] & termios.ISIG and time.monotonic() < deadline:
            time.sleep(.01)
        assert not termios.tcgetattr(slave)[3] & termios.ISIG
        os.write(master, UP)
        read_until(master, b"\x1b[Krun input_probe", 3)  # History survives app return.
        os.write(master, b"\x03")
        read_until(master, b"M$> ", 3)
        # App output may end mid-row, or an app may leave its Display cursor there.
        submit(b"cat /sd/partial", b"NO-NEWLINE\r\nM$> ")
        os.write(master, b"hello\n")
        read_until(master, b"q/Enter to exit", 3)
        os.write(master, b"q")
        read_until(master, b"\r\nM$> ", 3)
        submit(b"pwd", b"/\r\n")
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
        # Observe final editable text before submitting, using eager keystrokes.
        def visible(keys, line):
            os.write(master, keys)
            return read_until(master, b"\x1b[K" + line + b"\r", 3)

        # Let each full paste sit beyond debounce before submission. The final
        # committed line must match exactly, not just an intermediate redraw.
        def pasted(line, output=None):
            visible(line, line)
            time.sleep(.08)
            result = submit(b"", b"\x1b[K" + line + b"\r\n")
            if output is not None:
                assert output in result, result

        pasted(b"cd /flash")
        submit(b"pwd", b"/flash\r\n")
        pasted(b"cat /flash/ft8/setting.txt", b"EXPANDED-FILE")
        pasted(b"cd /flash/ft8")
        pasted(b"cat setting.txt", b"EXPANDED-FILE")
        submit(b"cd")
        # Human typing pauses between characters; /f still expands while idle.
        for byte in b"cd /f":
            os.write(master, bytes([byte]))
            time.sleep(.05)
        read_until(master, b"\x1b[Kcd /flash\r", 3)
        submit(b"")
        submit(b"pwd", b"/flash\r\n")
        visible(b"cd f", b"cd ft8")
        submit(b"")
        submit(b"pwd", b"/flash/ft8\r\n")
        visible(b"cat s", b"cat setting.txt")
        submit(b"", b"EXPANDED-FILE")
        visible(UP, b"cat setting.txt")  # History stores expanded text.
        submit(b"\x03")
        # Pending work cannot expand after navigation, deletion, or Tab.
        for keys in (b"cat RT" + LEFT + RIGHT, b"cat RTX\x7f",
                     b"cat RTX" + LEFT + DELETE, b"cat RT\t"):
            visible(keys, b"cat RT")
            time.sleep(.08)
            submit(b"", b"\x1b[Kcat RT\r\n")
        visible(UP, b"cat RT")
        time.sleep(.08)
        submit(b"", b"\x1b[Kcat RT\r\n")
        visible(b"cat RT", b"cat RT26092")
        visible(b"\x7f", b"cat RT2609")  # Deleting does not re-expand.
        visible(LEFT + RIGHT, b"cat RT2609")
        visible(b"\t" + b"\x7f", b"cat RT260")  # Tab cannot expand or list choices.
        submit(b"\x03")
        visible(b"unknown se", b"unknown se")
        submit(b"\x03")
        visible(b"unknown ./s", b"unknown ./setting.txt")
        submit(b"\x03")
        submit(b"cd")
        os.write(master, b"exit\n")
        assert process.wait(timeout=3) == 0
        assert termios.tcgetattr(slave) == original
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        os.close(master)
        os.close(slave)

    # EOF and an output error must release the shell raw-mode lease too.
    for failure in (False, True):
        master, slave = pty.openpty()
        original = termios.tcgetattr(slave)
        sink = open("/dev/full", "wb") if failure else None
        process = subprocess.Popen([executable], stdin=slave,
                                   stdout=sink if failure else slave, stderr=slave, env=env)
        try:
            deadline = time.monotonic() + 3
            while termios.tcgetattr(slave)[3] & termios.ICANON and time.monotonic() < deadline:
                time.sleep(.01)
            assert not termios.tcgetattr(slave)[3] & termios.ICANON
            os.write(master, b"x" if failure else b"\x04")
            assert process.wait(timeout=3) == 0
            assert termios.tcgetattr(slave) == original
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            if sink:
                sink.close()
            os.close(master)
            os.close(slave)

    result = subprocess.run([executable], input="pwd\nh\ncd /f\npwd\nexit\n", text=True,
                            capture_output=True, env=env, timeout=3)
    assert result.returncode == 0 and "M$> /\n" in result.stdout
    assert "cd: cannot change directory" in result.stdout
    assert "\x1b" not in result.stdout
print("Linux shell history, editing, terminal leases and redirected input: PASS")
