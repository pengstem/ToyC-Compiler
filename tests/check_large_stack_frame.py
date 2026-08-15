#!/usr/bin/env python3
"""Exercise a frame and local offsets beyond RV32's signed 12-bit immediates."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
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
        timeout=20,
        check=False,
    )


def make_source(count: int) -> str:
    declarations = "\n".join(f"int v{i}=seed+{i};" for i in range(count))
    additions = "\n".join(f"s=s+v{i};" for i in range(count))
    return f"""\
int opaque(int x) {{
    if (x < 0) {{ return opaque(x + 1); }}
    return x + 1;
}}

int main() {{
    int seed=opaque(0);
    {declarations}
    int s=0;
    {additions}
    return s;
}}
"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--riscv-cc", required=True)
    parser.add_argument("--qemu", required=True)
    args = parser.parse_args()

    local_count = 530
    source = make_source(local_count).encode()
    compiled = run([args.compiler, "-opt"], data=source)
    if compiled.returncode != 0:
        raise RuntimeError(compiled.stderr.decode(errors="replace"))

    assembly = compiled.stdout.decode(errors="replace")
    if re.search(r"(?m)^\s*li t2, -([2-9][0-9]{3,})\n\s*add sp, sp, t2$", assembly) is None:
        raise RuntimeError("stress case no longer exercises a frame larger than 2 KiB")

    with tempfile.TemporaryDirectory(prefix="toycc-large-frame-") as temp:
        root = Path(temp)
        asm_path = root / "case.s"
        elf_path = root / "case.elf"
        asm_path.write_text(STARTUP_ASM + assembly, encoding="utf-8")
        linked = run(
            [
                args.riscv_cc,
                "-nostdlib",
                "-nostartfiles",
                "-static",
                "-march=rv32im",
                "-mabi=ilp32",
                str(asm_path),
                "-o",
                str(elf_path),
            ]
        )
        if linked.returncode != 0:
            raise RuntimeError(linked.stderr.decode(errors="replace"))

        actual = run([args.qemu, str(elf_path)])
        expected = (local_count * (local_count + 1) // 2) & 0xFF
        if actual.returncode != expected:
            raise RuntimeError(f"expected exit {expected}, got {actual.returncode}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
