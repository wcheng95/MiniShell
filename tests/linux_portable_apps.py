#!/usr/bin/env python3

import os
import subprocess
import sys
import tempfile


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: linux_portable_apps.py <minishell> <app-dir>", file=sys.stderr)
        return 2

    minishell = os.path.abspath(sys.argv[1])
    app_dir = os.path.abspath(sys.argv[2])
    payload = "MiniShell portable apps\n"

    with tempfile.TemporaryDirectory(prefix="minishell-apps-") as root:
        sd = os.path.join(root, "sd")
        os.makedirs(sd, exist_ok=True)
        source = os.path.join(sd, "source.txt")
        with open(source, "w", encoding="utf-8") as handle:
            handle.write(payload)

        env = os.environ.copy()
        env["MINISHELL_APP_DIR"] = app_dir
        env["MINISHELL_ROOT"] = root

        commands = "\n".join(
            [
                "apps",
                "cat /sd/source.txt",
                "cp /sd/source.txt /sd/copy.txt",
                "cat /sd/copy.txt",
                "mkdir /sd/work",
                "mv /sd/copy.txt /sd/work/moved.txt",
                "cat /sd/work/moved.txt",
                "rm /sd/work/moved.txt",
                "rmdir /sd/work",
                "exit",
                "",
            ]
        )

        process = subprocess.run(
            [minishell],
            input=commands,
            text=True,
            capture_output=True,
            env=env,
        )

        output = process.stdout + process.stderr
        required_apps = ["cat", "cp", "mkdir", "mv", "nano", "rm", "rmdir"]
        missing = [name for name in required_apps if f"{name}\n" not in output]

        if process.returncode != 0 or missing or output.count(payload) != 3:
            print(output)
            print(
                "returncode=",
                process.returncode,
                "missing_apps=",
                missing,
                "payload_count=",
                output.count(payload),
            )
            return 1

        if os.path.exists(os.path.join(sd, "copy.txt")):
            print("copy.txt still exists after mv/rm")
            return 1
        if os.path.exists(os.path.join(sd, "work")):
            print("work directory still exists after rmdir")
            return 1
        with open(source, "r", encoding="utf-8") as handle:
            if handle.read() != payload:
                print("source file was modified")
                return 1

        print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
