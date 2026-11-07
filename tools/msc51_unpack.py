#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Assemble Microsoft C 5.1 DOS compiler files from extracted floppy images.

The input is a directory of original floppy images or an already extracted
file tree. Disk images are read with mtools, without executing DOS programs.
Only tool, header, and library files are copied into BIN/LIB/INCLUDE.
"""

import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from msc51 import validate_compiler


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


def assemble_files(source: Path, destination: Path) -> dict[str, int]:
    counts = {"BIN": 0, "LIB": 0, "INCLUDE": 0}
    for file in sorted(source.rglob("*")):
        if not file.is_file():
            continue
        relative = destination_for(file, source)
        if relative is None:
            continue
        output = destination / relative
        output.parent.mkdir(parents=True, exist_ok=True)
        if output.exists():
            if output.read_bytes() != file.read_bytes():
                raise ValueError(f"Conflicting floppy contents for {relative}: {file}")
            continue
        shutil.copyfile(file, output)
        counts[relative.parts[0]] += 1
    return counts


def unpack(source: Path, destination: Path, mcopy: str = "mcopy") -> dict[str, int]:
    source = source.resolve()
    destination = destination.resolve()
    if not source.is_dir():
        raise ValueError(f"No compiler disk directory: {source}")
    if source == destination or source in destination.parents or destination in source.parents:
        raise ValueError("Output must be outside the input directory")
    images = disk_images(source)
    if images:
        with tempfile.TemporaryDirectory(prefix="gateway-msc51-") as temp:
            raw = Path(temp)
            extract_images(images, raw, mcopy=mcopy)
            counts = assemble_files(raw, destination)
    else:
        counts = assemble_files(source, destination)
    validate_compiler(destination)
    return counts


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="directory of raw floppy images or DOS files")
    parser.add_argument("output", type=Path, help="assembled compiler installation directory")
    parser.add_argument("--mcopy", default="mcopy", help="mtools mcopy executable")
    args = parser.parse_args(argv)
    try:
        counts = unpack(args.source, args.output, mcopy=args.mcopy)
    except (ValueError, OSError) as error:
        parser.error(str(error))
    print("Microsoft C 5.1 ready in " + str(args.output))
    print(", ".join(f"{key}: {value} files" for key, value in counts.items()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
