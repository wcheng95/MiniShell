#!/usr/bin/env python3
"""Convert js8-activity-v1 JSONL into compact human-readable activity text."""

import argparse
import json
import sys
from pathlib import Path


def frequency_text(event):
    audio_mhz = event.get("audio_millihz")
    rf_mhz = event.get("rf_millihz")
    if rf_mhz is not None:
        rf_hz = rf_mhz / 1000.0
        if audio_mhz is not None:
            return f"{rf_hz / 1_000_000:10.6f} MHz  {audio_mhz / 1000:+8.3f} Hz"
        return f"{rf_hz / 1_000_000:10.6f} MHz"
    if audio_mhz is not None:
        return f"audio {audio_mhz / 1000:+8.3f} Hz"
    return ""


def quality_text(event):
    score = event.get("score")
    errors = event.get("hard_errors")
    if score is None and errors is None:
        return ""
    parts = []
    if score is not None:
        parts.append(f"score={score}")
    if errors is not None:
        parts.append(f"err={errors}")
    return " [" + " ".join(parts) + "]"


def quote(text):
    return json.dumps(text, ensure_ascii=False)


def describe(event):
    kind = event.get("event", "?")

    if kind in ("HB", "CQ"):
        call = event.get("call", "")
        grid = event.get("grid", "")
        beacon = event.get("beacon", kind)
        body = f"{beacon:<10} {call}"
        if grid:
            body += f" {grid}"
        return body.rstrip()

    if kind == "COMPOUND":
        call = event.get("call", "")
        grid = event.get("grid", "")
        body = f"COMPOUND   {call}"
        if grid:
            body += f" {grid}"
        if event.get("compound_directed"):
            body += f" directed extra={event.get('extra', 0)} bits3={event.get('bits3', 0)}"
        return body.rstrip()

    if kind == "DIRECTED":
        source = event.get("from", "")
        target = event.get("to", "")
        body = f"DIRECTED   {source} -> {target}"
        command = event.get("command", "").strip()
        if command:
            body += f"  {command}"
        if "number" in event:
            body += f" {event['number']}"
        if event.get("ack"):
            body += " [ACK]"
        if event.get("end73"):
            body += " [73]"
        if event.get("free_text"):
            body += " [free]"
        return body

    if kind == "DATA":
        codec = event.get("codec", "none")
        return f"DATA       {codec:<7} {quote(event.get('text', ''))}"

    if kind == "MESSAGE":
        source = event.get("from", "")
        target = event.get("to", "")
        first = event.get("first_slot")
        last = event.get("last_slot")
        slots = ""
        if first is not None and last is not None:
            slots = f" slots={first}..{last}"
        return f"MESSAGE    {source} -> {target}{slots}  {quote(event.get('text', ''))}"

    return f"{kind:<10} {quote(event)}"


def format_event(event, show_quality=True):
    utc = event.get("utc")
    if utc:
        stamp = utc.replace("T", " ").removesuffix("Z")
    elif "slot" in event:
        stamp = f"slot {event['slot']}"
    else:
        stamp = "-"

    freq = frequency_text(event)
    body = describe(event)
    quality = quality_text(event) if show_quality else ""

    if freq:
        return f"{stamp}  {freq}  {body}{quality}"
    return f"{stamp}  {body}{quality}"


def iter_lines(stream, source_name):
    for lineno, raw in enumerate(stream, 1):
        if not raw.strip():
            continue
        try:
            event = json.loads(raw)
        except json.JSONDecodeError as exc:
            print(f"{source_name}:{lineno}: invalid JSON: {exc}", file=sys.stderr)
            continue
        if event.get("schema") != "js8-activity-v1":
            print(f"{source_name}:{lineno}: unsupported schema {event.get('schema')!r}", file=sys.stderr)
            continue
        yield event


def main():
    parser = argparse.ArgumentParser(
        description="Convert MiniShell js8-activity-v1 JSONL to readable text."
    )
    parser.add_argument("path", nargs="?", default="-",
                        help="JSONL file, or -/omitted for stdin")
    parser.add_argument("--no-quality", action="store_true",
                        help="omit candidate score / LDPC hard-error diagnostics")
    parser.add_argument("--messages-only", action="store_true",
                        help="show only completed MESSAGE events")
    args = parser.parse_args()

    if args.path == "-":
        stream = sys.stdin
        source_name = "<stdin>"
        close = False
    else:
        stream = Path(args.path).open("r", encoding="utf-8", errors="replace")
        source_name = args.path
        close = True

    try:
        for event in iter_lines(stream, source_name):
            if args.messages_only and event.get("event") != "MESSAGE":
                continue
            print(format_event(event, show_quality=not args.no_quality))
    finally:
        if close:
            stream.close()


if __name__ == "__main__":
    main()
