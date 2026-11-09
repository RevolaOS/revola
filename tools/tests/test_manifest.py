# SPDX-License-Identifier: GPL-3.0-only
"""manifest.bin layout and the rules manifest.toml must follow."""

import struct
import unittest

import manifest

DOCUMENT = {
    "id": "hello",
    "name": "Hello",
    "description": "Says hello",
    "version": "0.1.0",
    "author": "Paul",
    "license": "GPL-3.0-only",
    "api": "1.2",
    "enabled": False,
    "module": [
        {"name": "main", "trigger": "always"},
        {"name": "editor", "trigger": "screen", "screens": ["hello.editor", "x"]},
        {"name": "dsp", "trigger": "manual"},
    ],
}


def strings(data, start, offset):
    end = data.index(b"\0", start + offset)
    return data[start + offset : end].decode()


class ManifestTests(unittest.TestCase):
    def test_header_modules_and_strings(self):
        data = manifest.compile_manifest(DOCUMENT)
        magic, version, api, flags, count, bytes_, *texts = manifest.HEADER.unpack_from(
            data
        )
        self.assertEqual(
            (magic, version, api, flags, count), (0x464D5652, 1, 0x10002, 0, 3)
        )
        table = manifest.HEADER.size + count * manifest.MODULE.size
        self.assertEqual(len(data), table + bytes_)
        self.assertEqual(
            [strings(data, table, t) for t in texts],
            ["hello", "Hello", "Says hello", "0.1.0", "Paul", "GPL-3.0-only"],
        )
        modules = [
            manifest.MODULE.unpack_from(
                data, manifest.HEADER.size + i * manifest.MODULE.size
            )
            for i in range(count)
        ]
        name, file, trigger, screens, first = modules[1]
        self.assertEqual(
            (strings(data, table, name), strings(data, table, file), trigger, screens),
            ("editor", "editor.rvx", 1, 2),
        )
        self.assertEqual(strings(data, table, first), "hello.editor")
        after = first + len("hello.editor") + 1
        self.assertEqual(strings(data, table, after), "x")
        self.assertEqual(modules[0][2:4], (0, 0))
        self.assertEqual(modules[2][2:4], (2, 0))

    def test_enabled_flag(self):
        data = manifest.compile_manifest({**DOCUMENT, "enabled": True})
        self.assertEqual(struct.unpack_from("<I", data, 12)[0], 1)

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
