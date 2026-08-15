#!/usr/bin/env python3
"""Verify constant modular matrix recurrences are removed before codegen."""

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
    if re.search(r"(?m)^L\d+:$", assembly):
        raise RuntimeError("constant modular matrix loop survived optimization")
    if len(assembly.splitlines()) > 55:
        raise RuntimeError("modular matrix summary regressed beyond its code-size budget")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
