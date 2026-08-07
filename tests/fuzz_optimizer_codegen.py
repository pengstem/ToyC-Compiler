#!/usr/bin/env python3
"""Deterministically differential-test optimized ToyC code on RV32."""

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


def run(command: list[str], *, input_text: str | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        input=input_text,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=20,
        check=False,
    )


def opaque_function() -> str:
    lines = ["int opaque(int x) {"]
    for index in range(30):
        delta = index % 7 + 1
        operator = "+" if index % 2 == 0 else "-"
        lines.append(f"  x = x {operator} {delta};")
    lines.extend(["  return x;", "}"])
    return "\n".join(lines)


def make_case(rng: random.Random, case_index: int) -> str:
    parameter_count = rng.randint(9, 24)
    parameters = [f"p{i}" for i in range(parameter_count)]
    lines = [opaque_function(), "", f"int work({', '.join('int ' + p for p in parameters)}) {{"]

    variable_count = rng.randint(8, 14)
    for index in range(variable_count):
        source = parameters[rng.randrange(parameter_count)]
        lines.append(f"  int v{index} = {source};")

    # Repeated pure expressions exercise CSE; the intervening reassignment must
    # invalidate only expressions that actually read the changed value.
    lines.extend(
        [
            "  int e0 = (v0 + v1) * (v2 - v3);",
            "  int e1 = (v0 + v1) * (v2 - v3);",
            "  v0 = (v0 + v4 + 17) % 997;",
            "  int e2 = (v0 + v1) * (v2 - v3);",
        ]
    )

    for step in range(36):
        dest = rng.randrange(variable_count)
        lhs = rng.randrange(variable_count)
        rhs = rng.randrange(variable_count)
        kind = rng.randrange(5)
        if kind == 0:
            expression = f"v{lhs}"
        elif kind == 1:
            expression = f"(v{lhs} + v{rhs} + {step % 11}) % 997"
        elif kind == 2:
            expression = f"(v{lhs} - v{rhs} + 997) % 997"
        elif kind == 3:
            expression = f"(v{lhs} * {step % 7 + 2} + v{rhs}) % 997"
        else:
            expression = f"(v{lhs} + v{rhs}) * (v{lhs} - v{rhs}) % 997"
        lines.append(f"  v{dest} = {expression};")

    # A mutually recursive but unobservable SCC must not be kept alive merely
    # by its own loop-carried uses.
    lines.extend(
        [
            "  int dead0 = v3;",
            "  int dead1 = v4;",
            "  int i = 0;",
            "  int sum = e0 + e1 + e2;",
        ]
    )
    lines.append("  while (i < 7) {")
    lines.append("    int a = v0;")
    lines.append("    int b = a;")
    lines.append("    int c = b;")
    lines.append("    sum = (sum + c + v1 * v2) % 10007;")
    lines.append("    v1 = (v1 + 3) % 997;")
    lines.append("    if (dead0 < dead1 && dead0 > -997) {")
    lines.append("      dead0 = (dead0 * 3 + dead1 + i) % 997;")
    lines.append("    } else {")
    lines.append("      dead0 = (dead0 * 7 - dead1 + i) % 991;")
    lines.append("    }")
    lines.append("    dead1 = (dead1 * 5 + dead0 + i) % 997;")
    lines.append("    if (dead1 < 0) { dead1 = -dead1; }")
    lines.append("    i = i + 1;")
    lines.append("  }")
    weighted = " + ".join(f"v{i} * {i + 1}" for i in range(variable_count))
    lines.extend([f"  return sum + {weighted};", "}", "", "int main() {"])
    lines.append(f"  int seed = opaque({case_index % 31 + 1});")
    arguments = [f"(seed + {index * 3 + 1}) % 997" for index in range(parameter_count)]
    lines.append(f"  int result = work({', '.join(arguments)});")
    # Constant-trip nested affine loops exercise the runtime-initial-value
    # summary. Include zero-trip and negative-induction starts across cases.
    outer_bound = rng.randint(1, 6)
    inner_start = rng.randint(-3, 4)
    inner_end = rng.randint(-2, 6)
    coeff_i = rng.randint(-4, 5)
    coeff_j = rng.randint(-4, 5)
    constant = rng.randint(-9, 11)
    second_coeff = rng.randint(-3, 4)
    lines.extend(
        [
            "  int ni = 0;",
            "  int nj = 0;",
            "  int affine0 = result;",
            "  int affine1 = seed;",
            f"  while (ni < {outer_bound}) {{",
            f"    nj = {inner_start};",
            f"    while (nj <= {inner_end}) {{",
            f"      affine0 = affine0 + ni * {coeff_i} + nj * {coeff_j} + {constant};",
            f"      affine1 = affine1 - ni + nj * {second_coeff};",
            "      nj = nj + 1;",
            "    }",
            "    ni = ni + 1;",
            "  }",
            "  result = result + affine0 + affine1 + nj;",
        ]
    )
    # Coupled affine state exercises matrix exponentiation and, in particular,
    # sequential source assignments: each right-hand side observes all earlier
    # writes in the same iteration. Keep coefficients/trips small so the host C
    # reference stays within signed-int range while still covering negative and
    # zero coefficients.
    coupled_start = rng.randint(-2, 2)
    coupled_trips = rng.randint(2, 6)
    coupled_is_le = rng.choice((False, True))
    coupled_bound = coupled_start + coupled_trips - (1 if coupled_is_le else 0)
    coupled_relation = "<=" if coupled_is_le else "<"
    coupled_a = rng.randint(-3, 5)
    coupled_b = rng.randint(-3, 5)
    coupled_total = rng.randint(-7, 9)
    coeff_a = rng.randint(-1, 1)
    coeff_b = rng.choice((-1, 1))
    bias = rng.randint(-3, 3)
    lines.extend(
        [
            f"  int ci = {coupled_start};",
            f"  int coupled_a = result % 17 + {coupled_a};",
            f"  int coupled_b = seed % 19 + {coupled_b};",
            f"  int coupled_total = {coupled_total};",
            f"  while (ci {coupled_relation} {coupled_bound}) {{",
            f"    int coupled_next = coupled_a * {coeff_a} + coupled_b * {coeff_b} + {bias};",
            "    coupled_a = coupled_b + ci;",
            "    coupled_b = coupled_next - coupled_a;",
            "    coupled_total = coupled_total + coupled_a - coupled_b;",
            "    ci = ci + 1;",
            "  }",
            "  result = result + coupled_a + coupled_b + coupled_total + ci;",
        ]
    )
    lines.append("  result = result % 251;")
    lines.append("  if (result < 0) { result = result + 251; }")
    lines.extend(["  return result;", "}"])
    return "\n".join(lines) + "\n"


def compile_and_run(args: argparse.Namespace, source: str, case_dir: Path) -> tuple[int, int]:
    source_path = case_dir / "case.c"
    source_path.write_text(source, encoding="utf-8")

    host_path = case_dir / "host"
    host = run([args.host_cc, "-O2", "-w", str(source_path), "-o", str(host_path)])
    if host.returncode != 0:
        raise RuntimeError(f"host compile failed:\n{host.stderr}")
    host_run = run([str(host_path)])

    generated = run([args.compiler, "-opt"], input_text=source)
    if generated.returncode != 0:
        raise RuntimeError(f"toycc failed:\n{generated.stderr}\n{source}")
    assembly_path = case_dir / "case.s"
    assembly_path.write_text(STARTUP_ASM + "\n" + generated.stdout, encoding="utf-8")
    executable_path = case_dir / "case.elf"
    link = run(
        [
            args.riscv_cc,
            "-x",
            "assembler",
            "-nostdlib",
            "-nostartfiles",
            "-static",
            "-march=rv32im",
            "-mabi=ilp32",
            str(assembly_path),
            "-o",
            str(executable_path),
        ]
    )
    if link.returncode != 0:
        raise RuntimeError(f"RV32 link failed:\n{link.stderr}\n{generated.stdout}")
    rv_run = run([args.qemu, str(executable_path)])
    return host_run.returncode, rv_run.returncode


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--host-cc", required=True)
    parser.add_argument("--riscv-cc", required=True)
    parser.add_argument("--qemu", required=True)
    parser.add_argument("--cases", type=int, default=64)
    parser.add_argument("--seed", type=int, default=0x5EED)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    rng = random.Random(args.seed)
    with tempfile.TemporaryDirectory(prefix="toycc-differential-") as directory:
        root = Path(directory)
        for case_index in range(args.cases):
            source = make_case(rng, case_index)
            case_dir = root / str(case_index)
            case_dir.mkdir()
            expected, actual = compile_and_run(args, source, case_dir)
            if expected != actual:
                failed_source = Path(tempfile.gettempdir()) / "toycc-fuzz-failure.c"
                failed_source.write_text(source, encoding="utf-8")
                print(
                    f"case {case_index} failed: expected {expected}, got {actual}; "
                    f"source saved to {failed_source}"
                )
                return 1
    print(f"{args.cases} deterministic optimizer/codegen cases passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
