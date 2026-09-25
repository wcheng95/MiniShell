"""Real shell/startup CWD inheritance through unchanged portable FS consumers."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

executable, app_dir = map(os.path.abspath, sys.argv[1:3])
with tempfile.TemporaryDirectory(prefix="minishell-cwd-") as temporary:
    root = Path(temporary)
    directory = root / "flash/ft8"
    directory.mkdir(parents=True)
    config = root / "flash/minishell"
    config.mkdir()
    (root / "sd").mkdir()
    (directory / "setting.txt").write_text("CWD-CONTENT\n")
    (config / "alias.txt").write_text("cd=missing\npwd=missing\nhere=pwd\ngo=cd /flash/ft8\n")
    env = dict(os.environ, MINISHELL_ROOT=temporary, MINISHELL_APP_DIR=app_dir)

    def run(commands):
        result = subprocess.run([executable], input="\n".join(commands)+"\n", text=True,
                                capture_output=True, env=env, timeout=15)
        assert result.returncode == 0, result.stdout + result.stderr
        return result.stdout + result.stderr

    commands = ["pwd", "cd /flash/ft8", "pwd", "cat setting.txt", "pwd", "cd .", "pwd",
                "cd ..", "pwd", "go", "here", "cd missing", "pwd", "cd setting.txt", "pwd",
                "cd", "cd one two", "pwd extra", "pwd", "ls .", "mkdir tmp",
                "cp setting.txt tmp/copy.txt", "mv tmp/copy.txt ./renamed.txt", "cat renamed.txt",
                "cp renamed.txt /sd/export.txt", "rm renamed.txt", "rmdir tmp", "pwd", "exit"]
    output = run(commands)
    segments = output.split("M$> ")[1:]
    assert len(segments) == len(commands), output
    expected = {0:"/", 2:"/flash/ft8", 4:"/flash/ft8", 6:"/flash/ft8", 8:"/flash",
                10:"/flash/ft8", 12:"/flash/ft8", 14:"/flash/ft8", 18:"/flash/ft8", 27:"/flash/ft8"}
    for index, path in expected.items():
        assert segments[index] == path+"\n", (index, output)
    assert output.count("cd: cannot change directory") == 2, output
    assert output.count("usage: cd <path>") == 2 and output.count("usage: pwd") == 1, output
    assert "missing: command not found" not in output, output
    assert output.count("CWD-CONTENT") == 2 and "setting.txt\n" in segments[19], output
    assert (root / "sd/export.txt").read_text() == "CWD-CONTENT\n"
    assert not (directory / "renamed.txt").exists() and not (directory / "tmp").exists()
    assert run(["pwd", "exit"]).split("M$> ")[1] == "/\n"  # New session resets.

    (config / "setting.txt").write_text("startup=cd /flash/ft8;cat setting.txt;pwd\n")
    output = run(["pwd", "cat ./setting.txt", "pwd", "exit"])
    startup, *prompts = output.split("M$> ")
    assert "CWD-CONTENT\n/flash/ft8\n" in startup, output
    assert prompts[0] == "/flash/ft8\n" and prompts[2] == "/flash/ft8\n", output
    assert output.count("CWD-CONTENT") == 2, output
print("Linux CWD, startup, aliases and relative portable app paths: PASS")
