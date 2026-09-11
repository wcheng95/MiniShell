#!/usr/bin/env python3

from __future__ import annotations

import pathlib
import re
import sys
import tempfile

SOURCE_SUFFIXES = {".c", ".h", ".cc", ".cpp", ".hpp"}
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*"([^"]+)"')

# Application-local dependency rules. Add another entry when a new application
# needs the same architectural enforcement; keep the checker itself generic.
APP_RULES = {
    "ft8": {
        "enforced_roots": {"main", "include", "src"},
        "module_paths": {
            "main": ("main",),
            "shared": ("include",),
            "app_controller": ("src/app_controller",),
            "auto_seq": ("src/auto_seq",),
            "config_service": ("src/config_service",),
            "presentation_profile": ("src/presentation_profile",),
            "rx_audio_adapter": ("src/rx_audio_adapter",),
            "rx_frontend": ("src/rx_frontend",),
            "rx_result_builder": ("src/rx_result_builder",),
            "rx_slot_framer": ("src/rx_slot_framer",),
            "storage_service": ("src/storage_service",),
            "tx_lifecycle": ("src/tx_lifecycle",),
            "ui_shell": ("src/ui_shell",),
            "ft8_engine": ("src/ft8_engine",),
        },
        "private_headers": {
            "src/app_controller/app_controller_internal.h": "app_controller",
        },
        "forbidden_source_patterns": {
            "main/ft8_main.c": (
                (r"\bui\s*\.\s*(?:screen|submenu)\b",
                 "ft8_main must not inspect UiShell screen/submenu state"),
                (r"\b(?:SCREEN_|UI_SUBMENU_)",
                 "ft8_main must not encode UIScreen/submenu policy"),
                (r"\bmodel\s*\.\s*[A-Za-z_]",
                 "ft8_main must not inspect individual UiModel fields"),
            ),
        },
        "allowed": {
            "main": {"main", "shared", "app_controller", "presentation_profile", "ui_shell"},
            "shared": {"shared"},
            "app_controller": {
                "app_controller", "shared", "auto_seq", "config_service",
                "presentation_profile", "rx_audio_adapter", "rx_frontend",
                "rx_result_builder", "rx_slot_framer", "storage_service",
                "tx_lifecycle", "ui_shell", "ft8_engine",
            },
            "auto_seq": {"auto_seq"},
            "config_service": {"config_service"},
            "presentation_profile": {"presentation_profile"},
            "rx_audio_adapter": {"rx_audio_adapter"},
            "rx_frontend": {"rx_frontend"},
            "rx_result_builder": {"rx_result_builder", "ft8_engine"},
            "rx_slot_framer": {"rx_slot_framer"},
            "storage_service": {"storage_service"},
            "tx_lifecycle": {"tx_lifecycle"},
            "ui_shell": {"ui_shell", "shared", "presentation_profile"},
            "ft8_engine": {"ft8_engine"},
        },
    },
    "keyer": {
        "enforced_roots": {"main", "include", "src"},
        "module_paths": {
            "main": ("main",),
            "shared": ("include",),
            "app_controller": ("src/app_controller",),
            "config_service": ("src/config_service",),
            "keyer_engine": ("src/keyer_engine",),
            "keyin": ("src/keyin",),
            "keyout": ("src/keyout",),
            "sidetone": ("src/sidetone",),
        },
        "private_headers": {},
        "forbidden_source_patterns": {},
        "allowed": {
            "main": {"main", "app_controller"},
            # keyer_types currently reuses the portable engine's paddle-mode
            # type. This is a type dependency only; orchestration remains in
            # app_controller.
            "shared": {"shared", "keyer_engine"},
            "app_controller": {
                "app_controller", "shared", "config_service",
                "keyer_engine", "keyin", "keyout", "sidetone",
            },
            "config_service": {"config_service", "shared"},
            "keyer_engine": {"keyer_engine"},
            "keyin": {"keyin", "shared"},
            "keyout": {"keyout", "shared"},
            "sidetone": {"sidetone"},
        },
    },
}


def module_for_path(app_root: pathlib.Path, path: pathlib.Path, rule: dict) -> str | None:
    rel = path.relative_to(app_root).as_posix()
    matches: list[tuple[int, str]] = []
    for module, prefixes in rule["module_paths"].items():
        for prefix in prefixes:
            prefix = prefix.rstrip("/")
            if rel == prefix or rel.startswith(prefix + "/"):
                matches.append((len(prefix), module))
    if not matches:
        return None
    matches.sort(reverse=True)
    return matches[0][1]


def build_header_index(app_root: pathlib.Path) -> dict[str, list[pathlib.Path]]:
    index: dict[str, list[pathlib.Path]] = {}
    for path in app_root.rglob("*.h"):
        if not path.is_file():
            continue
        rel = path.relative_to(app_root).as_posix()
        index.setdefault(rel, []).append(path)
        index.setdefault(path.name, []).append(path)
        if rel.startswith("include/"):
            index.setdefault(rel[len("include/"):], []).append(path)
    return index


def resolve_local_header(include: str,
                         index: dict[str, list[pathlib.Path]]) -> pathlib.Path | None:
    candidates = index.get(include, [])
    unique = {path.resolve() for path in candidates}
    if len(unique) == 1:
        return next(iter(unique))
    if len(unique) > 1:
        raise ValueError(f"ambiguous local include {include}")
    return None


def check_app(root: pathlib.Path, app_name: str) -> list[str]:
    if app_name not in APP_RULES:
        return [f"unknown app rule set: {app_name}"]

    app_root = root / "apps" / app_name
    if not app_root.is_dir():
        return [f"missing application source tree: {app_root}"]

    rule = APP_RULES[app_name]
    header_index = build_header_index(app_root)
    violations: list[str] = []
    checked_files = 0

    for path in sorted(app_root.rglob("*")):
        if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
            continue

        rel = path.relative_to(app_root)
        src_module = module_for_path(app_root, path, rule)
        if src_module is None:
            if rel.parts and rel.parts[0] in rule["enforced_roots"]:
                violations.append(
                    f"{path.relative_to(root)}: source/header has no module owner"
                )
            continue
        checked_files += 1

        text = path.read_text(encoding="utf-8")
        rel_text = rel.as_posix()
        for pattern, reason in rule.get("forbidden_source_patterns", {}).get(rel_text, ()):
            if re.search(pattern, text):
                violations.append(
                    f"{path.relative_to(root)}: forbidden lifecycle coupling: {reason}"
                )

        for line_number, line in enumerate(text.splitlines(), start=1):
            match = INCLUDE_RE.match(line)
            if not match:
                continue

            include = match.group(1)
            try:
                target = resolve_local_header(include, header_index)
            except ValueError as exc:
                violations.append(
                    f"{path.relative_to(root)}:{line_number}: {exc}"
                )
                continue

            if target is None:
                continue

            dst_module = module_for_path(app_root, target, rule)
            if dst_module is None:
                violations.append(
                    f"{path.relative_to(root)}:{line_number}: local include {include} "
                    f"has no module owner"
                )
                continue

            target_rel = target.relative_to(app_root).as_posix()
            private_owner = rule.get("private_headers", {}).get(target_rel)
            if private_owner is not None and src_module != private_owner:
                violations.append(
                    f"{path.relative_to(root)}:{line_number}: private header {include} "
                    f"is owned by {private_owner}, not {src_module}"
                )
                continue

            allowed = rule["allowed"].get(src_module, {src_module})
            if dst_module not in allowed:
                violations.append(
                    f"{path.relative_to(root)}:{line_number}: forbidden dependency "
                    f"{src_module} -> {dst_module} via {include}"
                )

    if checked_files == 0:
        violations.append(f"no source files checked for app {app_name}")
    return violations


def self_test() -> int:
    with tempfile.TemporaryDirectory() as temp:
        root = pathlib.Path(temp)
        app = root / "apps" / "ft8"
        (app / "main").mkdir(parents=True)
        (app / "src" / "app_controller").mkdir(parents=True)
        (app / "src" / "config_service").mkdir(parents=True)
        (app / "src" / "ui_shell").mkdir(parents=True)
        (app / "include" / "ft8").mkdir(parents=True)

        (app / "include" / "ft8" / "app_types.h").write_text(
            "#pragma once\n", encoding="utf-8"
        )
        (app / "src" / "app_controller" / "app_controller_internal.h").write_text(
            "#pragma once\n", encoding="utf-8"
        )
        (app / "src" / "config_service" / "config_service.h").write_text(
            "#pragma once\n", encoding="utf-8"
        )
        (app / "src" / "ui_shell" / "ui_shell.h").write_text(
            '#pragma once\n#include "ft8/app_types.h"\n', encoding="utf-8"
        )
        (app / "main" / "probe.c").write_text(
            '#include "ui_shell.h"\n#include "config_service.h"\n', encoding="utf-8"
        )

        violations = check_app(root, "ft8")
        if not any("main -> config_service" in item for item in violations):
            print("app_dependency_boundary self-test: FAIL (forbidden edge not detected)")
            return 1

        (app / "main" / "probe.c").write_text(
            '#include "app_controller_internal.h"\n', encoding="utf-8"
        )
        violations = check_app(root, "ft8")
        if not any("private header" in item for item in violations):
            print("app_dependency_boundary self-test: FAIL (private header not protected)")
            return 1

        (app / "main" / "probe.c").write_text(
            '#include "ui_shell.h"\n#include "ft8/app_types.h"\n', encoding="utf-8"
        )
        violations = check_app(root, "ft8")
        if violations:
            print("app_dependency_boundary self-test: FAIL")
            for violation in violations:
                print(f"  {violation}")
            return 1

        ft8_main = app / "main" / "ft8_main.c"
        ft8_main.write_text("void f(void) { ui.screen = 0; }\n", encoding="utf-8")
        violations = check_app(root, "ft8")
        if not any("forbidden lifecycle coupling" in item for item in violations):
            print("app_dependency_boundary self-test: FAIL (lifecycle coupling not detected)")
            return 1
        ft8_main.write_text("void f(void) { (void)0; }\n", encoding="utf-8")

        rogue = app / "src" / "rogue"
        rogue.mkdir()
        (rogue / "rogue.c").write_text("int rogue(void) { return 0; }\n", encoding="utf-8")
        violations = check_app(root, "ft8")
        if not any("source/header has no module owner" in item for item in violations):
            print("app_dependency_boundary self-test: FAIL (unowned module not detected)")
            return 1

    print("app_dependency_boundary self-test: PASS")
    return 0


def main() -> int:
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        return self_test()
    if len(sys.argv) != 3:
        print("usage: app_dependency_boundary.py <source-root> <app>", file=sys.stderr)
        print("       app_dependency_boundary.py --self-test", file=sys.stderr)
        return 2

    root = pathlib.Path(sys.argv[1]).resolve()
    app_name = sys.argv[2]
    violations = check_app(root, app_name)
    if violations:
        print(f"{app_name} dependency-boundary violations:")
        for violation in violations:
            print(f"  {violation}")
        return 1

    print(f"app_dependency_boundary: PASS ({app_name})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
