import os
import subprocess
import sys

executable, app_dir = sys.argv[1:3]
environment = os.environ.copy()
environment["MINISHELL_APP_DIR"] = app_dir

# hello is a normal foreground Display/Input application. Run it last so its
# blocking input lifecycle can be exercised without feeding later shell
# commands through the application's input stream.
process = subprocess.run(
    [executable],
    input="status\napps\nhello\nq",
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
    "q/Enter to exit",
]
missing = [item for item in required if item not in output]

if process.returncode != 0 or missing or output.count("Hello from MiniShell.") != 1:
    print(output)
    print("returncode=", process.returncode, "missing=", missing)
    sys.exit(1)

print(output)
