#!/usr/bin/env python3
"""Verify that a rootless loop recurrence is eliminated."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--case", required=True)
    parser.add_argument("--must-keep-case", required=True)
    args = parser.parse_args()
    process = subprocess.run(
        [args.compiler, "-opt"],
        input=Path(args.case).read_bytes(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=10,
        check=False,
    )
    if process.returncode != 0:
        raise RuntimeError(process.stderr.decode(errors="replace"))
    assembly = process.stdout.decode(errors="replace")
    match = re.search(r"(?ms)^main:\n(.*?)^\s*\.size\s+main,", assembly)
    if match is None:
        raise RuntimeError("generated assembly has no main")
    body = match.group(1)
    if re.search(r"(?m)^\s*(?:div|rem|mul)\b", body):
        raise RuntimeError(f"dead recurrence arithmetic remains:\n{body}")
    if re.search(r"(?m)^\s*(?:b\w+|j)\s+L\w+", body):
        raise RuntimeError(f"summarizable loop remains:\n{body}")

    process = subprocess.run(
        [args.compiler, "-opt"],
        input=Path(args.must_keep_case).read_bytes(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=10,
        check=False,
    )
    if process.returncode != 0:
        raise RuntimeError(process.stderr.decode(errors="replace"))
    assembly = process.stdout.decode(errors="replace")
    match = re.search(r"(?ms)^main:\n(.*?)^\s*\.size\s+main,", assembly)
    if match is None:
        raise RuntimeError("generated assembly has no main")
    body = match.group(1)
    if not re.search(r"(?m)^\s*(?:b\w+|j)\s+L\w+", body):
        raise RuntimeError(f"observable recurrence was deleted:\n{body}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
