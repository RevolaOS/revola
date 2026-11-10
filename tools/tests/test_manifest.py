# SPDX-License-Identifier: GPL-3.0-only
"""The compiled manifest layout and the rules manifest.toml must follow."""

import struct
import unittest

import manifest

DOCUMENT = {
    "id": "hello_world",
    "name": "Hello World",
    "description": "Says hello",
    "version": "0.1.0",
    "author": "Paul",
    "license": "GPL-3.0-only",
    "api": "1.2",
    "enabled": False,
    "module": [
        {"name": "main", "trigger": "always"},
        {"name": "editor", "trigger": "screen", "screens": ["editor", "x"]},
        {"name": "dsp", "trigger": "manual"},
    ],
}


def strings(data, start, offset):
    end = data.index(b"\0", start + offset)
    return data[start + offset : end].decode()


class ManifestTests(unittest.TestCase):
    def test_header_modules_and_strings(self):
        data = manifest.compile_manifest(DOCUMENT)
        magic, version, api, flags, count, sites, bytes_, *texts = (
            manifest.HEADER.unpack_from(data)
        )
        self.assertEqual(
            (magic, version, api, flags, count, sites),
            (0x464D5652, 2, 0x10002, 0, 3, 0),
        )
        table = manifest.HEADER.size + count * manifest.MODULE.size
        self.assertEqual(len(data), table + bytes_)
        self.assertEqual(
            [strings(data, table, t) for t in texts],
            [
                "hello_world",
                "Hello World",
                "Says hello",
                "0.1.0",
                "Paul",
                "GPL-3.0-only",
            ],
        )
        modules = [
            manifest.MODULE.unpack_from(
                data, manifest.HEADER.size + i * manifest.MODULE.size
            )
            for i in range(count)
        ]
        name, trigger, screens, first = modules[1]
        self.assertEqual(
            (strings(data, table, name), trigger, screens), ("editor", 1, 2)
        )
        self.assertEqual(strings(data, table, first), "editor")
        after = first + len("editor") + 1
        self.assertEqual(strings(data, table, after), "x")
        self.assertEqual(modules[0][1:3], (0, 0))
        self.assertEqual(modules[2][1:3], (2, 0))

    def test_enabled_flag(self):
        data = manifest.compile_manifest({**DOCUMENT, "enabled": True})
        self.assertEqual(struct.unpack_from("<I", data, 12)[0], 1)

    def test_stability_flag(self):
        def flags(document):
            return struct.unpack_from("<I", manifest.compile_manifest(document), 12)[0]

        self.assertEqual(flags({**DOCUMENT, "enabled": True}) >> 1, 0)
        for value, bits in (("alpha", 1), ("beta", 2), ("stable", 3)):
            with self.subTest(value):
                self.assertEqual(
                    flags({**DOCUMENT, "enabled": True, "stability": value}),
                    1 | bits << 1,
                )

    def test_raw_sites(self):
        site = {
            "name": "clear",
            "address": 0x08001000,
            "kind": "call",
            "expected": 0xEB000010,
            "argc": 2,
        }
        word = {"name": "limit", "address": 0x08002000, "kind": "word", "expected": 7}
        data = manifest.compile_manifest({**DOCUMENT, "site": [site, word]})
        count, sites, bytes_ = manifest.HEADER.unpack_from(data)[4:7]
        self.assertEqual(sites, 2)
        start = manifest.HEADER.size
        table = start + sites * manifest.SITE.size + count * manifest.MODULE.size
        self.assertEqual(len(data), table + bytes_)
        address, expected, kind, argc, name = manifest.SITE.unpack_from(data, start)
        self.assertEqual(
            (address, expected, kind, argc), (0x08001000, 0xEB000010, 0, 2)
        )
        self.assertEqual(strings(data, table, name), "clear")
        self.assertEqual(
            manifest.SITE.unpack_from(data, start + manifest.SITE.size)[:4],
            (0x08002000, 7, 4, 0),
        )

    def test_rejects_invalid_sites(self):
        site = {
            "name": "clear",
            "address": 0x08001000,
            "kind": "call",
            "expected": 0xEB000010,
            "argc": 2,
        }
        cases = {
            "entry kind": {"kind": "entry"},
            "insert kind": {"kind": "insert"},
            "chain": {"chain": 1},
            "unaligned": {"address": 0x08001002},
            "outside the image": {"address": 0x08B40000},
            "call without BL": {"expected": 0xE1A00000},
            "five arguments": {"argc": 5},
            "word with arguments": {"kind": "word", "argc": 1},
            "no name": {"name": ""},
        }
        for name, change in cases.items():
            with self.subTest(name), self.assertRaises(ValueError):
                manifest.compile_manifest({**DOCUMENT, "site": [{**site, **change}]})
        with self.assertRaises(ValueError):
            manifest.compile_manifest(
                {**DOCUMENT, "site": [site, {**site, "name": "b"}]}
            )
        with self.assertRaises(ValueError):
            manifest.compile_manifest(
                {
                    **DOCUMENT,
                    "site": [
                        {**site, "name": f"s{i}", "address": 0x08001000 + 4 * i}
                        for i in range(9)
                    ],
                }
            )

    def test_rejects_invalid_documents(self):
        main = DOCUMENT["module"][0]
        cases = {
            "upper-case id": {"id": "Hello"},
            "long id": {"id": "a" * 33},
            "license": {"license": "Proprietary"},
            "missing author": {"author": None},
            "no modules": {"module": []},
            "main not first": {"module": [DOCUMENT["module"][2], main]},
            "second always": {"module": [main, {"name": "b", "trigger": "always"}]},
            "trigger": {"module": [main, {"name": "b", "trigger": "playback"}]},
            "screens without trigger": {
                "module": [main, {"name": "b", "trigger": "manual", "screens": ["s"]}]
            },
            "screen trigger without screens": {
                "module": [main, {"name": "b", "trigger": "screen"}]
            },
            "repeated module": {
                "module": [main, {"name": "main", "trigger": "manual"}]
            },
            "stability": {"stability": "experimental"},
            "module name": {
                "module": [main, {"name": "Bad Name", "trigger": "manual"}]
            },
        }
        for name, change in cases.items():
            document = {**DOCUMENT, **change}
            if change.get("author", "") is None:
                del document["author"]
            with self.subTest(name), self.assertRaises(ValueError):
                manifest.compile_manifest(document)

    def test_assets_defaults_and_order(self):
        document = {
            **DOCUMENT,
            "asset": [
                {
                    "name": "eq-icon",
                    "file": "assets/eq.png",
                    "resident": True,
                    "menu": True,
                },
                {"name": "song", "file": "a.mp3", "compress": False},
            ],
        }
        self.assertEqual(
            manifest.assets(document),
            [
                {
                    "name": "eq-icon",
                    "file": "assets/eq.png",
                    "resident": True,
                    "compress": True,
                    "menu": True,
                },
                {
                    "name": "song",
                    "file": "a.mp3",
                    "resident": False,
                    "compress": False,
                    "menu": False,
                },
            ],
        )
        self.assertEqual(manifest.assets(DOCUMENT), [])

    def test_rejects_invalid_assets(self):
        good = {"name": "icon", "file": "icon.png"}
        cases = {
            "name": [{**good, "name": "Icon"}],
            "long name": [{**good, "name": "x" * 32}],
            "repeated": [good, good],
            "no file": [{"name": "icon"}],
            "resident not a flag": [{**good, "resident": 1}],
            "unknown key": [{**good, "kind": "bitmap"}],
            "uncompressed bitmap": [{**good, "compress": False}],
            "menu data": [{"name": "song", "file": "a.mp3", "menu": True}],
        }
        for name, assets in cases.items():
            with self.subTest(name), self.assertRaises(ValueError):
                manifest.assets({**DOCUMENT, "asset": assets})
