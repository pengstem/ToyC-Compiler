#!/usr/bin/env python3
"""Verify finite strided dead-loop deletion and overflow fallback."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess


def has_backedge(compiler: str, source: str) -> tuple[bool, str]:
    process = subprocess.run(
        [compiler, "-opt"],
        input=Path(source).read_bytes(),
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
    labels: dict[str, int] = {}
    lines = body.splitlines()
    for index, line in enumerate(lines):
        label = re.fullmatch(r"\s*(L\w*):\s*", line)
        if label:
            labels[label.group(1)] = index
    branch = re.compile(r"\s*(?:b\w+|j)\s+.*?(L\w*)\s*$")
    return (
        any(
            (target := branch.fullmatch(line))
            and target.group(1) in labels
            and labels[target.group(1)] < index
            for index, line in enumerate(lines)
        ),
        body,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", required=True)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--overflow-case", required=True)
    args = parser.parse_args()

    backedge, body = has_backedge(args.compiler, args.case)
    if backedge:
        raise RuntimeError(f"finite strided dead loop remains:\n{body}")
    if "li a0, 83" not in body:
        raise RuntimeError(f"unexpected strided-loop result:\n{body}")

    overflow_backedge, overflow_body = has_backedge(args.compiler, args.overflow_case)
    if not overflow_backedge:
        raise RuntimeError(f"overflowing strided loop was unsafely deleted:\n{overflow_body}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
