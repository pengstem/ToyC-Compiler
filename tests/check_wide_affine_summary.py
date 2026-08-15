#!/usr/bin/env python3
"""Verify that a wide coupled recurrence is replaced by bounded affine algebra."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess


def compile_case(compiler: str, source: bytes, timeout: float) -> str:
    process = subprocess.run(
        [compiler, "-opt"],
        input=source,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=timeout,
        check=False,
    )
    if process.returncode != 0:
        raise RuntimeError(process.stderr.decode(errors="replace"))
    return process.stdout.decode(errors="replace")


def generated_helper_case(state_count: int) -> bytes:
    lines = ["int evolve(int limit) {"]
    lines.extend(f"  int s{i} = {i + 1};" for i in range(state_count))
    lines.extend(["  int i = 0;", "  while (i < limit) {"])
    lines.extend(
        f"    int n{i} = (s{i} * 3 + s{(i + 1) % state_count} * 5 + "
        f"s{(i + 2) % state_count} * 7) % 10007;"
        for i in range(state_count)
    )
    lines.extend(f"    s{i} = n{i};" for i in range(state_count))
    lines.extend(
        [
            "    i = i + 1;",
            "  }",
            "  return (" + " + ".join(f"s{i}" for i in range(state_count)) + ") % 251;",
            "}",
            "int main() { return evolve(1000000); }",
        ]
    )
    return ("\n".join(lines) + "\n").encode()


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
    args = parser.parse_args()

    assembly = compile_case(args.compiler, Path(args.case).read_bytes(), 10)
    if backward_edges(assembly):
        raise RuntimeError(f"wide coupled recurrence still has a backedge:\n{assembly}")

    # A 100-state helper crosses both the ordinary helper-inline budget and the
    # former 96-variable affine workset. The unused original helper may retain
    # its own loop, but main must contain neither the call nor a runtime backedge.
    generated = compile_case(args.compiler, generated_helper_case(100), 20)
    main_match = re.search(r"(?ms)^main:\s*$.*?(?=^\s*\.size\s+main\b)", generated)
    if not main_match:
        raise RuntimeError("generated wide helper case has no main assembly body")
    main_assembly = main_match.group(0)
    if re.search(r"\bcall\s+evolve\b", main_assembly) or backward_edges(main_assembly):
        raise RuntimeError(f"100-state helper survived in main:\n{main_assembly}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
