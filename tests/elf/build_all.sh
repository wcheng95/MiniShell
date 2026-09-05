#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$HERE/out"
APPS=(abi_system abi_memory abi_fs abi_time_location abi_display abi_input)

mkdir -p "$OUT"
rm -f "$OUT"/*.elf

for app in "${APPS[@]}"; do
    project="$HERE/$app"
    echo
    echo "=== building $app ==="
    (
        cd "$project"
        idf.py set-target esp32p4
        idf.py elf
    )

    src="$project/build/$app.app.elf"
    if [[ ! -f "$src" ]]; then
        echo "expected ELF not found: $src" >&2
        exit 1
    fi
    cp "$src" "$OUT/$app.elf"
done

echo
echo "=== collected ELF tests ==="
ls -lh "$OUT"/*.elf

if [[ $# -gt 1 ]]; then
    echo "usage: $0 [mounted-sd-apps-directory]" >&2
    exit 2
fi

if [[ $# -eq 1 ]]; then
    dest="$1"
    if [[ ! -d "$dest" ]]; then
        echo "destination is not a directory: $dest" >&2
        exit 2
    fi
    cp "$OUT"/*.elf "$dest"/
    echo
    echo "copied six ELF tests to: $dest"
fi
