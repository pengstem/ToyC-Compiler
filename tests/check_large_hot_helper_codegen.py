#!/usr/bin/env python3
"""Ensure a large modular helper is inlined and its long recurrence summarized."""

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

    proc = subprocess.run(
        [args.compiler, "-opt"],
        input=Path(args.case).read_bytes(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.decode(errors="replace"))

    main_body = function_body(proc.stdout.decode(errors="replace"), "main")
    if re.search(r"(?m)^\s*call\s+mix\b", main_body):
        raise RuntimeError("large helper was not inlined into the hot loop")
    lines = main_body.splitlines()
    labels: dict[str, int] = {}
    for index, line in enumerate(lines):
        match = re.fullmatch(r"\s*([.$A-Za-z_][\w.$]*):\s*", line)
        if match:
            labels[match.group(1)] = index
    branch = re.compile(r"\s*(?:b\w+|j)\s+.*?([.$A-Za-z_][\w.$]*)\s*$")
    if any(
        (match := branch.fullmatch(line))
        and match.group(1) in labels
        and labels[match.group(1)] < index
        for index, line in enumerate(lines)
    ):
        raise RuntimeError(f"long modular helper recurrence still has a backedge:\n{main_body}")
    if "li a0, 29" not in main_body:
        raise RuntimeError(f"unexpected summarized helper result:\n{main_body}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
