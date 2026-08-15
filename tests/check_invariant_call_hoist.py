#!/usr/bin/env python3
"""Verify invariant pure calls move before loops while state-dependent calls remain."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess


def optimized_ir(compiler: str, case: str) -> list[str]:
    process = subprocess.run(
        [compiler, "-opt", "--emit-ir"],
        input=Path(case).read_bytes(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=10,
        check=False,
    )
    if process.returncode != 0:
        raise RuntimeError(process.stderr.decode(errors="replace"))
    text = process.stdout.decode(errors="replace")
    begin = text.index("FUNC_BEGIN main")
    end = text.index("FUNC_END main", begin)
    return text[begin:end].splitlines()


def call_is_before_loop(lines: list[str]) -> bool:
    call = next((index for index, line in enumerate(lines) if "CALL" in line and "countdown" in line), -1)
    loop = next((index for index, line in enumerate(lines) if line.startswith("BRANCH ")), -1)
    return call >= 0 and loop >= 0 and call < loop


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--case", required=True)
    parser.add_argument("--must-keep-case", required=True)
    args = parser.parse_args()

    if not call_is_before_loop(optimized_ir(args.compiler, args.case)):
        raise RuntimeError("pure invariant call was not hoisted before the loop")
    if call_is_before_loop(optimized_ir(args.compiler, args.must_keep_case)):
        raise RuntimeError("global-reading call was unsafely hoisted")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
