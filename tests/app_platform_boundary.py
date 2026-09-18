#!/usr/bin/env python3
"""Textual platform/purity checks, not a preprocessor or call-graph analyzer."""

import pathlib
import posixpath
import re
import sys
import tempfile

from architecture_rules import APP_RULES
from app_dependency_boundary import INCLUDE_RE, SOURCE_SUFFIXES, module_for_path, strip_comments

FORBIDDEN_PREFIXES = (
    "platform/", "linux/", "alsa/", "sys/", "freertos/", "driver/",
    "soc/", "hal/", "esp_private/", "esp_", "m5",
)
FORBIDDEN_HEADERS = {
    "arduino.h", "unistd.h", "fcntl.h", "dirent.h", "pthread.h",
    "dlfcn.h", "termios.h", "poll.h", "sdkconfig.h", "platform_backend.h", "adv_internal.h",
}
NATIVE_RE = re.compile(
    r"\b(?:snd_\w+|pthread_\w+|dlopen|dlsym|dlclose|esp_\w+|gpio_\w+|"
    r"i2s_\w+|[vx]Task\w+|tud_\w+|tinyusb_\w+|fopen|opendir|"
    r"clock_gettime|usleep|nanosleep)\b"
)
TOKEN_RE = re.compile(r"\b(?:ESP_PLATFORM|CONFIG_IDF_TARGET\w*|__linux__|M5Unified|M5Cardputer)\b")
HEAP_RE = re.compile(r"\b(?:malloc|calloc|realloc|aligned_alloc|free|strdup|asprintf)\s*\(")


def check_app(root, app_name):
    if app_name not in APP_RULES:
        return [f"unknown app rule set: {app_name}"]
    rule = APP_RULES[app_name]
    app_root = root / "apps" / app_name
    violations = []
    files = sorted(p for p in app_root.rglob("*")
                   if p.is_file() and p.suffix in SOURCE_SUFFIXES)
    if not files:
        return [f"no source files checked for app {app_name}"]
    for path in files:
        rel = path.relative_to(app_root).as_posix()
        module = module_for_path(app_root, path, rule)
        if module is None:
            violations.append(f"{rel}: source/header has no module owner")
        text = strip_comments(path.read_text(encoding="utf-8"))
        for number, line in enumerate(text.splitlines(), 1):
            location = f"{app_name}/{rel}:{number}"
            match = INCLUDE_RE.match(line)
            if match:
                header = match.group(2)
                # Inspect components too: relative spelling must not hide private headers.
                parts = pathlib.PurePosixPath(posixpath.normpath(header)).parts
                suffixes = ["/".join(parts[i:]).lower() for i in range(len(parts))]
                if any(h in FORBIDDEN_HEADERS or h.startswith(FORBIDDEN_PREFIXES)
                       for h in suffixes) and header not in rule.get("header_exceptions", {}).get(rel, set()):
                    violations.append(f"{location}: forbidden include {header}")
                if "minishell/api.h" in suffixes and module not in rule["api_modules"]:
                    violations.append(f"{location}: pure module {module} must not include MiniShell API")
                continue
            code = re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', '""', line)
            for match in NATIVE_RE.finditer(code):
                if match[0] not in rule.get("native_exceptions", {}).get(rel, set()):
                    violations.append(f"{location}: forbidden native symbol {match[0]}")
            if TOKEN_RE.search(code):
                violations.append(f"{location}: forbidden platform token")
            if module in rule.get("no_heap_modules", set()) and HEAP_RE.search(code):
                violations.append(f"{location}: AutoSeq heap call")
    return violations


def self_test():
    cases = 0
    with tempfile.TemporaryDirectory() as temp:
        root = pathlib.Path(temp)
        for app, pure in (("ft8", "auto_seq"), ("keyer", "keyer_engine")):
            path = root / "apps" / app / "src" / pure / "probe.c"
            path.parent.mkdir(parents=True)
            def expect(source, reason=None):
                nonlocal cases
                path.write_text(source, encoding="utf-8")
                errors = check_app(root, app)
                assert (any(reason in e for e in errors) if reason else not errors), (source, errors)
                cases += 1
            expect('#include <stdint.h>\n// fopen("ignored");\nconst char *s = "esp_fake()";\n')
            for syntax in ('"{}"', '<{}>'):
                expect('#include ' + syntax.format('minishell/api.h'), 'pure module')
                for header in sorted(FORBIDDEN_HEADERS) + [p + 'probe.h' for p in FORBIDDEN_PREFIXES]:
                    expect('#include ' + syntax.format(header), 'forbidden include')
            for symbol in ('snd_open', 'pthread_create', 'dlopen', 'dlsym', 'dlclose',
                           'esp_restart', 'gpio_set_level', 'i2s_write', 'vTaskDelay',
                           'xTaskCreate', 'tud_task', 'tinyusb_init', 'fopen', 'opendir',
                           'clock_gettime', 'usleep', 'nanosleep'):
                expect(f'void f(void) {{ {symbol}(); }}', 'forbidden native symbol')
            for token in ('ESP_PLATFORM', 'CONFIG_IDF_TARGET_ESP32', '__linux__', 'M5Unified', 'M5Cardputer'):
                expect(f'#ifdef {token}\n#endif', 'platform token')
            if app == 'ft8':
                for call in ('malloc', 'calloc', 'realloc', 'aligned_alloc', 'free', 'strdup', 'asprintf'):
                    expect(f'void f(void) {{ {call}(0); }}', 'AutoSeq heap call')
            expect('#include <minishell/./api.h>', 'pure module')
            expect('#include "../platform/private.h"', 'forbidden include')
            expect('int valid;')
            for module in APP_RULES[app]['api_modules']:
                edge = root / 'apps' / app / APP_RULES[app]['module_paths'][module][0] / 'edge.h'
                edge.parent.mkdir(parents=True, exist_ok=True)
                edge.write_text('#include "minishell/api.h"\n')
            assert not check_app(root, app), check_app(root, app)
        host = root / 'apps/ft8/tools/ft8_decode.c'
        host.parent.mkdir(parents=True)
        host.write_text('void f(void) { fopen(0, 0); }')
        assert not check_app(root, 'ft8')
        host.write_text('void f(void) { opendir(0); }')
        assert any('forbidden native symbol' in e for e in check_app(root, 'ft8'))
        vendor = root / 'apps/ft8/src/ft8_engine/vendor/kissfft/kiss_fft.h'
        vendor.parent.mkdir(parents=True)
        host.write_text('void f(void) { fopen(0, 0); }')
        vendor.write_text('#include <sys/types.h>')
        assert not check_app(root, 'ft8')
        vendor.write_text('#include <sys/stat.h>')
        assert any('forbidden include' in e for e in check_app(root, 'ft8'))
    print(f'app_platform_boundary self-test: PASS ({cases} cases plus API edges/host exception)')
    return 0


def main():
    if sys.argv[1:] == ['--self-test']:
        return self_test()
    if len(sys.argv) != 3:
        print('usage: app_platform_boundary.py <source-root> <app> | --self-test')
        return 2
    errors = check_app(pathlib.Path(sys.argv[1]).resolve(), sys.argv[2])
    if errors:
        print('\n'.join(errors))
        return 1
    print(f'app_platform_boundary: PASS ({sys.argv[2]})')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
