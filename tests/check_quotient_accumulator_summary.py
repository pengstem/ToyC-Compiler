#!/usr/bin/env python3
"""Verify quotient-bucket closed forms and their signed/state safety gates."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess


def compile_main(compiler: str, source: str) -> str:
    process = subprocess.run(
        [compiler, "-opt"],
        input=Path(source).read_bytes(),
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
        raise RuntimeError("generated assembly has no main")
    return match.group(1)


def has_loop(body: str) -> bool:
    return re.search(r"(?m)^\s*(?:b\w+|j)\s+L\w+", body) is not None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--case", required=True)
    parser.add_argument("--must-keep-case", action="append", default=[])
    args = parser.parse_args()

    body = compile_main(args.compiler, args.case)
    if has_loop(body):
        raise RuntimeError(f"quotient accumulator loop remains:\n{body}")
    for source in args.must_keep_case:
        body = compile_main(args.compiler, source)
        if not has_loop(body):
            raise RuntimeError(f"unsafe quotient loop was summarized: {source}\n{body}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
