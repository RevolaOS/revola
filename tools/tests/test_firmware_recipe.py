# SPDX-License-Identifier: GPL-3.0-only
"""Recipe construction is independent of Apple bytes; Rust checks preimages."""

import struct
import unittest
from pathlib import Path

import declarations
import sites
from declarations import Copy, Kind, Write
from recipes import fingerprint, firmware


def firmware_header_dir():
    return Path(__file__).resolve().parents[2] / "firmware"


def word(kind, address, expected, symbol="", value=0):
    return Write(kind, address, struct.pack("<I", expected), value, symbol)


class FirmwareRecipeTests(unittest.TestCase):
    def setUp(self):
        self.load_offset = 0xB798
        self.original = bytearray(self.load_offset + 256)
        self.original[self.load_offset : self.load_offset + 8] = b"native!!"
        self.code = bytes(128)
        self.symbols = {
            "__osos_load_offset": self.load_offset,
            "__osos_iram_base": 0x22000000,
            "__core_start": 0x08B3C000,
            "__core_end": 0x08B3C080,
            "__core_limit": 0x08B3C100,
            "__core_file_offset": len(self.original),
            "hook": 0x08B3C000,
            "table": 0x08B3C020,
        }
        self.fingerprint = fingerprint(self.original)

    def build(self, patches):
        return firmware.build(self.code, patches, self.symbols, self.fingerprint)

    def replay(self, recipe):
        output = bytearray()
        for segment in recipe.segments:
            size = segment["bytes"]
            if segment["kind"] == "zero":
                output.extend(bytes(size))
            else:
                source = self.original if segment["kind"] == "input" else recipe.data
                offset = segment["offset"]
                output.extend(source[offset : offset + size])
        self.assertEqual(len(output), recipe.size)
        return output

    def test_hooks_header_lengths_and_deferred_preimages(self):
        patches = [
            word(Kind.POINTER, firmware.BASE, 0x12345678, "table"),
            word(Kind.CALL, firmware.BASE + 4, 0xE350006B, "hook"),
            Write(
                Kind.JUMP,
                firmware.BASE + 8,
                struct.pack("<II", 0xE2400064, 0xE3500015),
                symbol="hook",
            ),
        ]
        recipe = self.build(patches)
        image = self.replay(recipe)
        self.assertEqual(
            image[self.load_offset : self.load_offset + 16].hex(),
            "20c0b308fdef2ceb04f01fe500c0b308",
        )
        self.assertEqual(
            recipe.checks[0],
            {"name": "osos", "offset": self.load_offset, "hex": "78563412"},
        )
        for offset in (0xC, 0x10, 0x14):
            self.assertEqual(
                struct.unpack_from("<I", image, offset)[0], len(image) - 0x800
            )

    def test_site_preimages_map_dram_and_iram_to_file_offsets(self):
        table = [
            sites.Site(firmware.BASE + 8, 0, 0xEB000001, 0, sites.CALL, 1, 1),
            sites.Site(0x22000010, 0, 0xE92D4010, 0xE1A04000, sites.ENTRY, 1, 1),
        ]
        recipe = firmware.build(self.code, [], self.symbols, self.fingerprint, table)
        self.assertIn(
            {"name": "osos", "offset": self.load_offset + 8, "hex": "010000eb"},
            recipe.checks,
        )
        self.assertIn(
            {"name": "osos", "offset": 0x810, "hex": "10402de90040a0e1"},
            recipe.checks,
        )
        outside = [sites.Site(0x22000000 + self.load_offset, 0, 0, 0, sites.CALL, 0, 1)]
        with self.assertRaisesRegex(ValueError, "outside"):
            firmware.build(self.code, [], self.symbols, self.fingerprint, outside)

    def test_native_array_copies_preserve_adjacent_literals(self):
        self.code = bytes(40) + b"CFW!" + bytes(84)
        recipe = self.build([Copy("table", 0, 12, firmware.BASE, 8)])
        image = self.replay(recipe)
        self.assertEqual(
            image[len(self.original) + 32 : len(self.original) + 44], b"native!!CFW!"
        )
        self.assertNotIn(b"native!!", recipe.data)

    def test_dynamic_reservation_includes_bss_and_matches_heap_and_file_size(self):
        start = self.symbols["__core_start"]
        self.symbols["__core_end"] = start + 332140
        self.symbols["__core_limit"] = start + 328 * 1024
        self.code = b"CFW!" + bytes(124)
        image = self.replay(
            self.build([word(Kind.POINTER, firmware.BASE, 0, "__core_limit")])
        )
        self.assertEqual(len(image), len(self.original) + 328 * 1024)
        self.assertEqual(
            struct.unpack_from("<I", image, self.load_offset)[0], start + 328 * 1024
        )
        self.assertEqual(
            image[len(self.original) : len(self.original) + len(self.code)], self.code
        )
        self.assertEqual(
            image[len(self.original) + len(self.code) :],
            bytes(328 * 1024 - len(self.code)),
        )
        for offset in firmware.IMAGE_SIZE_FIELDS:
            self.assertEqual(
                struct.unpack_from("<I", image, offset)[0], len(image) - 0x800
            )

    def test_rejects_overlap_and_invalid_write_ranges(self):
        valid = word(Kind.WORD, firmware.BASE, 0, value=42)
        for patches in (
            [valid, valid],
            [word(Kind.WORD, firmware.BASE - 4, 0)],
            [word(Kind.WORD, firmware.BASE + 256, 0)],
            [word(Kind.WORD, firmware.BASE + 1, 0)],
        ):
            with self.subTest(patches=patches), self.assertRaises(ValueError):
                self.build(patches)

    def test_rejects_writes_inside_the_bitmap_bank(self):
        bank = (firmware.BASE + 64, firmware.BASE + 128)
        inside = word(Kind.WORD, firmware.BASE + 124, 0, value=1)
        outside = word(Kind.WORD, firmware.BASE + 128, 0, value=1)
        with self.assertRaises(ValueError):
            firmware.build(
                self.code, [inside], self.symbols, self.fingerprint, (), bank
            )
        firmware.build(self.code, [outside], self.symbols, self.fingerprint, (), bank)

    def test_bitmap_bank_comes_from_the_firmware_header(self):
        start, end = firmware.bitmap_bank(firmware_header_dir())
        self.assertLess(start, end)

    def test_rejects_missing_thumb_and_out_of_range_symbols(self):
        patch = word(Kind.CALL, firmware.BASE, 0, "hook")
        for destination in (0x08B3BFFC, 0x08B3C001, 0x08B3C080, 0x08B3C100):
            self.symbols["hook"] = destination
            with self.subTest(destination=destination), self.assertRaises(ValueError):
                self.build([patch])
        for name in ("hook", "__osos_load_offset"):
            value = self.symbols.pop(name)
            with (
                self.subTest(name=name),
                self.assertRaisesRegex(
                    ValueError, "Missing linked patch symbol: " + name
                ),
            ):
                self.build([patch])
            self.symbols[name] = value

    def test_rejects_invalid_and_overlapping_copies(self):
        valid = Copy("table", 0, 16, firmware.BASE, 8)
        cases = [
            [valid, valid],
            [Copy("table", 12, 16, firmware.BASE, 8)],
            [Copy("table", 0, 16, firmware.BASE - 1, 8)],
            [Copy("table", 0, 16, firmware.BASE + 252, 8)],
            [Copy("table", 96, 128, firmware.BASE, 8)],
        ]
        for patches in cases:
            with self.subTest(patches=patches), self.assertRaises(ValueError):
                self.build(patches)
        self.code = bytes(257)
        with self.assertRaisesRegex(ValueError, "reserved"):
            self.build([])

    def test_metadata_decodes_explicit_fields(self):
        raw = declarations.RECORD.pack(5, firmware.BASE, 4, 12, 8, b"table")
        self.assertEqual(
            declarations.parse(raw), [Copy("table", 4, 12, firmware.BASE, 8)]
        )
        for data in (raw[:-1], bytes(declarations.RECORD.size), raw[:20] + b"x" * 64):
            with self.subTest(data=data), self.assertRaises(ValueError):
                declarations.parse(data)
        for destination in (firmware.BASE + 1, firmware.BASE + (1 << 25) + 8):
            with self.assertRaises(ValueError):
                firmware.arm_call(firmware.BASE, destination)
