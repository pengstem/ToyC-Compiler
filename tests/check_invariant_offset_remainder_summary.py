#!/usr/bin/env python3
"""Verify periodic sums with safe invariant offsets use a bounded summary."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess


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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--case", required=True)
    parser.add_argument("--negative-case", required=True)
    args = parser.parse_args()

    assembly = compile_case(args.compiler, args.case)
    edges = backward_edges(assembly)
    if len(edges) > 2:
        raise RuntimeError(f"invariant-offset inner loop survived:\n{assembly}")

    negative = compile_case(args.compiler, args.negative_case)
    if not backward_edges(negative):
        raise RuntimeError(f"negative offset unsafely lost its loop:\n{negative}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
