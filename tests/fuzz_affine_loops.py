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
    addi sp, sp, -16
    sw a0, 0(sp)
    li a0, 1
    mv a1, sp
    li a2, 4
    li a7, 64
    ecall
    lw a0, 0(sp)
    addi sp, sp, 16
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
    trips = rng.randint(0, 200)
    step = rng.randint(1, 3)
    bound = start + trips * step
    a = rng.randint(-100, 100)
    b = rng.randint(-100, 100)
    total = rng.randint(-100, 100)
    c1 = rng.randint(-8, 8)
    c2 = rng.randint(-8, 8)
    bias = rng.randint(-20, 20)
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
    outer_bound = rng.randint(0, 40)
    inner_bound = rng.randint(0, 40)
    total = rng.randint(-100, 100)
    value = rng.randint(-100, 100)
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


def matrix_case(rng: random.Random) -> str:
    count = rng.randint(10, 20)
    trips = rng.randint(0, 120)
    values = [rng.randint(-100, 100) for _ in range(count)]
    lines = ["int main() {", "    int i = 0;"]
    lines.extend(f"    int v{index} = {value};" for index, value in enumerate(values))
    lines.append(f"    while (i < {trips}) {{")
    for index in range(count):
        lhs = rng.randrange(count)
        rhs = rng.randrange(count)
        sign = "+" if rng.randrange(2) == 0 else "-"
        lines.append(f"        v{index} = v{lhs} {sign} v{rhs};")
    lines.extend(["        i = i + 1;", "    }"])
    result = " + ".join(f"v{index} * {index + 1}" for index in range(count))
    lines.extend([f"    return ({result} + i) % 251;", "}"])
    return "\n".join(lines) + "\n"


def function_case(rng: random.Random) -> str:
    trips = rng.randint(0, 120)
    values = [rng.randint(-100, 100) for _ in range(8)]
    arguments = ", ".join(str(value) for value in values)
    return f"""\
int work(int a, int b, int c, int d, int e, int f, int g, int h) {{
    if (a < -10000) {{
        return work(a + 1, b, c, d, e, f, g, h);
    }}
    int i = 0;
    while (i < {trips}) {{
        int next = a * 2 + b * 3 + c;
        a = b + d;
        b = next;
        c = c + e - f;
        d = d + g + i;
        e = e + h;
        i = i + 1;
    }}
    return a + b * 2 + c * 3 + d * 4 + e * 5 + i;
}}

int main() {{
    return work({arguments});
}}
"""


def moving_bound_case(rng: random.Random) -> str:
    start = rng.randint(-20, 0)
    limit = rng.randint(10, 200)
    induction_step = rng.randint(1, 5)
    bound_step = rng.randint(-4, induction_step - 1)
    a = rng.randint(-100, 100)
    b = rng.randint(-100, 100)
    return f"""\
int main() {{
    int i = {start};
    int bound = {limit};
    int a = {a};
    int b = {b};
    while (i <= bound) {{
        int next = a * 2 + b * 3 + i;
        a = b + bound;
        b = next;
        i = i + {induction_step};
        bound = bound + {bound_step};
    }}
    return (a + b + i * 3 + bound * 5) % 251;
}}
"""


def check_case(args: argparse.Namespace, source: str, work: Path) -> tuple[bool, str]:
    source_path = work / "case.c"
    host_source = source.replace("int main()", "int toy_main()", 1)
    host_source += (
        "\n#include <unistd.h>\n"
        "int main(void) { int value = toy_main(); "
        "write(1, &value, sizeof(value)); return value; }\n"
    )
    source_path.write_text(host_source, encoding="utf-8")
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
    if actual.returncode != expected.returncode or actual.stdout != expected.stdout:
        return False, (
            f"expected rc={expected.returncode}, value={expected.stdout.hex()}, "
            f"got rc={actual.returncode}, value={actual.stdout.hex()}"
        )
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

    generators = (
        coupled_case,
        descending_case,
        nested_case,
        matrix_case,
        function_case,
        moving_bound_case,
    )
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
