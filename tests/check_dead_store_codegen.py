#!/usr/bin/env python3
"""Verify that overwritten loop stores and their expression chains disappear."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--case", required=True)
    args = parser.parse_args()
    process = subprocess.run(
        [args.compiler, "-opt", "--emit-ir"],
        input=Path(args.case).read_bytes(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=10,
        check=False,
    )
    if process.returncode != 0:
        raise RuntimeError(process.stderr.decode(errors="replace"))
    ir = process.stdout.decode(errors="replace")
    if "#12345" in ir or "#6789" in ir or "#997" in ir:
        raise RuntimeError(f"overwritten expression remains:\n{ir}")
    if "#7" not in ir:
        raise RuntimeError(f"live modulo expression was lost:\n{ir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
