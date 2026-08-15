#!/usr/bin/env python3
"""Verify loop-invariant empty paths are tested once before loop entry."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess


def compile_case(compiler: str, case: str, *, emit_ir: bool = False) -> str:
    command = [compiler, "-opt"]
    if emit_ir:
        command.append("--emit-ir")
    process = subprocess.run(
        command,
        input=Path(case).read_bytes(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=10,
        check=False,
    )
    if process.returncode != 0:
        raise RuntimeError(process.stderr.decode(errors="replace"))
    return process.stdout.decode(errors="replace")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--case", required=True)
    parser.add_argument("--live-induction-case", required=True)
    args = parser.parse_args()

    assembly = compile_case(args.compiler, args.case)
    if re.search(r"(?m)^\s*b\w+\s+[^\n]*,?\s*(L\d+)\n\s*j\s+L\d+\nL\d+:$", assembly) is None:
        raise RuntimeError(f"invariant guard was not hoisted before loop entry:\n{assembly}")

    live_ir = compile_case(args.compiler, args.live_induction_case, emit_ir=True)
    if re.search(r"(?m)^BEQZ L\d+:, %\w+\nBRANCH L\d+:\nLABEL L\d+:$", live_ir):
        raise RuntimeError(f"loop with live final induction was unsafely skipped:\n{live_ir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
