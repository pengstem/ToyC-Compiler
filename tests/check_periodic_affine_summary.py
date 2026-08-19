#!/usr/bin/env python3
"""Verify that a modulo-controlled affine loop is summarized without a backedge."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess


def backward_edges(assembly: str) -> list[str]:
    lines = assembly.splitlines()
    labels: dict[str, int] = {}
    for index, line in enumerate(lines):
        match = re.fullmatch(r"\s*([.$A-Za-z_][\w.$]*):\s*", line)
        if match:
            labels[match.group(1)] = index
    branch = re.compile(r"\s*(?:b\w+|j)\s+.*?([.$A-Za-z_][\w.$]*)\s*$")
    return [
        line
        for index, line in enumerate(lines)
        if (match := branch.fullmatch(line))
        and match.group(1) in labels
        and labels[match.group(1)] < index
    ]


def compile_case(compiler: str, case: str) -> str:
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--case", required=True)
    parser.add_argument("--must-keep-case", action="append", default=[])
    args = parser.parse_args()

    assembly = compile_case(args.compiler, args.case)
    if backward_edges(assembly):
        raise RuntimeError(f"periodic affine loop still has a backedge:\n{assembly}")
    for case in args.must_keep_case:
        fallback = compile_case(args.compiler, case)
        if not backward_edges(fallback):
            raise RuntimeError(f"unsafe case unexpectedly lost its loop ({case}):\n{fallback}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
