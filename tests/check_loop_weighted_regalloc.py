#!/usr/bin/env python3
"""Verify that cold live values do not spill hot loop state."""

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


def backward_edge_ranges(body: str) -> list[tuple[int, int]]:
    lines = body.splitlines()
    labels: dict[str, int] = {}
    for index, line in enumerate(lines):
        match = re.fullmatch(r"\s*([.$A-Za-z_][\w.$]*):\s*", line)
        if match:
            labels[match.group(1)] = index

    ranges: list[tuple[int, int]] = []
    branch = re.compile(r"\s*(?:b\w+|j)\s+.*?([.$A-Za-z_][\w.$]*)\s*$")
    for index, line in enumerate(lines):
        match = branch.fullmatch(line)
        if not match:
            continue
        target = labels.get(match.group(1))
        if target is not None and target < index:
            ranges.append((target, index))
    return ranges


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

    body = function_body(process.stdout.decode(errors="replace"), "main")
    lines = body.splitlines()
    loops = backward_edge_ranges(body)
    if not loops:
        raise RuntimeError("stress case no longer contains a runtime loop")

    hot_code = "\n".join(
        line for begin, end in loops for line in lines[begin : end + 1]
    )
    if re.search(r"(?m)^\s*(?:lw|sw)\b", hot_code):
        raise RuntimeError(f"hot loop still contains stack traffic:\n{hot_code}")
    increments = re.findall(r"(?m)^\s*addi\s+\w+,\s*\w+,\s*1\s*$", hot_code)
    if len(increments) < 2:
        raise RuntimeError(f"loop backedge does not cover two iterations:\n{hot_code}")
    if "call opaque" not in body or "call consume" not in body:
        raise RuntimeError("stress case lost the calls that keep cold values live")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
