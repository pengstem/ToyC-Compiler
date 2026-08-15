#!/usr/bin/env python3
"""Ensure a large non-recursive helper is inlined at a hot call site."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess


def function_body(assembly: str, name: str) -> str:
    match = re.search(
        rf"(?ms)^{re.escape(name)}:\n(.*?)^\s*\.size\s+{re.escape(name)},",
        assembly,
    )
    if match is None:
        raise RuntimeError(f"generated assembly has no {name} function")
    return match.group(1)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--case", required=True)
    args = parser.parse_args()

    proc = subprocess.run(
        [args.compiler, "-opt"],
        input=Path(args.case).read_bytes(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.decode(errors="replace"))

    main_body = function_body(proc.stdout.decode(errors="replace"), "main")
    if re.search(r"(?m)^\s*call\s+mix\b", main_body):
        raise RuntimeError("large helper was not inlined into the hot loop")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
