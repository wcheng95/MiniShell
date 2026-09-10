#!/usr/bin/env python3

from __future__ import annotations

import pathlib
import re
import sys

SOURCE_SUFFIXES = {".c", ".h", ".cc", ".cpp", ".hpp"}
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')
AUTO_SEQ_HEAP_CALL_RE = re.compile(
    r"\b(?:malloc|calloc|realloc|aligned_alloc|free|strdup|asprintf)\s*\("
)

FORBIDDEN_INCLUDE_PREFIXES = (
    "platform/",
    "linux/",
    "freertos/",
    "driver/",
    "soc/",
    "hal/",
    "esp_private/",
    "esp_",
    "m5",
)

FORBIDDEN_INCLUDES = {
    "platform_backend.h",
    "adv_internal.h",
    "sdkconfig.h",
    "Arduino.h",
    "unistd.h",
    "fcntl.h",
    "dirent.h",
    "pthread.h",
    "dlfcn.h",
}

FORBIDDEN_TOKENS = (
    "ESP_PLATFORM",
    "CONFIG_IDF_TARGET",
    "__linux__",
    "M5Unified",
    "M5Cardputer",
)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: ft8_platform_boundary.py <source-root>", file=sys.stderr)
        return 2

    root = pathlib.Path(sys.argv[1]).resolve()
    ft8_root = root / "apps" / "ft8"
    auto_seq_root = ft8_root / "src" / "auto_seq"
    if not ft8_root.is_dir():
        print(f"missing MiniFT8 source tree: {ft8_root}", file=sys.stderr)
        return 2

    violations: list[str] = []
    files = sorted(
        path for path in ft8_root.rglob("*")
        if path.is_file() and path.suffix in SOURCE_SUFFIXES
    )

    for path in files:
        rel = path.relative_to(root)
        text = path.read_text(encoding="utf-8")
        for line_number, line in enumerate(text.splitlines(), start=1):
            match = INCLUDE_RE.match(line)
            if match:
                header = match.group(1)
                lower = header.lower()
                if header in FORBIDDEN_INCLUDES or any(
                    lower.startswith(prefix.lower()) for prefix in FORBIDDEN_INCLUDE_PREFIXES
                ):
                    violations.append(f"{rel}:{line_number}: forbidden include {header}")

            for token in FORBIDDEN_TOKENS:
                if token in line:
                    violations.append(f"{rel}:{line_number}: forbidden platform token {token}")

            if auto_seq_root in path.parents:
                heap_match = AUTO_SEQ_HEAP_CALL_RE.search(line)
                if heap_match:
                    violations.append(
                        f"{rel}:{line_number}: AutoSeq heap call {heap_match.group(0).rstrip('(').strip()}"
                    )

    if violations:
        print("MiniFT8 platform-boundary violations:")
        for violation in violations:
            print(f"  {violation}")
        return 1

    print(f"ft8_platform_boundary: PASS ({len(files)} source/header files; AutoSeq heap-free)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
