#!/usr/bin/env python3
"""Verify finite dead-loop deletion and moving-bound termination safety."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--case", required=True)
    parser.add_argument("--descending-case", required=True)
    parser.add_argument("--reversed-case", required=True)
    parser.add_argument("--break-case", required=True)
    parser.add_argument("--continue-case", required=True)
    parser.add_argument("--live-break-case", required=True)
    parser.add_argument("--live-continue-case", required=True)
    parser.add_argument("--unsafe-continue-case", required=True)
    parser.add_argument("--must-keep-case", required=True)
    parser.add_argument("--overflow-case", required=True)
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
    match = re.search(r"(?ms)^main:\n(.*?)^\s*\.size\s+main,", assembly)
    if match is None:
        raise RuntimeError("generated assembly has no main")
    body = match.group(1)
    if re.search(r"(?m)^\s*(?:b\w+|j)\s+L\w+", body):
        raise RuntimeError(f"dead loop remains:\n{body}")
    if "li a0, 73" not in body:
        raise RuntimeError(f"unexpected dead-loop result:\n{body}")

    process = subprocess.run(
        [args.compiler, "-opt"],
        input=Path(args.descending_case).read_bytes(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=10,
        check=False,
    )
    if process.returncode != 0:
        raise RuntimeError(process.stderr.decode(errors="replace"))
    assembly = process.stdout.decode(errors="replace")
    match = re.search(r"(?ms)^main:\n(.*?)^\s*\.size\s+main,", assembly)
    if match is None:
        raise RuntimeError("generated assembly has no main")
    body = match.group(1)
    if re.search(r"(?m)^\s*(?:b\w+|j)\s+L\w+", body):
        raise RuntimeError(f"descending dead loop remains:\n{body}")
    if "li a0, 91" not in body:
        raise RuntimeError(f"unexpected descending-loop result:\n{body}")

    process = subprocess.run(
        [args.compiler, "-opt"],
        input=Path(args.reversed_case).read_bytes(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=10,
        check=False,
    )
    if process.returncode != 0:
        raise RuntimeError(process.stderr.decode(errors="replace"))
    assembly = process.stdout.decode(errors="replace")
    match = re.search(r"(?ms)^main:\n(.*?)^\s*\.size\s+main,", assembly)
    if match is None:
        raise RuntimeError("generated assembly has no main")
    body = match.group(1)
    if re.search(r"(?m)^\s*(?:b\w+|j)\s+L\w+", body):
        raise RuntimeError(f"reversed-comparison dead loop remains:\n{body}")
    if "li a0, 83" not in body:
        raise RuntimeError(f"unexpected reversed-loop result:\n{body}")

    process = subprocess.run(
        [args.compiler, "-opt"],
        input=Path(args.break_case).read_bytes(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=10,
        check=False,
    )
    if process.returncode != 0:
        raise RuntimeError(process.stderr.decode(errors="replace"))
    assembly = process.stdout.decode(errors="replace")
    match = re.search(r"(?ms)^main:\n(.*?)^\s*\.size\s+main,", assembly)
    if match is None:
        raise RuntimeError("generated assembly has no main")
    body = match.group(1)
    if re.search(r"(?m)^\s*(?:b\w+|j)\s+L\w+", body):
        raise RuntimeError(f"dead loop with break remains:\n{body}")
    if "li a0, 67" not in body:
        raise RuntimeError(f"unexpected dead-break result:\n{body}")

    process = subprocess.run(
        [args.compiler, "-opt"],
        input=Path(args.continue_case).read_bytes(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=10,
        check=False,
    )
    if process.returncode != 0:
        raise RuntimeError(process.stderr.decode(errors="replace"))
    assembly = process.stdout.decode(errors="replace")
    match = re.search(r"(?ms)^main:\n(.*?)^\s*\.size\s+main,", assembly)
    if match is None:
        raise RuntimeError("generated assembly has no main")
    body = match.group(1)
    if re.search(r"(?m)^\s*(?:b\w+|j)\s+L\w+", body):
        raise RuntimeError(f"dead loop with continue remains:\n{body}")
    if "li a0, 79" not in body:
        raise RuntimeError(f"unexpected dead-continue result:\n{body}")

    process = subprocess.run(
        [args.compiler, "-opt"],
        input=Path(args.live_break_case).read_bytes(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=10,
        check=False,
    )
    if process.returncode != 0:
        raise RuntimeError(process.stderr.decode(errors="replace"))
    assembly = process.stdout.decode(errors="replace")
    match = re.search(r"(?ms)^main:\n(.*?)^\s*\.size\s+main,", assembly)
    if match is None:
        raise RuntimeError("generated assembly has no main")
    if not re.search(r"(?m)^\s*(?:b\w+|j)\s+L\w+", match.group(1)):
        raise RuntimeError("live loop with break was unsafely deleted")

    process = subprocess.run(
        [args.compiler, "-opt"],
        input=Path(args.live_continue_case).read_bytes(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=10,
        check=False,
    )
    if process.returncode != 0:
        raise RuntimeError(process.stderr.decode(errors="replace"))
    assembly = process.stdout.decode(errors="replace")
    match = re.search(r"(?ms)^main:\n(.*?)^\s*\.size\s+main,", assembly)
    if match is None:
        raise RuntimeError("generated assembly has no main")
    if not re.search(r"(?m)^\s*(?:b\w+|j)\s+L\w+", match.group(1)):
        raise RuntimeError("live loop with continue was unsafely deleted")

    process = subprocess.run(
        [args.compiler, "-opt"],
        input=Path(args.unsafe_continue_case).read_bytes(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=10,
        check=False,
    )
    if process.returncode != 0:
        raise RuntimeError(process.stderr.decode(errors="replace"))
    assembly = process.stdout.decode(errors="replace")
    match = re.search(r"(?ms)^main:\n(.*?)^\s*\.size\s+main,", assembly)
    if match is None:
        raise RuntimeError("generated assembly has no main")
    if not re.search(r"(?m)^\s*(?:b\w+|j)\s+L\w+", match.group(1)):
        raise RuntimeError("continue that bypasses induction was unsafely deleted")

    process = subprocess.run(
        [args.compiler, "-opt"],
        input=Path(args.must_keep_case).read_bytes(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=10,
        check=False,
    )
    if process.returncode != 0:
        raise RuntimeError(process.stderr.decode(errors="replace"))
    assembly = process.stdout.decode(errors="replace")
    match = re.search(r"(?ms)^main:\n(.*?)^\s*\.size\s+main,", assembly)
    if match is None:
        raise RuntimeError("generated assembly has no main")
    body = match.group(1)
    if not re.search(r"(?m)^\s*(?:b\w+|j)\s+L\w+", body):
        raise RuntimeError(f"moving-bound loop was unsafely deleted:\n{body}")

    process = subprocess.run(
        [args.compiler, "-opt"],
        input=Path(args.overflow_case).read_bytes(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=10,
        check=False,
    )
    if process.returncode != 0:
        raise RuntimeError(process.stderr.decode(errors="replace"))
    assembly = process.stdout.decode(errors="replace")
    match = re.search(
        r"(?ms)^unsafe_inclusive_loop:\n(.*?)^\s*\.size\s+unsafe_inclusive_loop,",
        assembly,
    )
    if match is None:
        raise RuntimeError("generated assembly has no unsafe_inclusive_loop")
    if not re.search(r"(?m)^\s*(?:b\w+|j)\s+L\w+", match.group(1)):
        raise RuntimeError("overflowing inclusive loop was unsafely deleted")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
