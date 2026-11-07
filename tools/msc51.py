#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Run Microsoft C 5.1 through DOSBox, preserving the original binaries.

The Nix wrapper supplies a pinned, unpacked historical compiler by default;
the --compiler option accepts an independently installed copy. Generated C
and executable outputs stay in a writable directory outside the Nix store.
"""

import argparse
import hashlib
import os
from pathlib import Path
import re
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
        # DOS shells can open a redirected file before evaluating an IF.
        # Only execute the ECHO (and create RUNFAIL.LOG) on genuine failure.
        "IF ERRORLEVEL 1 GOTO RUN_FAILED\r\n"
        "GOTO DONE\r\n"
        ":BUILD_FAILED\r\n"
        "ECHO Microsoft CL returned an error > CLFAIL.LOG\r\n"
        "GOTO DONE\r\n"
        ":RUN_FAILED\r\n"
        "ECHO DOS probe returned an error > RUNFAIL.LOG\r\n"
        ":DONE\r\n"
        "EXIT\r\n"
    )


def msc51_enums_to_macros(source: str) -> str:
    """Keep full-width original constants in 16-bit MSC's C89 preprocessor.

    MS C 5.1 uses 16-bit int, while many recovered layout/bit constants are
    larger than INT_MAX. The staged headers use macros so enum overflow
    cannot silently change their values; checked-in headers stay untouched.
    """
    def transform(match: re.Match[str]) -> str:
        body = re.sub(r"/\*.*?\*/", "", match.group(1), flags=re.DOTALL)
        declarations = [item.strip() for item in body.split(",") if item.strip()]
        defines = []
        for declaration in declarations:
            parts = declaration.split("=", 1)
            if len(parts) != 2 or re.fullmatch(r"[A-Za-z_]\w*", parts[0].strip()) is None:
                raise ValueError(f"Unrecognized C89 enum item: {declaration!r}")
            defines.append(f"#define {parts[0].strip()} ({parts[1].strip()})")
        return "\n".join(defines)

    return re.sub(r"\benum\s*\{([^{}]*)\}\s*;", transform, source, flags=re.DOTALL)


def msc51_far_compounds_to_calls(source: str) -> tuple[str, bool]:
    """Convert only C99 GateFarPtr two-field literals to C89 helper calls."""
    pattern = re.compile(r"\(\s*GateFarPtr\s*\)\s*\{")
    pieces = []
    offset = 0
    found = False
    while match := pattern.search(source, offset):
        depth = 0
        separator = None
        cursor = match.end()
        while cursor < len(source):
            ch = source[cursor]
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
            elif ch == "," and depth == 0:
                if separator is not None:
                    raise ValueError("GateFarPtr compound has more than two fields")
                separator = cursor
            elif ch == "}" and depth == 0:
                break
            cursor += 1
        if cursor == len(source) or separator is None or depth != 0:
            raise ValueError("Unbalanced GateFarPtr compound literal")
        pieces.append(source[offset:match.start()])
        pieces.append("gate_msc51_far_pointer(" + source[match.end():separator].strip() + ", " +
                      source[separator + 1:cursor].strip() + ")")
        offset = cursor + 1
        found = True
    pieces.append(source[offset:])
    return "".join(pieces), found


MSC51_TOKEN = re.compile(
    r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|'
    r'(?P<identifier>[A-Za-z_][A-Za-z_0-9]*)', re.DOTALL)


def msc51_identifiers(source: str) -> set[str]:
    """Identify C tokens, excluding comments, strings and character literals."""
    return {match.group("identifier") for match in MSC51_TOKEN.finditer(source)
            if match.group("identifier") is not None}


def msc51_short_names(sources: list[str]) -> dict[str, str]:
    """Provide distinct, deterministic identifiers within MSC 5.1's 31-char limit.

    Plain truncation produces real collisions among the original address and
    field names; retain the full reverse mapping in generated SYMBOL.MAP.
    """
    identifiers = set().union(*(msc51_identifiers(source) for source in sources))
    used = set(identifiers)
    names = {}
    for original in sorted(name for name in identifiers if len(name) > 31):
        alias = original[:22] + "_" + hashlib.sha1(original.encode("ascii")).hexdigest()[:8]
        if alias in used:
            raise ValueError(f"MSC 5.1 identifier collision: {original} -> {alias}")
        names[original] = alias
        used.add(alias)
    return names


def msc51_rename_identifiers(source: str, names: dict[str, str]) -> str:
    """Rename compiler-facing tokens without touching address-bearing comments."""
    return MSC51_TOKEN.sub(
        lambda match: names.get(match.group("identifier"), match.group(0))
        if match.group("identifier") is not None else match.group(0), source)


MEMORY_SOURCES = ("GATEMEM.H", "STDINT.H", "MEMORY.C", "MEMCHK.C")
DOS_FILE_SOURCES = ("GATEDOS.H", "GATEMEM.H", "STDINT.H", "DOSFILE.C", "FILECHK.C",
                    "MEMORY.C")


def memory_source_signature(output: Path) -> str:
    """Reject a stale DOS memory object after an adapter/staging change."""
    digest = hashlib.sha256(b"MS C 5.1 /AL /Od memory adapter v1\n")
    for name in MEMORY_SOURCES:
        digest.update(name.encode("ascii") + b"\0" + (output / name).read_bytes() + b"\0")
    return digest.hexdigest()[:16]


def dos_file_source_signature(output: Path) -> str:
    """Reject obsolete DOS file objects, including memory-ABI changes."""
    digest = hashlib.sha256(b"MS C 5.1 /AL /Od DOS file adapter v1\n")
    for name in DOS_FILE_SOURCES:
        digest.update(name.encode("ascii") + b"\0" + (output / name).read_bytes() + b"\0")
    return digest.hexdigest()[:16]


def stage_launcher(output: Path) -> list[str]:
    """Stage recovered launcher C and a diagnostic DOS link attempt.

    The original resident services and graphics adapters are not yet DOS
    objects. The optional, separately verified memory adapter is real DOS
    code; LINK is still expected to report unresolved original services.
    """
    output.mkdir(parents=True, exist_ok=True)
    source_dir = REPO / "decomp/launcher"
    sources = sorted(source_dir.glob("*.c"))
    headers = sorted(source_dir.glob("*.h"))
    aliases = {path.name: f"H{index:02d}.H" for index, path in enumerate(headers)}
    aliases.update({path.name: f"M{index:02d}.C" for index, path in enumerate(sources)})
    if len(aliases) != len(headers) + len(sources):
        raise ValueError("A launcher file basename is ambiguous")

    include_pattern = re.compile(r'(?m)^(#include\s+)"([^"]+)"')

    def rewrite_includes(match: re.Match[str]) -> str:
        filename = Path(match.group(2)).name
        if filename not in aliases:
            raise ValueError(f"Unmapped launcher include: {match.group(2)}")
        return match.group(1) + '"' + aliases[filename] + '"'

    rewritten = []
    for path in [*headers, *sources]:
        content = path.read_text()
        content = include_pattern.sub(rewrite_includes, content)
        content = re.sub(r"(?m)^#include\s*<stdint\.h>", '#include "STDINT.H"', content)
        if path.suffix == ".h":
            content = msc51_enums_to_macros(content)
            content = re.sub(r"\b_Noreturn\s+", "", content)
        else:
            content, had_compounds = msc51_far_compounds_to_calls(content)
            if had_compounds:
                includes = list(re.finditer(r"(?m)^#include[^\n]*$", content))
                if not includes:
                    raise ValueError(f"No includes before GateFarPtr helper: {path}")
                at = includes[-1].end()
                helper = ("\n\n/* C89 equivalent of a two-field GateFarPtr literal. */\n"
                          "static GateFarPtr gate_msc51_far_pointer(uint16_t segment, uint16_t offset)\n"
                          "{\n    GateFarPtr result;\n    result.segment = segment;\n"
                          "    result.offset = offset;\n    return result;\n}\n")
                content = content[:at] + helper + content[at:]
        rewritten.append((path, content))

    shortened = msc51_short_names([content for _path, content in rewritten])
    mapping = []
    for path, content in rewritten:
        alias = aliases[path.name]
        (output / alias).write_text(msc51_rename_identifiers(content, shortened))
        mapping.append(f"{alias}  {path.relative_to(REPO).as_posix()}")

    (output / "STDINT.H").write_text(
        "/* Generated C89 widths for 16-bit Microsoft C 5.1, /AL model. */\n"
        "#ifndef GATE_MSC51_STDINT_H\n#define GATE_MSC51_STDINT_H\n"
        "typedef unsigned char uint8_t;\n"
        "typedef signed char int8_t;\n"
        "typedef unsigned short uint16_t;\n"
        "typedef signed short int16_t;\n"
        "typedef unsigned long uint32_t;\n"
        "typedef signed long int32_t;\n"
        "#endif\n"
    )
    (output / "SOURCE.MAP").write_text("\n".join(mapping) + "\n")
    (output / "SYMBOL.MAP").write_text(
        "# DOS compiler identifier -> original identifier (original addresses remain in source comments)\n"
        + "".join(f"{alias}  {name}\n" for name, alias in shortened.items()))
    # These files are hand-written DOS host code, not recovered game modules.
    # Compile and exercise them separately so the 28 existing game objects
    # need not be rebuilt for each adapter iteration.
    (output / "GATEMEM.H").write_text(
        "/* Microsoft C 5.1 DOS host adapter; not original game code. */\n"
        '#include "STDINT.H"\n'
        "typedef struct GateFarPtr { uint16_t segment; uint16_t offset; } GateFarPtr;\n"
        "void gate_dos_bind_ds(uint16_t segment);\n"
        "uint16_t gate_ds_segment(void);\n"
        "uint16_t gate_ds_word(uint16_t offset);\n"
        "void gate_ds_set_word(uint16_t offset, uint16_t value);\n"
        "uint8_t gate_far_byte(uint16_t segment, uint16_t offset);\n"
        "void gate_far_set_byte(uint16_t segment, uint16_t offset, uint8_t value);\n"
        "uint16_t gate_far_word(uint16_t segment, uint16_t offset);\n"
        "void gate_far_set_word(uint16_t segment, uint16_t offset, uint16_t value);\n"
        "GateFarPtr gate_stack_pointer(void *buffer);\n"
    )
    for dos_name, staged_name in (("msc51_memory.c", "MEMORY.C"),
                                  ("msc51_memory_probe.c", "MEMCHK.C")):
        (output / staged_name).write_text((source_dir / "dos" / dos_name).read_text())
    memory_signature = memory_source_signature(output)
    memory_batch = [
        "@ECHO OFF", "ECHO MS C 5.1 DOS memory adapter > MEMSTART.LOG",
        "SET PATH=C:\\BIN;C:\\", "SET INCLUDE=C:\\INCLUDE;C:\\",
        "SET LIB=C:\\LIB;C:\\", "SET TEMP=D:\\", "SET TMP=D:\\",
        "CL /AL /Od /c MEMORY.C > MEMBUILD.LOG", "IF ERRORLEVEL 1 GOTO FAILED",
        "IF NOT EXIST MEMORY.OBJ GOTO FAILED",
        "CL /AL /Od MEMCHK.C MEMORY.OBJ /FeMEMCHK.EXE /link LLIBCR.LIB LIBH.LIB/NOD >> MEMBUILD.LOG",
        "IF ERRORLEVEL 1 GOTO FAILED", "IF NOT EXIST MEMCHK.EXE GOTO FAILED",
        "MEMCHK.EXE > MEMTEST.LOG", "IF ERRORLEVEL 1 GOTO FAILED",
        f"ECHO {memory_signature} > MEMSIG.LOG",
        "ECHO Microsoft C DOS memory probe passed > MEMOK.LOG",
        "GOTO DONE", ":FAILED",
        "ECHO Microsoft C DOS memory probe failed > MEMFAIL.LOG",
        ":DONE", "EXIT",
    ]
    (output / "MEMORY.BAT").write_text("\r\n".join(memory_batch) + "\r\n")
    (output / "GATEDOS.H").write_text(
        "/* Microsoft C 5.1 DOS file adapter; not original game code. */\n"
        '#include "GATEMEM.H"\n'
        "typedef struct GateDosFileResult {\n"
        "    uint16_t ax;\n    uint16_t dx;\n    uint8_t carry;\n"
        "} GateDosFileResult;\n"
        "GateDosFileResult gate_dos_file_open(GateFarPtr path, uint16_t access, uint16_t create);\n"
        "GateDosFileResult gate_dos_file_close(uint16_t handle);\n"
        "GateDosFileResult gate_dos_file_read(uint16_t handle, GateFarPtr dest, uint16_t count);\n"
        "GateDosFileResult gate_dos_file_write(uint16_t handle, GateFarPtr src, uint16_t count);\n"
        "GateDosFileResult gate_dos_file_seek(uint16_t handle, uint16_t origin, uint32_t offset);\n"
    )
    for dos_name, staged_name in (("msc51_dos_file.c", "DOSFILE.C"),
                                  ("msc51_dos_file_probe.c", "FILECHK.C")):
        (output / staged_name).write_text((source_dir / "dos" / dos_name).read_text())
    file_signature = dos_file_source_signature(output)
    file_batch = [
        "@ECHO OFF", "ECHO MS C 5.1 DOS file adapter > FILSTART.LOG",
        "SET PATH=C:\\BIN;C:\\", "SET INCLUDE=C:\\INCLUDE;C:\\",
        "SET LIB=C:\\LIB;C:\\", "SET TEMP=D:\\", "SET TMP=D:\\",
        "CL /AL /Od /c DOSFILE.C > FILBUILD.LOG", "IF ERRORLEVEL 1 GOTO FAILED",
        "IF NOT EXIST DOSFILE.OBJ GOTO FAILED",
        "CL /AL /Od FILECHK.C DOSFILE.OBJ MEMORY.OBJ /FeFILECHK.EXE /link LLIBCR.LIB LIBH.LIB/NOD >> FILBUILD.LOG",
        "IF ERRORLEVEL 1 GOTO FAILED", "IF NOT EXIST FILECHK.EXE GOTO FAILED",
        "FILECHK.EXE > FILTEST.LOG", "IF ERRORLEVEL 1 GOTO FAILED",
        f"ECHO {file_signature} > FILSIG.LOG",
        "ECHO Microsoft C DOS file probe passed > FILOK.LOG",
        "GOTO DONE", ":FAILED",
        "ECHO Microsoft C DOS file probe failed > FILFAIL.LOG",
        ":DONE", "EXIT",
    ]
    (output / "DOSFILE.BAT").write_text("\r\n".join(file_batch) + "\r\n")
    lines = [
        "@ECHO OFF", "ECHO Gateway MSC 5.1 launcher objects > START.LOG",
        "SET PATH=C:\\BIN;C:\\", "SET INCLUDE=C:\\INCLUDE;C:\\",
        "SET LIB=C:\\LIB;C:\\", "SET TEMP=D:\\", "SET TMP=D:\\",
        "ECHO Compiling recovered launcher translation units > BUILD.LOG",
    ]
    for index in range(len(sources)):
        filename = f"M{index:02d}.C"
        lines.extend((f"ECHO Compiling {filename} >> BUILD.LOG",
                      f"CL /AL /Od /c {filename} >> BUILD.LOG",
                      "IF ERRORLEVEL 1 GOTO BUILD_FAILED",
                      f"IF NOT EXIST M{index:02d}.OBJ GOTO BUILD_FAILED"))
    lines.extend(("ECHO All reconstructed launcher objects compiled > SUCCESS.LOG",
                  "GOTO DONE", ":BUILD_FAILED",
                  "ECHO Microsoft C failed to compile a module > BUILDFAIL.LOG",
                  ":DONE", "EXIT"))
    (output / "COMPILE.BAT").write_text("\r\n".join(lines) + "\r\n")
    objects = [f"M{index:02d}.OBJ" for index in range(len(sources))]
    # Microsoft's LINK response file is prompt-oriented: object modules,
    # output executable, map, libraries/options, and an empty DEF response.
    # Each trailing + extends the SAME response across CRLFs (MS KB Q40429).
    response = ([f"{name}+" for name in objects] + ["MEMORY.OBJ+", "DOSFILE.OBJ",
                "GATEWAY.EXE", "GATEWAY.MAP", "LLIBCR.LIB LIBH.LIB /NOD", ";"])
    (output / "LINK.RSP").write_text("\r\n".join(response) + "\r\n")
    link_batch = [
        "@ECHO OFF", "ECHO Gateway MSC 5.1 link attempt > LNKSTART.LOG",
        "SET PATH=C:\\BIN;C:\\", "SET INCLUDE=C:\\INCLUDE;C:\\",
        "SET LIB=C:\\LIB;C:\\", "SET TEMP=D:\\", "SET TMP=D:\\",
        # The current directory contains LINK.BAT. Calling bare LINK here
        # executes this batch file recursively instead of Microsoft's linker.
        "C:\\BIN\\LINK.EXE @LINK.RSP > LINK.LOG", "IF ERRORLEVEL 1 GOTO LINK_FAILED",
        "IF NOT EXIST GATEWAY.EXE GOTO LINK_FAILED",
        "ECHO All reconstructed launcher objects linked > LINKDONE.LOG",
        "GOTO DONE", ":LINK_FAILED",
        "ECHO Original DOS services or linker dependencies unresolved > LINKFAIL.LOG",
        ":DONE", "EXIT",
    ]
    (output / "LINK.BAT").write_text("\r\n".join(link_batch) + "\r\n")
    return objects


def remove_probe_outputs(output: Path) -> None:
    """Ensure a previous DOS run cannot satisfy the next run's checks."""
    names = {"START.LOG", "INTROCHK.EXE", "TEST.LOG", "BUILD.LOG", "CLFAIL.LOG", "RUNFAIL.LOG"}
    if output.is_dir():
        for entry in output.iterdir():
            if entry.is_file() and entry.name.upper() in names:
                entry.unlink()


def remove_launcher_outputs(output: Path, objects: list[str]) -> None:
    """A stale .OBJ or success marker cannot satisfy a fresh DOS compile."""
    names = {"START.LOG", "BUILD.LOG", "BUILDFAIL.LOG", "SUCCESS.LOG", *objects}
    if output.is_dir():
        for entry in output.iterdir():
            if entry.is_file() and entry.name.upper() in names:
                entry.unlink()


def validate_launcher_objects(output: Path, objects: list[str]) -> None:
    """Only link the outputs of a previously successful DOS compilation."""
    missing = [name for name in objects if locate_case_insensitive(output, name) is None]
    if (missing or locate_case_insensitive(output, "START.LOG") is None or
            locate_case_insensitive(output, "SUCCESS.LOG") is None or
            locate_case_insensitive(output, "BUILDFAIL.LOG") is not None):
        raise ValueError(f"Cannot link launcher: {len(missing)} of {len(objects)} "
                         "objects missing or compiler success marker absent. "
                         "Run msc51-compile-launcher first.")


def validate_memory_adapter(output: Path) -> None:
    """Require a successful real-DOS probe, not a link-satisfying stub."""
    if (locate_case_insensitive(output, "MEMORY.OBJ") is None or
            locate_case_insensitive(output, "MEMSTART.LOG") is None or
            locate_case_insensitive(output, "MEMOK.LOG") is None or
            locate_case_insensitive(output, "MEMFAIL.LOG") is not None):
        raise ValueError("Cannot link launcher: DOS memory adapter is unverified. "
                         "Run msc51-compile-memory first.")
    signature = locate_case_insensitive(output, "MEMSIG.LOG")
    if signature is None or signature.read_text().strip() != memory_source_signature(output):
        raise ValueError("Cannot link launcher: DOS memory adapter source has changed "
                         "since its test. Run msc51-compile-memory again.")


def remove_memory_outputs(output: Path) -> None:
    names = {"MEMSTART.LOG", "MEMOK.LOG", "MEMFAIL.LOG", "MEMBUILD.LOG", "MEMSIG.LOG",
             "MEMTEST.LOG", "MEMORY.OBJ", "MEMCHK.EXE", "MEMCHK.OBJ"}
    if output.is_dir():
        for entry in output.iterdir():
            if entry.is_file() and entry.name.upper() in names:
                entry.unlink()


def print_memory_diagnostics(output: Path) -> None:
    for name in ("MEMFAIL.LOG", "MEMBUILD.LOG", "MEMTEST.LOG", "MEMOK.LOG"):
        path = locate_case_insensitive(output, name)
        content = path.read_text(errors="replace") if path else "(not created)"
        print(f"--- {name} ---\n{content[-8_000:].rstrip()}", file=sys.stderr)
    print(f"Memory adapter workspace: {output}", file=sys.stderr)


def validate_dos_file_adapter(output: Path) -> None:
    """Require a successful DOS INT 21h file-operations round trip."""
    if (locate_case_insensitive(output, "DOSFILE.OBJ") is None or
            locate_case_insensitive(output, "FILSTART.LOG") is None or
            locate_case_insensitive(output, "FILOK.LOG") is None or
            locate_case_insensitive(output, "FILFAIL.LOG") is not None):
        raise ValueError("Cannot link launcher: DOS file adapter is unverified. "
                         "Run msc51-compile-dos-files first.")
    signature = locate_case_insensitive(output, "FILSIG.LOG")
    if signature is None or signature.read_text().strip() != dos_file_source_signature(output):
        raise ValueError("Cannot link launcher: DOS file adapter source has changed "
                         "since its test. Run msc51-compile-dos-files again.")


def remove_dos_file_outputs(output: Path) -> None:
    names = {"FILSTART.LOG", "FILOK.LOG", "FILFAIL.LOG", "FILBUILD.LOG", "FILSIG.LOG",
             "FILTEST.LOG", "DOSFILE.OBJ", "FILECHK.EXE", "FILECHK.OBJ"}
    if output.is_dir():
        for entry in output.iterdir():
            if entry.is_file() and entry.name.upper() in names:
                entry.unlink()


def print_dos_file_diagnostics(output: Path) -> None:
    for name in ("FILFAIL.LOG", "FILBUILD.LOG", "FILTEST.LOG", "FILOK.LOG"):
        path = locate_case_insensitive(output, name)
        content = path.read_text(errors="replace") if path else "(not created)"
        print(f"--- {name} ---\n{content[-8_000:].rstrip()}", file=sys.stderr)
    print(f"DOS file adapter workspace: {output}", file=sys.stderr)


def remove_launcher_link_outputs(output: Path) -> None:
    """Never accept a previous link's EXE, map or success marker."""
    names = {"LNKSTART.LOG", "LINKSTAR.LOG", "LINKSTART.LOG", "LINK.LOG",
             "LINKFAIL.LOG", "LINKDONE.LOG",
             "GATEWAY.EXE", "GATEWAY.MAP"}
    if output.is_dir():
        for entry in output.iterdir():
            if entry.is_file() and entry.name.upper() in names:
                entry.unlink()


def print_launcher_link_diagnostics(output: Path) -> None:
    for name in ("LINKFAIL.LOG", "LINK.LOG", "LINKDONE.LOG"):
        path = locate_case_insensitive(output, name)
        content = path.read_text(errors="replace") if path is not None else "(not created)"
        if len(content) > 16_000:
            content = "[earlier linker diagnostics omitted; see full LINK.LOG]\n" + content[-16_000:]
        print(f"--- {name} ---\n{content.rstrip()}", file=sys.stderr)
    print(f"Launcher link workspace: {output}", file=sys.stderr)


def print_launcher_diagnostics(output: Path) -> None:
    for name in ("BUILDFAIL.LOG", "BUILD.LOG", "SUCCESS.LOG"):
        path = locate_case_insensitive(output, name)
        content = path.read_text(errors="replace") if path is not None else "(not created)"
        if len(content) > 12_000:
            content = "[earlier compiler output omitted]\n" + content[-12_000:]
        print(f"--- {name} ---\n{content.rstrip()}", file=sys.stderr)
    print(f"Launcher compiler workspace: {output}", file=sys.stderr)


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


def dosbox_commands(compiler: Path, output: Path, run_probe: bool,
                    batch_file: str = "BUILD.BAT") -> list[str]:
    for path in (compiler, output):
        if any(ch in str(path) for ch in ('"', '\n', '\r')):
            raise ValueError(f"Path cannot be mounted safely in DOSBox: {path}")
    commands = [f'mount c "{compiler}" -ro', f'mount d "{output}"', "d:"]
    if run_probe:
        commands.append(batch_file)
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
    parser.add_argument("command", choices=("probe", "shell", "prepare", "install",
                                            "prepare-launcher", "compile-launcher",
                                            "compile-memory", "compile-dos-files",
                                            "link-launcher"))
    parser.add_argument("--compiler", default=os.environ.get("GATEWAY_MSC51_DIR"),
                        help="installed Microsoft C 5.1 directory (or GATEWAY_MSC51_DIR)")
    parser.add_argument("--output", type=Path,
                        help="writable DOS workspace; never the Nix store")
    parser.add_argument("--dosbox", default="dosbox-staging")
    parser.add_argument("--install-dir", type=Path,
                        help="writable target for installing the original Microsoft compiler")
    parser.add_argument("--floppy", type=Path, action="append", default=[],
                        help="one original floppy image, repeat in original disk order")
    args = parser.parse_args(argv)
    launcher = args.command in ("prepare-launcher", "compile-launcher",
                                "compile-memory", "compile-dos-files", "link-launcher")
    output = (args.output or Path("build/msc51-launcher" if launcher else "build/intro-msc51")).expanduser().resolve()
    objects = []
    if launcher:
        objects = stage_launcher(output)
        print(f"Staged {len(objects)} original launcher C modules for MSC 5.1 in {output}", flush=True)
    elif args.command not in ("shell", "install"):
        stage_probe(output)
        print(f"Staged original-code MSC 5.1 probe in {output}", flush=True)
    if args.command in ("prepare", "prepare-launcher"):
        return 0
    try:
        if args.command == "install":
            if args.install_dir is None:
                parser.error("--install-dir is required for the install command")
            destination = args.install_dir.expanduser().resolve()
            commands = install_commands(destination, [p.expanduser().resolve() for p in args.floppy])
            destination.mkdir(parents=True, exist_ok=True)
        else:
            if args.command == "link-launcher":
                validate_launcher_objects(output, objects)
            if args.command in ("compile-dos-files", "link-launcher"):
                validate_memory_adapter(output)
            if args.command == "link-launcher":
                validate_dos_file_adapter(output)
            if not args.compiler:
                parser.error("set GATEWAY_MSC51_DIR or pass --compiler /path/to/installed/MSC51")
            compiler = validate_compiler(Path(args.compiler))
            commands = dosbox_commands(
                compiler, output,
                args.command in ("probe", "compile-launcher", "compile-memory",
                                 "compile-dos-files", "link-launcher"),
                ("LINK.BAT" if args.command == "link-launcher" else
                 "DOSFILE.BAT" if args.command == "compile-dos-files" else
                 "MEMORY.BAT" if args.command == "compile-memory" else
                 "COMPILE.BAT" if launcher else "BUILD.BAT"))
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
    elif args.command == "compile-launcher":
        remove_launcher_outputs(output, objects)
        remove_launcher_link_outputs(output)
        command.append("--exit")
    elif args.command == "compile-memory":
        remove_memory_outputs(output)
        remove_launcher_link_outputs(output)
        command.append("--exit")
    elif args.command == "compile-dos-files":
        remove_dos_file_outputs(output)
        remove_launcher_link_outputs(output)
        command.append("--exit")
    elif args.command == "link-launcher":
        remove_launcher_link_outputs(output)
        command.append("--exit")
    print("DOSBox commands: " + " ; ".join(commands), flush=True)
    result = subprocess.run(command, check=False)
    if result.returncode != 0:
        if args.command == "probe":
            print_probe_diagnostics(output)
        elif args.command == "compile-launcher":
            print_launcher_diagnostics(output)
        elif args.command == "compile-memory":
            print_memory_diagnostics(output)
        elif args.command == "compile-dos-files":
            print_dos_file_diagnostics(output)
        elif args.command == "link-launcher":
            print_launcher_link_diagnostics(output)
        return result.returncode
    if args.command == "compile-memory":
        try:
            validate_memory_adapter(output)
        except ValueError:
            print("Microsoft C did not compile and verify the DOS memory adapter.", file=sys.stderr)
            print_memory_diagnostics(output)
            return 1
        print("Microsoft C 5.1 verified DOS memory access, DS binding and stack pointers; "
              "MEMORY.OBJ can now be linked with the 28 launcher objects.")
        return 0
    if args.command == "compile-dos-files":
        try:
            validate_dos_file_adapter(output)
        except ValueError:
            print("Microsoft C did not compile and verify the DOS file adapter.", file=sys.stderr)
            print_dos_file_diagnostics(output)
            return 1
        print("Microsoft C 5.1 verified DOS file create/open/read/write/seek/close; "
              "DOSFILE.OBJ can now be linked with the launcher objects.")
        return 0
    if args.command == "link-launcher":
        started = locate_case_insensitive(output, "LNKSTART.LOG")
        success = locate_case_insensitive(output, "LINKDONE.LOG")
        failed = locate_case_insensitive(output, "LINKFAIL.LOG")
        exe = locate_case_insensitive(output, "GATEWAY.EXE")
        if (started is None or success is None or failed is not None or
                exe is None or not exe.is_file() or exe.stat().st_size == 0):
            print("Microsoft LINK did not produce a verified launcher executable. "
                  "Missing original DOS/resident symbols are expected.", file=sys.stderr)
            print_launcher_link_diagnostics(output)
            return 1
        print("Microsoft LINK produced GATEWAY.EXE from all 28 launcher objects; "
              "execution and original DOS semantics remain unverified.")
        return 0
    if args.command == "compile-launcher":
        success = locate_case_insensitive(output, "SUCCESS.LOG")
        started = locate_case_insensitive(output, "START.LOG")
        failed = locate_case_insensitive(output, "BUILDFAIL.LOG")
        missing = [name for name in objects if locate_case_insensitive(output, name) is None]
        if success is None or started is None or failed is not None or missing:
            print(f"Microsoft C 5.1 launcher object compilation incomplete; "
                  f"missing {len(missing)} of {len(objects)} objects.", file=sys.stderr)
            print_launcher_diagnostics(output)
            return 1
        print(f"Microsoft C 5.1 compiled all {len(objects)} launcher objects; "
              "linkage to original DOS/resident services is still unavailable.")
        return 0
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
