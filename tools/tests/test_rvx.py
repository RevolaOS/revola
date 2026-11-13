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
    "rv_module_entry": 0x11,
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


def asset(name, data, preload="never", compress=True, kind=rvx.KIND_DATA):
    return {
        "name": name,
        "kind": kind,
        "data": data,
        "preload": preload,
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
            asset("icon", b"\x11" * 64, preload="optional", kind=rvx.KIND_BITMAP),
            asset("song", bytes(range(256)), compress=False),
        ]
        packed, _ = rvx.pack_extension(compiled, two_modules(), assets, 0x10002, 0x660EB6FF)
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
                "body_hash": rvx.fnv1a(packed[rvx.HEADER.size :]),
            },
        )
        self.assertEqual(
            packed[rvx.HEADER.size : rvx.HEADER.size + len(compiled)], compiled
        )
        catalog = rvx.HEADER.size + ((len(compiled) + 3) & ~3)
        first = catalog + 5 * 20
        self.assertEqual(
            [e["name"] for e in entries],
            [rvx.fnv1a(n.encode()) for n in ("main", "dsp", "icon", "table", "song")],
        )
        self.assertEqual([e["preload"] for e in entries], [0, 0, 1, 2, 2])
        main, dsp, icon, table, song = entries
        # main, the preloadable icon, dsp (kept by its manifest class), the on-disk assets
        self.assertEqual(rvx.manifest_module_classes(compiled), [0, 1])
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

    def test_body_hash_tells_a_changed_build_of_the_same_size(self):
        self.assertEqual(rvx.fnv1a(b""), 0x811C9DC5)
        self.assertEqual(rvx.fnv1a(b"a"), 0xE40C292C)
        compiled = manifest.compile_manifest(DOCUMENT)
        before, _ = rvx.pack_extension(compiled, two_modules(), [], 0, 0)
        main = rvx.module_image(
            SYMBOLS, bytes([1]) + bytes(range(1, 0x48)), [0x44, 0x4C]
        )
        after, _ = rvx.pack_extension(compiled, [("main", main), two_modules()[1]], [], 0, 0)
        old, new = rvx.unpack(before)[0], rvx.unpack(after)[0]
        self.assertEqual(len(before), len(after))
        self.assertNotEqual(old.pop("body_hash"), new.pop("body_hash"))
        self.assertEqual(old, new)

    def test_kept_modules_follow_the_preloadable_assets(self):
        document = {
            **DOCUMENT,
            "module": [
                *DOCUMENT["module"],
                {"name": "rare", "trigger": "manual", "preload": "never"},
                {"name": "core", "trigger": "manual", "preload": "always"},
            ],
        }
        compiled = manifest.compile_manifest(document)
        rare = rvx.module_image({**SYMBOLS, "__bss_end": 0x50}, bytes(0x50), [])
        modules = [*two_modules(), ("rare", rare), ("core", rare)]
        assets = [asset("icon", b"\x11" * 64, preload="optional"), asset("table", bytes(9))]
        packed, _ = rvx.pack_extension(compiled, modules, assets, 0, 0)
        entries = rvx.unpack(packed)[1]
        self.assertEqual(rvx.manifest_module_classes(compiled), [0, 1, 2, 0])
        self.assertEqual([e["preload"] for e in entries], [0, 0, 0, 0, 1, 2])
        by_offset = sorted(entries, key=lambda e: e["offset"])
        self.assertEqual(
            [e["name"] for e in by_offset],
            [rvx.fnv1a(n.encode()) for n in ("main", "icon", "dsp", "core", "rare", "table")],
        )

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
            "entry in data": ({**SYMBOLS, "rv_module_entry": 0x51}, []),
            "bss before data": ({**SYMBOLS, "__bss_end": 0x4C}, []),
            "unaligned data": ({**SYMBOLS, "__data_start": 0x42}, []),
            "missing symbol": (
                {k: v for k, v in SYMBOLS.items() if k != "rv_module_entry"},
                [],
            ),
        }
        for name, (values, offsets) in cases.items():
            with self.subTest(name), self.assertRaises(ValueError):
                rvx.module_image(values, bytes(0x50), offsets)

    def test_unpack_refuses_what_the_core_refuses(self):
        compiled = manifest.compile_manifest(DOCUMENT)
        packed, _ = rvx.pack_extension(
            compiled,
            two_modules(),
            [asset("icon", b"\x22" * 40, preload="always"), asset("more", b"\x33" * 8)],
            0,
            0,
        )
        catalog = rvx.HEADER.size + ((len(compiled) + 3) & ~3)
        icon = catalog + 2 * 20

        def entry_field(index, at, value):
            data = bytearray(packed)
            struct.pack_into("<I", data, catalog + index * 20 + at, value)
            return bytes(data)

        def header_field(index, value):
            data = bytearray(packed)
            struct.pack_into("<I", data, index * 4, value)
            return bytes(data)

        def preload_classes(classes):
            data = bytearray(packed)
            for index, value in classes.items():
                data[catalog + index * 20 + 6] = value
            return bytes(data)

        cases = {
            "format 0": header_field(1, 0),
            "fewer entries than modules": header_field(5, 1),
            "catalog past the end": header_field(5, 4000),
            "truncated payload": packed[:-4],
            "unaligned payload": entry_field(2, 8, packed.find(b"\x22") + 1),
            "payload past the end": entry_field(2, 12, len(packed)),
            "module kind after the modules": bytes(
                bytearray(packed[: icon + 4]) + b"\0" + packed[icon + 5 :]
            ),
            "module size differs": entry_field(0, 12, 0x6C + 4),
            "main with a preload class": preload_classes({0: 1}),
            "module with a preload class": preload_classes({1: 2}),
            "asset class 3": preload_classes({2: 3}),
            "assets out of order": preload_classes({2: 1, 3: 0}),
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
