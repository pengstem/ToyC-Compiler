#!/usr/bin/env python3
"""Verify that proven affine loops become loop-free runtime algebra."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--case", action="append", required=True)
    args = parser.parse_args()

    for case in args.case:
        process = subprocess.run(
            [args.compiler, "-opt"],
            input=Path(case).read_bytes(),
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
            raise RuntimeError(f"generated assembly has no main for {case}")
        body = match.group(1)
        if re.search(r"(?m)^\s*(?:b\w+|j)\s+L\w+", body):
            raise RuntimeError(f"affine loop remains in {case}:\n{body}")
        if not re.search(r"(?m)^\s*mul\b", body):
            raise RuntimeError(f"expected runtime affine algebra in {case}:\n{body}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
