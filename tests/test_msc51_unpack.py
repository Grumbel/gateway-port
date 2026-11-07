#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Tests for assembling a DOS toolchain from historical disk images."""

import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
import msc51_unpack as unpacker


FILES = {
    "setup/CL.EXE": b"cl",
    "setup/C1.EXE": b"c1",
    "setup/C1L.EXE": b"c1l",
    "compiler/C2.EXE": b"c2",
    "compiler/C3.EXE": b"c3",
    "utility/LINK.EXE": b"link",
    "libraries/LLIBCR.LIB": b"library",
    "include/INCLUDE/STDIO.H": b"header",
    "include/INCLUDE/SYS/STAT.H": b"nested header",
    "setup/PACKING.LST": b"ignored",
}


def make_tree(root: Path) -> None:
    for relative, data in FILES.items():
        file = root / relative
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_bytes(data)


class MSC51UnpackTests(unittest.TestCase):
    def test_offline_files_are_arranged_as_dos_compiler(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            source = root / "images"
            source.mkdir()
            make_tree(source)
            counts = unpacker.unpack(source, root / "output")
            self.assertEqual(counts, {"BIN": 6, "LIB": 1, "INCLUDE": 2})
            self.assertEqual((root / "output/BIN/CL.EXE").read_bytes(), b"cl")
            self.assertEqual((root / "output/LIB/LLIBCR.LIB").read_bytes(), b"library")
            self.assertEqual((root / "output/INCLUDE/SYS/STAT.H").read_bytes(), b"nested header")
            self.assertFalse((root / "output/PACKING.LST").exists())

    def test_different_compiler_passes_keep_first_and_record_alternates(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            make_tree(root / "disks")
            (root / "disks/setup/C1L.EXE").unlink()
            first = root / "disks/compiler/C1L.EXE"
            first.write_bytes(b"first C1L")
            duplicate = root / "disks/disk-06/C1L.EXE"
            duplicate.parent.mkdir(parents=True)
            duplicate.write_bytes(b"second C1L")
            unpacker.assemble_files(root / "disks", root / "output")
            self.assertEqual((root / "output/BIN/C1L.EXE").read_bytes(), b"first C1L")
            manifest = json.loads((root / "output/compiler-manifest.json").read_text())
            self.assertEqual(manifest["selection_policy"],
                             "prefer real-mode DOS MZ over protected-mode EXE, otherwise first sorted source")
            self.assertEqual(manifest["conflicts"], [{
                "path": "BIN/C1L.EXE",
                "selected": {
                    "disk_file": "compiler/C1L.EXE",
                    "sha256": hashlib.sha256(b"first C1L").hexdigest(),
                },
                "ignored": {
                    "disk_file": "disk-06/C1L.EXE",
                    "sha256": hashlib.sha256(b"second C1L").hexdigest(),
                },
            }])
            with self.assertRaisesRegex(ValueError, "Conflicting floppy contents for BIN/C1L.EXE"):
                # Strict mode remains available to detect unexpectedly mixed
                # compiler disk sets rather than silently accepting variants.
                unpacker.assemble_files(root / "disks", root / "strict-output",
                                        strict_duplicates=True)

    def test_existing_output_is_not_merged_with_old_compiler(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            make_tree(root / "source")
            (root / "output").mkdir()
            (root / "output/stale").touch()
            with self.assertRaisesRegex(ValueError, "Output directory is not empty"):
                unpacker.unpack(root / "source", root / "output")

    def test_real_mode_compiler_variant_replaces_protected_mode_one(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            source = root / "disks"
            (source / "disk-00").mkdir(parents=True)
            (source / "disk-01").mkdir()
            protected = bytearray(b"MZ" + bytes(126))
            protected[0x3c:0x40] = (0x40).to_bytes(4, "little")
            protected[0x40:0x42] = b"NE"
            (source / "disk-00/CL.EXE").write_bytes(protected)
            dos = b"MZ" + bytes(126)
            (source / "disk-01/CL.EXE").write_bytes(dos)
            # After finding the real-mode DOS tool, a later PE alternative
            # must not replace it merely because it is the last copy.
            (source / "disk-02").mkdir()
            win32 = bytearray(protected)
            win32[0x40:0x44] = b"PE\0\0"
            (source / "disk-02/CL.EXE").write_bytes(win32)

            unpacker.assemble_files(source, root / "out")
            self.assertEqual((root / "out/BIN/CL.EXE").read_bytes(), dos)
            manifest = json.loads((root / "out/compiler-manifest.json").read_text())
            self.assertEqual(manifest["files"]["BIN/CL.EXE"]["disk_file"], "disk-01/CL.EXE")
            self.assertEqual(len(manifest["conflicts"]), 2)
            self.assertEqual(manifest["conflicts"][0]["ignored"]["disk_file"],
                             "disk-00/CL.EXE")
            self.assertEqual(manifest["conflicts"][1]["ignored"]["disk_file"],
                             "disk-02/CL.EXE")

    def test_output_must_not_overlap_source(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            make_tree(root / "source")
            with self.assertRaisesRegex(ValueError, "outside the input"):
                unpacker.unpack(root / "source", root / "source/output")

    def test_image_extractor_passes_raw_images_to_mtools(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            one = root / "01.IMG"
            two = root / "02.ima"
            one.touch()
            two.touch()
            self.assertEqual(unpacker.disk_images(root), [one, two])
            with mock.patch.object(unpacker.subprocess, "run") as run:
                run.return_value.returncode = 0
                unpacker.extract_images([one, two], root / "extracted")
            self.assertEqual(run.call_count, 2)
            self.assertEqual(run.call_args_list[0].args[0],
                             ["mcopy", "-s", "-i", str(one), "::", str(root / "extracted/disk-00")])

    def test_manifest_maps_extracted_volumes_to_original_images(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            source = root / "images"
            source.mkdir()
            (source / "Disk1.IMG").touch()

            def fake_extract(images, target, mcopy="mcopy"):
                self.assertEqual([image.name for image in images], ["Disk1.IMG"])
                make_tree(target / "disk-00")

            with mock.patch.object(unpacker, "extract_images", side_effect=fake_extract):
                unpacker.unpack(source, root / "output")
            manifest = json.loads((root / "output/compiler-manifest.json").read_text())
            self.assertEqual(manifest["source_images"], ["Disk1.IMG"])
            self.assertEqual(manifest["files"]["BIN/CL.EXE"]["disk_file"],
                             "disk-00/setup/CL.EXE")

    @unittest.skipUnless(shutil.which("mformat") and shutil.which("mcopy"),
                         "mtools unavailable; the Nix tooling check runs this integration test")
    def test_real_fat_image_round_trip(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            source = root / "images"
            source.mkdir()
            image = source / "compiler.img"
            subprocess.run(["mformat", "-C", "-f", "1440", "-i", str(image), "::"], check=True)
            for relative, data in FILES.items():
                name = Path(relative).name
                file = root / name
                file.write_bytes(data)
                if name == "STDIO.H":
                    subprocess.run(["mmd", "-i", str(image), "::INCLUDE"], check=True)
                    dest = "::INCLUDE/STDIO.H"
                elif name == "STAT.H":
                    continue
                else:
                    dest = f"::{name}"
                subprocess.run(["mcopy", "-i", str(image), str(file), dest], check=True)
            result = unpacker.unpack(source, root / "result")
            self.assertGreater(result["BIN"], 0)
            self.assertEqual((root / "result/LIB/LLIBCR.LIB").read_bytes(), b"library")
            self.assertEqual((root / "result/INCLUDE/STDIO.H").read_bytes(), b"header")


if __name__ == "__main__":
    unittest.main()
