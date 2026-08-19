#!/usr/bin/env python3
"""Verify leading-break bound tightening and moving-threshold safety."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess


def has_backedge(compiler: str, source: str) -> bool:
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
    lines = process.stdout.decode(errors="replace").splitlines()
    labels: dict[str, int] = {}
    for index, line in enumerate(lines):
        match = re.fullmatch(r"\s*([.$A-Za-z_][\w.$]*):\s*", line)
        if match:
            labels[match.group(1)] = index
    branch = re.compile(r"\s*(?:b\w+|j)\s+.*?([.$A-Za-z_][\w.$]*)\s*$")
    return any(
        (match := branch.fullmatch(line))
        and match.group(1) in labels
        and labels[match.group(1)] < index
        for index, line in enumerate(lines)
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--case", required=True)
    parser.add_argument("--must-keep-case", required=True)
    args = parser.parse_args()
    if has_backedge(args.compiler, args.case):
        raise RuntimeError("leading-break affine loop still has a backedge")
    if not has_backedge(args.compiler, args.must_keep_case):
        raise RuntimeError("moving leading-break threshold was unsafely tightened")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
