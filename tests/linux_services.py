#!/usr/bin/env python3

import os
import subprocess
import sys
import tempfile


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: linux_services.py <minishell> <app-dir>", file=sys.stderr)
        return 2

    minishell = os.path.abspath(sys.argv[1])
    app_dir = os.path.abspath(sys.argv[2])

    with tempfile.TemporaryDirectory(prefix="minishell-services-") as root:
        env = os.environ.copy()
        env["MINISHELL_APP_DIR"] = app_dir
        env["MINISHELL_ROOT"] = root

        process = subprocess.run(
            [minishell],
            input="run service_probe\nrun service_probe\nexit\n",
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            env=env,
            timeout=10,
            check=False,
        )

        output = process.stdout
        if process.returncode != 0:
            print(output, end="")
            return 1
        if output.count("service_probe: PASS") != 2:
            print(output, end="")
            return 1
        if "service_probe: FAIL" in output:
            print(output, end="")
            return 1

        expected_dirs = ["sd", "flash", ".state"]
        for name in expected_dirs:
            if not os.path.isdir(os.path.join(root, name)):
                print(output, end="")
                print(f"missing logical root directory: {name}")
                return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
