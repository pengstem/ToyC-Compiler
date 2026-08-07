#!/usr/bin/env python3
"""Differentially test coupled and nested affine loop summaries on RV32."""

from __future__ import annotations

import argparse
from pathlib import Path
import random
import subprocess
import tempfile


STARTUP_ASM = """\
    .section .text
    .globl _start
_start:
    call main
    li a7, 93
    ecall
"""


def run(command: list[str], *, data: bytes | None = None) -> subprocess.CompletedProcess[bytes]:
    return subprocess.run(
        command,
        input=data,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=10,
        check=False,
    )


def coupled_case(rng: random.Random) -> str:
    start = rng.randint(-3, 3)
    trips = rng.randint(0, 10)
    step = rng.randint(1, 3)
    bound = start + trips * step
    a = rng.randint(-5, 5)
    b = rng.randint(-5, 5)
    total = rng.randint(-5, 5)
    c1 = rng.randint(-2, 2)
    c2 = rng.randint(-2, 2)
    bias = rng.randint(-3, 3)
    return f"""\
int main() {{
    int i = {start};
    int a = {a};
    int b = {b};
    int total = {total};
    while (i < {bound}) {{
        int next = a * {c1} + b * {c2} + {bias};
        a = b + i;
        b = next;
        total = total + a - b;
        i = i + {step};
    }}
    return (a + b + total + i) % 251;
}}
"""


def descending_case(rng: random.Random) -> str:
    start = rng.randint(2, 12)
    trips = rng.randint(0, start + 1)
    limit = start - trips
    a = rng.randint(-8, 8)
    b = rng.randint(-8, 8)
    return f"""\
int main() {{
    int i = {start};
    int a = {a};
    int b = {b};
    while (i > {limit}) {{
        int next = a + b * 2;
        a = b - i;
        b = next;
        i = i - 1;
    }}
    return (a * 3 + b * 5 + i) % 251;
}}
"""


def nested_case(rng: random.Random) -> str:
    outer_bound = rng.randint(0, 8)
    inner_bound = rng.randint(0, 8)
    total = rng.randint(-5, 5)
    value = rng.randint(-5, 5)
    return f"""\
int main() {{
    int outer = 0;
    int inner = 0;
    int total = {total};
    int value = {value};
    while (outer < {outer_bound}) {{
        inner = 0;
        while (inner < {inner_bound}) {{
            total = total + outer * 2 + inner * 3 + value;
            value = value + outer + 1;
            inner = inner + 1;
        }}
        outer = outer + 1;
    }}
    return (total + value + outer + inner) % 251;
}}
"""


def check_case(args: argparse.Namespace, source: str, work: Path) -> tuple[bool, str]:
    source_path = work / "case.c"
    source_path.write_text(source, encoding="utf-8")
    reference = work / "reference"
    host = run([args.host_cc, "-O0", "-w", str(source_path), "-o", str(reference)])
    if host.returncode != 0:
        return False, host.stderr.decode(errors="replace")
    expected = run([str(reference)])

    compiled = run([args.compiler, "-opt"], data=source.encode())
    if compiled.returncode != 0:
        return False, compiled.stderr.decode(errors="replace")
    assembly = work / "case.s"
    assembly.write_bytes(STARTUP_ASM.encode() + compiled.stdout)
    executable = work / "case.elf"
    linked = run(
        [
            args.riscv_cc,
            "-nostdlib",
            "-nostartfiles",
            "-static",
            "-march=rv32im",
            "-mabi=ilp32",
            str(assembly),
            "-o",
            str(executable),
        ]
    )
    if linked.returncode != 0:
        return False, linked.stderr.decode(errors="replace")
    actual = run([args.qemu, str(executable)])
    if actual.returncode != expected.returncode:
        return False, f"expected rc={expected.returncode}, got rc={actual.returncode}"
    return True, ""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--host-cc", required=True)
    parser.add_argument("--riscv-cc", required=True)
    parser.add_argument("--qemu", required=True)
    parser.add_argument("--cases", type=int, default=90)
    parser.add_argument("--seed", type=int, default=0xAFF1E)
    args = parser.parse_args()

    generators = (coupled_case, descending_case, nested_case)
    rng = random.Random(args.seed)
    with tempfile.TemporaryDirectory(prefix="toycc-affine-") as directory:
        root = Path(directory)
        for index in range(args.cases):
            source = generators[index % len(generators)](rng)
            work = root / str(index)
            work.mkdir()
            ok, reason = check_case(args, source, work)
            if not ok:
                print(f"case {index} failed: {reason}")
                print(source)
                return 1
    print(f"passed {args.cases} affine-loop differential cases")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
