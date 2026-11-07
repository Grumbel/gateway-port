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


def executable_format(data: bytes) -> str:
    """Distinguish real-mode MZ from PE/NE/LE/LX programs with DOS stubs."""
    if data[:2] != b"MZ":
        return "unknown"
    if len(data) >= 0x40:
        new_header = int.from_bytes(data[0x3c:0x40], "little")
        if 0 < new_header <= len(data) - 4 and data[new_header:new_header + 4] == b"PE\0\0":
            return "Windows PE"
        if 0 < new_header <= len(data) - 2:
            signature = data[new_header:new_header + 2]
            if signature == b"NE":
                return "Windows/OS2 NE"
            if signature == b"LE":
                return "LE executable"
            if signature == b"LX":
                return "OS2 LX"
    return "DOS MZ"


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
    missing = [name for name in ("CL.EXE", "C1.EXE", "C1L.EXE", "C2.EXE", "C3.EXE", "LINK.EXE")
               if not any(locate_case_insensitive(base, name) for base in bin_dirs)]
    # MSC 5.1 ships its long-integer arithmetic helpers separately from the
    # model-specific run-time library, in LIBH.LIB.
    missing += [name for name in ("LLIBCR.LIB", "LIBH.LIB")
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
    for name in ("CL.EXE", "C1.EXE", "C1L.EXE", "C2.EXE", "C3.EXE", "LINK.EXE"):
        path = next(path for base in bin_dirs
                    if (path := locate_case_insensitive(base, name)) is not None)
        kind = executable_format(path.read_bytes())
        if kind not in ("DOS MZ", "unknown"):
            raise ValueError(
                f"{name} is a {kind} program ({path}), not a real-mode DOS tool. "
                "Select a DOS variant from the original Microsoft C 5.1 disks."
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
        "ECHO Gateway MSC 5.1 batch started > START.LOG\r\n"
        "SET PATH=C:\\BIN;C:\\\r\n"
        "SET INCLUDE=C:\\INCLUDE;C:\\\r\n"
        "SET LIB=C:\\LIB;C:\\\r\n"
        "SET TEMP=D:\\\r\n"
        "SET TMP=D:\\\r\n"
        "IF EXIST INTROCHK.EXE DEL INTROCHK.EXE\r\n"
        "CL /AL /Od PROBE.C RANDOM.C /FeINTROCHK.EXE /link LLIBCR.LIB LIBH.LIB/NOD > BUILD.LOG\r\n"
        "IF ERRORLEVEL 1 GOTO BUILD_FAILED\r\n"
        "IF NOT EXIST INTROCHK.EXE GOTO BUILD_FAILED\r\n"
        "INTROCHK.EXE > TEST.LOG\r\n"
        "IF ERRORLEVEL 1 ECHO DOS probe returned an error > RUNFAIL.LOG\r\n"
        "GOTO DONE\r\n"
        ":BUILD_FAILED\r\n"
        "ECHO Microsoft CL returned an error > CLFAIL.LOG\r\n"
        ":DONE\r\n"
        "EXIT\r\n"
    )


def remove_probe_outputs(output: Path) -> None:
    """Ensure a previous DOS run cannot satisfy the next run's checks."""
    names = {"START.LOG", "INTROCHK.EXE", "TEST.LOG", "BUILD.LOG", "CLFAIL.LOG", "RUNFAIL.LOG"}
    if output.is_dir():
        for entry in output.iterdir():
            if entry.is_file() and entry.name.upper() in names:
                entry.unlink()


def print_probe_diagnostics(output: Path) -> None:
    """Report the DOS compiler's output before asking for a user's logs."""
    started = locate_case_insensitive(output, "START.LOG")
    if started is None:
        print("BUILD.BAT did not create START.LOG; DOSBox may not have "
              "executed the batch file or could not write to drive D.", file=sys.stderr)
    for name in ("CLFAIL.LOG", "RUNFAIL.LOG", "BUILD.LOG", "TEST.LOG"):
        path = locate_case_insensitive(output, name)
        if path is None or not path.is_file():
            print(f"{name}: not created", file=sys.stderr)
            continue
        content = path.read_text(errors="replace")
        # Compiler diagnostics are usually short, but don't flood terminal
        # output if an incorrect installation emits megabytes of text.
        if len(content) > 12_000:
            content = "[earlier output omitted]\n" + content[-12_000:]
        print(f"--- {path} ---", file=sys.stderr)
        print(content.rstrip() or "(empty)", file=sys.stderr)
    print("Probe workspace: " + str(output), file=sys.stderr)


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
        remove_probe_outputs(output)
        command.append("--exit")
    print("DOSBox commands: " + " ; ".join(commands), flush=True)
    result = subprocess.run(command, check=False)
    if result.returncode != 0:
        if args.command == "probe":
            print_probe_diagnostics(output)
        return result.returncode
    if args.command == "probe":
        exe = locate_case_insensitive(output, "INTROCHK.EXE")
        log = locate_case_insensitive(output, "TEST.LOG")
        started = locate_case_insensitive(output, "START.LOG")
        compiler_failed = locate_case_insensitive(output, "CLFAIL.LOG")
        run_failed = locate_case_insensitive(output, "RUNFAIL.LOG")
        if compiler_failed is not None or started is None:
            print("DOSBox did not complete the Microsoft C compiler successfully.",
                  file=sys.stderr)
            print_probe_diagnostics(output)
            return 1
        if run_failed is not None:
            print("The DOS executable returned a failure status.", file=sys.stderr)
            print_probe_diagnostics(output)
            return 1
        if exe is None or not exe.is_file() or log is None or not log.is_file():
            print("Microsoft C 5.1 did not produce both INTROCHK.EXE and TEST.LOG.",
                  file=sys.stderr)
            print_probe_diagnostics(output)
            return 1
        test_log = log.read_text(errors="replace")
        if "Intro C runtime: PASS (32 DOS rand steps)" not in test_log:
            print("Microsoft C 5.1 compiled the probe, but its DOS run failed.",
                  file=sys.stderr)
            print_probe_diagnostics(output)
            return 1
        print("Microsoft C 5.1 DOS probe passed (32 original random steps)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
