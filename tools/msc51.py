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
SELECTOR_SOURCES = ("GATEMEM.H", "STDINT.H", "SEL.C", "SELCHK.C", "MEMORY.C")
STATIC_DATA_SOURCES = ("GATEMEM.H", "GATEDOS.H", "STDINT.H", "DATA.C",
                       "DATACHK.C", "DATAREAL.C", "SEL.C", "MEMORY.C", "DOSFILE.C")
DOS_FILE_SOURCES = ("GATEDOS.H", "GATEMEM.H", "STDINT.H", "DOSFILE.C", "FILECHK.C",
                    "MEMORY.C")
RTL_STATE_SOURCES = ("RTLLAY.H", "RTLIMAC.H", "RTLSTATE.H", "GATEMEM.H", "STDINT.H",
                     "RTLIMG.C", "RTLTEXT.C", "RTLPAL.C", "RTLGR.C", "RTLCHK.C",
                     "MEMORY.C")
HARDWARE_SOURCES = ("RTLLAY.H", "GATEMEM.H", "STDINT.H", "RTLHW.C", "HWCHK.C",
                    "MEMORY.C")
RUNTIME_FILE_SOURCES = ("GATEMEM.H", "STDINT.H", "RTFILE.C", "RTFCHK.C", "MEMORY.C")
PICTURE_DCL_SOURCES = ("GATEMEM.H", "GATEDOS.H", "STDINT.H",
                       "RTLIMAC.H", "RTLLAY.H", "PICTDCL.C", "PICCHK.C",
                       "DOSFILE.C", "RTLIMG.C", "MEMORY.C")
RESIDENT_STATE_SOURCES = ("GATEMEM.H", "STDINT.H", "RTLLAY.H", "RESSTATE.C",
                          "RESCHK.C", "MEMORY.C")
HEAP_SOURCES = ("GATEMEM.H", "STDINT.H", "HEAP.C", "HEAPCHK.C", "MEMORY.C")
INPUT_SOURCES = ("GATEMEM.H", "STDINT.H", "RTLLAY.H", "INP.C", "INPCHK.C",
                 "MEMORY.C")
CLOCK_SOURCES = ("GATEMEM.H", "STDINT.H", "RTLLAY.H", "CLOCK.C",
                 "CLKCHK.C", "TIMER.C", "TMRCHK.C", "MEMORY.C")
CRT_SOURCES = ("GATEMEM.H", "STDINT.H", "CRTENV.C", "CRTCHK.C", "MEMORY.C")


def memory_source_signature(output: Path) -> str:
    """Reject a stale DOS memory object after an adapter/staging change."""
    digest = hashlib.sha256(b"MS C 5.1 /AL /Od memory adapter v1\n")
    for name in MEMORY_SOURCES:
        digest.update(name.encode("ascii") + b"\0" + (output / name).read_bytes() + b"\0")
    return digest.hexdigest()[:16]


def selector_source_signature(output: Path) -> str:
    digest = hashlib.sha256(b"MS C 5.1 /AL /Od original startup selector windows v1\n")
    for name in SELECTOR_SOURCES:
        digest.update(name.encode("ascii") + b"\0" + (output / name).read_bytes() + b"\0")
    return digest.hexdigest()[:16]


def static_data_source_signature(output: Path) -> str:
    digest = hashlib.sha256(b"MS C 5.1 /AL /Od original EXE static data v1\n")
    for name in STATIC_DATA_SOURCES:
        digest.update(name.encode("ascii") + b"\0" + (output / name).read_bytes() + b"\0")
    return digest.hexdigest()[:16]


def dos_file_source_signature(output: Path) -> str:
    """Reject obsolete DOS file objects, including memory-ABI changes."""
    digest = hashlib.sha256(b"MS C 5.1 /AL /Od DOS file adapter v1\n")
    for name in DOS_FILE_SOURCES:
        digest.update(name.encode("ascii") + b"\0" + (output / name).read_bytes() + b"\0")
    return digest.hexdigest()[:16]


def rtl_state_source_signature(output: Path) -> str:
    """The probe must cover the exact same original adapters as the link."""
    digest = hashlib.sha256(b"MS C 5.1 /AL /Od RTL state adapter v1\n")
    for name in RTL_STATE_SOURCES:
        digest.update(name.encode("ascii") + b"\0" + (output / name).read_bytes() + b"\0")
    return digest.hexdigest()[:16]


def hardware_source_signature(output: Path) -> str:
    digest = hashlib.sha256(b"MS C 5.1 /AL /Od RTL hardware adapter v1\n")
    for name in HARDWARE_SOURCES:
        digest.update(name.encode("ascii") + b"\0" + (output / name).read_bytes() + b"\0")
    return digest.hexdigest()[:16]


def runtime_file_source_signature(output: Path) -> str:
    digest = hashlib.sha256(b"MS C 5.1 /AL /Od runtime file services v1\n")
    for name in RUNTIME_FILE_SOURCES:
        digest.update(name.encode("ascii") + b"\0" + (output / name).read_bytes() + b"\0")
    return digest.hexdigest()[:16]


def picture_dcl_source_signature(output: Path) -> str:
    digest = hashlib.sha256(b"MS C 5.1 /AL /Od streamed DCL picture adapter v1\n")
    for name in PICTURE_DCL_SOURCES:
        digest.update(name.encode("ascii") + b"\0" + (output / name).read_bytes() + b"\0")
    return digest.hexdigest()[:16]


def resident_state_source_signature(output: Path) -> str:
    digest = hashlib.sha256(b"MS C 5.1 /AL /Od resident state services v1\n")
    for name in RESIDENT_STATE_SOURCES:
        digest.update(name.encode("ascii") + b"\0" + (output / name).read_bytes() + b"\0")
    return digest.hexdigest()[:16]


def heap_source_signature(output: Path) -> str:
    digest = hashlib.sha256(b"MS C 5.1 /AL /Od DOS heap services v1\n")
    for name in HEAP_SOURCES:
        digest.update(name.encode("ascii") + b"\0" + (output / name).read_bytes() + b"\0")
    return digest.hexdigest()[:16]


def input_source_signature(output: Path) -> str:
    digest = hashlib.sha256(b"MS C 5.1 /AL /Od BIOS keyboard and mouse services v1\n")
    for name in INPUT_SOURCES:
        digest.update(name.encode("ascii") + b"\0" + (output / name).read_bytes() + b"\0")
    return digest.hexdigest()[:16]


def clock_source_signature(output: Path) -> str:
    digest = hashlib.sha256(b"MS C 5.1 /AL /Od DOS clock and INT 1Ch probe v3\n")
    for name in CLOCK_SOURCES:
        digest.update(name.encode("ascii") + b"\0" + (output / name).read_bytes() + b"\0")
    return digest.hexdigest()[:16]


def crt_source_signature(output: Path) -> str:
    digest = hashlib.sha256(b"MS C 5.1 /AL /Od original getenv and atol CRT v1\n")
    for name in CRT_SOURCES:
        digest.update(name.encode("ascii") + b"\0" + (output / name).read_bytes() + b"\0")
    return digest.hexdigest()[:16]


def stage_original_rtl_state(source: str, names: dict[str, str]) -> str:
    """Stage an existing native source's original DS access, not a DOS stub.

    The main launcher and these added adapters must share the exact same
    Microsoft C 5.1 31-character aliases for all external symbols.
    """
    include_re = re.compile(r'(?m)^#include "(?:gate_rtl_image|gate_rtl|gate_rtl_palette|gate_rtl_graphics)\.h"\n')
    includes = list(include_re.finditer(source))
    if not includes:
        raise ValueError("RTL state source is missing a recognized include")
    content = include_re.sub("", source)
    at = includes[0].start()
    content = (content[:at] + '#include "RTLSTATE.H"\n#include "RTLLAY.H"\n'
               '#include "RTLIMAC.H"\n' + content[at:])
    content, compounds = msc51_far_compounds_to_calls(content)
    if compounds:
        anchor = '#include "RTLLAY.H"'
        helper = ('\n\nstatic GateFarPtr gate_msc51_far_pointer(uint16_t segment, uint16_t offset)\n'
                  '{\n    GateFarPtr result;\n    result.segment = segment;\n'
                  '    result.offset = offset;\n    return result;\n}\n')
        content = content.replace(anchor, anchor + helper, 1)
    return msc51_rename_identifiers(content, names)


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
        if path.name == "gate_rtl_file.c":
            # DOS /Od must preserve the original uninitialized BP-2 stack
            # word and execute a real DIV on zero; the host differential
            # tests retain their explicit stack/fault oracle callbacks.
            content = "#define GATE_MSC51_ORIGINAL_FILE_ABI 1\n" + content
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
    for dos_name, staged_name in (("msc51_startup_selectors.c", "SEL.C"),
                                  ("msc51_startup_selectors_probe.c", "SELCHK.C")):
        (output / staged_name).write_text((source_dir / "dos" / dos_name).read_text())
    selector_signature = selector_source_signature(output)
    selector_batch = [
        "@ECHO OFF", "ECHO MS C 5.1 startup segment aliases > SELSTART.LOG",
        "SET PATH=C:\\BIN;C:\\", "SET INCLUDE=C:\\INCLUDE;C:\\",
        "SET LIB=C:\\LIB;C:\\", "SET TEMP=D:\\", "SET TMP=D:\\",
        "CL /AL /Od /c SEL.C > SELBUILD.LOG", "IF ERRORLEVEL 1 GOTO FAILED",
        "IF NOT EXIST SEL.OBJ GOTO FAILED",
        "CL /AL /Od SELCHK.C SEL.OBJ MEMORY.OBJ /FeSELCHK.EXE /link LLIBCR.LIB LIBH.LIB/NOD >> SELBUILD.LOG",
        "IF ERRORLEVEL 1 GOTO FAILED", "IF NOT EXIST SELCHK.EXE GOTO FAILED",
        "SELCHK.EXE > SELTEST.LOG", "IF ERRORLEVEL 1 GOTO FAILED",
        f"ECHO {selector_signature} > SELSIG.LOG",
        "ECHO DOS startup selector alias probe passed > SELOK.LOG",
        "GOTO DONE", ":FAILED", "ECHO DOS startup selector alias probe failed > SELFAIL.LOG",
        ":DONE", "EXIT",
    ]
    (output / "SELECT.BAT").write_text("\r\n".join(selector_batch) + "\r\n")
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
    for dos_name, staged_name in (("msc51_startup_static_data.c", "DATA.C"),
                                  ("msc51_startup_static_data_probe.c", "DATACHK.C"),
                                  ("msc51_startup_original_probe.c", "DATAREAL.C")):
        (output / staged_name).write_text((source_dir / "dos" / dos_name).read_text())
    data_signature = static_data_source_signature(output)
    data_batch = [
        "@ECHO OFF", "ECHO MS C 5.1 original EXE static data copy > DATSTART.LOG",
        "SET PATH=C:\\BIN;C:\\", "SET INCLUDE=C:\\INCLUDE;C:\\",
        "SET LIB=C:\\LIB;C:\\", "SET TEMP=D:\\", "SET TMP=D:\\",
        "CL /AL /Od /c DATA.C > DATBUILD.LOG", "IF ERRORLEVEL 1 GOTO FAILED",
        "IF NOT EXIST DATA.OBJ GOTO FAILED",
        "CL /AL /Od DATACHK.C DATA.OBJ SEL.OBJ MEMORY.OBJ DOSFILE.OBJ /FeDATACHK.EXE /link LLIBCR.LIB LIBH.LIB/NOD >> DATBUILD.LOG",
        "IF ERRORLEVEL 1 GOTO FAILED", "IF NOT EXIST DATACHK.EXE GOTO FAILED",
        "DATACHK.EXE > DATTEST.LOG", "IF ERRORLEVEL 1 GOTO FAILED",
        "CL /AL /Od DATAREAL.C DATA.OBJ SEL.OBJ MEMORY.OBJ DOSFILE.OBJ /FeDATAREAL.EXE /link LLIBCR.LIB LIBH.LIB/NOD >> DATBUILD.LOG",
        "IF ERRORLEVEL 1 GOTO FAILED", "IF NOT EXIST DATAREAL.EXE GOTO FAILED",
        "IF EXIST E:\\GATE.EXE DATAREAL.EXE > DATREAL.LOG",
        "IF ERRORLEVEL 1 GOTO FAILED",
        f"ECHO {data_signature} > DATSIG.LOG",
        "ECHO DOS static EXE data copy probe passed > DATOK.LOG",
        "GOTO DONE", ":FAILED", "ECHO DOS static EXE data copy probe failed > DATFAIL.LOG",
        ":DONE", "EXIT",
    ]
    (output / "DATA.BAT").write_text("\r\n".join(data_batch) + "\r\n")
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
    # Keep the original/native accessor source as the single definition of
    # these RTL operations. Only the includes, C89 compound literals and
    # historical compiler symbol-length limit change for the DOS build.
    layout = msc51_enums_to_macros((source_dir / "gate_memory_layout.h").read_text())
    (output / "RTLLAY.H").write_text(msc51_rename_identifiers(layout, shortened))
    image_constants = msc51_enums_to_macros((source_dir / "gate_rtl_image.h").read_text())
    required_constants = ("GATE_RTL_VIDEO_MODE_OFFSET", "GATE_RTL_FILE_HANDLE_OFFSET",
                          "GATE_RTL_HEADER_OFFSET", "GATE_RTL_SIZE_TABLE_OFFSET",
                          "GATE_RTL_TRANSFORM_OFFSET", "GATE_PREPARE_STATUS_WORD")
    image_defines = []
    for name in required_constants:
        match = re.search(rf"(?m)^#define {name} \(([^\n]+)\)$", image_constants)
        if match is None:
            raise ValueError(f"Original image header is missing {name}")
        image_defines.append(match.group(0))
    (output / "RTLIMAC.H").write_text(
        "/* Image constants extracted from the original recovered header. */\n" +
        msc51_rename_identifiers("\n".join(image_defines) + "\n", shortened))
    (output / "RTLSTATE.H").write_text(msc51_rename_identifiers(
        (source_dir / "dos/msc51_rtl_state.h").read_text(), shortened))
    (output / "RTLCHK.C").write_text(msc51_rename_identifiers(
        (source_dir / "dos/msc51_rtl_state_probe.c").read_text(), shortened))
    for source_name, staged_name in (("intro_host_rtl_globals.c", "RTLIMG.C"),
                                     ("intro_host_rtl_text_globals.c", "RTLTEXT.C"),
                                     ("intro_host_rtl_palette_globals.c", "RTLPAL.C"),
                                     ("intro_host_rtl_graphics_globals.c", "RTLGR.C")):
        (output / staged_name).write_text(stage_original_rtl_state(
            (REPO / "src" / source_name).read_text(), shortened))
    rtl_signature = rtl_state_source_signature(output)
    rtl_batch = [
        "@ECHO OFF", "ECHO MS C 5.1 native RTL state accessors > RTLSTART.LOG",
        "SET PATH=C:\\BIN;C:\\", "SET INCLUDE=C:\\INCLUDE;C:\\",
        "SET LIB=C:\\LIB;C:\\", "SET TEMP=D:\\", "SET TMP=D:\\",
        "CL /AL /Od /c RTLIMG.C > RTLBUILD.LOG", "IF ERRORLEVEL 1 GOTO FAILED",
        "IF NOT EXIST RTLIMG.OBJ GOTO FAILED",
        "CL /AL /Od /c RTLTEXT.C >> RTLBUILD.LOG", "IF ERRORLEVEL 1 GOTO FAILED",
        "IF NOT EXIST RTLTEXT.OBJ GOTO FAILED",
        "CL /AL /Od /c RTLPAL.C >> RTLBUILD.LOG", "IF ERRORLEVEL 1 GOTO FAILED",
        "IF NOT EXIST RTLPAL.OBJ GOTO FAILED",
        "CL /AL /Od /c RTLGR.C >> RTLBUILD.LOG", "IF ERRORLEVEL 1 GOTO FAILED",
        "IF NOT EXIST RTLGR.OBJ GOTO FAILED",
        "CL /AL /Od RTLCHK.C RTLIMG.OBJ RTLTEXT.OBJ RTLPAL.OBJ RTLGR.OBJ MEMORY.OBJ /FeRTLCHK.EXE /link LLIBCR.LIB LIBH.LIB/NOD >> RTLBUILD.LOG",
        "IF ERRORLEVEL 1 GOTO FAILED", "IF NOT EXIST RTLCHK.EXE GOTO FAILED",
        "RTLCHK.EXE > RTLTEST.LOG", "IF ERRORLEVEL 1 GOTO FAILED",
        f"ECHO {rtl_signature} > RTLSIG.LOG",
        "ECHO Original RTL state probe passed > RTLOK.LOG",
        "GOTO DONE", ":FAILED",
        "ECHO Original RTL state probe failed > RTLFAIL.LOG",
        ":DONE", "EXIT",
    ]
    (output / "RTLST.BAT").write_text("\r\n".join(rtl_batch) + "\r\n")
    for original, staged in (("msc51_hardware.c", "RTLHW.C"),
                             ("msc51_hardware_probe.c", "HWCHK.C")):
        (output / staged).write_text(msc51_rename_identifiers(
            (source_dir / "dos" / original).read_text(), shortened))
    hardware_signature = hardware_source_signature(output)
    hardware_batch = [
        "@ECHO OFF", "ECHO MS C 5.1 RTL hardware adapter > HWSTART.LOG",
        "SET PATH=C:\\BIN;C:\\", "SET INCLUDE=C:\\INCLUDE;C:\\",
        "SET LIB=C:\\LIB;C:\\", "SET TEMP=D:\\", "SET TMP=D:\\",
        "CL /AL /Od /c RTLHW.C > HWBUILD.LOG", "IF ERRORLEVEL 1 GOTO FAILED",
        "IF NOT EXIST RTLHW.OBJ GOTO FAILED",
        "CL /AL /Od HWCHK.C RTLHW.OBJ MEMORY.OBJ /FeHWCHK.EXE /link LLIBCR.LIB LIBH.LIB/NOD >> HWBUILD.LOG",
        "IF ERRORLEVEL 1 GOTO FAILED", "IF NOT EXIST HWCHK.EXE GOTO FAILED",
        "HWCHK.EXE > HWTEST.LOG", "IF ERRORLEVEL 1 GOTO FAILED",
        f"ECHO {hardware_signature} > HWSIG.LOG",
        "ECHO Real-mode RTL hardware adapter passed > HWOK.LOG",
        "GOTO DONE", ":FAILED",
        "ECHO Real-mode RTL hardware adapter failed > HWFAIL.LOG",
        ":DONE", "EXIT",
    ]
    (output / "RTLHW.BAT").write_text("\r\n".join(hardware_batch) + "\r\n")
    for original, staged in (("msc51_runtime_files.c", "RTFILE.C"),
                             ("msc51_runtime_files_probe.c", "RTFCHK.C")):
        (output / staged).write_text(msc51_rename_identifiers(
            (source_dir / "dos" / original).read_text(), shortened))
    runtime_file_signature = runtime_file_source_signature(output)
    runtime_file_batch = [
        "@ECHO OFF", "ECHO MS C 5.1 runtime FAR string and path services > RTFSTART.LOG",
        "SET PATH=C:\\BIN;C:\\", "SET INCLUDE=C:\\INCLUDE;C:\\",
        "SET LIB=C:\\LIB;C:\\", "SET TEMP=D:\\", "SET TMP=D:\\",
        "CL /AL /Od /c RTFILE.C > RTFBUILD.LOG", "IF ERRORLEVEL 1 GOTO FAILED",
        "IF NOT EXIST RTFILE.OBJ GOTO FAILED",
        "CL /AL /Od RTFCHK.C RTFILE.OBJ MEMORY.OBJ /FeRTFCHK.EXE /link LLIBCR.LIB LIBH.LIB/NOD >> RTFBUILD.LOG",
        "IF ERRORLEVEL 1 GOTO FAILED", "IF NOT EXIST RTFCHK.EXE GOTO FAILED",
        "RTFCHK.EXE > RTFTEST.LOG", "IF ERRORLEVEL 1 GOTO FAILED",
        f"ECHO {runtime_file_signature} > RTFSIG.LOG",
        "ECHO Real-mode runtime file services passed > RTFOK.LOG",
        "GOTO DONE", ":FAILED",
        "ECHO Real-mode runtime file services failed > RTFFAIL.LOG",
        ":DONE", "EXIT",
    ]
    (output / "RTFILE.BAT").write_text("\r\n".join(runtime_file_batch) + "\r\n")
    for original, staged in (("msc51_picture_dcl.c", "PICTDCL.C"),
                             ("msc51_picture_dcl_probe.c", "PICCHK.C")):
        (output / staged).write_text(msc51_rename_identifiers(
            (source_dir / "dos" / original).read_text(), shortened))
    picture_signature = picture_dcl_source_signature(output)
    picture_batch = [
        "@ECHO OFF", "ECHO MS C 5.1 streaming DCL decoder > PICSTART.LOG",
        "SET PATH=C:\\BIN;C:\\", "SET INCLUDE=C:\\INCLUDE;C:\\",
        "SET LIB=C:\\LIB;C:\\", "SET TEMP=D:\\", "SET TMP=D:\\",
        "CL /AL /Od /c PICTDCL.C > PICBUILD.LOG", "IF ERRORLEVEL 1 GOTO FAILED",
        "IF NOT EXIST PICTDCL.OBJ GOTO FAILED",
        "CL /AL /Od PICCHK.C PICTDCL.OBJ DOSFILE.OBJ RTLIMG.OBJ MEMORY.OBJ /FePICCHK.EXE /link LLIBCR.LIB LIBH.LIB/NOD >> PICBUILD.LOG",
        "IF ERRORLEVEL 1 GOTO FAILED", "IF NOT EXIST PICCHK.EXE GOTO FAILED",
        "PICCHK.EXE > PICTEST.LOG", "IF ERRORLEVEL 1 GOTO FAILED",
        f"ECHO {picture_signature} > PICSIG.LOG",
        "ECHO Real-mode picture decoder probe passed > PICOK.LOG",
        "GOTO DONE", ":FAILED",
        "ECHO Real-mode picture decoder probe failed > PICFAIL.LOG",
        ":DONE", "EXIT",
    ]
    (output / "PICTDCL.BAT").write_text("\r\n".join(picture_batch) + "\r\n")
    for original, staged in (("msc51_resident_state.c", "RESSTATE.C"),
                             ("msc51_resident_state_probe.c", "RESCHK.C")):
        (output / staged).write_text(msc51_rename_identifiers(
            (source_dir / "dos" / original).read_text(), shortened))
    resident_state_signature = resident_state_source_signature(output)
    resident_state_batch = [
        "@ECHO OFF", "ECHO MS C 5.1 resident mode and drawing state > RESSTART.LOG",
        "SET PATH=C:\\BIN;C:\\", "SET INCLUDE=C:\\INCLUDE;C:\\",
        "SET LIB=C:\\LIB;C:\\", "SET TEMP=D:\\", "SET TMP=D:\\",
        "CL /AL /Od /c RESSTATE.C > RESBUILD.LOG", "IF ERRORLEVEL 1 GOTO FAILED",
        "IF NOT EXIST RESSTATE.OBJ GOTO FAILED",
        "CL /AL /Od RESCHK.C RESSTATE.OBJ MEMORY.OBJ /FeRESCHK.EXE /link LLIBCR.LIB LIBH.LIB/NOD >> RESBUILD.LOG",
        "IF ERRORLEVEL 1 GOTO FAILED", "IF NOT EXIST RESCHK.EXE GOTO FAILED",
        "RESCHK.EXE > RESTEST.LOG", "IF ERRORLEVEL 1 GOTO FAILED",
        f"ECHO {resident_state_signature} > RESSIG.LOG",
        "ECHO Real-mode resident state service probe passed > RESOK.LOG",
        "GOTO DONE", ":FAILED",
        "ECHO Real-mode resident state service probe failed > RESFAIL.LOG",
        ":DONE", "EXIT",
    ]
    (output / "RESST.BAT").write_text("\r\n".join(resident_state_batch) + "\r\n")
    for original, staged in (("msc51_heap.c", "HEAP.C"),
                             ("msc51_heap_probe.c", "HEAPCHK.C")):
        (output / staged).write_text(msc51_rename_identifiers(
            (source_dir / "dos" / original).read_text(), shortened))
    heap_signature = heap_source_signature(output)
    heap_batch = [
        "@ECHO OFF", "ECHO MS C 5.1 real DOS conventional heap > HEAPSTRT.LOG",
        "SET PATH=C:\\BIN;C:\\", "SET INCLUDE=C:\\INCLUDE;C:\\",
        "SET LIB=C:\\LIB;C:\\", "SET TEMP=D:\\", "SET TMP=D:\\",
        "CL /AL /Od /c HEAP.C > HEAPBLD.LOG", "IF ERRORLEVEL 1 GOTO FAILED",
        "IF NOT EXIST HEAP.OBJ GOTO FAILED",
        "CL /AL /Od HEAPCHK.C HEAP.OBJ MEMORY.OBJ /FeHEAPCHK.EXE /link LLIBCR.LIB LIBH.LIB/NOD >> HEAPBLD.LOG",
        "IF ERRORLEVEL 1 GOTO FAILED", "IF NOT EXIST HEAPCHK.EXE GOTO FAILED",
        "HEAPCHK.EXE > HEAPTST.LOG", "IF ERRORLEVEL 1 GOTO FAILED",
        f"ECHO {heap_signature} > HEAPSIG.LOG",
        "ECHO Real-mode DOS heap probe passed > HEAPOK.LOG",
        "GOTO DONE", ":FAILED", "ECHO Real-mode DOS heap probe failed > HEAPFAIL.LOG",
        ":DONE", "EXIT",
    ]
    (output / "HEAP.BAT").write_text("\r\n".join(heap_batch) + "\r\n")
    for original, staged in (("msc51_input.c", "INP.C"),
                             ("msc51_input_probe.c", "INPCHK.C")):
        (output / staged).write_text(msc51_rename_identifiers(
            (source_dir / "dos" / original).read_text(), shortened))
    input_signature = input_source_signature(output)
    input_batch = [
        "@ECHO OFF", "ECHO MS C 5.1 BIOS keyboard and mouse > INPSTART.LOG",
        "SET PATH=C:\\BIN;C:\\", "SET INCLUDE=C:\\INCLUDE;C:\\",
        "SET LIB=C:\\LIB;C:\\", "SET TEMP=D:\\", "SET TMP=D:\\",
        "CL /AL /Od /c INP.C > INPBUILD.LOG", "IF ERRORLEVEL 1 GOTO FAILED",
        "IF NOT EXIST INP.OBJ GOTO FAILED",
        "CL /AL /Od INPCHK.C INP.OBJ MEMORY.OBJ /FeINPCHK.EXE /link LLIBCR.LIB LIBH.LIB/NOD >> INPBUILD.LOG",
        "IF ERRORLEVEL 1 GOTO FAILED", "IF NOT EXIST INPCHK.EXE GOTO FAILED",
        "INPCHK.EXE > INPTEST.LOG", "IF ERRORLEVEL 1 GOTO FAILED",
        f"ECHO {input_signature} > INPSIG.LOG",
        "ECHO Real-mode BIOS input probe passed > INPOK.LOG",
        "GOTO DONE", ":FAILED", "ECHO Real-mode BIOS input probe failed > INPFAIL.LOG",
        ":DONE", "EXIT",
    ]
    (output / "INPUT.BAT").write_text("\r\n".join(input_batch) + "\r\n")
    for original, staged in (("msc51_clock.c", "CLOCK.C"),
                             ("msc51_clock_probe.c", "CLKCHK.C"),
                             ("msc51_timer_irq.c", "TIMER.C"),
                             ("msc51_timer_irq_probe.c", "TMRCHK.C")):
        (output / staged).write_text(msc51_rename_identifiers(
            (source_dir / "dos" / original).read_text(), shortened))
    clock_signature = clock_source_signature(output)
    clock_batch = [
        "@ECHO OFF", "ECHO MS C 5.1 real DOS clock and waits > CLKSTART.LOG",
        "SET PATH=C:\\BIN;C:\\", "SET INCLUDE=C:\\INCLUDE;C:\\",
        "SET LIB=C:\\LIB;C:\\", "SET TEMP=D:\\", "SET TMP=D:\\",
        "CL /AL /Od /c CLOCK.C > CLKBUILD.LOG", "IF ERRORLEVEL 1 GOTO FAILED",
        "IF NOT EXIST CLOCK.OBJ GOTO FAILED",
        "CL /AL /Od CLKCHK.C CLOCK.OBJ MEMORY.OBJ /FeCLKCHK.EXE /link LLIBCR.LIB LIBH.LIB/NOD >> CLKBUILD.LOG",
        "IF ERRORLEVEL 1 GOTO FAILED", "IF NOT EXIST CLKCHK.EXE GOTO FAILED",
        "CLKCHK.EXE > CLKTEST.LOG", "IF ERRORLEVEL 1 GOTO FAILED",
        "CL /AL /Od /c TIMER.C >> CLKBUILD.LOG", "IF ERRORLEVEL 1 GOTO FAILED",
        "IF NOT EXIST TIMER.OBJ GOTO FAILED",
        "CL /AL /Od TMRCHK.C TIMER.OBJ /FeTMRCHK.EXE /link LLIBCR.LIB LIBH.LIB/NOD >> CLKBUILD.LOG",
        "IF ERRORLEVEL 1 GOTO FAILED", "IF NOT EXIST TMRCHK.EXE GOTO FAILED",
        "TMRCHK.EXE > TMRTEST.LOG", "IF ERRORLEVEL 1 GOTO FAILED",
        f"ECHO {clock_signature} > CLKSIG.LOG",
        "ECHO Real-mode DOS clock probe passed > CLKOK.LOG",
        "GOTO DONE", ":FAILED", "ECHO Real-mode DOS clock probe failed > CLKFAIL.LOG",
        ":DONE", "EXIT",
    ]
    (output / "CLOCK.BAT").write_text("\r\n".join(clock_batch) + "\r\n")
    for original, staged in (("msc51_crt.c", "CRTENV.C"),
                             ("msc51_crt_probe.c", "CRTCHK.C")):
        (output / staged).write_text(msc51_rename_identifiers(
            (source_dir / "dos" / original).read_text(), shortened))
    crt_signature = crt_source_signature(output)
    crt_batch = [
        "@ECHO OFF", "ECHO MS C 5.1 original environment and atol > CRTSTART.LOG",
        "SET PATH=C:\\BIN;C:\\", "SET INCLUDE=C:\\INCLUDE;C:\\",
        "SET LIB=C:\\LIB;C:\\", "SET TEMP=D:\\", "SET TMP=D:\\",
        "CL /AL /Od /c CRTENV.C > CRTBUILD.LOG", "IF ERRORLEVEL 1 GOTO FAILED",
        "IF NOT EXIST CRTENV.OBJ GOTO FAILED",
        "CL /AL /Od CRTCHK.C CRTENV.OBJ MEMORY.OBJ /FeCRTCHK.EXE /link LLIBCR.LIB LIBH.LIB/NOD >> CRTBUILD.LOG",
        "IF ERRORLEVEL 1 GOTO FAILED", "IF NOT EXIST CRTCHK.EXE GOTO FAILED",
        "CRTCHK.EXE > CRTTEST.LOG", "IF ERRORLEVEL 1 GOTO FAILED",
        f"ECHO {crt_signature} > CRTSIG.LOG",
        "ECHO Original Microsoft DOS CRT probe passed > CRTOK.LOG",
        "GOTO DONE", ":FAILED", "ECHO Microsoft DOS CRT probe failed > CRTFAIL.LOG",
        ":DONE", "EXIT",
    ]
    (output / "CRT.BAT").write_text("\r\n".join(crt_batch) + "\r\n")
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
    response = ([f"{name}+" for name in objects] + ["MEMORY.OBJ+", "DOSFILE.OBJ+",
                "RTLIMG.OBJ+", "RTLTEXT.OBJ+", "RTLPAL.OBJ+", "RTLGR.OBJ+",
                "RTLHW.OBJ+", "RTFILE.OBJ+", "RESSTATE.OBJ+", "HEAP.OBJ+",
                "INP.OBJ+", "CLOCK.OBJ+", "CRTENV.OBJ+", "PICTDCL.OBJ",
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


def validate_link_prerequisites(output: Path, objects: list[str]) -> None:
    """Report every missing/stale probe before running Microsoft LINK.

    The separately compiled adapters depend on shared staged headers. A
    change to a header may invalidate several objects at once; rejecting
    only the first forces unnecessary DOSBox/link retry cycles.
    """
    validations = (
        ("msc51-compile-launcher", lambda: validate_launcher_objects(output, objects)),
        ("msc51-compile-memory", lambda: validate_memory_adapter(output)),
        ("msc51-compile-dos-files", lambda: validate_dos_file_adapter(output)),
        ("msc51-compile-rtl-state", lambda: validate_rtl_state_adapter(output)),
        ("msc51-compile-hardware", lambda: validate_hardware_adapter(output)),
        ("msc51-compile-runtime-files", lambda: validate_runtime_file_adapter(output)),
        ("msc51-compile-picture-dcl", lambda: validate_picture_dcl_adapter(output)),
        ("msc51-compile-resident-state", lambda: validate_resident_state_adapter(output)),
        ("msc51-compile-heap", lambda: validate_heap_adapter(output)),
        ("msc51-compile-input", lambda: validate_input_adapter(output)),
        ("msc51-compile-clock", lambda: validate_clock_adapter(output)),
        ("msc51-compile-crt", lambda: validate_crt_adapter(output)),
    )
    failures = []
    for command, validate in validations:
        try:
            validate()
        except ValueError as error:
            failures.append(f"  nix run -L .#{command}\n    {error}")
    if failures:
        raise ValueError("Cannot link launcher; rebuild these prerequisites "
                         "and rerun LINK:\n" + "\n".join(failures))


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


def validate_selector_probe(output: Path) -> None:
    if (any(locate_case_insensitive(output, name) is None
            for name in ("SEL.OBJ", "SELCHK.EXE", "SELSTART.LOG", "SELOK.LOG",
                         "SELTEST.LOG")) or
            locate_case_insensitive(output, "SELFAIL.LOG") is not None):
        raise ValueError("Microsoft DOS startup selector probe failed. "
                         "Run msc51-compile-selectors again.")
    signature = locate_case_insensitive(output, "SELSIG.LOG")
    if signature is None or signature.read_text().strip() != selector_source_signature(output):
        raise ValueError("DOS selector source changed since its test. "
                         "Run msc51-compile-selectors again.")
    result = locate_case_insensitive(output, "SELTEST.LOG")
    if "Selector alias probe: PASS (12 data, 33 lower, 160-byte overlap)" not in result.read_text():
        raise ValueError("DOS selector test did not verify both overlapping windows.")


def remove_selector_outputs(output: Path) -> None:
    names = {"SEL.OBJ", "SELCHK.OBJ", "SELCHK.EXE", "SELSTART.LOG", "SELBUILD.LOG",
             "SELTEST.LOG", "SELOK.LOG", "SELFAIL.LOG", "SELSIG.LOG"}
    if output.is_dir():
        for entry in output.iterdir():
            if entry.is_file() and entry.name.upper() in names:
                entry.unlink()


def print_selector_diagnostics(output: Path) -> None:
    for name in ("SELFAIL.LOG", "SELBUILD.LOG", "SELTEST.LOG", "SELOK.LOG"):
        path = locate_case_insensitive(output, name)
        content = path.read_text(errors="replace") if path else "(not created)"
        print(f"--- {name} ---\n{content[-8_000:].rstrip()}", file=sys.stderr)
    print(f"DOS selector workspace: {output}", file=sys.stderr)


def validate_static_data_probe(output: Path, *, require_original: bool = False) -> None:
    if (any(locate_case_insensitive(output, name) is None
            for name in ("DATA.OBJ", "DATACHK.EXE", "DATAREAL.EXE", "DATSTART.LOG",
                         "DATOK.LOG", "DATTEST.LOG")) or
            locate_case_insensitive(output, "DATFAIL.LOG") is not None):
        raise ValueError("DOS static EXE data copy probe failed. "
                         "Run msc51-compile-data again.")
    signature = locate_case_insensitive(output, "DATSIG.LOG")
    if signature is None or signature.read_text().strip() != static_data_source_signature(output):
        raise ValueError("DOS static data source changed since its test. "
                         "Run msc51-compile-data again.")
    result = locate_case_insensitive(output, "DATTEST.LOG")
    if re.search(r"^Static EXE data probe: PASS \(5976 bytes at DS:3592h\)\s*$",
                 result.read_text(), re.MULTILINE) is None:
        raise ValueError("DOS static data copy log does not verify all 5976 bytes.")
    if require_original:
        real = locate_case_insensitive(output, "DATREAL.LOG")
        if (real is None or re.search(
                r"^Original EXE data probe: PASS \(5976 bytes, matching disk image\)\s*$",
                real.read_text(), re.MULTILINE) is None):
            raise ValueError("DOS original GATE.EXE data test is missing or failed.")


def remove_static_data_outputs(output: Path) -> None:
    names = {"DATA.OBJ", "DATACHK.OBJ", "DATACHK.EXE", "DATAREAL.OBJ",
             "DATAREAL.EXE", "DATREAL.LOG", "DATSTART.LOG",
             "DATBUILD.LOG", "DATTEST.LOG", "DATOK.LOG", "DATFAIL.LOG", "DATSIG.LOG",
             "DATASRC.BIN"}
    if output.is_dir():
        for entry in output.iterdir():
            if entry.is_file() and entry.name.upper() in names:
                entry.unlink()


def print_static_data_diagnostics(output: Path) -> None:
    for name in ("DATFAIL.LOG", "DATBUILD.LOG", "DATTEST.LOG", "DATREAL.LOG", "DATOK.LOG"):
        path = locate_case_insensitive(output, name)
        content = path.read_text(errors="replace") if path else "(not created)"
        print(f"--- {name} ---\n{content[-8_000:].rstrip()}", file=sys.stderr)
    print(f"DOS static data workspace: {output}", file=sys.stderr)


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


def validate_rtl_state_adapter(output: Path) -> None:
    """Only link the RTL native-source accessors after a real DOS probe."""
    if (any(locate_case_insensitive(output, name) is None
            for name in ("RTLIMG.OBJ", "RTLTEXT.OBJ", "RTLPAL.OBJ", "RTLGR.OBJ",
                         "RTLSTART.LOG", "RTLOK.LOG")) or
            locate_case_insensitive(output, "RTLFAIL.LOG") is not None):
        raise ValueError("Cannot link launcher: RTL state accessors are unverified. "
                         "Run msc51-compile-rtl-state first.")
    signature = locate_case_insensitive(output, "RTLSIG.LOG")
    if signature is None or signature.read_text().strip() != rtl_state_source_signature(output):
        raise ValueError("Cannot link launcher: RTL state source has changed "
                         "since its test. Run msc51-compile-rtl-state again.")


def remove_rtl_state_outputs(output: Path) -> None:
    names = {"RTLSTART.LOG", "RTLOK.LOG", "RTLFAIL.LOG", "RTLBUILD.LOG",
             "RTLSIG.LOG", "RTLTEST.LOG", "RTLIMG.OBJ", "RTLTEXT.OBJ",
             "RTLPAL.OBJ", "RTLGR.OBJ",
             "RTLCHK.EXE", "RTLCHK.OBJ"}
    if output.is_dir():
        for entry in output.iterdir():
            if entry.is_file() and entry.name.upper() in names:
                entry.unlink()


def print_rtl_state_diagnostics(output: Path) -> None:
    for name in ("RTLFAIL.LOG", "RTLBUILD.LOG", "RTLTEST.LOG", "RTLOK.LOG"):
        path = locate_case_insensitive(output, name)
        content = path.read_text(errors="replace") if path else "(not created)"
        print(f"--- {name} ---\n{content[-8_000:].rstrip()}", file=sys.stderr)
    print(f"RTL state adapter workspace: {output}", file=sys.stderr)


def validate_hardware_adapter(output: Path) -> None:
    if (any(locate_case_insensitive(output, name) is None
            for name in ("RTLHW.OBJ", "HWSTART.LOG", "HWOK.LOG")) or
            locate_case_insensitive(output, "HWFAIL.LOG") is not None):
        raise ValueError("Cannot link launcher: RTL hardware adapter is unverified. "
                         "Run msc51-compile-hardware first.")
    signature = locate_case_insensitive(output, "HWSIG.LOG")
    if signature is None or signature.read_text().strip() != hardware_source_signature(output):
        raise ValueError("Cannot link launcher: RTL hardware source has changed "
                         "since its test. Run msc51-compile-hardware again.")


def remove_hardware_outputs(output: Path) -> None:
    names = {"HWSTART.LOG", "HWOK.LOG", "HWFAIL.LOG", "HWBUILD.LOG", "HWSIG.LOG",
             "HWTEST.LOG", "RTLHW.OBJ", "HWCHK.EXE", "HWCHK.OBJ"}
    if output.is_dir():
        for entry in output.iterdir():
            if entry.is_file() and entry.name.upper() in names:
                entry.unlink()


def print_hardware_diagnostics(output: Path) -> None:
    for name in ("HWFAIL.LOG", "HWBUILD.LOG", "HWTEST.LOG", "HWOK.LOG"):
        path = locate_case_insensitive(output, name)
        content = path.read_text(errors="replace") if path else "(not created)"
        print(f"--- {name} ---\n{content[-8_000:].rstrip()}", file=sys.stderr)
    print(f"RTL hardware adapter workspace: {output}", file=sys.stderr)


def validate_runtime_file_adapter(output: Path) -> None:
    if (any(locate_case_insensitive(output, name) is None
            for name in ("RTFILE.OBJ", "RTFSTART.LOG", "RTFOK.LOG")) or
            locate_case_insensitive(output, "RTFFAIL.LOG") is not None):
        raise ValueError("Cannot link launcher: runtime FAR string/file services "
                         "are unverified. Run msc51-compile-runtime-files first.")
    signature = locate_case_insensitive(output, "RTFSIG.LOG")
    if signature is None or signature.read_text().strip() != runtime_file_source_signature(output):
        raise ValueError("Cannot link launcher: runtime FAR string/file source has changed "
                         "since its test. Run msc51-compile-runtime-files again.")


def remove_runtime_file_outputs(output: Path) -> None:
    names = {"RTFSTART.LOG", "RTFOK.LOG", "RTFFAIL.LOG", "RTFBUILD.LOG",
             "RTFSIG.LOG", "RTFTEST.LOG", "RTFILE.OBJ", "RTFCHK.EXE", "RTFCHK.OBJ"}
    if output.is_dir():
        for entry in output.iterdir():
            if entry.is_file() and entry.name.upper() in names:
                entry.unlink()


def print_runtime_file_diagnostics(output: Path) -> None:
    for name in ("RTFFAIL.LOG", "RTFBUILD.LOG", "RTFTEST.LOG", "RTFOK.LOG"):
        path = locate_case_insensitive(output, name)
        content = path.read_text(errors="replace") if path else "(not created)"
        print(f"--- {name} ---\n{content[-8_000:].rstrip()}", file=sys.stderr)
    print(f"Runtime FAR string/file workspace: {output}", file=sys.stderr)


def validate_picture_dcl_adapter(output: Path) -> None:
    if (any(locate_case_insensitive(output, name) is None
            for name in ("PICTDCL.OBJ", "PICSTART.LOG", "PICOK.LOG")) or
            locate_case_insensitive(output, "PICFAIL.LOG") is not None):
        raise ValueError("Cannot link launcher: DCL picture decoder is unverified. "
                         "Run msc51-compile-picture-dcl first.")
    signature = locate_case_insensitive(output, "PICSIG.LOG")
    if signature is None or signature.read_text().strip() != picture_dcl_source_signature(output):
        raise ValueError("Cannot link launcher: DCL picture decoder source has changed "
                         "since its test. Run msc51-compile-picture-dcl again.")


def remove_picture_dcl_outputs(output: Path) -> None:
    names = {"PICSTART.LOG", "PICOK.LOG", "PICFAIL.LOG", "PICBUILD.LOG",
             "PICSIG.LOG", "PICTEST.LOG", "PICTDCL.OBJ", "PICCHK.EXE", "PICCHK.OBJ"}
    if output.is_dir():
        for entry in output.iterdir():
            if entry.is_file() and entry.name.upper() in names:
                entry.unlink()


def print_picture_dcl_diagnostics(output: Path) -> None:
    for name in ("PICFAIL.LOG", "PICBUILD.LOG", "PICTEST.LOG", "PICOK.LOG"):
        path = locate_case_insensitive(output, name)
        content = path.read_text(errors="replace") if path else "(not created)"
        print(f"--- {name} ---\n{content[-8_000:].rstrip()}", file=sys.stderr)
    print(f"DCL picture decoder workspace: {output}", file=sys.stderr)


def validate_resident_state_adapter(output: Path) -> None:
    if (any(locate_case_insensitive(output, name) is None
            for name in ("RESSTATE.OBJ", "RESSTART.LOG", "RESOK.LOG")) or
            locate_case_insensitive(output, "RESFAIL.LOG") is not None):
        raise ValueError("Cannot link launcher: resident state and BIOS services "
                         "are unverified. Run msc51-compile-resident-state first.")
    signature = locate_case_insensitive(output, "RESSIG.LOG")
    if signature is None or signature.read_text().strip() != resident_state_source_signature(output):
        raise ValueError("Cannot link launcher: resident state source has changed "
                         "since its test. Run msc51-compile-resident-state again.")


def remove_resident_state_outputs(output: Path) -> None:
    names = {"RESSTART.LOG", "RESOK.LOG", "RESFAIL.LOG", "RESBUILD.LOG",
             "RESSIG.LOG", "RESTEST.LOG", "RESSTATE.OBJ", "RESCHK.EXE", "RESCHK.OBJ"}
    if output.is_dir():
        for entry in output.iterdir():
            if entry.is_file() and entry.name.upper() in names:
                entry.unlink()


def print_resident_state_diagnostics(output: Path) -> None:
    for name in ("RESFAIL.LOG", "RESBUILD.LOG", "RESTEST.LOG", "RESOK.LOG"):
        path = locate_case_insensitive(output, name)
        content = path.read_text(errors="replace") if path else "(not created)"
        print(f"--- {name} ---\n{content[-8_000:].rstrip()}", file=sys.stderr)
    print(f"Resident state workspace: {output}", file=sys.stderr)


def validate_heap_adapter(output: Path) -> None:
    if (any(locate_case_insensitive(output, name) is None
            for name in ("HEAP.OBJ", "HEAPSTRT.LOG", "HEAPOK.LOG")) or
            locate_case_insensitive(output, "HEAPFAIL.LOG") is not None):
        raise ValueError("Cannot link launcher: DOS heap services are unverified. "
                         "Run msc51-compile-heap first.")
    signature = locate_case_insensitive(output, "HEAPSIG.LOG")
    if signature is None or signature.read_text().strip() != heap_source_signature(output):
        raise ValueError("Cannot link launcher: heap adapter source has changed "
                         "since its test. Run msc51-compile-heap again.")


def remove_heap_outputs(output: Path) -> None:
    names = {"HEAPSTRT.LOG", "HEAPOK.LOG", "HEAPFAIL.LOG", "HEAPBLD.LOG",
             "HEAPSIG.LOG", "HEAPTST.LOG", "HEAP.OBJ", "HEAPCHK.EXE", "HEAPCHK.OBJ"}
    if output.is_dir():
        for entry in output.iterdir():
            if entry.is_file() and entry.name.upper() in names:
                entry.unlink()


def print_heap_diagnostics(output: Path) -> None:
    for name in ("HEAPFAIL.LOG", "HEAPBLD.LOG", "HEAPTST.LOG", "HEAPOK.LOG"):
        path = locate_case_insensitive(output, name)
        content = path.read_text(errors="replace") if path else "(not created)"
        print(f"--- {name} ---\n{content[-8_000:].rstrip()}", file=sys.stderr)
    print(f"DOS heap workspace: {output}", file=sys.stderr)


def validate_input_adapter(output: Path) -> None:
    if (any(locate_case_insensitive(output, name) is None
            for name in ("INP.OBJ", "INPSTART.LOG", "INPOK.LOG")) or
            locate_case_insensitive(output, "INPFAIL.LOG") is not None):
        raise ValueError("Cannot link launcher: BIOS keyboard and mouse services "
                         "are unverified. Run msc51-compile-input first.")
    signature = locate_case_insensitive(output, "INPSIG.LOG")
    if signature is None or signature.read_text().strip() != input_source_signature(output):
        raise ValueError("Cannot link launcher: input adapter source has changed "
                         "since its test. Run msc51-compile-input again.")


def remove_input_outputs(output: Path) -> None:
    names = {"INPSTART.LOG", "INPOK.LOG", "INPFAIL.LOG", "INPBUILD.LOG",
             "INPSIG.LOG", "INPTEST.LOG", "INP.OBJ", "INPCHK.EXE", "INPCHK.OBJ"}
    if output.is_dir():
        for entry in output.iterdir():
            if entry.is_file() and entry.name.upper() in names:
                entry.unlink()


def print_input_diagnostics(output: Path) -> None:
    for name in ("INPFAIL.LOG", "INPBUILD.LOG", "INPTEST.LOG", "INPOK.LOG"):
        path = locate_case_insensitive(output, name)
        content = path.read_text(errors="replace") if path else "(not created)"
        print(f"--- {name} ---\n{content[-8_000:].rstrip()}", file=sys.stderr)
    print(f"BIOS input workspace: {output}", file=sys.stderr)


def validate_clock_adapter(output: Path) -> None:
    if (any(locate_case_insensitive(output, name) is None
            for name in ("CLOCK.OBJ", "TIMER.OBJ", "TMRCHK.EXE",
                         "TMRTEST.LOG", "CLKSTART.LOG", "CLKOK.LOG")) or
            locate_case_insensitive(output, "CLKFAIL.LOG") is not None):
        raise ValueError("Cannot link launcher: DOS clock services are unverified. "
                         "Run msc51-compile-clock first.")
    signature = locate_case_insensitive(output, "CLKSIG.LOG")
    if signature is None or signature.read_text().strip() != clock_source_signature(output):
        raise ValueError("Cannot link launcher: clock adapter source has changed "
                         "since its test. Run msc51-compile-clock again.")
    # CLKSIG.LOG is written by CLOCK.BAT, not by TMRCHK.EXE. A stale probe
    # binary can therefore exit successfully and let the batch mark a new
    # source revision as verified. Check the guest's actual output contract:
    # bundle 054 adds DOS date/time restoration after the PIT/vector reset.
    timer_log = locate_case_insensitive(output, "TMRTEST.LOG")
    timer_text = timer_log.read_text(errors="replace") if timer_log else ""
    if (re.search(r"^INT 1Ch probe: PASS ticks=[0-9]+, prior vector restored\s*$",
                  timer_text, re.MULTILINE) is None or
            re.search(r"^PIT 60Hz probe: PASS ticks=[0-9]+ "
                      r"loops_per_tick=[0-9]+, prior vector restored, "
                      r"PIT reset to FFFFh, DOS date/time restored\s*$",
                      timer_text, re.MULTILINE) is None):
        raise ValueError("Cannot link launcher: TMRTEST.LOG does not confirm "
                         "the current PIT, interrupt-vector and DOS date/time "
                         "restoration probe. Run msc51-compile-clock from "
                         "the current bundle and inspect CLKBUILD.LOG.")


def remove_clock_outputs(output: Path) -> None:
    names = {"CLKSTART.LOG", "CLKOK.LOG", "CLKFAIL.LOG", "CLKBUILD.LOG",
             "CLKSIG.LOG", "CLKTEST.LOG", "TMRTEST.LOG", "CLOCK.OBJ",
             "CLKCHK.EXE", "CLKCHK.OBJ", "TIMER.OBJ", "TMRCHK.EXE", "TMRCHK.OBJ"}
    if output.is_dir():
        for entry in output.iterdir():
            if entry.is_file() and entry.name.upper() in names:
                entry.unlink()


def print_clock_diagnostics(output: Path) -> None:
    for name in ("CLKFAIL.LOG", "CLKBUILD.LOG", "CLKTEST.LOG",
                 "TMRTEST.LOG", "CLKOK.LOG"):
        path = locate_case_insensitive(output, name)
        content = path.read_text(errors="replace") if path else "(not created)"
        print(f"--- {name} ---\n{content[-8_000:].rstrip()}", file=sys.stderr)
    print(f"DOS clock workspace: {output}", file=sys.stderr)


def validate_crt_adapter(output: Path) -> None:
    if (any(locate_case_insensitive(output, name) is None
            for name in ("CRTENV.OBJ", "CRTSTART.LOG", "CRTOK.LOG")) or
            locate_case_insensitive(output, "CRTFAIL.LOG") is not None):
        raise ValueError("Cannot link launcher: real Microsoft C environment and atol "
                         "services are unverified. Run msc51-compile-crt first.")
    signature = locate_case_insensitive(output, "CRTSIG.LOG")
    if signature is None or signature.read_text().strip() != crt_source_signature(output):
        raise ValueError("Cannot link launcher: CRT adapter source has changed "
                         "since its test. Run msc51-compile-crt again.")


def remove_crt_outputs(output: Path) -> None:
    names = {"CRTSTART.LOG", "CRTOK.LOG", "CRTFAIL.LOG", "CRTBUILD.LOG",
             "CRTSIG.LOG", "CRTTEST.LOG", "CRTENV.OBJ", "CRTCHK.EXE", "CRTCHK.OBJ"}
    if output.is_dir():
        for entry in output.iterdir():
            if entry.is_file() and entry.name.upper() in names:
                entry.unlink()


def print_crt_diagnostics(output: Path) -> None:
    for name in ("CRTFAIL.LOG", "CRTBUILD.LOG", "CRTTEST.LOG", "CRTOK.LOG"):
        path = locate_case_insensitive(output, name)
        content = path.read_text(errors="replace") if path else "(not created)"
        print(f"--- {name} ---\n{content[-8_000:].rstrip()}", file=sys.stderr)
    print(f"DOS CRT workspace: {output}", file=sys.stderr)


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
                    batch_file: str = "BUILD.BAT",
                    original_game_dir: Path | None = None) -> list[str]:
    for path in (compiler, output, *([original_game_dir] if original_game_dir else [])):
        if any(ch in str(path) for ch in ('"', '\n', '\r')):
            raise ValueError(f"Path cannot be mounted safely in DOSBox: {path}")
    commands = [f'mount c "{compiler}" -ro', f'mount d "{output}"', "d:"]
    if original_game_dir is not None:
        commands.insert(2, f'mount e "{original_game_dir}" -ro')
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
                                            "compile-memory", "compile-selectors", "compile-data",
                                            "compile-dos-files",
                                            "compile-rtl-state", "compile-hardware",
                                            "compile-runtime-files",
                                            "compile-picture-dcl",
                                            "compile-resident-state",
                                            "compile-heap",
                                            "compile-input",
                                            "compile-clock",
                                            "compile-crt",
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
    parser.add_argument("--original-game-dir", type=Path,
                        help="compile-data only: verify pinned GATE.EXE mounted read-only as drive E")
    args = parser.parse_args(argv)
    if args.original_game_dir is not None and args.command != "compile-data":
        parser.error("--original-game-dir is supported only by compile-data")
    launcher = args.command in ("prepare-launcher", "compile-launcher",
                                "compile-memory", "compile-selectors", "compile-data",
                                "compile-dos-files",
                                "compile-rtl-state", "compile-hardware",
                                "compile-runtime-files", "compile-picture-dcl",
                                "compile-resident-state",
                                "compile-heap",
                                "compile-input",
                                "compile-clock",
                                "compile-crt",
                                "link-launcher")
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
        original_game_dir = None
        if args.original_game_dir is not None:
            original_game_dir = args.original_game_dir.expanduser().resolve()
            original_exe = locate_case_insensitive(original_game_dir, "GATE.EXE")
            if original_exe is None or not original_exe.is_file():
                raise ValueError(f"GATE.EXE not found in {original_game_dir}")
            expected = "767ccbbacbd6b304a5bab072894a45ca426a2196ae369b2f11d02ef92bb7134b"
            if hashlib.sha256(original_exe.read_bytes()).hexdigest() != expected:
                raise ValueError("GATE.EXE does not match the pinned original SHA-256")
        if args.command == "install":
            if args.install_dir is None:
                parser.error("--install-dir is required for the install command")
            destination = args.install_dir.expanduser().resolve()
            commands = install_commands(destination, [p.expanduser().resolve() for p in args.floppy])
            destination.mkdir(parents=True, exist_ok=True)
        else:
            if args.command == "link-launcher":
                validate_link_prerequisites(output, objects)
            if args.command in ("compile-selectors", "compile-data", "compile-dos-files", "compile-rtl-state",
                                "compile-hardware", "compile-runtime-files",
                                "compile-picture-dcl",
                                "compile-resident-state",
                                "compile-heap",
                                "compile-input",
                                "compile-clock",
                                "compile-crt"):
                validate_memory_adapter(output)
            if args.command == "compile-picture-dcl":
                validate_dos_file_adapter(output)
                validate_rtl_state_adapter(output)
            if args.command == "compile-data":
                validate_selector_probe(output)
                validate_dos_file_adapter(output)
            if not args.compiler:
                parser.error("set GATEWAY_MSC51_DIR or pass --compiler /path/to/installed/MSC51")
            compiler = validate_compiler(Path(args.compiler))
            commands = dosbox_commands(
                compiler, output,
                args.command in ("probe", "compile-launcher", "compile-memory",
                                 "compile-selectors", "compile-data",
                                 "compile-dos-files", "compile-rtl-state",
                                 "compile-hardware", "compile-runtime-files",
                                 "compile-picture-dcl",
                                 "compile-resident-state",
                                 "compile-heap",
                                 "compile-input",
                                 "compile-clock",
                                 "compile-crt",
                                 "link-launcher"),
                ("LINK.BAT" if args.command == "link-launcher" else
                 "SELECT.BAT" if args.command == "compile-selectors" else
                 "DATA.BAT" if args.command == "compile-data" else
                 "DOSFILE.BAT" if args.command == "compile-dos-files" else
                 "RTLST.BAT" if args.command == "compile-rtl-state" else
                 "RTLHW.BAT" if args.command == "compile-hardware" else
                 "RTFILE.BAT" if args.command == "compile-runtime-files" else
                 "PICTDCL.BAT" if args.command == "compile-picture-dcl" else
                 "RESST.BAT" if args.command == "compile-resident-state" else
                 "HEAP.BAT" if args.command == "compile-heap" else
                 "INPUT.BAT" if args.command == "compile-input" else
                 "CLOCK.BAT" if args.command == "compile-clock" else
                 "CRT.BAT" if args.command == "compile-crt" else
                 "MEMORY.BAT" if args.command == "compile-memory" else
                 "COMPILE.BAT" if launcher else "BUILD.BAT"),
                original_game_dir=original_game_dir)
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
    elif args.command == "compile-selectors":
        remove_selector_outputs(output)
        command.append("--exit")
    elif args.command == "compile-data":
        remove_static_data_outputs(output)
        command.append("--exit")
    elif args.command == "compile-dos-files":
        remove_dos_file_outputs(output)
        remove_launcher_link_outputs(output)
        command.append("--exit")
    elif args.command == "compile-rtl-state":
        remove_rtl_state_outputs(output)
        remove_launcher_link_outputs(output)
        command.append("--exit")
    elif args.command == "compile-hardware":
        remove_hardware_outputs(output)
        remove_launcher_link_outputs(output)
        command.append("--exit")
    elif args.command == "compile-runtime-files":
        remove_runtime_file_outputs(output)
        remove_launcher_link_outputs(output)
        command.append("--exit")
    elif args.command == "compile-picture-dcl":
        remove_picture_dcl_outputs(output)
        remove_launcher_link_outputs(output)
        command.append("--exit")
    elif args.command == "compile-resident-state":
        remove_resident_state_outputs(output)
        remove_launcher_link_outputs(output)
        command.append("--exit")
    elif args.command == "compile-heap":
        remove_heap_outputs(output)
        remove_launcher_link_outputs(output)
        command.append("--exit")
    elif args.command == "compile-input":
        remove_input_outputs(output)
        remove_launcher_link_outputs(output)
        command.append("--exit")
    elif args.command == "compile-clock":
        remove_clock_outputs(output)
        remove_launcher_link_outputs(output)
        command.append("--exit")
    elif args.command == "compile-crt":
        remove_crt_outputs(output)
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
        elif args.command == "compile-selectors":
            print_selector_diagnostics(output)
        elif args.command == "compile-data":
            print_static_data_diagnostics(output)
        elif args.command == "compile-dos-files":
            print_dos_file_diagnostics(output)
        elif args.command == "compile-rtl-state":
            print_rtl_state_diagnostics(output)
        elif args.command == "compile-hardware":
            print_hardware_diagnostics(output)
        elif args.command == "compile-runtime-files":
            print_runtime_file_diagnostics(output)
        elif args.command == "compile-picture-dcl":
            print_picture_dcl_diagnostics(output)
        elif args.command == "compile-resident-state":
            print_resident_state_diagnostics(output)
        elif args.command == "compile-heap":
            print_heap_diagnostics(output)
        elif args.command == "compile-input":
            print_input_diagnostics(output)
        elif args.command == "compile-clock":
            print_clock_diagnostics(output)
        elif args.command == "compile-crt":
            print_crt_diagnostics(output)
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
    if args.command == "compile-selectors":
        try:
            validate_selector_probe(output)
        except ValueError as error:
            print(f"Microsoft C did not verify startup selector aliases: {error}",
                  file=sys.stderr)
            print_selector_diagnostics(output)
            return 1
        print("Microsoft C 5.1 verified 45 original startup selector bindings "
              "over two overlapping 64KiB windows; SEL.OBJ remains probe-only "
              "until data loading and relocation are reconstructed.")
        return 0
    if args.command == "compile-data":
        try:
            validate_static_data_probe(output, require_original=original_game_dir is not None)
        except ValueError as error:
            print(f"Microsoft C did not verify startup static data copy: {error}",
                  file=sys.stderr)
            print_static_data_diagnostics(output)
            return 1
        print("Microsoft C 5.1 verified a 5976-byte generated fixture can be copied "
              "from original EXE offsets to runtime DS:3592"
              + (" and confirmed the same copy against read-only GATE.EXE"
                 if original_game_dir is not None else "")
              + "; DATA.OBJ remains probe-only pending the rest of startup and RTLink fixups.")
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
    if args.command == "compile-rtl-state":
        try:
            validate_rtl_state_adapter(output)
        except ValueError:
            print("Microsoft C did not compile and verify the original RTL state accessors.",
                  file=sys.stderr)
            print_rtl_state_diagnostics(output)
            return 1
        print("Microsoft C 5.1 verified the original linked RTL state and mode selectors; "
              "all four RTL accessor objects are ready for the launcher link.")
        return 0
    if args.command == "compile-hardware":
        try:
            validate_hardware_adapter(output)
        except ValueError:
            print("Microsoft C did not compile and verify the real-mode RTL hardware adapter.",
                  file=sys.stderr)
            print_hardware_diagnostics(output)
            return 1
        print("Microsoft C 5.1 verified the RTL video clipping state and port-read smoke test; "
              "RTLHW.OBJ is ready for the launcher link.")
        return 0
    if args.command == "compile-runtime-files":
        try:
            validate_runtime_file_adapter(output)
        except ValueError:
            print("Microsoft C did not compile and verify runtime FAR string/file services.",
                  file=sys.stderr)
            print_runtime_file_diagnostics(output)
            return 1
        print("Microsoft C 5.1 verified FAR strncpy, resource formatting and DOS path access; "
              "RTFILE.OBJ is ready for the launcher link.")
        return 0
    if args.command == "compile-picture-dcl":
        try:
            validate_picture_dcl_adapter(output)
        except ValueError:
            print("Microsoft C did not compile and verify the streaming picture decoder.",
                  file=sys.stderr)
            print_picture_dcl_diagnostics(output)
            return 1
        print("Microsoft C 5.1 verified DCL decoding and mode-12h/13h segmented pixels; "
              "PICTDCL.OBJ is ready for the launcher link.")
        return 0
    if args.command == "compile-resident-state":
        try:
            validate_resident_state_adapter(output)
        except ValueError:
            print("Microsoft C did not compile and verify resident mode/drawing state.",
                  file=sys.stderr)
            print_resident_state_diagnostics(output)
            return 1
        print("Microsoft C 5.1 verified resident drawing words and BIOS mode selection; "
              "RESSTATE.OBJ is ready for the launcher link.")
        return 0
    if args.command == "compile-heap":
        try:
            validate_heap_adapter(output)
        except ValueError:
            print("Microsoft C did not compile and verify DOS heap operations.",
                  file=sys.stderr)
            print_heap_diagnostics(output)
            return 1
        print("Microsoft C 5.1 verified DOS heap allocations including >64KiB VFX buffers; "
              "HEAP.OBJ is ready for the launcher link.")
        return 0
    if args.command == "compile-input":
        try:
            validate_input_adapter(output)
        except ValueError:
            print("Microsoft C did not compile and verify BIOS keyboard/mouse input.",
                  file=sys.stderr)
            print_input_diagnostics(output)
            return 1
        print("Microsoft C 5.1 verified the BIOS keyboard buffer and DS unget slot; "
              "INP.OBJ is ready for the launcher link.")
        return 0
    if args.command == "compile-clock":
        try:
            validate_clock_adapter(output)
        except ValueError:
            print("Microsoft C did not verify DOS clock/wait and INT 1Ch services.",
                  file=sys.stderr)
            print_clock_diagnostics(output)
            return 1
        print("Microsoft C 5.1 verified DOS clock/waits and the standalone "
              "INT 1Ch callback probe; CLOCK.OBJ is ready for the launcher link. "
              "TIMER.OBJ is still probe-only.")
        return 0
    if args.command == "compile-crt":
        try:
            validate_crt_adapter(output)
        except ValueError:
            print("Microsoft C did not compile and verify the getenv/atol adapter.",
                  file=sys.stderr)
            print_crt_diagnostics(output)
            return 1
        print("Microsoft C 5.1 verified FAR getenv and atol through its large-model CRT; "
              "CRTENV.OBJ is ready for the launcher link.")
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
