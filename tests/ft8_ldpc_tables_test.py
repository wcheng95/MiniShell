#!/usr/bin/env python3
import re
import sys
from pathlib import Path


def initializer(text: str, name: str) -> str:
    start = text.index(name)
    start = text.index("{", text.index("=", start))
    depth = 0
    for i in range(start, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
    raise AssertionError(f"unterminated initializer: {name}")


def rows(block: str):
    return [
        [int(x) for x in re.findall(r"\b\d+\b", match.group(1))]
        for match in re.finditer(r"\{([^{}]*)\}", block)
    ]


def numbers(block: str):
    return [int(x) for x in re.findall(r"\b\d+\b", block)]


def main() -> int:
    root = Path(sys.argv[1])
    source = root / "apps/ft8/src/ft8_engine/ft8_ldpc.c"
    text = source.read_text(encoding="utf-8")

    parity = rows(initializer(text, "kParityChecks"))
    lengths = numbers(initializer(text, "kParityRowLengths"))
    reverse = rows(initializer(text, "kReverseChecks"))

    assert len(parity) == 83, len(parity)
    assert len(lengths) == 83, len(lengths)
    assert len(reverse) == 174, len(reverse)

    derived = [[] for _ in range(174)]
    edge_count = 0

    for check, (entries, length) in enumerate(zip(parity, lengths), start=1):
        active = [value for value in entries if value != 0]
        assert len(active) == length, (check, length, active)
        for variable in active:
            assert 1 <= variable <= 174, (check, variable)
            derived[variable - 1].append(check)
            edge_count += 1

    assert edge_count == 522, edge_count
    for variable, expected in enumerate(derived, start=1):
        assert len(expected) == 3, (variable, expected)
        assert reverse[variable - 1] == expected, (
            variable,
            reverse[variable - 1],
            expected,
        )

    print("ft8_ldpc_tables_test: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
