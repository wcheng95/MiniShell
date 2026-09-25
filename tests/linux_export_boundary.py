#!/usr/bin/env python3

import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


def dynamic_symbols(path, selection):
    output = subprocess.check_output(
        ["nm", "-D", selection, str(path)], text=True
    )
    return {line.split()[-1].split("@")[0] for line in output.splitlines() if line.strip()}


def main():
    if len(sys.argv) != 4:
        print("usage: linux_export_boundary.py <minishell> <private-probe> <hello>")
        return 2
    minishell, private_probe, hello = (Path(arg).resolve() for arg in sys.argv[1:])
    for path in (minishell, private_probe, hello):
        if not path.is_file():
            raise AssertionError(f"missing runtime fixture: {path}")

    private_symbol = "filesystem_handles_reset"
    exports = dynamic_symbols(minishell, "--defined-only")
    assert "mini_api_get" in exports, "public entry point is not exported"
    assert private_symbol not in exports, "private helper is exported"
    private_family = re.compile(
        r"^(filesystem_handles_|filesystem_path_|filesystem_quota_|"
        r"minishell_filesystem_cwd_|minishell_services_|minishell_.*_service_|linux_)"
    )
    leaked = sorted(symbol for symbol in exports if private_family.match(symbol))
    assert not leaked, f"private dynamic exports: {leaked}"
    assert private_symbol in dynamic_symbols(private_probe, "--undefined-only")
    assert "mini_api_get" in dynamic_symbols(hello, "--undefined-only")

    with tempfile.TemporaryDirectory(prefix="minishell-export-") as root:
        app_dir = Path(root) / "apps"
        app_dir.mkdir()
        shutil.copyfile(private_probe, app_dir / "private_import_probe.so")
        shutil.copyfile(hello, app_dir / "hello.so")
        env = os.environ.copy()
        env["MINISHELL_ROOT"] = root
        env["MINISHELL_APP_DIR"] = str(app_dir)
        env["LC_ALL"] = "C"
        process = subprocess.run(
            [str(minishell)], input="run private_import_probe\nhello\nq",
            text=True, capture_output=True, env=env, timeout=10,
        )
        output = process.stdout + process.stderr
        assert process.returncode == 0, output
        assert re.search(
            r"app: dlopen [^\n]*private_import_probe\.so: [^\n]*"
            r"undefined symbol: filesystem_handles_reset(?:\s|$)", output
        ), output
        assert "app: private_import_probe launch failed" in output, output
        assert output.count("Hello from MiniShell.") == 1, output
        assert "q/Enter to exit" in output, output
        assert "app: hello" not in output, output

    print("linux export boundary: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
