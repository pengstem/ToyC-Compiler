#!/usr/bin/env python3
"""Verify that a coupled constant-trip recurrence is replaced by affine algebra."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--case", required=True)
    parser.add_argument("--reset-case", required=True)
    parser.add_argument("--must-keep-reset-dependency-case", required=True)
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

    branch = re.compile(r"\s*(?:b\w+|j)\s+.*?([.$A-Za-z_][\w.$]*)\s*$")
    backward_edges = [
        line
        for index, line in enumerate(lines)
        if (match := branch.fullmatch(line))
        and match.group(1) in labels
        and labels[match.group(1)] < index
    ]
    if backward_edges:
        raise RuntimeError(f"coupled recurrence still has a backedge:\n{assembly}")
    process = subprocess.run(
        [args.compiler, "-opt"],
        input=Path(args.reset_case).read_bytes(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=10,
        check=False,
    )
    if process.returncode != 0:
        raise RuntimeError(process.stderr.decode(errors="replace"))
    assembly = process.stdout.decode(errors="replace")
    lines = assembly.splitlines()
    labels = {}
    for index, line in enumerate(lines):
        match = re.fullmatch(r"\s*([.$A-Za-z_][\w.$]*):\s*", line)
        if match:
            labels[match.group(1)] = index
    backward_edges = [
        line
        for index, line in enumerate(lines)
        if (match := branch.fullmatch(line))
        and match.group(1) in labels
        and labels[match.group(1)] < index
    ]
    if backward_edges:
        raise RuntimeError(f"killed reset-state recurrence still has a backedge:\n{assembly}")

    process = subprocess.run(
        [args.compiler, "-opt"],
        input=Path(args.must_keep_reset_dependency_case).read_bytes(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=10,
        check=False,
    )
    if process.returncode != 0:
        raise RuntimeError(process.stderr.decode(errors="replace"))
    assembly = process.stdout.decode(errors="replace")
    lines = assembly.splitlines()
    labels = {}
    for index, line in enumerate(lines):
        match = re.fullmatch(r"\s*([.$A-Za-z_][\w.$]*):\s*", line)
        if match:
            labels[match.group(1)] = index
    backward_edges = [
        line
        for index, line in enumerate(lines)
        if (match := branch.fullmatch(line))
        and match.group(1) in labels
        and labels[match.group(1)] < index
    ]
    if not backward_edges:
        raise RuntimeError("reset state with a live entry dependency was unsafely summarized")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
