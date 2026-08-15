#!/usr/bin/env python3
"""Verify graph coloring keeps the scalar matrix kernel out of the stack."""

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
    match = re.search(r"(?ms)^L1:\n(.*?)^L0:\n", assembly)
    if match is None:
        raise RuntimeError("matrix stress case no longer contains its runtime loop")
    loop_body = match.group(1)
    if re.search(r"(?m)^\s*(?:lw|sw)\b", loop_body):
        raise RuntimeError(f"matrix loop still spills live state:\n{loop_body}")
    if len(assembly.splitlines()) > 235:
        raise RuntimeError("matrix assembly regressed beyond the coloring budget")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
