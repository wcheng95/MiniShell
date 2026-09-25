"""Exercise boot settings through the actual Linux runtime, Filesystem and apps."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

executable, app_dir = map(os.path.abspath, sys.argv[1:3])
with tempfile.TemporaryDirectory(prefix="minishell-startup-") as temporary:
    root = Path(temporary)
    config = root / "flash/minishell"
    config.mkdir(parents=True)
    (root / "sd").mkdir()
    env = dict(os.environ, MINISHELL_ROOT=temporary, MINISHELL_APP_DIR=app_dir)

    def run():
        result = subprocess.run([executable], input="help\nexit\n", text=True,
                                capture_output=True, env=env, timeout=15)
        assert result.returncode == 0, result.stdout + result.stderr
        return result.stdout + result.stderr

    assert run().count("M$> ") == 2  # Missing file is normal.
    (root / "sd/source").write_text("STARTUP-CONTENT\n")
    (config / "alias.txt").write_text("copy=cp /sd/source /sd/copied\nshow=cat /sd/copied\n")
    setting = config / "setting.txt"
    setting.write_text("SSID=MiniShell\nPW=12345678\nbrightness=50\n"
                       "startup=;copy;;missing;show;exit;status;\n")
    output = run()
    startup, prompt = output.split("M$> ", 1)
    assert startup.count("STARTUP-CONTENT") == 1 and "STARTUP-CONTENT" not in prompt, output
    assert "missing: command not found" in startup and "platform : linux" in startup, output
    assert output.count("M$> ") == 2 and "show this help" in prompt, output
    assert (root / "sd/copied").read_text() == "STARTUP-CONTENT\n"
    setting.write_text("startup=missing\nstartup=\nbrightness=0\n")
    assert "missing:" not in run()
    setting.write_text("startup=missing\n" + "#" * 1024)
    assert "missing:" not in run()  # Never execute an oversized file's prefix.
print("Linux resident startup: PASS")
