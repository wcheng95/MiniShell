#!/usr/bin/env python3
"""QMX CAT command literals belong only to MiniFT8's radio_qmx adapter."""
import os
from pathlib import Path
import re
import sys
import tempfile
from app_dependency_boundary import strip_comments

COMMAND = re.compile(r'"(?:MD6;|FR0;|FT0;|FA(?:%|[0-9])|TX;|RX;|TA(?:%|[0-9])|TM(?:%|[0-9]))')


def check_transport(root):
    errors = []
    allowed = root / "apps/ft8/src/radio_control/radio_qmx.c"
    for directory in (root / "core", root / "platform", root / "apps/ft8"):
        for parent, dirs, files in os.walk(directory):
            dirs[:] = [d for d in dirs if not d.startswith("build") and
                       d not in {"managed_components", "__pycache__"}]
            for name in files:
                path = Path(parent) / name
                if path == allowed or path.suffix not in {".c", ".h", ".cc", ".cpp", ".hpp"}:
                    continue
                if COMMAND.search(strip_comments(path.read_text())):
                    errors.append(f"{path.relative_to(root)}: CAT command outside radio_qmx")
    return errors


def self_test():
    for command in ('MD6;', 'FR0;', 'FT0;', 'FA%011u;', 'TX;', 'RX;', 'TA%04d;', 'TM%02d;'):
        assert COMMAND.search('const char *command = "' + command + '";')
    assert not COMMAND.search('const char *message = "Serial read failed";')
    with tempfile.TemporaryDirectory(prefix="cat-boundary-") as temp:
        root = Path(temp)
        adapter = root / "apps/ft8/src/radio_control/radio_qmx.c"
        adapter.parent.mkdir(parents=True)
        adapter.write_text('const char *command = "TX;";')
        assert not check_transport(root)
        for rel in ("core/serial.c", "platform/linux/serial.c", "platform/adv/serial.cpp",
                    "apps/ft8/src/app_controller/controller.c",
                    "apps/ft8/src/app_controller/app_controller_tx_physical.c",
                    "apps/ft8/src/app_controller/app_tx_schedule.c",
                    "apps/ft8/src/log_service/log_service.c", "apps/ft8/src/radio_control/control.c"):
            source = root / rel
            source.parent.mkdir(parents=True, exist_ok=True)
            for command in ('TX;', 'RX;', 'TA%04d.%02d;'):
                source.write_text('const char *command = "' + command + '";')
                assert len(check_transport(root)) == 1, (rel, command)
            source.write_text('// "TX;"\n/* "RX;" */\nconst char *message = "Serial";')
            assert not check_transport(root)


if __name__ == "__main__":
    self_test()
    errors = check_transport(Path(sys.argv[1]).resolve())
    if errors:
        raise SystemExit("\n".join(errors))
    print("Serial transport / radio_qmx CAT boundary and self-tests: PASS")
