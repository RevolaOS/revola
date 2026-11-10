# SPDX-License-Identifier: GPL-3.0-only
"""The rvx layout, the lz codec and the checks rvx-pack makes before writing one."""

import random
import struct
import unittest
from pathlib import Path

import manifest
import rvx

SYMBOLS = {
    "__data_start": 0x40,
    "__data_end": 0x50,
    "__bss_end": 0x58,
    "rvx_entry": 0x11,
}
DOCUMENT = {
    "id": "eq",
    "name": "EQ",
    "description": "Bands",
    "version": "0.1.0",
    "author": "Paul",
    "license": "GPL-3.0-only",
    "module": [
        {"name": "main", "trigger": "always"},
        {"name": "dsp", "trigger": "manual"},
    ],
}
THEMES = Path(__file__).resolve().parents[4] / "iPod/osos-themes"


def asset(name, data, resident=False, compress=True, kind=rvx.KIND_DATA):
    return {
        "name": name,
        "kind": kind,
        "data": data,
        "resident": resident,
        "compress": compress,
    }


def two_modules():
    main = rvx.module_image(SYMBOLS, bytes(range(0x48)), [0x44, 0x4C])
    dsp = rvx.module_image({**SYMBOLS, "__bss_end": 0x50}, bytes(0x50), [])
    return [("main", main), ("dsp", dsp)]


class RvxTests(unittest.TestCase):
    def test_header_manifest_catalog_then_payloads_in_load_order(self):
        compiled = manifest.compile_manifest(DOCUMENT)
        assets = [
            asset("table", bytes(range(7))),
            asset("icon", b"\x11" * 64, resident=True, kind=rvx.KIND_BITMAP),
            asset("song", bytes(range(256)), compress=False),
        ]
        packed, _ = rvx.pack_extension(
            compiled, two_modules(), assets, 0x10002, 0x660EB6FF
        )
        header, entries = rvx.unpack(packed)
        self.assertEqual(
            header,
            {
                "magic": rvx.MAGIC,
                "format": 1,
                "api": 0x10002,
                "firmware": 0x660EB6FF,
                "manifest": len(compiled),
                "entries": 5,
                "resident": 1,
            },
        )
        self.assertEqual(packed[28 : 28 + len(compiled)], compiled)
        catalog = 28 + ((len(compiled) + 3) & ~3)
        first = catalog + 5 * 20
        self.assertEqual(
            [e["name"] for e in entries],
            [rvx.fnv1a(n) for n in ("main", "dsp", "icon", "table", "song")],
        )
        self.assertEqual(
            [e["resident"] for e in entries], [False, False, True, False, False]
        )
        main, dsp, icon, table, song = entries
        # main, then the resident icon, then dsp, then the on-disk assets
        self.assertEqual(main["offset"], first)
        self.assertEqual(icon["offset"], first + main["stored"])
        self.assertEqual(dsp["offset"], icon["offset"] + ((icon["stored"] + 3) & ~3))
        self.assertEqual(table["offset"], dsp["offset"] + dsp["stored"])
        self.assertEqual(
            {k: main[k] for k in ("text", "data", "bss", "relocations", "entry")},
            {"text": 0x40, "data": 0x10, "bss": 8, "relocations": 2, "entry": 0x11},
        )
        body = packed[first + 20 : first + 20 + 0x50]
        self.assertEqual(body, bytes(range(0x48)) + bytes(8))
        self.assertEqual(
            packed[first + 0x64 : first + 0x6C], struct.pack("<II", 0x44, 0x4C)
        )
        self.assertEqual(icon["codec"], rvx.LZ)
        self.assertEqual(
            (table["codec"], table["stored"]), (rvx.RAW, 7)
        )  # lz would grow it
        self.assertEqual((song["codec"], song["stored"]), (rvx.RAW, 256))
        self.assertEqual(rvx.payload(packed, icon), b"\x11" * 64)
        self.assertEqual(rvx.payload(packed, table), bytes(range(7)))
        self.assertEqual(len(packed), song["offset"] + 256)

    def test_module_count_must_match_the_manifest(self):
        compiled = manifest.compile_manifest(DOCUMENT)
        with self.assertRaises(ValueError):
            rvx.pack_extension(compiled, two_modules()[:1], [], 0, 0)
        with self.assertRaises(ValueError):
            rvx.pack_extension(b"RVMF" + bytes(28), two_modules(), [], 0, 0)

    def test_rejects_catalog_name_collisions(self):
        compiled = manifest.compile_manifest(DOCUMENT)
        with self.assertRaises(ValueError):
            rvx.pack_extension(compiled, two_modules(), [asset("dsp", b"x")], 0, 0)

    def test_rejects_bad_layouts_and_relocations(self):
        cases = {
            "unaligned relocation": (SYMBOLS, [0x42]),
            "relocation past the image": (SYMBOLS, [0x4D]),
            "entry in data": ({**SYMBOLS, "rvx_entry": 0x51}, []),
            "bss before data": ({**SYMBOLS, "__bss_end": 0x4C}, []),
            "unaligned data": ({**SYMBOLS, "__data_start": 0x42}, []),
            "missing symbol": (
                {k: v for k, v in SYMBOLS.items() if k != "rvx_entry"},
                [],
            ),
        }
        for name, (values, offsets) in cases.items():
            with self.subTest(name), self.assertRaises(ValueError):
                rvx.module_image(values, bytes(0x50), offsets)

    def test_unpack_refuses_what_the_core_refuses(self):
        compiled = manifest.compile_manifest(DOCUMENT)
        packed, _ = rvx.pack_extension(
            compiled, two_modules(), [asset("icon", b"\x22" * 40, resident=True)], 0, 0
        )
        catalog = 28 + ((len(compiled) + 3) & ~3)
        icon = catalog + 2 * 20

        def entry_field(index, at, value):
            data = bytearray(packed)
            struct.pack_into("<I", data, catalog + index * 20 + at, value)
            return bytes(data)

        def header_field(index, value):
            data = bytearray(packed)
            struct.pack_into("<I", data, index * 4, value)
            return bytes(data)

        cases = {
            "format 0": header_field(1, 0),
            "residents past the catalog": header_field(6, 2),
            "fewer entries than modules": header_field(5, 1),
            "catalog past the end": header_field(5, 4000),
            "truncated payload": packed[:-4],
            "unaligned payload": entry_field(2, 8, packed.find(b"\x22") + 1),
            "payload past the end": entry_field(2, 12, len(packed)),
            "module kind after the modules": bytes(
                bytearray(packed[: icon + 4]) + b"\0" + packed[icon + 5 :]
            ),
            "module size differs": entry_field(0, 12, 0x6C + 4),
        }
        for name, data in cases.items():
            with self.subTest(name), self.assertRaises(ValueError):
                rvx.unpack(data)

    def test_lz_round_trip(self):
        rng = random.Random(1)
        samples = [
            b"",
            b"a",
            b"ab" * 3000,
            bytes(rng.randrange(256) for _ in range(5000)),
            bytes(rng.choice(b"\0\0\0\x01\xff") for _ in range(9000)),
            b"\x00" * 300 + b"x" + b"\x00" * 5000,  # match lengths past 255 + 18
        ]
        for data in samples:
            with self.subTest(len(data)):
                self.assertEqual(rvx.lz_decode(rvx.lz_encode(data), len(data)), data)

    @unittest.skipUnless(THEMES.is_dir(), "theme branch checkout not found")
    def test_lz_round_trip_on_theme_bitmaps(self):
        import bitmap

        pngs = sorted(p for p in THEMES.rglob("*.png") if ".git" not in p.parts)[::50]
        for path in pngs:
            try:
                data = bitmap.png_to_bmap(path.read_bytes())
            except ValueError:
                continue
            with self.subTest(path.name):
                packed = rvx.lz_encode(data)
                self.assertEqual(rvx.lz_decode(packed, len(data)), data)

    def test_lz_refuses_malformed_streams(self):
        data = b"hello hello hello hello"
        packed = rvx.lz_encode(data)
        cases = {
            "ends early": (packed[:-1], len(data)),
            "trailing bytes": (packed + b"\0", len(data)),
            "distance before the start": (b"\x01\x00\x05", 3),
            "match past the output": (b"\x02a\x10\x00", 3),
            "longer output asked": (packed, len(data) + 1),
        }
        for name, (stream, size) in cases.items():
            with self.subTest(name), self.assertRaises(ValueError):
                rvx.lz_decode(stream, size)

    def test_api_word(self):
        self.assertEqual(rvx.api_word("1.3"), 0x10003)

    def test_words_naming_firmware_symbols_stay_as_linked(self):
        symbols = (
            "     2: 000005b4     0 SECTION LOCAL  DEFAULT    3 .data\n"
            "    79: 08259b28     0 NOTYPE  GLOBAL DEFAULT  ABS GraphicsContext_SetColor\n"
        )
        table = (
            "Relocation section '.rel.text' at offset 0x13c1c contains 3 entries:\n"
            " Offset     Info    Type                Sym. Value  Symbol's Name\n"
            "00000014  00000202 R_ARM_ABS32            000005b4   .data\n"
            "0000019c  00004f02 R_ARM_ABS32            08259b28   GraphicsContext_SetColor\n"
            "000001a0  00000102 R_ARM_ABS32            00000000   .text\n"
            "000001a8  00000028 R_ARM_V4BX            \n"
        )
        absolute = rvx.absolute_symbols(symbols)
        self.assertEqual(absolute, {"GraphicsContext_SetColor"})
        self.assertEqual(rvx.abs32_offsets(table, absolute), [0x14, 0x1A0])
