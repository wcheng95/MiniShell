#!/usr/bin/env python3

import os
import shutil
import subprocess
import sys
import tempfile
import wave


def fnv1a(data: bytes) -> int:
    value = 0xCBF29CE484222325
    for byte in data:
        value ^= byte
        value = (value * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def main() -> int:
    if len(sys.argv) != 4:
        print("usage: linux_audio.py <minishell> <app-dir> <wav-fixture>", file=sys.stderr)
        return 2

    minishell = os.path.abspath(sys.argv[1])
    app_dir = os.path.abspath(sys.argv[2])
    fixture = os.path.abspath(sys.argv[3])

    with wave.open(fixture, "rb") as wav:
        if wav.getframerate() != 12000 or wav.getsampwidth() != 2 or wav.getnchannels() != 2:
            print("fixture is not 12 kHz / S16 / 2-channel", file=sys.stderr)
            return 2
        expected_frames = wav.getnframes()
        payload = wav.readframes(expected_frames)
        if len(payload) != expected_frames * 4:
            print("fixture payload length mismatch", file=sys.stderr)
            return 2
        expected_hash = fnv1a(payload)

    with tempfile.TemporaryDirectory(prefix="minishell-audio-") as root:
        flash = os.path.join(root, "flash")
        os.makedirs(flash, exist_ok=True)
        target = os.path.join(flash, "kfs16b12k.wav")
        shutil.copyfile(fixture, target)

        env = os.environ.copy()
        env["MINISHELL_APP_DIR"] = app_dir
        env["MINISHELL_ROOT"] = root

        command = "run audio_probe /flash/kfs16b12k.wav\n" * 2 + "exit\n"
        process = subprocess.run(
            [minishell],
            input=command,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            env=env,
            timeout=10,
            check=False,
        )

        output = process.stdout
        if process.returncode != 0:
            print(output, end="")
            return 1

        expected = (
            f"audio_probe: PASS frames={expected_frames} "
            f"hash={expected_hash:016x}"
        )
        if output.count(expected) != 2:
            print(output, end="")
            print(f"expected twice: {expected}")
            return 1
        if "audio_probe: FAIL" in output:
            print(output, end="")
            return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
