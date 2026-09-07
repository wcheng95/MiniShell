#!/usr/bin/env python3

import os
import subprocess
import sys
import tempfile


def run_minishell(minishell: str, app_dir: str, root: str, commands: str) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    env["MINISHELL_APP_DIR"] = app_dir
    env["MINISHELL_ROOT"] = root
    return subprocess.run(
        [minishell],
        input=commands,
        text=True,
        capture_output=True,
        env=env,
    )


def main() -> int:
    if len(sys.argv) != 4:
        print("usage: linux_directory.py <minishell> <runtime-app-dir> <test-app-dir>", file=sys.stderr)
        return 2

    minishell = os.path.abspath(sys.argv[1])
    runtime_app_dir = os.path.abspath(sys.argv[2])
    test_app_dir = os.path.abspath(sys.argv[3])

    with tempfile.TemporaryDirectory(prefix="minishell-dir-") as root:
        sd = os.path.join(root, "sd")
        os.makedirs(os.path.join(sd, "folder"), exist_ok=True)
        with open(os.path.join(sd, "alpha.txt"), "w", encoding="utf-8") as handle:
            handle.write("alpha\n")
        with open(os.path.join(sd, ".secret"), "w", encoding="utf-8") as handle:
            handle.write("hidden\n")

        probe = run_minishell(
            minishell,
            test_app_dir,
            root,
            "dir_probe /sd\nexit\n",
        )
        probe_output = probe.stdout + probe.stderr
        required_probe = ["F alpha.txt", "F .secret", "D folder", "dir_probe: PASS"]
        if probe.returncode != 0 or any(text not in probe_output for text in required_probe):
            print(probe_output)
            return 1
        if "F .\n" in probe_output or "D .\n" in probe_output or "F ..\n" in probe_output or "D ..\n" in probe_output:
            print("dot entries leaked through Filesystem ABI")
            print(probe_output)
            return 1

        root_listing = run_minishell(
            minishell,
            runtime_app_dir,
            root,
            "ls\nexit\n",
        )
        root_output = root_listing.stdout + root_listing.stderr
        if (root_listing.returncode != 0 or
                "/sd\n" not in root_output or "/flash\n" not in root_output or
                "/sd/\n" in root_output or "/flash/\n" in root_output):
            print("ls root entries should be /sd and /flash")
            print(root_output)
            return 1

        listing = run_minishell(
            minishell,
            runtime_app_dir,
            root,
            "ls /sd\nexit\n",
        )
        listing_output = listing.stdout + listing.stderr
        if listing.returncode != 0 or "alpha.txt" not in listing_output or "folder/" not in listing_output:
            print(listing_output)
            return 1
        if ".secret" in listing_output:
            print("ls should hide dot files by default")
            print(listing_output)
            return 1

        print(probe_output)
        print(root_output)
        print(listing_output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
