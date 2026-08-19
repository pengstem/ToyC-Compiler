#!/usr/bin/env python3
"""Verify bounded helper loops are summarized only in proven nonnegative contexts."""

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


def function_assembly(assembly: str, name: str) -> str:
    match = re.search(
        rf"(?m)^{re.escape(name)}:\s*$.*?^\s*\.size\s+{re.escape(name)},[^\n]*$",
        assembly,
        re.DOTALL,
    )
    if match is None:
        raise RuntimeError(f"function {name} is missing:\n{assembly}")
    return match.group(0)


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
    parser.add_argument("--must-keep-case", action="append", default=[])
    parser.add_argument("--must-keep-nested-case", action="append", default=[])
    args = parser.parse_args()

    assembly = compile_case(args.compiler, args.case)
    main_body = function_assembly(assembly, "main")
    if backward_edges(main_body):
        raise RuntimeError(f"bounded helper loop remains in main:\n{main_body}")
    # The standalone helper has an unknown signed argument and must retain its
    # loop. Only the inlined copy inside the proven outer range is summarized.
    helper_body = function_assembly(assembly, "bounded_bucket")
    if not backward_edges(helper_body):
        raise RuntimeError(f"unproven standalone helper lost its loop:\n{helper_body}")

    for case in args.must_keep_case:
        fallback = function_assembly(compile_case(args.compiler, case), "main")
        if not backward_edges(fallback):
            raise RuntimeError(f"unsafe case unexpectedly lost its loop ({case}):\n{fallback}")
    for case in args.must_keep_nested_case:
        fallback = function_assembly(compile_case(args.compiler, case), "main")
        if len(backward_edges(fallback)) < 2:
            raise RuntimeError(f"unsafe case unexpectedly lost its inner loop ({case}):\n{fallback}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
