#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Assemble Microsoft C 5.1 DOS compiler files from extracted floppy images.

The input is a directory of original floppy images or an already extracted
file tree. Disk images are read with mtools, without executing DOS programs.
Only tool, header, and library files are copied into BIN/LIB/INCLUDE.
"""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from msc51 import executable_format, validate_compiler


IMAGE_SUFFIXES = frozenset((".img", ".ima", ".dsk", ".vfd"))
BIN_SUFFIXES = frozenset((".exe", ".com", ".err", ".hlp"))
LIB_SUFFIXES = frozenset((".lib", ".obj"))
HEADER_SUFFIXES = frozenset((".h", ".inc"))


def disk_images(source: Path) -> list[Path]:
    return sorted(
        (entry for entry in source.rglob("*")
         if entry.is_file() and entry.suffix.lower() in IMAGE_SUFFIXES),
        key=lambda file: str(file).lower(),
    )


def extract_images(images: list[Path], target: Path, mcopy: str = "mcopy") -> None:
    if not images:
        raise ValueError("no DOS floppy images found")
    for number, image in enumerate(images):
        volume = target / f"disk-{number:02d}"
        volume.mkdir(parents=True)
        result = subprocess.run(
            [mcopy, "-s", "-i", str(image), "::", str(volume)],
            capture_output=True, text=True, check=False,
        )
        if result.returncode:
            raise ValueError(
                f"Cannot extract {image} as a DOS FAT image with mtools: "
                f"{result.stderr.strip()}"
            )


def destination_for(file: Path, source: Path) -> Path | None:
    """Choose a DOS installation path for a file on an original disk."""
    parts = file.relative_to(source).parts
    upper = [part.upper() for part in parts]
    suffix = file.suffix.lower()
    if suffix in HEADER_SUFFIXES:
        if "INCLUDE" in upper[:-1]:
            # The unpack directory itself can also be called "include".
            index = max(i for i, part in enumerate(upper[:-1]) if part == "INCLUDE")
            return Path("INCLUDE", *upper[index + 1:])
        if len(parts) == 1:
            return Path("INCLUDE", upper[-1])
        # STARTUP/SOURCE headers are not the installed public include tree.
        return None
    if suffix in LIB_SUFFIXES:
        return Path("LIB", upper[-1])
    if suffix in BIN_SUFFIXES:
        return Path("BIN", upper[-1])
    return None


def assemble_files(source: Path, destination: Path, strict_duplicates: bool = False,
                   source_images: list[str] | None = None) -> dict[str, int]:
    counts = {"BIN": 0, "LIB": 0, "INCLUDE": 0}
    selected: dict[str, dict[str, str]] = {}
    conflicts: list[dict[str, object]] = []
    for file in sorted(source.rglob("*")):
        if not file.is_file():
            continue
        relative = destination_for(file, source)
        if relative is None:
            continue
        key = relative.as_posix()
        data = file.read_bytes()
        candidate = {
            "disk_file": file.relative_to(source).as_posix(),
            "sha256": hashlib.sha256(data).hexdigest(),
        }
        output = destination / relative
        output.parent.mkdir(parents=True, exist_ok=True)
        if output.exists():
            if key not in selected:
                raise ValueError(f"Pre-existing compiler output must be cleaned first: {output}")
            installed = output.read_bytes()
            if installed != data:
                if strict_duplicates:
                    raise ValueError(f"Conflicting floppy contents for {relative}: {file}")
                # The 5.1 disks contain both real-mode DOS and protected-mode
                # OS/2 compiler passes under the same names. An NE/PE program
                # starts with an MZ DOS *stub*, which may just print
                # 'This program cannot be run in DOS mode' under DOSBox.
                # Prefer a genuine real-mode DOS MZ executable; for equal or
                # unknown formats, the original first-copy rule still applies.
                installed_format = executable_format(installed)
                candidate_format = executable_format(data)
                use_candidate = (relative.suffix.upper() == ".EXE"
                                 and candidate_format == "DOS MZ"
                                 and installed_format != "DOS MZ")
                if use_candidate:
                    conflicts.append({"path": key, "selected": candidate, "ignored": selected[key]})
                    print(f"msc51: selecting {candidate['disk_file']} ({candidate_format}) over "
                          f"{selected[key]['disk_file']} ({installed_format}) for {key}", flush=True)
                    output.write_bytes(data)
                    selected[key] = candidate
                else:
                    conflicts.append({"path": key, "selected": selected[key], "ignored": candidate})
                    print(f"msc51: keeping {selected[key]['disk_file']} ({installed_format}) over "
                          f"{candidate['disk_file']} ({candidate_format}) for {key}", flush=True)
            continue
        shutil.copyfile(file, output)
        selected[key] = candidate
        counts[relative.parts[0]] += 1
    destination.mkdir(parents=True, exist_ok=True)
    (destination / "compiler-manifest.json").write_text(json.dumps({
        "selection_policy": "prefer real-mode DOS MZ over protected-mode EXE, otherwise first sorted source",
        "source_images": source_images or [],
        "files": selected,
        "conflicts": conflicts,
    }, indent=2, sort_keys=True) + "\n")
    return counts


def unpack(source: Path, destination: Path, mcopy: str = "mcopy",
           strict_duplicates: bool = False) -> dict[str, int]:
    source = source.resolve()
    destination = destination.resolve()
    if not source.is_dir():
        raise ValueError(f"No compiler disk directory: {source}")
    if source == destination or source in destination.parents or destination in source.parents:
        raise ValueError("Output must be outside the input directory")
    if destination.is_dir() and any(destination.iterdir()):
        raise ValueError(f"Output directory is not empty: {destination}")
    images = disk_images(source)
    if images:
        with tempfile.TemporaryDirectory(prefix="gateway-msc51-") as temp:
            raw = Path(temp)
            extract_images(images, raw, mcopy=mcopy)
            counts = assemble_files(
                raw, destination, strict_duplicates=strict_duplicates,
                source_images=[image.relative_to(source).as_posix() for image in images])
    else:
        counts = assemble_files(source, destination, strict_duplicates=strict_duplicates)
    validate_compiler(destination)
    return counts


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="directory of raw floppy images or DOS files")
    parser.add_argument("output", type=Path, help="assembled compiler installation directory")
    parser.add_argument("--mcopy", default="mcopy", help="mtools mcopy executable")
    parser.add_argument("--strict-duplicates", action="store_true",
                        help="fail on different floppy files with the same installed DOS name")
    args = parser.parse_args(argv)
    try:
        counts = unpack(args.source, args.output, mcopy=args.mcopy,
                        strict_duplicates=args.strict_duplicates)
    except (ValueError, OSError) as error:
        parser.error(str(error))
    print("Microsoft C 5.1 ready in " + str(args.output))
    print(", ".join(f"{key}: {value} files" for key, value in counts.items()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
