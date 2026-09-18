#!/usr/bin/env python3
"""CAT command literals belong to applications, never the raw transport."""
from pathlib import Path
import re
import sys
from app_dependency_boundary import strip_comments

COMMAND = re.compile(r'"(?:MD6;|FR0;|FT0;|FA(?:%|[0-9])|TX;|RX;|TA(?:%|[0-9])|TM(?:%|[0-9]))')


def check_transport(root):
    errors = []
    for directory in (root / "core", root / "platform/linux"):
        for path in directory.rglob("*"):
            if path.suffix not in {".c", ".h", ".cpp"}:
                continue
            if COMMAND.search(strip_comments(path.read_text())):
                errors.append(f"{path.relative_to(root)}: CAT command below application boundary")
    return errors


if __name__ == "__main__":
    for command in ('MD6;', 'FR0;', 'FT0;', 'FA%011u;', 'TX;', 'RX;', 'TA%04d;', 'TM%02d;'):
        assert COMMAND.search('const char *command = "' + command + '";')
    assert not COMMAND.search('const char *message = "Serial read failed";')
    errors = check_transport(Path(sys.argv[1]).resolve())
    if errors:
        raise SystemExit("\n".join(errors))
    print("Serial transport / application CAT boundary: PASS")
