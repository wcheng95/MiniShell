#!/usr/bin/env python3
"""Compatibility entry point; policy and enforcement live in generic checkers."""
import pathlib
import sys

from app_dependency_boundary import check_app as check_dependencies
from app_platform_boundary import check_app as check_platform
from serial_protocol_boundary import check_transport


def main():
    if len(sys.argv) != 2:
        print('usage: ft8_platform_boundary.py <source-root>', file=sys.stderr)
        return 2
    root = pathlib.Path(sys.argv[1]).resolve()
    errors = check_dependencies(root, 'ft8') + check_platform(root, 'ft8')
    errors += check_transport(root)
    if errors:
        print('\n'.join(errors))
        return 1
    print('ft8_platform_boundary: PASS (dependencies, platform, purity, AutoSeq heap)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
