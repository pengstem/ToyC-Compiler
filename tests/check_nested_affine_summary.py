#!/usr/bin/env python3
"""Verify that constant-trip affine inner loops no longer have backedges."""

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
    lines = assembly.splitlines()
    labels: dict[str, int] = {}
    for index, line in enumerate(lines):
        match = re.fullmatch(r"\s*([.$A-Za-z_][\w.$]*):\s*", line)
        if match:
            labels[match.group(1)] = index

    backward_edges: list[str] = []
    branch = re.compile(r"\s*(?:b\w+|j)\s+.*?([.$A-Za-z_][\w.$]*)\s*$")
    for index, line in enumerate(lines):
        match = branch.fullmatch(line)
        if match and match.group(1) in labels and labels[match.group(1)] < index:
            backward_edges.append(line)
    if len(backward_edges) != 1:
        raise RuntimeError(
            "expected only the outer runtime-loop backedge, got "
            f"{len(backward_edges)}:\n{assembly}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
