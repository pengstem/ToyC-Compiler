#!/usr/bin/env python3
"""Guard the code-shape properties of the hot leaf-call benchmark."""

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

    source = Path(args.case).read_bytes()
    proc = subprocess.run(
        [args.compiler, "-opt"],
        input=source,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.decode(errors="replace"))

    assembly = proc.stdout.decode(errors="replace")
    classify = function_body(assembly, "classify")
    hot_main = function_body(assembly, "main")

    failures: list[str] = []
    if re.search(r"(?m)^\s*call\s+classify\b", hot_main):
        failures.append("classify was not inlined into the hot loop")
    if re.search(r"(?m)^\s*(?:addi\s+sp|sw\s+|lw\s+)", classify):
        failures.append("leaf classify still has stack-frame traffic")
    if len(re.findall(r"(?m)^\s*bge\s+", hot_main)) < 12:
        failures.append("loop guard constants were not lowered to direct branches")
    if re.search(r"(?m)^\s*slti\s+", hot_main):
        failures.append("hot loop still contains slti plus branch guard sequences")

    if failures:
        raise RuntimeError("; ".join(failures))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
