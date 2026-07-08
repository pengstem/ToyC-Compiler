#!/usr/bin/env python3
"""Run one ToyC compiler test case through the RISC-V assembly pipeline."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
from typing import Sequence


STARTUP_ASM = """\
    .section .text
    .globl _start
_start:
    call main
    li a7, 93
    ecall
"""


def run_command(
    cmd: Sequence[str],
    *,
    input_bytes: bytes | None = None,
    timeout: float,
) -> subprocess.CompletedProcess[bytes]:
    return subprocess.run(
        list(cmd),
        input=input_bytes,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=timeout,
        check=False,
    )


def print_process_failure(
    title: str,
    proc: subprocess.CompletedProcess[bytes],
    cmd: Sequence[str],
) -> None:
    print(f"{title} failed with exit code {proc.returncode}", file=sys.stderr)
    print(f"command: {' '.join(cmd)}", file=sys.stderr)
    if proc.stdout:
        print("stdout:", file=sys.stderr)
        print(proc.stdout.decode(errors="replace"), file=sys.stderr)
    if proc.stderr:
        print("stderr:", file=sys.stderr)
        print(proc.stderr.decode(errors="replace"), file=sys.stderr)


def has_start_symbol(asm: str) -> bool:
    return bool(
        re.search(r"(?m)^\s*\.globl\s+_start\b", asm)
        or re.search(r"(?m)^\s*_start\s*:", asm)
    )


def compile_reference(
    args: argparse.Namespace,
    case_path: Path,
    work_dir: Path,
) -> tuple[bytes, int]:
    ref_exe = work_dir / "reference"
    cmd = [
        args.host_cc,
        "-x",
        "c",
        str(case_path),
        "-O2",
        "-w",
        "-o",
        str(ref_exe),
    ]
    proc = run_command(cmd, timeout=args.timeout)
    if proc.returncode != 0:
        print_process_failure("reference compile", proc, cmd)
        raise SystemExit(1)

    input_path = case_path.with_suffix(".in")
    runtime_input = input_path.read_bytes() if input_path.exists() else None
    proc = run_command([str(ref_exe)], input_bytes=runtime_input, timeout=args.timeout)
    if proc.returncode < 0:
        print_process_failure("reference run", proc, [str(ref_exe)])
        raise SystemExit(1)
    return proc.stdout, proc.returncode


def compile_actual(
    args: argparse.Namespace,
    case_path: Path,
    work_dir: Path,
) -> Path:
    source = case_path.read_bytes()
    generated_asm = work_dir / "generated.s"
    combined_asm = work_dir / "combined.s"
    actual_exe = work_dir / "actual.elf"

    compiler_cmd = [args.compiler]
    if args.opt:
        compiler_cmd.append("-opt")
    proc = run_command(compiler_cmd, input_bytes=source, timeout=args.timeout)
    if proc.returncode != 0:
        print_process_failure("toycc compile", proc, compiler_cmd)
        raise SystemExit(1)
    generated_asm.write_bytes(proc.stdout)

    asm_text = proc.stdout.decode(errors="replace")
    if args.startup == "on" or (args.startup == "auto" and not has_start_symbol(asm_text)):
        combined_asm.write_text(STARTUP_ASM + "\n" + asm_text, encoding="utf-8")
        asm_to_link = combined_asm
    else:
        asm_to_link = generated_asm

    link_cmd = [
        args.riscv_cc,
        "-x",
        "assembler",
        "-nostdlib",
        "-nostartfiles",
        "-static",
        f"-march={args.riscv_arch}",
        f"-mabi={args.riscv_abi}",
        str(asm_to_link),
        "-o",
        str(actual_exe),
    ]
    proc = run_command(link_cmd, timeout=args.timeout)
    if proc.returncode != 0:
        print_process_failure("riscv link", proc, link_cmd)
        print(f"generated assembly: {generated_asm}", file=sys.stderr)
        raise SystemExit(1)
    return actual_exe


def run_actual(
    args: argparse.Namespace,
    case_path: Path,
    actual_exe: Path,
) -> tuple[bytes, int]:
    input_path = case_path.with_suffix(".in")
    runtime_input = input_path.read_bytes() if input_path.exists() else None
    proc = run_command(
        [args.qemu, str(actual_exe)],
        input_bytes=runtime_input,
        timeout=args.timeout,
    )
    if proc.returncode < 0:
        print_process_failure("qemu run", proc, [args.qemu, str(actual_exe)])
        raise SystemExit(1)
    return proc.stdout, proc.returncode


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True, help="toycc executable")
    parser.add_argument("--case", required=True, help="ToyC/C source test case")
    parser.add_argument("--work-dir", required=True, help="per-case scratch directory")
    parser.add_argument("--host-cc", required=True, help="host C compiler for reference")
    parser.add_argument("--riscv-cc", required=True, help="RISC-V assembler/linker driver")
    parser.add_argument("--qemu", required=True, help="qemu-riscv32 executable")
    parser.add_argument("--timeout", type=float, default=5.0, help="per-step timeout in seconds")
    parser.add_argument("--riscv-arch", default="rv32im", help="RISC-V ISA passed to -march")
    parser.add_argument("--riscv-abi", default="ilp32", help="RISC-V ABI passed to -mabi")
    parser.add_argument("--startup", choices=["auto", "on", "off"], default="auto")
    parser.add_argument("--opt", action="store_true", help="pass -opt to toycc")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    case_path = Path(args.case).resolve()
    work_dir = Path(args.work_dir).resolve()
    os.makedirs(work_dir, exist_ok=True)

    expected_stdout, expected_code = compile_reference(args, case_path, work_dir)
    actual_exe = compile_actual(args, case_path, work_dir)
    actual_stdout, actual_code = run_actual(args, case_path, actual_exe)

    ok = True
    if actual_code != expected_code:
        print(
            f"exit code mismatch for {case_path}: expected {expected_code}, got {actual_code}",
            file=sys.stderr,
        )
        ok = False
    if actual_stdout != expected_stdout:
        print(f"stdout mismatch for {case_path}", file=sys.stderr)
        print(f"expected: {expected_stdout!r}", file=sys.stderr)
        print(f"actual:   {actual_stdout!r}", file=sys.stderr)
        ok = False
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
