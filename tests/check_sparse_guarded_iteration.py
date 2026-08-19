#!/usr/bin/env python3
"""Verify single-index guarded loops are sparsified without losing live prefixes."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess


def optimized_ir(compiler: str, case: str) -> str:
    process = subprocess.run(
        [compiler, "-opt", "--emit-ir"],
        input=Path(case).read_bytes(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=10,
        check=False,
    )
    if process.returncode != 0:
        raise RuntimeError(process.stderr.decode(errors="replace"))
    return process.stdout.decode(errors="replace")


def guarded_loop_remains(ir: str) -> bool:
    columns = re.findall(r"LOCAL_VAR_DECL\s+(%column\.\d+)", ir)
    return any(re.search(rf"LT\s+%\S+,\s*{re.escape(column)},", ir) for column in columns)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--case", required=True)
    parser.add_argument("--must-keep-case", required=True)
    args = parser.parse_args()

    optimized = optimized_ir(args.compiler, args.case)
    if guarded_loop_remains(optimized):
        raise RuntimeError(f"single-index guarded loop was not sparsified:\n{optimized}")

    fallback = optimized_ir(args.compiler, args.must_keep_case)
    if not guarded_loop_remains(fallback):
        raise RuntimeError(f"escaping prefix state unexpectedly lost its loop:\n{fallback}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
