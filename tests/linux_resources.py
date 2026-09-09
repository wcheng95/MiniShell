#!/usr/bin/env python3

import os
import subprocess
import sys
import tempfile


def run_shell(executable: str, app_dir: str, root: str, commands: str, extra_env=None):
    env = os.environ.copy()
    env["MINISHELL_APP_DIR"] = app_dir
    env["MINISHELL_ROOT"] = root
    if extra_env:
        env.update(extra_env)
    return subprocess.run(
        [executable],
        input=commands,
        text=True,
        capture_output=True,
        env=env,
    )


def main() -> int:
    if len(sys.argv) != 4:
        print("usage: linux_resources.py <minishell> <runtime-app-dir> <test-app-dir>", file=sys.stderr)
        return 2

    minishell = os.path.abspath(sys.argv[1])
    runtime_apps = os.path.abspath(sys.argv[2])
    test_apps = os.path.abspath(sys.argv[3])

    with tempfile.TemporaryDirectory(prefix="minishell-resource-") as root:
        probe = run_shell(
            minishell,
            test_apps,
            root,
            "resource_probe\nexit\n",
            {
                "MINISHELL_MEMORY_LIMIT": "4K",
                "MINISHELL_STORAGE_LIMIT": "4K",
            },
        )
        probe_output = probe.stdout + probe.stderr
        if probe.returncode != 0 or "resource_probe: PASS" not in probe_output:
            print(probe_output)
            return 1

    with tempfile.TemporaryDirectory(prefix="minishell-tools-") as root:
        tools = run_shell(
            minishell,
            runtime_apps,
            root,
            "free\ndf\ndate 2040-01-02 03:04:05\ndate\nexit\n",
            {
                "MINISHELL_MEMORY_LIMIT": "8M",
                "MINISHELL_STORAGE_LIMIT": "64K",
            },
        )
        tools_output = tools.stdout + tools.stderr
        required = [
            "app used 0B (0 allocs)",
            "heap free 8.0M",
            "total 64.0K",
            "2040-01-02 03:04:05 UTC",
        ]
        missing = [item for item in required if item not in tools_output]
        if tools.returncode != 0 or missing or tools_output.count("2040-01-02 03:04:05 UTC") != 2:
            print(tools_output)
            print("missing=", missing)
            return 1

        restarted = run_shell(
            minishell,
            runtime_apps,
            root,
            "date\nexit\n",
            {
                "MINISHELL_MEMORY_LIMIT": "8M",
                "MINISHELL_STORAGE_LIMIT": "64K",
            },
        )
        restarted_output = restarted.stdout + restarted.stderr
        if restarted.returncode != 0 or "2040-01-02" in restarted_output or " UTC" not in restarted_output:
            print(restarted_output)
            print("date correction incorrectly persisted")
            return 1

        print(probe_output)
        print(tools_output)
        print(restarted_output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
