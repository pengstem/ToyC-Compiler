#!/usr/bin/env python3
"""Differentially stress register and stack arguments across calls."""

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


def parameters(count: int) -> str:
    return ", ".join(f"int p{i}" for i in range(count))


def arguments(values: list[str]) -> str:
    return ", ".join(values)


def weighted_sum(count: int) -> str:
    return " + ".join(f"p{i} * {i + 1}" for i in range(count))


def direct_case(count: int, values: list[int]) -> str:
    return f"""\
int target({parameters(count)}) {{
    return {weighted_sum(count)};
}}

int main() {{
    return target({arguments([str(value) for value in values])});
}}
"""


def forwarding_case(count: int, values: list[int], order: list[int], deltas: list[int]) -> str:
    forwarded = [f"p{index} + {deltas[position]}" for position, index in enumerate(order)]
    recursive = ["p0 + 1", *[f"p{i}" for i in range(1, count)]]
    return f"""\
int target({parameters(count)}) {{
    return {weighted_sum(count)};
}}

int wrapper({parameters(count)}) {{
    if (p0 < -1000) {{
        return wrapper({arguments(recursive)});
    }}
    p1 = p1 + 3;
    p{count - 1} = p{count - 1} - 2;
    return target({arguments(forwarded)});
}}

int main() {{
    return wrapper({arguments([str(value) for value in values])});
}}
"""


def nested_case(count: int, values: list[int], order: list[int]) -> str:
    base = [str(value) for value in values]
    permuted = [base[index] for index in order]
    outer = [
        f"helper({arguments(base)})" if i in (0, count - 1) else permuted[i]
        for i in range(count)
    ]
    return f"""\
int target({parameters(count)}) {{
    return {weighted_sum(count)};
}}

int helper({parameters(count)}) {{
    if (p0 < -1000) {{
        return helper(p0 + 1, {arguments([f'p{i}' for i in range(1, count)])});
    }}
    return p0 - p{count - 1};
}}

int main() {{
    return target({arguments(outer)});
}}
"""


def check_case(args: argparse.Namespace, source: str, work: Path) -> tuple[bool, str]:
    source_path = work / "case.c"
    source_path.write_text(source, encoding="utf-8")

    reference = work / "reference"
    process = run([args.host_cc, "-O0", "-w", str(source_path), "-o", str(reference)])
    if process.returncode != 0:
        return False, f"host compile failed:\n{process.stderr.decode(errors='replace')}"
    expected = run([str(reference)])

    compiled = run([args.compiler, "-opt"], data=source.encode())
    if compiled.returncode != 0:
        return False, f"toycc failed:\n{compiled.stderr.decode(errors='replace')}"
    assembly = work / "case.s"
    assembly.write_bytes(STARTUP_ASM.encode() + compiled.stdout)

    executable = work / "case.elf"
    process = run(
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
    if process.returncode != 0:
        return False, f"RISC-V link failed:\n{process.stderr.decode(errors='replace')}"
    actual = run([args.qemu, str(executable)])
    if actual.returncode != expected.returncode or actual.stdout != expected.stdout:
        return False, f"expected rc={expected.returncode}, got rc={actual.returncode}"
    return True, ""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--host-cc", required=True)
    parser.add_argument("--riscv-cc", required=True)
    parser.add_argument("--qemu", required=True)
    parser.add_argument("--cases", type=int, default=120)
    parser.add_argument("--seed", type=int, default=0xF19)
    args = parser.parse_args()

    rng = random.Random(args.seed)
    with tempfile.TemporaryDirectory(prefix="toycc-many-args-") as temp:
        root = Path(temp)
        for index in range(args.cases):
            count = rng.randint(9, 20)
            values = [rng.randint(-20, 20) for _ in range(count)]
            order = list(range(count))
            rng.shuffle(order)
            deltas = [rng.randint(-3, 3) for _ in range(count)]
            kind = index % 3
            if kind == 0:
                source = direct_case(count, values)
            elif kind == 1:
                source = forwarding_case(count, values, order, deltas)
            else:
                source = nested_case(count, values, order)
            work = root / str(index)
            work.mkdir()
            ok, reason = check_case(args, source, work)
            if not ok:
                print(f"case {index} ({count} arguments) failed: {reason}")
                print(source)
                return 1
    print(f"passed {args.cases} many-argument differential cases")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
