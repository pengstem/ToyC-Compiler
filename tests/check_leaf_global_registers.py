#!/usr/bin/env python3
"""Verify a leaf scalar-global kernel uses argument registers without loop memory traffic."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--case", required=True)
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
    if len(re.findall(r"(?m)^\s*lw\s+a[1-7],\s*0\(t2\)$", assembly)) < 4:
        raise RuntimeError("leaf global allocator did not use the spare argument-register bank")
    if re.search(r"(?m)^\s*sw\s+a0,\s*0\(t2\)$", assembly):
        raise RuntimeError("a0 must remain reserved for the function return value")
    match = re.search(r"(?ms)^L1:\n(.*?)^L0:\n", assembly)
    if match is None:
        raise RuntimeError("global pressure case no longer contains its runtime loop")
    loop_body = match.group(1)
    if re.search(r"(?m)^\s*(?:la|lw|sw)\b", loop_body):
        raise RuntimeError(f"global matrix loop still performs memory traffic:\n{loop_body}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
