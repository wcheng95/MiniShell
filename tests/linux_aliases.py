#!/usr/bin/env python3
"""Exercise resident aliases through real Linux shell, FS and portable apps."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

executable, app_dir = map(os.path.abspath, sys.argv[1:3])
with tempfile.TemporaryDirectory(prefix="minishell-aliases-") as temporary:
    root = Path(temporary)
    (root / "sd").mkdir()
    env = dict(os.environ, MINISHELL_ROOT=temporary, MINISHELL_APP_DIR=app_dir)
    def run(commands):
        result = subprocess.run([executable], input="\n".join(commands)+"\n", text=True,
                                capture_output=True, env=env, timeout=15)
        assert result.returncode == 0, result.stdout + result.stderr
        return result.stdout + result.stderr
    alias = root / "flash/minishell/alias.txt"
    output = run(["help", "missing", "exit"])
    assert "show this help" in output and "missing: command not found" in output
    assert "alias: cannot" not in output and not alias.exists()
    alias.parent.mkdir(parents=True, exist_ok=True)
    (root / "sd/source.txt").write_text("SOURCE-CONTENT\n")
    (root / "sd/a=b=c.txt").write_text("EQUALS-CONTENT\n")
    (root / "sd/new.txt").write_text("RELOADED-CONTENT\n")
    (root / "sd/aliases.next").write_text("live=cat /sd/new.txt\n")
    long_rhs = "cp /sd/source.txt ".ljust(253)
    alias.write_text("\n # comment\ninvalid\n=apps\nwrong name=help\nempty=\n"
                     "h=help\na=apps\nx=cat /sd/a=b=c.txt\n"
                     "copy=cp /sd/source.txt\nshow=cat /sd/source.txt\n"
                     "dup=missing\ndup=cat /sd/source.txt\n"
                     "recursive=other\nother=apps\n"
                     "help=missing\nstatus=missing\napps=missing\nrun=missing\nexit=missing\n"
                     "long=" + "a"*800 + "\n"
                     "live=cat /sd/source.txt\nz=" + long_rhs + "\n"
                     "last=cat /sd/source.txt")
    assert "never-run" not in run(["exit", "never-run"])
    output = run(["h", "a", "help", "status", "apps", "show", "copy /sd/copied.txt",
                  "cat /sd/copied.txt", "x", "dup", "recursive", "run h", "long",
                  "z /sd/should-not-exist", "last", "live",
                  "cp /sd/aliases.next /flash/minishell/alias.txt", "live", "exit", "never-run"])
    assert output.count("show this help") == 2, output
    assert output.count("platform : linux") == 1, output
    assert output.count("SOURCE-CONTENT") == 5, output
    assert output.count("EQUALS-CONTENT") == 1 and output.count("RELOADED-CONTENT") == 1, output
    assert "other: command not found" in output and "run: h not found" in output, output
    assert "long: command not found" in output, output
    assert output.count("alias: expansion too long") == 1, output
    assert "missing: command not found" not in output and "never-run" not in output, output
    assert "ft8\n" in output and "cat\n" in output, output
    assert not (root / "sd/should-not-exist").exists()
    assert (root / "sd/copied.txt").read_text() == "SOURCE-CONTENT\n"
print("Linux aliases: builtin/app dispatch, defaults, equals, last-wins, one expansion, overflow and live reload PASS")
