#!/usr/bin/env python3

from __future__ import annotations

import pathlib
import re
import sys
import tempfile

from architecture_rules import APP_RULES

SOURCE_SUFFIXES = {".c", ".h", ".cc", ".cpp", ".hpp"}
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]')
# Standard C names must not accidentally resolve through basename fallback.
STANDARD_HEADERS = set(
    "assert complex ctype errno fenv float inttypes iso646 limits locale math "
    "setjmp signal stdalign stdarg stdatomic stdbool stddef stdint stdio stdlib "
    "stdnoreturn string tgmath threads time uchar wchar wctype".split()
)


def strip_comments(text):
    # Preserve string literals (including include names) and diagnostic line numbers.
    return re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/',
                  lambda m: "\n" * m[0].count("\n") + " "
                  if m[0].startswith(("//", "/*")) else m[0],
                  text, flags=re.S)


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


def build_header_index(app_root):
    index = {}
    for path in app_root.rglob("*"):
        if path.is_file() and path.suffix in {".h", ".hpp"}:
            index.setdefault(path.name, []).append(path.resolve())
    return index


def resolve_local_header(include, quoted, source, app_root, rule, index):
    def normalized(base):
        candidate = (base / include).resolve()
        if not candidate.is_relative_to(app_root):
            raise ValueError(f"local include escapes application root: {include}")
        return candidate

    if quoted:
        candidate = normalized(source.parent)
        if candidate.is_file():
            return candidate
    candidate = normalized(app_root)
    if candidate.is_file():
        return candidate
    if include.endswith(".h") and include[:-2] in STANDARD_HEADERS:
        return None
    candidates = set()
    for prefix in rule["include_roots"]:
        candidate = normalized(app_root / prefix)
        if candidate.is_file():
            candidates.add(candidate)
    if not candidates and "/" not in include:
        candidates = set(index.get(include, []))
    if any(not path.is_relative_to(app_root) for path in candidates):
        raise ValueError(f"local include escapes application root: {include}")
    if len(candidates) > 1:
        raise ValueError(f"ambiguous local include {include}")
    return next(iter(candidates), None)


def check_app(root: pathlib.Path, app_name: str) -> list[str]:
    if app_name not in APP_RULES:
        return [f"unknown app rule set: {app_name}"]

    app_root = (root / "apps" / app_name).resolve()
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

        text = strip_comments(path.read_text(encoding="utf-8"))
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

            include = match.group(2)
            try:
                target = resolve_local_header(include, match.group(1) == '"',
                                              path, app_root, rule, header_index)
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
    cases = 0
    with tempfile.TemporaryDirectory() as temp:
        root = pathlib.Path(temp)
        for name in ("ft8", "keyer"):
            app = root / "apps" / name

            def write(rel, text="#pragma once\n"):
                path = app / rel
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text, encoding="utf-8")
                return path

            write("src/app_controller/app_controller.h")
            write("src/config_service/config_service.h")
            probe = write("main/probe.c", "")

            def expect(source, reason=None):
                nonlocal cases
                probe.write_text(source, encoding="utf-8")
                errors = check_app(root, name)
                assert (any(reason in e for e in errors) if reason else not errors), (source, errors)
                cases += 1

            # Owner-relative, app-relative, declared roots and basename fallback.
            for header in ("config_service.h", "src/config_service/config_service.h",
                           "../src/config_service/./config_service.h"):
                for opening, closing in (("\"", "\""), ("<", ">")):
                    if header.startswith("..") and opening == "<":
                        continue
                    expect(f"#include {opening}{header}{closing}\n", "main -> config_service")
            for header in ("app_controller.h", "src/app_controller/app_controller.h",
                           "../src/app_controller/./app_controller.h"):
                expect(f'#include "{header}"\n')
            write("src/app_controller/nested/canonical.hpp")
            expect('#include <nested/canonical.hpp>\n')
            write("src/app_controller/nested/fallback.hpp")
            expect('#include <fallback.hpp>\n')
            write("src/app_controller/collision.h")
            write("src/config_service/collision.h")
            expect('#include <collision.h>\n', "ambiguous local include")
            expect('#include "collision.h"\n', "ambiguous local include")
            # Same-directory selection takes precedence over global ambiguity.
            write("main/collision.h")
            expect('#include "./collision.h"\n')
            expect('#include "../../outside.h"\n', "escapes application root")
            expect('#include <../outside.h>\n', "escapes application root")
            write("misc/unowned.h")
            expect('#include "misc/unowned.h"\n', "has no module owner")
            rogue = write("src/rogue/rogue.c", "int rogue;\n")
            expect("", "source/header has no module owner")
            rogue.unlink()
            # Common standard header names in unrelated nested paths do not
            # turn ordinary standard includes into local dependencies.
            write("src/config_service/nested/stdio.h")
            expect('#include <stdio.h>\n#include <stdint.h>\n')
            if name == "ft8":
                write("src/app_controller/app_controller_internal.h")
                for header in ("app_controller_internal.h",
                               "src/app_controller/app_controller_internal.h",
                               "../src/app_controller/app_controller_internal.h"):
                    expect(f'#include "{header}"\n', "private header")
                expect('#include <app_controller_internal.h>\n', "private header")
                expect('#include <src/app_controller/app_controller_internal.h>\n', "private header")
                write("src/app_controller/owner.c", '#include "./app_controller_internal.h"\n')
                expect("")
                lifecycle = write("main/ft8_main.c", "")
                for source in ("ui.screen = 0;", "ui.submenu = 0;", "SCREEN_MAIN;",
                               "UI_SUBMENU_TX;", "model.field = 0;"):
                    lifecycle.write_text(source)
                    expect("", "forbidden lifecycle coupling")
                lifecycle.write_text("")
                encoder = write("src/tx_encoder/tx_encoder.c", "")
                write("src/auto_seq/auto_seq_tx_intent.h")
                write("src/ft8_engine/ft8_message_codec.h")
                for module in ("radio_control", "rx_audio_adapter", "storage_service", "ui_shell"):
                    write(f"src/{module}/{module}.h")
                    encoder.write_text(f'#include "{module}.h"\n')
                    expect("", f"tx_encoder -> {module}")
                encoder.write_text('#include "auto_seq_tx_intent.h"\n#include "ft8_message_codec.h"\n')
                expect("")
                offset = write("src/tx_offset/tx_offset.c", "")
                write("src/config_service/config_service.h")
                for module in ("radio_control", "rx_audio_adapter", "storage_service", "ui_shell", "tx_encoder"):
                    write(f"src/{module}/{module}.h")
                    offset.write_text(f'#include "{module}.h"\n')
                    expect("", f"tx_offset -> {module}")
                for call in ("rand", "srand", "random", "srandom", "getrandom", "esp_random"):
                    offset.write_text(f"void f(void) {{ {call}(); }}")
                    expect("", "application-owned PRNG")
                offset.write_text('#include "config_service.h"\n#include "auto_seq_tx_intent.h"\n')
                expect("")
                write("src/tx_encoder/tx_channel.h")
                expect('#include "tx_channel.h"\n', "private header")
            expect('#include <app_controller.h>\n')
    print(f"app_dependency_boundary self-test: PASS ({cases} cases)")
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
