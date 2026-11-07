#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Run Microsoft C 5.1 through DOSBox, preserving the original binaries.

The Nix wrapper supplies a pinned, unpacked historical compiler by default;
the --compiler option accepts an independently installed copy. Generated C
and executable outputs stay in a writable directory outside the Nix store.
"""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


REPO = Path(__file__).resolve().parent.parent


def locate_case_insensitive(directory: Path, name: str) -> Path | None:
    if not directory.is_dir():
        return None
    return next((p for p in directory.iterdir() if p.name.upper() == name.upper()), None)


def validate_compiler(directory: Path) -> Path:
    directory = directory.expanduser().resolve()
    if not directory.is_dir():
        raise ValueError(f"Microsoft C installation not found: {directory}")
    bin_dir = locate_case_insensitive(directory, "BIN")
    lib_dir = locate_case_insensitive(directory, "LIB")
    include_dir = locate_case_insensitive(directory, "INCLUDE")
    bin_dirs = [directory] + ([bin_dir] if bin_dir is not None else [])
    lib_dirs = [directory] + ([lib_dir] if lib_dir is not None else [])
    missing = [name for name in ("CL.EXE", "C1.EXE", "C2.EXE", "C3.EXE", "LINK.EXE")
               if not any(locate_case_insensitive(base, name) for base in bin_dirs)]
    missing += [name for name in ("LLIBCR.LIB",)
                if not any(locate_case_insensitive(base, name) for base in lib_dirs)]
    if not any(locate_case_insensitive(base, "STDIO.H")
               for base in [directory] + ([include_dir] if include_dir is not None else [])):
        missing.append("STDIO.H")
    if missing:
        if any(entry.suffix.lower() in (".img", ".ima", ".dsk", ".vfd")
               for entry in directory.rglob("*") if entry.is_file()):
            raise ValueError(
                f"{directory} contains floppy images, not an installed compiler. "
                "Use 'nix run .#msc51-unpack -- IMAGE-DIR OUTPUT-DIR' or "
                "the default pinned flake compiler."
            )
        raise ValueError(
            f"Incomplete Microsoft C installation at {directory}: missing {', '.join(missing)}. "
            "Install the complete Microsoft C 5.1 disk set first."
        )
    return directory


def stage_probe(output: Path) -> None:
    output.mkdir(parents=True, exist_ok=True)
    original_probe = (REPO / "decomp/launcher/dos/intro_probe.c").read_text()
    original_random = (REPO / "decomp/launcher/gate_c_runtime_random.c").read_text()
    assert '#include "../gate_exe.h"' in original_probe
    assert '#include "gate_rtl_image.h"' in original_random

    # MSC 5.1 predates C99/stdint.h. Only the includes/types are adapted;
    # all recovered random-generator expressions remain unchanged.
    (output / "GATE.H").write_text(
        "/* Generated compatibility header; original C99 code is unchanged. */\n"
        "typedef unsigned short uint16_t;\n"
        "typedef unsigned long uint32_t;\n"
        "uint16_t gate_ds_word(uint16_t offset);\n"
        "void gate_ds_set_word(uint16_t offset, uint16_t value);\n"
    )
    (output / "PROBE.C").write_text(
        original_probe.replace('#include "../gate_exe.h"', '#include "GATE.H"', 1)
    )
    (output / "RANDOM.C").write_text(
        original_random.replace('#include "gate_rtl_image.h"', '#include "GATE.H"', 1)
    )
    (output / "BUILD.BAT").write_text(
        "@ECHO OFF\r\n"
        "SET PATH=C:\\BIN;C:\\\r\n"
        "SET INCLUDE=C:\\INCLUDE;C:\\\r\n"
        "SET LIB=C:\\LIB;C:\\\r\n"
        "IF EXIST INTROCHK.EXE DEL INTROCHK.EXE\r\n"
        "CL /AL /Od PROBE.C RANDOM.C /FeINTROCHK.EXE > BUILD.LOG\r\n"
        "IF EXIST INTROCHK.EXE INTROCHK.EXE > TEST.LOG\r\n"
    )


def dosbox_commands(compiler: Path, output: Path, run_probe: bool) -> list[str]:
    for path in (compiler, output):
        if any(ch in str(path) for ch in ('"', '\n', '\r')):
            raise ValueError(f"Path cannot be mounted safely in DOSBox: {path}")
    commands = [f'mount c "{compiler}" -ro', f'mount d "{output}"', "d:"]
    if run_probe:
        commands.append("BUILD.BAT")
    else:
        commands.extend(("SET PATH=C:\\BIN;C:\\", "SET INCLUDE=C:\\INCLUDE;C:\\", "SET LIB=C:\\LIB;C:\\"))
    return commands


def install_commands(destination: Path, floppies: list[Path]) -> list[str]:
    if not floppies:
        raise ValueError("pass at least one --floppy IMAGE (in disk order)")
    for path in [destination, *floppies]:
        if any(ch in str(path) for ch in ('"', '\n', '\r')):
            raise ValueError(f"Path cannot be mounted safely in DOSBox: {path}")
    for floppy in floppies:
        if not floppy.is_file() or floppy.suffix.lower() not in (".img", ".ima"):
            raise ValueError(f"Expected a DOS floppy .img/.ima image: {floppy}")
    floppy_args = ' '.join(f'"{image}"' for image in floppies)
    return [f'mount c "{destination}"', f'mount a {floppy_args} -t floppy', "a:", "SETUP.EXE"]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("probe", "shell", "prepare", "install"))
    parser.add_argument("--compiler", default=os.environ.get("GATEWAY_MSC51_DIR"),
                        help="installed Microsoft C 5.1 directory (or GATEWAY_MSC51_DIR)")
    parser.add_argument("--output", type=Path, default=Path("build/intro-msc51"),
                        help="writable DOS workspace; never the Nix store")
    parser.add_argument("--dosbox", default="dosbox-staging")
    parser.add_argument("--install-dir", type=Path,
                        help="writable target for installing the original Microsoft compiler")
    parser.add_argument("--floppy", type=Path, action="append", default=[],
                        help="one original floppy image, repeat in original disk order")
    args = parser.parse_args(argv)
    output = args.output.expanduser().resolve()
    if args.command not in ("shell", "install"):
        stage_probe(output)
        print(f"Staged original-code MSC 5.1 probe in {output}", flush=True)
    if args.command == "prepare":
        return 0
    try:
        if args.command == "install":
            if args.install_dir is None:
                parser.error("--install-dir is required for the install command")
            destination = args.install_dir.expanduser().resolve()
            commands = install_commands(destination, [p.expanduser().resolve() for p in args.floppy])
            destination.mkdir(parents=True, exist_ok=True)
        else:
            if not args.compiler:
                parser.error("set GATEWAY_MSC51_DIR or pass --compiler /path/to/installed/MSC51")
            compiler = validate_compiler(Path(args.compiler))
            commands = dosbox_commands(compiler, output, args.command == "probe")
            output.mkdir(parents=True, exist_ok=True)
    except ValueError as error:
        parser.error(str(error))
    dosbox = shutil.which(args.dosbox)
    if dosbox is None:
        parser.error(f"DOSBox executable not found: {args.dosbox}")
    command = [dosbox, *(part for line in commands for part in ("-c", line))]
    if args.command == "probe":
        # An old successful EXE/LOG must never masquerade as a fresh build.
        for name in ("INTROCHK.EXE", "TEST.LOG", "BUILD.LOG"):
            previous = output / name
            if previous.is_file():
                previous.unlink()
        command.append("--exit")
    print("DOSBox commands: " + " ; ".join(commands), flush=True)
    result = subprocess.run(command, check=False)
    if result.returncode != 0:
        return result.returncode
    if args.command == "probe":
        exe = output / "INTROCHK.EXE"
        log = output / "TEST.LOG"
        if not exe.is_file() or not log.is_file():
            print(f"Build did not produce {exe} and {log}; inspect {output / 'BUILD.LOG'}", file=sys.stderr)
            return 1
        test_log = log.read_text(errors="replace")
        if "Intro C runtime: PASS (32 DOS rand steps)" not in test_log:
            print(f"DOS probe failed; inspect {log}", file=sys.stderr)
            return 1
        print("Microsoft C 5.1 DOS probe passed (32 original random steps)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
