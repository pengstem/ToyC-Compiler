#!/usr/bin/env python3
"""Verify that repeated hot-loop constants are materialized in the preheader."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess


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

    assembly = process.stdout.decode(errors="replace")
    body_label = assembly.find(".L_main_body:")
    if body_label < 0:
        raise RuntimeError("generated assembly has no main body label")
    preheader = assembly[:body_label]

    labels: dict[str, int] = {}
    lines = assembly.splitlines()
    for index, line in enumerate(lines):
        match = re.fullmatch(r"\s*([.$A-Za-z_][\w.$]*):\s*", line)
        if match:
            labels[match.group(1)] = index
    loop_ranges: list[tuple[int, int]] = []
    branch = re.compile(r"\s*(?:b\w+|j)\s+.*?([.$A-Za-z_][\w.$]*)\s*$")
    for index, line in enumerate(lines):
        match = branch.fullmatch(line)
        if match and match.group(1) in labels and labels[match.group(1)] < index:
            loop_ranges.append((labels[match.group(1)], index))
    if not loop_ranges:
        raise RuntimeError("stress case no longer contains a runtime loop")
    hot_code = "\n".join(
        line for begin, end in loop_ranges for line in lines[begin : end + 1]
    )

    constants = (1103515245, 1152921497, 1000000007, 354224107, 97)
    for value in constants:
        load = rf"(?m)^\s*li\s+([as]\d+),\s*{value}\s*$"
        if re.search(load, preheader) is None:
            raise RuntimeError(f"hot constant {value} was not hoisted:\n{assembly}")
        if re.search(rf"(?m)^\s*li\s+\w+,\s*{value}\s*$", hot_code):
            raise RuntimeError(f"hot loop still materializes {value}:\n{hot_code}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
