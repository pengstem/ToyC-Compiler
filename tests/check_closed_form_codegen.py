#!/usr/bin/env python3
"""Verify that a proven constant loop becomes an optimal constant return."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--case", required=True)
    parser.add_argument("--expected", required=True, type=int)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    source = Path(args.case).read_text(encoding="utf-8")
    process = subprocess.run(
        [args.compiler, "-opt"],
        input=source,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=10,
        check=False,
    )
    if process.returncode != 0:
        print(process.stderr)
        return 1

    match = re.search(r"(?ms)^main:\n(.*?)^\s*\.size\s+main,", process.stdout)
    if not match:
        print("main function not found in generated assembly")
        return 1
    body = match.group(1)
    expected = f"li a0, {args.expected}"
    if expected not in body:
        print(f"missing `{expected}` in main:\n{body}")
        return 1
    forbidden = re.compile(r"(?m)^\s*(?:b\w+|j|call|lw|sw|mul|div|rem)\b")
    if forbidden.search(body):
        print(f"constant loop left runtime work in main:\n{body}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
