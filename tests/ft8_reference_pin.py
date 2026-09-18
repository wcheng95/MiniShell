#!/usr/bin/env python3
"""Check literal reference declarations; no network or YAML dependency required."""

import pathlib
import re
import sys
import tempfile

CANONICAL = "491e757ae6b1e4cfd2b9a6ba10f48b35643849e0"
REPOSITORY = "wcheng95/Mini-FT8"
WAV = "tests/tx_e2e/golden/ft8_cq_w1xyz_fn42.wav"
WORKFLOW = ".github/workflows/ft8-reference.yml"
DOCS = ("AGENTS.md", "docs/MiniFT8/README.md", "docs/MiniFT8/development.md")
SHA_RE = re.compile(r"\b[0-9a-fA-F]{40}\b")


def check(root):
    errors = []
    try:
        workflow = (root / WORKFLOW).read_text()
        # Isolate checkout steps so a SHA in a comment cannot mask a stale ref.
        steps = re.split(r"(?m)^      - ", workflow)[1:]
        references = [step for step in steps if re.search(r"(?m)^\s+repository:", step)]
        if len(references) != 1:
            errors.append("workflow: expected one external reference checkout")
        for step in references:
            def scalar(key):
                match = re.search(r"(?m)^          " + key + r":\s*([^\n#]+)", step)
                return match[1].strip().strip("\"'") if match else None
            if not re.search(r"(?m)^        uses: actions/checkout@", step):
                errors.append("workflow: reference step is not a checkout")
            if scalar("repository") != REPOSITORY:
                errors.append("workflow: incorrect reference repository")
            if scalar("ref") != CANONICAL:
                errors.append("workflow: incorrect canonical ref")
        paths = re.findall(r"reference/Mini-FT8/([^\s\"']+\.wav)", workflow)
        if not paths or set(paths) != {WAV}:
            errors.append("workflow: incorrect golden WAV path")

        # Parse structurally too when already installed; never require installation.
        try:
            import yaml
        except ImportError:
            pass
        else:
            data = yaml.safe_load(workflow)
            checkouts = [step for job in data["jobs"].values() for step in job["steps"]
                         if "repository" in step.get("with", {})]
            if len(checkouts) != 1 or checkouts[0]["with"].get("repository") != REPOSITORY or checkouts[0]["with"].get("ref") != CANONICAL:
                errors.append("workflow: structural checkout mismatch")

        for name in DOCS:
            text = (root / name).read_text()
            # These repository-bearing fenced blocks are the canonical reference
            # declarations. Other commit hashes elsewhere in prose are unrelated.
            declarations = [block for block in re.findall(r"```[^\n]*\n(.*?)```", text, re.S)
                            if REPOSITORY in block]
            if not declarations:
                errors.append(f"{name}: missing reference declaration")
            for declaration in declarations:
                pins = {sha.lower() for sha in SHA_RE.findall(declaration)}
                if pins != {CANONICAL}:
                    errors.append(f"{name}: conflicting or missing canonical reference pin")
    except (OSError, ValueError, KeyError, TypeError) as exc:
        errors.append(f"reference validation failed: {exc}")
    return errors


def self_test(root):
    with tempfile.TemporaryDirectory(prefix="ft8-pin-") as temp:
        fixture = pathlib.Path(temp)
        original = {name: (root / name).read_text() for name in (WORKFLOW, *DOCS)}
        for name, text in original.items():
            path = fixture / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
        assert not check(fixture), check(fixture)
        stale = "5bd3ef98f72388a850bebad04bd7300b90edb63c"
        for name in original:
            path = fixture / name
            path.write_text(original[name].replace(CANONICAL, stale))
            assert check(fixture), f"missed stale pin in {name}"
            path.write_text(original[name])
        for name in DOCS:
            path = fixture / name
            path.write_text(original[name].replace(CANONICAL, CANONICAL + "\n" + stale, 1))
            assert check(fixture), f"missed conflicting declaration in {name}"
            path.write_text(original[name].replace(CANONICAL, CANONICAL + "\n" + CANONICAL, 1))
            assert not check(fixture), "identical repeated pins must be allowed"
            path.write_text(original[name] + "\nUnrelated commit: " + stale + "\n")
            assert not check(fixture), "unrelated commits are not reference declarations"
            path.write_text(original[name])
        path = fixture / WORKFLOW
        for text in (original[WORKFLOW].replace(REPOSITORY, "other/repository"),
                     original[WORKFLOW].replace(WAV, "golden/other.wav"),
                     original[WORKFLOW].replace("ref: " + CANONICAL, "ref: " + stale) + "\n# " + CANONICAL):
            path.write_text(text)
            assert check(fixture), "missed workflow drift"
        path.write_text(original[WORKFLOW])
    print("ft8_reference_pin self-test: PASS")


def main():
    if len(sys.argv) not in (2, 3) or (len(sys.argv) == 3 and sys.argv[2] != "--self-test"):
        print("usage: ft8_reference_pin.py <source-root> [--self-test]", file=sys.stderr)
        return 2
    root = pathlib.Path(sys.argv[1]).resolve()
    errors = check(root)
    if errors:
        print("\n".join(errors))
        return 1
    if len(sys.argv) == 3:
        self_test(root)
    print("ft8_reference_pin: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
