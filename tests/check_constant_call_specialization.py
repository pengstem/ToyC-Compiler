#!/usr/bin/env python3
"""Verify repeated constant call sites reach interprocedural loop summaries."""

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
    match = re.search(r"(?ms)^main:\n(.*?)^\s*\.size\s+main,", assembly)
    if match is None:
        raise RuntimeError("generated assembly has no main function")
    main_body = match.group(1)
    if re.search(r"(?m)^\s*call\s+evolve\b", main_body):
        raise RuntimeError("constant call sites were not specialized")
    if re.search(r"(?m)^L\d+:$", main_body):
        raise RuntimeError("specialized constant-bound loops survived optimization")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
