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

# FQ-0 local dependency graph. New MiniFT8 modules are intentionally required
# to opt in here so their ownership/dependency direction is reviewed explicitly.
ALLOWED_LOCAL_DEPS: dict[str, set[str]] = {
    "main": {"main", "shared", "app_controller", "presentation_profile", "ui_shell"},
    "shared": {"shared"},
    "tools": {"tools", "ft8_engine"},
    "app_controller": {
        "app_controller",
        "shared",
        "auto_seq",
        "config_service",
        "storage_service",
        "tx_lifecycle",
        "rx_audio_adapter",
        "rx_frontend",
        "rx_slot_framer",
        "rx_result_builder",
        "ft8_engine",
    },
    "auto_seq": {"auto_seq"},
    "config_service": {"config_service"},
    "presentation_profile": {"presentation_profile"},
    "storage_service": {"storage_service"},
    "tx_lifecycle": {"tx_lifecycle"},
    "ui_shell": {"ui_shell", "shared", "presentation_profile"},
    "rx_audio_adapter": {"rx_audio_adapter"},
    "rx_frontend": {"rx_frontend"},
    "rx_slot_framer": {"rx_slot_framer"},
    # Factual result projection consumes typed protocol output from Ft8Engine.
    "rx_result_builder": {"rx_result_builder", "ft8_engine"},
    "ft8_engine": {"ft8_engine"},
}

# These are the only current MiniFT8 layers allowed to see a MiniShell service
# contract directly. Pure policy/DSP modules must remain MiniShell-agnostic.
MINISHELL_API_ALLOWED_MODULES = {
    "main",
    "app_controller",
    "storage_service",
    "rx_audio_adapter",
}


def module_for_path(path: pathlib.Path, ft8_root: pathlib.Path) -> str | None:
    rel = path.relative_to(ft8_root)
    parts = rel.parts
    if not parts:
        return None
    if parts[0] == "main":
        return "main"
    if parts[0] == "include":
        return "shared"
    if parts[0] == "tools":
        return "tools"
    if parts[0] == "src" and len(parts) >= 2:
        return parts[1]
    return None


def build_header_owners(files: list[pathlib.Path], ft8_root: pathlib.Path) -> dict[str, str]:
    owners: dict[str, str] = {}
    ambiguous: set[str] = set()

    for path in files:
        if path.suffix not in {".h", ".hpp"}:
            continue
        owner = module_for_path(path, ft8_root)
        if owner is None:
            continue
        name = path.name
        previous = owners.get(name)
        if previous is not None and previous != owner:
            ambiguous.add(name)
        else:
            owners[name] = owner

    for name in ambiguous:
        owners.pop(name, None)
    return owners


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
    header_owners = build_header_owners(files, ft8_root)

    for path in files:
        rel = path.relative_to(root)
        module = module_for_path(path, ft8_root)
        text = path.read_text(encoding="utf-8")

        if module is None:
            violations.append(f"{rel}: source file is outside a known MiniFT8 module")
            continue
        if module not in ALLOWED_LOCAL_DEPS:
            violations.append(
                f"{rel}: module {module!r} has no FQ-0 dependency rule; review ownership first"
            )
            continue

        for line_number, line in enumerate(text.splitlines(), start=1):
            match = INCLUDE_RE.match(line)
            if match:
                header = match.group(1)
                lower = header.lower()
                if header in FORBIDDEN_INCLUDES or any(
                    lower.startswith(prefix.lower()) for prefix in FORBIDDEN_INCLUDE_PREFIXES
                ):
                    violations.append(f"{rel}:{line_number}: forbidden include {header}")

                if header == "minishell/api.h" and module not in MINISHELL_API_ALLOWED_MODULES:
                    violations.append(
                        f"{rel}:{line_number}: pure module {module} must not include MiniShell API"
                    )

                if header == "app_controller_internal.h" and module != "app_controller":
                    violations.append(
                        f"{rel}:{line_number}: private app_controller state escaped its owner"
                    )

                owner = header_owners.get(pathlib.PurePosixPath(header).name)
                if owner is not None and owner not in ALLOWED_LOCAL_DEPS[module]:
                    violations.append(
                        f"{rel}:{line_number}: forbidden MiniFT8 dependency "
                        f"{module} -> {owner} via {header}"
                    )

            for token in FORBIDDEN_TOKENS:
                if token in line:
                    violations.append(f"{rel}:{line_number}: forbidden platform token {token}")

            if auto_seq_root in path.parents:
                heap_match = AUTO_SEQ_HEAP_CALL_RE.search(line)
                if heap_match:
                    violations.append(
                        f"{rel}:{line_number}: AutoSeq heap call "
                        f"{heap_match.group(0).rstrip('(').strip()}"
                    )

    if violations:
        print("MiniFT8 boundary violations:")
        for violation in violations:
            print(f"  {violation}")
        return 1

    print(
        "ft8_platform_boundary: PASS "
        f"({len(files)} source/header files; platform-clean; dependency-clean; AutoSeq heap-free)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
