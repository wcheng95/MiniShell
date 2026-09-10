#!/usr/bin/env python3

import errno
import os
import pty
import select
import shutil
import subprocess
import sys
import tempfile
import time


def read_until(fd: int, needle: bytes, timeout: float) -> bytes:
    deadline = time.monotonic() + timeout
    data = bytearray()
    while needle not in data:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError(f"timed out waiting for {needle!r}; got {bytes(data)!r}")
        readable, _, _ = select.select([fd], [], [], remaining)
        if not readable:
            continue
        try:
            chunk = os.read(fd, 4096)
        except OSError as exc:
            if exc.errno == errno.EIO:
                break
            raise
        if not chunk:
            break
        data.extend(chunk)
    if needle not in data:
        raise TimeoutError(f"stream ended waiting for {needle!r}; got {bytes(data)!r}")
    return bytes(data)


def current_screen(data: bytes) -> bytes:
    marker = b"\x1b[2J\x1b[H"
    pos = data.rfind(marker)
    return data[pos:] if pos >= 0 else data


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: linux_ft8.py <minishell> <app-dir>", file=sys.stderr)
        return 2

    minishell = os.path.abspath(sys.argv[1])
    app_dir = os.path.abspath(sys.argv[2])
    kfs_fixture = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                               "kfs16b12k.wav")
    master_fd, slave_fd = pty.openpty()

    try:
        with tempfile.TemporaryDirectory(prefix="minishell-ft8-") as root:
            station = os.path.join(root, "flash", "ft8", "station.txt")
            os.makedirs(os.path.dirname(station), exist_ok=True)
            with open(station, "w", encoding="utf-8") as handle:
                handle.write(
                    "# MiniFT8-V3 station.txt\n"
                    "callsign=ag6aq\n"
                    "grid=cm97\n"
                    "profile=0\n"
                    "band=3\n"
                    "skip_tx1=0\n"
                    "max_retry=3\n"
                    "cq_type=4\n"
                    "cq_ft=CQ TEST\n"
                    "free_text=TNX 73\n"
                    "fd_exchange=1b scv\n"
                )
            shutil.copyfile(kfs_fixture, os.path.join(root, "flash", "kfs.wav"))

            env = os.environ.copy()
            env["MINISHELL_APP_DIR"] = app_dir
            env["MINISHELL_ROOT"] = root
            process = subprocess.Popen(
                [minishell], stdin=slave_fd, stdout=slave_fd, stderr=slave_fd,
                env=env, close_fds=True,
            )
            os.close(slave_fd)
            slave_fd = -1
            transcript = bytearray()
            try:
                transcript.extend(read_until(master_fd, b"M$> ", 3.0))

                # Default DESKTOP launch still exercises live config mutation.
                os.write(master_fd, b"ft8\n")
                transcript.extend(read_until(master_fd, b"R T O S V", 3.0))
                os.write(master_fd, b"o")
                transcript.extend(read_until(master_fd, b"Protocol: FT8", 3.0))

                # O/1 is intentionally a no-op. C2 redraws only when the
                # rendered UiFrame changes, so do not require a duplicate frame.
                os.write(master_fd, b"1")
                os.write(master_fd, b"5")
                transcript.extend(read_until(master_fd, b"Skip TX1: OFF", 3.0))
                os.write(master_fd, b"3")
                transcript.extend(read_until(master_fd, b"Skip TX1: ON", 3.0))
                os.write(master_fd, b"q")
                transcript.extend(read_until(master_fd, b"M$> ", 3.0))

                with open(station, "r", encoding="utf-8") as handle:
                    saved = handle.read()
                if (
                    "callsign=AG6AQ\n" not in saved
                    or "grid=CM97\n" not in saved
                    or "profile=0\n" not in saved
                    or "band=3\n" not in saved
                    or "skip_tx1=1\n" not in saved
                    or "cq_type=4\n" not in saved
                    or "cq_ft=CQ TEST\n" not in saved
                    or "free_text=TNX 73\n" not in saved
                    or "fd_exchange=1B SCV\n" not in saved
                ):
                    raise RuntimeError(f"unexpected station.txt contents: {saved!r}")
                if "mode=" in saved or "mode0_" in saved or "presentation=" in saved:
                    raise RuntimeError(f"unexpected persisted state: {saved!r}")
                if os.path.exists(station + ".tmp"):
                    raise RuntimeError("atomic save left station.txt.tmp behind")

                os.write(master_fd, b"ft8 --profile desktop\n")
                transcript.extend(read_until(master_fd, b"R T O S V", 3.0))
                os.write(master_fd, b"v")
                transcript.extend(read_until(master_fd, b"System Info", 3.0))

                # V -> 1 Memory reads the real MiniShell Memory API while ft8 is alive.
                os.write(master_fd, b"1")
                memory_view = read_until(master_fd, b"RX: OFF", 3.0)
                if b"Heap free:" not in memory_view or b"App alloc:" not in memory_view:
                    raise RuntimeError(f"Memory view missing runtime fields: {memory_view!r}")
                transcript.extend(memory_view)
                os.write(master_fd, b"`")
                transcript.extend(read_until(master_fd, b"System Info", 3.0))

                os.write(master_fd, b"5")
                transcript.extend(read_until(master_fd, b"Presentation: DESKTOP", 3.0))
                os.write(master_fd, b"q")
                transcript.extend(read_until(master_fd, b"M$> ", 3.0))

                # ADV uses the locked 20-character top row; profile name is no longer there.
                os.write(master_fd, b"ft8 --profile adv\n")
                transcript.extend(read_until(master_fd, b"RX 20 ", 3.0))
                os.write(master_fd, b"o")
                transcript.extend(read_until(master_fd, b"Protocol: FT8", 3.0))
                os.write(master_fd, b"5")
                transcript.extend(read_until(master_fd, b"Skip TX1: ON", 3.0))
                os.write(master_fd, b"v")
                transcript.extend(read_until(master_fd, b"System Info", 3.0))
                os.write(master_fd, b"5")
                adv_system = read_until(master_fd, b"UI: text 20x7", 3.0)
                if b"Presentation: ADV" not in adv_system:
                    raise RuntimeError(f"ADV system view missing presentation label: {adv_system!r}")
                transcript.extend(adv_system)
                os.write(master_fd, b"q")
                transcript.extend(read_until(master_fd, b"M$> ", 3.0))

                # AS-3/AS-5 golden: use a clean non-Skip-TX1 station configuration.
                with open(station, "r", encoding="utf-8") as handle:
                    as3_station = handle.read()
                as3_station = as3_station.replace("skip_tx1=1\n", "skip_tx1=0\n")
                with open(station, "w", encoding="utf-8") as handle:
                    handle.write(as3_station)

                # Decode the real 2x2 kfs fixture. 16 messages => 3 RX pages.
                os.write(master_fd,
                         b"ft8 --profile adv --rx /flash/kfs.wav --rx-slot 12345\n")
                transcript.extend(read_until(master_fd, b" 1/3 ", 15.0))

                # Select every decoded line. AS-3 queues only factual CQs.
                os.write(master_fd, b"123456")
                os.write(master_fd, b"\x1b[B")
                transcript.extend(read_until(master_fd, b" 2/3 ", 3.0))
                os.write(master_fd, b"123456")
                os.write(master_fd, b"\x1b[B")
                transcript.extend(read_until(master_fd, b" 3/3 ", 3.0))
                os.write(master_fd, b"1234")

                # The eight factual CQs become eight RPLY contexts: T pages are 6 + 2.
                os.write(master_fd, b"t")
                page1 = read_until(master_fd, b"WN0KS    RPLY 0/3", 3.0)
                expected_page1 = (b"N4NJJ", b"AG6X", b"AE7KJ", b"W7RPS",
                                  b"N7REB", b"WN0KS")
                if b" 1/2 " not in page1 or page1.count(b"RPLY 0/3") != 6 or \
                        any(call not in page1 for call in expected_page1):
                    raise RuntimeError(
                        f"AS-3 T page 1 did not contain the six expected CQ contexts: {page1!r}"
                    )
                transcript.extend(page1)

                os.write(master_fd, b"\x1b[B")
                page2 = read_until(master_fd, b"KQ4PUG   RPLY 0/3", 3.0)
                if b" 2/2 " not in page2 or page2.count(b"RPLY 0/3") != 2 or \
                        b"N5CH" not in page2 or b"KQ4PUG" not in page2:
                    raise RuntimeError(
                        f"AS-3 T page 2 did not contain the two expected CQ contexts: {page2!r}"
                    )
                transcript.extend(page2)

                # AS-5: all eight CQs have the same TX parity. Return to page 1
                # and press Enter; V2 rotation moves the head to the end of the
                # contiguous same-parity run.
                os.write(master_fd, b"\x1b[B")
                transcript.extend(read_until(master_fd, b" 1/2 ", 3.0))
                os.write(master_fd, b"\r")
                rotated = read_until(master_fd, b"N5CH     RPLY 0/3", 3.0)
                rotated_screen = current_screen(rotated)
                expected_rotated_page1 = (b"AG6X", b"AE7KJ", b"W7RPS",
                                          b"N7REB", b"WN0KS", b"N5CH")
                if b" 1/2 " not in rotated_screen or \
                        any(call not in rotated_screen for call in expected_rotated_page1) or \
                        b"N4NJJ" in rotated_screen:
                    raise RuntimeError(
                        f"AS-5 same-parity rotation produced wrong page 1: {rotated_screen!r}"
                    )
                transcript.extend(rotated)

                # Drop visible line 1 (absolute queue index 0 => AG6X). A QSO
                # drop parks metadata inactive; only active rows remain visible.
                os.write(master_fd, b"1")
                dropped = read_until(master_fd, b"KQ4PUG   RPLY 0/3", 3.0)
                dropped_screen = current_screen(dropped)
                if b"AG6X" in dropped_screen or b"AE7KJ" not in dropped_screen or \
                        b"KQ4PUG" not in dropped_screen or b" 1/2 " not in dropped_screen:
                    raise RuntimeError(
                        f"AS-5 T drop did not remove the rotated head: {dropped_screen!r}"
                    )
                transcript.extend(dropped)

                # Page 2 now contains only N4NJJ. Drop it through page-local key 1;
                # the UI action must carry absolute index 6. Six active rows remain,
                # so paging collapses back to 1/1.
                os.write(master_fd, b"\x1b[B")
                page2_after_drop = read_until(master_fd, b"N4NJJ    RPLY 0/3", 3.0)
                if b" 2/2 " not in page2_after_drop:
                    raise RuntimeError(f"AS-5 expected one row on page 2: {page2_after_drop!r}")
                transcript.extend(page2_after_drop)
                os.write(master_fd, b"1")
                collapsed = read_until(master_fd, b" 1/1 ", 3.0)
                collapsed_screen = current_screen(collapsed)
                remaining = (b"AE7KJ", b"W7RPS", b"N7REB", b"WN0KS", b"N5CH", b"KQ4PUG")
                if b"N4NJJ" in collapsed_screen or b"AG6X" in collapsed_screen or \
                        any(call not in collapsed_screen for call in remaining):
                    raise RuntimeError(
                        f"AS-5 page-2 absolute drop/collapse failed: {collapsed_screen!r}"
                    )
                transcript.extend(collapsed)

                os.write(master_fd, b"q")
                transcript.extend(read_until(master_fd, b"M$> ", 3.0))

                os.write(master_fd, b"exit\n")
                return_code = process.wait(timeout=3.0)
                if return_code != 0:
                    raise RuntimeError(f"minishell exited with {return_code}")
            except Exception:
                print(transcript.decode("utf-8", errors="replace"), end="")
                process.kill()
                process.wait(timeout=3.0)
                raise

            print(transcript.decode("utf-8", errors="replace"), end="")
    finally:
        if slave_fd >= 0:
            os.close(slave_fd)
        os.close(master_fd)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
