import os
import subprocess
import sys

executable, app_dir = sys.argv[1:3]
environment = os.environ.copy()
environment["MINISHELL_APP_DIR"] = app_dir

process = subprocess.run(
    [executable],
    input="status\napps\nrun hello\nhello\nexit\n",
    text=True,
    capture_output=True,
    env=environment,
)

output = process.stdout + process.stderr
required = [
    "MiniShell",
    "M$> ",
    "platform : linux",
    "system   : ready",
    "memory   : ready",
    "fs       : ready",
    "time     : ready",
    "display  : ready",
    "input    : ready",
    "hello",
    "Hello from MiniShell.",
]
missing = [item for item in required if item not in output]

if process.returncode != 0 or missing or output.count("Hello from MiniShell.") != 2:
    print(output)
    print("returncode=", process.returncode, "missing=", missing)
    sys.exit(1)

print(output)
