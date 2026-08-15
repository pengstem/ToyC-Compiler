#!/usr/bin/env python3
"""Verify normalized modulo fusion keeps signed fallbacks and removes duplicate work."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess


def assembly(compiler: str, case: str) -> str:
    process = subprocess.run(
        [compiler, "-opt"],
        input=Path(case).read_bytes(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=10,
        check=False,
    )
    if process.returncode != 0:
        raise RuntimeError(process.stderr.decode(errors="replace"))
    return process.stdout.decode(errors="replace")


def function_body(program: str, name: str) -> str:
    match = re.search(rf"(?ms)^{re.escape(name)}:\n(.*?)^\s*\.size\s+{re.escape(name)},", program)
    if not match:
        raise RuntimeError(f"function {name} not found:\n{program}")
    return match.group(1)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--case", required=True)
    args = parser.parse_args()

    program = assembly(args.compiler, args.case)
    main_body = function_body(program, "main")
    if re.search(r"(?m)^\s*rem\s", main_body):
        raise RuntimeError(f"power-of-two normalized remainder still uses rem:\n{main_body}")
    # The loop is unrolled three ways (one magic remainder per copy).  The
    # deliberately signed tail normalization needs two more, and `% 251` one.
    if len(re.findall(r"(?m)^\s*mulh\s", main_body)) > 6:
        raise RuntimeError(f"nonnegative normalized remainder kept duplicate magic divisions:\n{main_body}")

    unknown_body = function_body(program, "normalize_unknown")
    if len(re.findall(r"(?m)^\s*mulh\s", unknown_body)) < 2:
        raise RuntimeError(f"signed unknown remainder lost its normalization fallback:\n{unknown_body}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
