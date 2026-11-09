# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Synthetic relocation and tamper tests for the original EXE callback."""

import importlib.util
from pathlib import Path
import sys
import unittest


SPEC = importlib.util.spec_from_file_location(
    "inspect_msc51_exe_callback",
    Path(__file__).resolve().parents[1] / "tools/inspect_msc51_exe_callback.py")
callback = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = callback
SPEC.loader.exec_module(callback)


def synthetic_source():
    source = bytearray([0x90] * callback.PREFIX_SIZE)
    source[:3] = b"\x55\x8b\xec"
    for start in callback.FAR_CALLS:
        source[start] = 0x9A
        source[start + 1:start + 3] = (0x1234).to_bytes(2, "little")
        source[start + 3:start + 5] = (0x0A46).to_bytes(2, "little")
    return source


class CallbackInspectionTests(unittest.TestCase):
    def test_all_far_segments_relocated(self):
        source = synthetic_source()
        live = bytearray(source)
        for start in callback.FAR_CALLS:
            live[start + 3:start + 5] = (0x1A46).to_bytes(2, "little")
        result = callback.compare_callback(bytes(source), bytes(live), 0x1000)
        self.assertTrue(result.settled)
        self.assertEqual(result.invariant_total, 115)

    def test_incomplete_loader_stage_is_not_settled(self):
        source = synthetic_source()
        live = bytearray(source)
        for start in callback.FAR_CALLS[:6]:
            live[start + 3:start + 5] = (0x1A46).to_bytes(2, "little")
        result = callback.compare_callback(bytes(source), bytes(live), 0x1000)
        self.assertFalse(result.settled)
        self.assertEqual((result.relocated_calls, result.original_calls), (6, 5))

    def test_modified_instruction_is_not_masked(self):
        source = synthetic_source()
        live = bytearray(source)
        for start in callback.FAR_CALLS:
            live[start + 3:start + 5] = (0x1A46).to_bytes(2, "little")
        live[0x44] ^= 0x01
        result = callback.compare_callback(bytes(source), bytes(live), 0x1000)
        self.assertEqual(result.invariant_equal, 114)
        self.assertFalse(result.settled)

    def test_missing_far_call_and_short_input_rejected(self):
        source = synthetic_source()
        source[callback.FAR_CALLS[0]] = 0x90
        with self.assertRaises(ValueError):
            callback.compare_callback(bytes(source), bytes(source), 0x1000)
        with self.assertRaises(ValueError):
            callback.compare_callback(bytes(source[:-1]), bytes(source), 0x1000)

    def test_source_requires_mz_header_and_opcode(self):
        image = bytearray(callback.SOURCE_OFFSET + callback.PREFIX_SIZE)
        image[:2] = b"MZ"
        image[callback.SOURCE_OFFSET:] = synthetic_source()
        self.assertEqual(callback.extract_source(image)[:3], b"\x55\x8b\xec")
        image[callback.SOURCE_OFFSET] ^= 1
        with self.assertRaises(ValueError):
            callback.extract_source(image)


if __name__ == "__main__":
    unittest.main()
