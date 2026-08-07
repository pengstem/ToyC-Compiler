#!/usr/bin/env python3
"""Verify dead pure-call deletion and call-side-effect safety."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess


def compile_source(compiler: str, path: str) -> str:
    process = subprocess.run(
        [compiler, "-opt"],
        input=Path(path).read_bytes(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=10,
        check=False,
    )
    if process.returncode != 0:
        raise RuntimeError(process.stderr.decode(errors="replace"))
    return process.stdout.decode(errors="replace")


def main_body(assembly: str) -> str:
    match = re.search(r"(?ms)^main:\n(.*?)^\s*\.size\s+main,", assembly)
    if match is None:
        raise RuntimeError("generated assembly has no main")
    return match.group(1)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--case", required=True)
    parser.add_argument("--loop-case", required=True)
    parser.add_argument("--nested-loop-case", required=True)
    parser.add_argument("--side-effect-case", required=True)
    parser.add_argument("--termination-case", required=True)
    parser.add_argument("--moving-loop-case", required=True)
    args = parser.parse_args()

    body = main_body(compile_source(args.compiler, args.case))
    if re.search(r"(?m)^\s*call\s+heavy\b", body):
        raise RuntimeError(f"dead pure call remains:\n{body}")
    if re.search(r"(?m)^\s*(?:b\w+|j)\s+L\w+", body):
        raise RuntimeError(f"newly summarizable loop remains:\n{body}")

    body = main_body(compile_source(args.compiler, args.loop_case))
    if re.search(r"(?m)^\s*call\s+heavy_loop\b", body):
        raise RuntimeError(f"dead finite-loop call remains:\n{body}")

    body = main_body(compile_source(args.compiler, args.nested_loop_case))
    if re.search(r"(?m)^\s*call\s+nested_loop\b", body):
        raise RuntimeError(f"dead nested-loop call remains:\n{body}")

    body = main_body(compile_source(args.compiler, args.side_effect_case))
    if not re.search(r"(?m)^\s*call\s+impure\b", body):
        raise RuntimeError("dead-result impure call was deleted")

    body = main_body(compile_source(args.compiler, args.termination_case))
    if not re.search(r"(?m)^\s*call\s+spin\b", body):
        raise RuntimeError("possibly nonterminating pure call was deleted")

    body = main_body(compile_source(args.compiler, args.moving_loop_case))
    if not re.search(r"(?m)^\s*(?:call\s+drift\b|(?:b\w+|j)\s+L\w+)", body):
        raise RuntimeError("moving-bound pure loop was unsafely deleted")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
