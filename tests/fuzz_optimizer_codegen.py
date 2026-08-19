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


def bounded_bucket_function(modulus: int, coefficient: int, bias: int) -> str:
    return "\n".join(
        [
            "int bounded_bucket(int value) {",
            f"  int bound = value % {modulus};",
            "  int index = 0;",
            "  int total = 1;",
            "  while (index < bound) {",
            f"    total = total + index * {coefficient} + {bias};",
            "    index = index + 1;",
            "  }",
            "  return total;",
            "}",
        ]
    )


def make_case(rng: random.Random, case_index: int) -> str:
    parameter_count = rng.randint(9, 24)
    parameters = [f"p{i}" for i in range(parameter_count)]
    bounded_modulus = rng.randint(2, 17)
    bounded_coefficient = rng.randint(1, 5)
    bounded_bias = rng.randint(-4, 6)
    lines = [
        "int fuzz_dead_global = 7;",
        "int fuzz_observed_global = 3;",
        "",
        "int read_fuzz_observed_global() { return fuzz_observed_global; }",
        "",
        opaque_function(),
        "",
        bounded_bucket_function(bounded_modulus, bounded_coefficient, bounded_bias),
        "",
        f"int work({', '.join('int ' + p for p in parameters)}) {{",
    ]

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
    dead_global_trips = rng.randint(2, 9)
    observed_global_value = rng.randint(-20, 30)
    lines.extend(
        [
            "  int dead_global_i = 0;",
            f"  while (dead_global_i < {dead_global_trips}) {{",
            "    fuzz_dead_global = "
            "(fuzz_dead_global * 13 + dead_global_i * 7 + 19) % 997;",
            "    dead_global_i = dead_global_i + 1;",
            "  }",
            f"  fuzz_observed_global = {observed_global_value};",
            "  result = result + read_fuzz_observed_global();",
        ]
    )
    dead_stride_step = rng.randint(2, 7)
    dead_stride_trips = rng.randint(2, 12)
    dead_stride_start = rng.randint(-5, 5)
    dead_stride_bound = dead_stride_start + dead_stride_step * dead_stride_trips - 1
    lines.extend(
        [
            f"  int dead_stride_i = {dead_stride_start};",
            "  int dead_stride_state = result % 17;",
            f"  while (dead_stride_i < {dead_stride_bound}) {{",
            "    dead_stride_state = "
            "(dead_stride_state * 11 + dead_stride_i * 5 + 23) % 991;",
            f"    dead_stride_i = dead_stride_i + {dead_stride_step};",
            "  }",
        ]
    )
    # A helper-local loop has a runtime bound, but the caller proves its input
    # nonnegative and periodic. Differentially cover the inner closed form and
    # the subsequent periodic outer-loop summary as one optimization cascade.
    bounded_trips = rng.randint(2, 35)
    lines.extend(
        [
            "  int bounded_i = 0;",
            "  int bounded_total = result;",
            f"  while (bounded_i < {bounded_trips}) {{",
            "    bounded_total = bounded_total + bounded_bucket(bounded_i);",
            "    bounded_i = bounded_i + 1;",
            "  }",
            "  result = result + bounded_total + bounded_i;",
        ]
    )
    # Nonnegative induction division is a sequence of constant-width quotient
    # buckets. Differentially exercise its closed form, including a helper-like
    # offset, signed scaling, and a constant added through a temporary chain.
    quotient_start = rng.randint(0, 5)
    quotient_trips = rng.randint(2, 40)
    quotient_offset = rng.randint(0, 9)
    quotient_divisor = rng.randint(2, 13)
    quotient_scale = rng.choice((-3, -2, -1, 1, 2, 3, 4))
    quotient_bias = rng.randint(-7, 9)
    lines.extend(
        [
            f"  int quotient_i = {quotient_start};",
            "  int quotient_total = result;",
            f"  while (quotient_i < {quotient_start + quotient_trips}) {{",
            "    quotient_total = quotient_total + "
            f"((quotient_i + {quotient_offset}) / {quotient_divisor}) * "
            f"{quotient_scale} + {quotient_bias};",
            "    quotient_i = quotient_i + 1;",
            "  }",
            "  result = result + quotient_total + quotient_i;",
        ]
    )
    # A small modular state machine has a finite transition graph independent
    # of its much larger trip count. Exercise cycle discovery and symbolic
    # proof that the accumulator remains an additive translation.
    state_modulus = rng.randint(3, 31)
    state_multiplier = rng.randint(2, 11)
    state_bias = rng.randint(1, 13)
    state_initial = rng.randrange(state_modulus)
    state_score = rng.randint(1, 4)
    state_trips = rng.randint(1100, 1250)
    lines.extend(
        [
            "  int state_i = 0;",
            f"  int cycle_state = {state_initial};",
            "  int cycle_total = result;",
            f"  while (state_i < {state_trips}) {{",
            "    cycle_state = "
            f"(cycle_state * {state_multiplier} + {state_bias}) % {state_modulus};",
            f"    cycle_total = cycle_total + cycle_state * {state_score} + 3;",
            "    state_i = state_i + 1;",
            "  }",
            "  result = result + cycle_total + cycle_state + state_i;",
        ]
    )
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
    # Keep more than twenty observable affine states live so matrix summary
    # coverage cannot silently regress to the old narrow dimension cap.
    wide_count = 21
    wide_trips = rng.randint(2, 8)
    lines.append("  int wide_i = 0;")
    for index in range(wide_count):
        lines.append(f"  int wide_{index} = result % 17 + {index - 10};")
    lines.append(f"  while (wide_i < {wide_trips}) {{")
    for index in range(wide_count - 1):
        lines.append(f"    wide_{index} = wide_{index + 1};")
    lines.append("    wide_20 = wide_0;")
    lines.append("    wide_i = wide_i + 1;")
    lines.append("  }")
    wide_sum = " + ".join(f"wide_{index}" for index in range(wide_count))
    lines.append(f"  result = result + {wide_sum} + wide_i;")
    # Nested coupled recurrences reset the inner state on every outer
    # iteration. The outer affine summary may compose with the inner one only
    # when those reset variables' entry values are fully killed.
    reset_outer_trips = rng.randint(2, 7)
    reset_inner_trips = rng.randint(2, 5)
    reset_a_initial = rng.randint(-3, 5)
    reset_b_initial = rng.randint(-3, 5)
    reset_i_coeff = rng.randint(-2, 2)
    reset_total_coeff = rng.choice((-2, -1, 1, 2))
    lines.extend(
        [
            "  int reset_i = 0;",
            "  int reset_j = 0;",
            "  int reset_a = 0;",
            "  int reset_b = 0;",
            "  int reset_total = result;",
            f"  while (reset_i < {reset_outer_trips}) {{",
            f"    reset_a = {reset_a_initial};",
            f"    reset_b = {reset_b_initial};",
            "    reset_j = 0;",
            f"    while (reset_j < {reset_inner_trips}) {{",
            "      int reset_next = reset_a + reset_b + "
            f"reset_i * {reset_i_coeff};",
            "      reset_a = reset_b;",
            "      reset_b = reset_next;",
            "      reset_j = reset_j + 1;",
            "    }",
            f"    reset_total = reset_total + reset_a * {reset_total_coeff} - reset_b;",
            "    reset_i = reset_i + 1;",
            "  }",
            "  result = result + reset_a + reset_b + reset_total + reset_j;",
        ]
    )
    # A monotone leading break only tightens a canonical loop's upper bound.
    # Exercise the normalized guard together with a coupled affine recurrence.
    break_upper = rng.randint(4, 10)
    break_threshold = rng.randint(1, break_upper - 1)
    break_a_initial = rng.randint(-4, 5)
    break_b_initial = rng.randint(-4, 5)
    lines.extend(
        [
            "  int break_i = 0;",
            f"  int break_a = result % 13 + {break_a_initial};",
            f"  int break_b = seed % 11 + {break_b_initial};",
            "  int break_total = 3;",
            f"  while (break_i < {break_upper}) {{",
            f"    if (break_i >= {break_threshold}) break;",
            "    int break_next = break_a + break_b + break_i;",
            "    break_a = break_b;",
            "    break_b = break_next;",
            "    break_total = break_total + break_a - break_b;",
            "    break_i = break_i + 1;",
            "  }",
            "  result = result + break_a + break_b + break_total + break_i;",
        ]
    )
    # A modulo-controlled affine branch has a finite phase period. The optimizer
    # may summarize a whole period, but must preserve sequential assignments and
    # the phase implied by a non-zero induction start.
    periodic_start = rng.randint(0, 8)
    periodic_trips = rng.randint(2, 12)
    periodic_bound = periodic_start + periodic_trips
    periodic_modulus = rng.randint(2, 7)
    periodic_residue = rng.randrange(periodic_modulus)
    periodic_relation = rng.choice(("==", "!="))
    periodic_a = rng.randint(-5, 7)
    periodic_b = rng.randint(-5, 7)
    periodic_total = rng.randint(-9, 11)
    then_a_coeff = rng.choice((-1, 1))
    then_b_coeff = rng.choice((-1, 1))
    else_a_coeff = rng.choice((-1, 1))
    else_i_coeff = rng.randint(-2, 2)
    lines.extend(
        [
            f"  int pi = {periodic_start};",
            f"  int periodic_a = result % 13 + {periodic_a};",
            f"  int periodic_b = seed % 11 + {periodic_b};",
            f"  int periodic_total = {periodic_total};",
            f"  while (pi < {periodic_bound}) {{",
            f"    if (pi % {periodic_modulus} {periodic_relation} {periodic_residue}) {{",
            f"      periodic_a = periodic_a * {then_a_coeff} + "
            f"periodic_b * {then_b_coeff} + pi;",
            "      periodic_total = periodic_total + periodic_a;",
            "    } else {",
            f"      periodic_b = periodic_b - periodic_a * {else_a_coeff} + "
            f"pi * {else_i_coeff};",
            "      periodic_total = periodic_total - periodic_b;",
            "    }",
            "    pi = pi + 1;",
            "  }",
            "  result = result + periodic_a + periodic_b + periodic_total + pi;",
        ]
    )
    # Polynomial induction accumulators exercise the closed-form sums for
    # powers zero through three, including a negative induction start.
    polynomial_start = rng.randint(-5, 3)
    polynomial_trips = rng.randint(2, 10)
    polynomial_is_le = rng.choice((False, True))
    polynomial_bound = polynomial_start + polynomial_trips - (1 if polynomial_is_le else 0)
    polynomial_relation = "<=" if polynomial_is_le else "<"
    polynomial_coefficients = [rng.choice((-3, -2, -1, 1, 2, 3)) for _ in range(4)]
    lines.extend(
        [
            f"  int polynomial_i = {polynomial_start};",
            "  int polynomial_sum = result;",
            f"  while (polynomial_i {polynomial_relation} {polynomial_bound}) {{",
            "    polynomial_sum = polynomial_sum + "
            f"polynomial_i * polynomial_i * polynomial_i * {polynomial_coefficients[3]} + "
            f"polynomial_i * polynomial_i * {polynomial_coefficients[2]} + "
            f"polynomial_i * {polynomial_coefficients[1]} + {polynomial_coefficients[0]};",
            "    polynomial_i = polynomial_i + 1;",
            "  }",
            "  result = result + polynomial_sum + polynomial_i;",
        ]
    )
    # A remainder-derived expression repeats after a finite number of phases.
    # Exercise direct and helper-inlined shapes without overflowing signed int.
    periodic_acc_start = rng.randint(0, 9)
    periodic_acc_trips = rng.randint(2, 30)
    periodic_acc_modulus = rng.randint(2, 19)
    periodic_acc_output_modulus = rng.randint(2, 17)
    periodic_acc_linear = rng.randint(-3, 4)
    periodic_acc_bias = rng.randint(-7, 9)
    lines.extend(
        [
            f"  int periodic_acc_i = {periodic_acc_start};",
            "  int periodic_acc_sum = result;",
            f"  while (periodic_acc_i < {periodic_acc_start + periodic_acc_trips}) {{",
            f"    int periodic_acc_r = periodic_acc_i % {periodic_acc_modulus};",
            "    periodic_acc_sum = periodic_acc_sum + "
            f"(periodic_acc_r * periodic_acc_r + periodic_acc_r * {periodic_acc_linear} + "
            f"{periodic_acc_bias}) % {periodic_acc_output_modulus};",
            "    periodic_acc_i = periodic_acc_i + 1;",
            "  }",
            "  result = result + periodic_acc_sum + periodic_acc_i;",
        ]
    )
    # A monotone affine guard divides the iteration range into at most three
    # constant-choice segments, including the singleton produced by ==/!=.
    threshold_start = rng.randint(0, 5)
    threshold_trips = rng.randint(3, 18)
    threshold_scale = rng.randint(1, 4)
    threshold_bias = rng.randint(-5, 7)
    threshold_point = rng.randint(threshold_start, threshold_start + threshold_trips - 1)
    threshold_relation = rng.choice(("<", "<=", ">", ">=", "==", "!="))
    threshold_value = threshold_point * threshold_scale + threshold_bias
    threshold_then = rng.randint(-3, 5)
    threshold_else = rng.randint(-4, 4)
    lines.extend(
        [
            f"  int threshold_i = {threshold_start};",
            "  int threshold_a = result;",
            "  int threshold_b = seed;",
            f"  while (threshold_i < {threshold_start + threshold_trips}) {{",
            f"    if (threshold_i * {threshold_scale} + {threshold_bias} "
            f"{threshold_relation} {threshold_value}) {{",
            f"      threshold_a = threshold_a + threshold_b + {threshold_then};",
            "    } else {",
            f"      threshold_b = threshold_b - threshold_a + {threshold_else};",
            "    }",
            "    threshold_i = threshold_i + 1;",
            "  }",
            "  result = result + threshold_a + threshold_b + threshold_i;",
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
    # ToyC targets RV32 two's-complement arithmetic; keep the host oracle from
    # exploiting C signed-overflow UB in generated stress expressions.
    host = run([args.host_cc, "-O0", "-fwrapv", "-w", str(source_path), "-o", str(host_path)])
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
