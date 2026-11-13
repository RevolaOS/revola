# SPDX-License-Identifier: GPL-3.0-only
"""The bitmap asset catalog of a built extension."""

import json
import subprocess
import sys
import tempfile
import tomllib
import unittest
from pathlib import Path

import assets
import bitmap
import manifest
import rvx

ROOT = Path(__file__).resolve().parents[1]
SYMBOLS = {
    "__data_start": 0x40,
    "__data_end": 0x50,
    "__bss_end": 0x58,
    "rv_module_entry": 0x11,
}
MANIFEST = """\
id = "fixture"
name = "Fixture"
description = "Two bitmaps"
version = "0.1.0"
author = "RevolaOS"
license = "GPL-3.0-only"

[[module]]
name = "main"
trigger = "always"

[[asset]]
name = "icon"
file = "icon.png"
menu = true

[[asset]]
name = "art"
file = "art.png"
preload = "always"
"""
ICON = bitmap.encode_mask(78, 121, bytes([0, 0, 0, 255]) * (78 * 121))
ART = bitmap.encode_bmap(2, 2, bytes([255, 0, 0, 255, 0, 255, 0, 255]) * 2)


def package(directory, names=("icon", "art")):
    """manifest.toml and an .rvx whose bitmaps are `names`, in `directory`."""
    toml = directory / "manifest.toml"
    toml.write_text(MANIFEST)
    compiled = manifest.compile_manifest(tomllib.loads(MANIFEST))
    main = rvx.module_image(SYMBOLS, bytes(range(0x48)), [0x44, 0x4C])
    bitmaps = [
        {
            "name": name,
            "kind": rvx.KIND_BITMAP,
            "data": ICON if name == "icon" else ART,
            "preload": "always" if name == "art" else "optional",
            "compress": True,
        }
        for name in names
    ]
    packed, _ = rvx.pack_extension(compiled, [("main", main)], bitmaps, 0x10002F, 1)
    file = directory / "fixture.rvx"
    file.write_bytes(packed)
    return toml, file


class AssetCatalogTests(unittest.TestCase):
    def test_bitmaps_of_a_package(self):
        with tempfile.TemporaryDirectory() as directory:
            toml, file = package(Path(directory))
            records = assets.catalog(toml, file)
            out = subprocess.run(
                [sys.executable, str(ROOT / "assets.py"), str(toml), str(file)],
                capture_output=True,
                text=True,
                check=True,
            ).stdout
        self.assertEqual(
            sorted(records, key=lambda r: r["asset"]),
            [
                {
                    "id": "fixture",
                    "asset": "art",
                    "menu": False,
                    "format": bitmap.INDEXED8,
                    "width": 2,
                    "height": 2,
                    "stride": 4,
                    "depth": 8,
                    "palette_colors": 2,
                    "bytes": len(ART),
                    "preload": "always",
                },
                {
                    "id": "fixture",
                    "asset": "icon",
                    "menu": True,
                    "format": bitmap.MASK8,
                    "width": 78,
                    "height": 121,
                    "stride": 80,
                    "depth": 8,
                    "palette_colors": 0,
                    "bytes": 28 + 80 * 121,
                    "preload": "optional",
                },
            ],
        )
        self.assertEqual([json.loads(line) for line in out.splitlines()], records)

    def test_a_bitmap_the_manifest_does_not_declare_is_an_error(self):
        with tempfile.TemporaryDirectory() as directory:
            toml, file = package(Path(directory), ("icon", "extra"))
            with self.assertRaisesRegex(ValueError, "not in the manifest"):
                assets.catalog(toml, file)

    def test_core_icons(self):
        records = assets.core_catalog(ROOT.parent / "core/assets")
        self.assertEqual([r["asset"] for r in records], ["extensions"])
        self.assertEqual(records[0]["id"], manifest.CORE_ID)
        self.assertEqual((records[0]["width"], records[0]["height"]), (78, 121))
        self.assertEqual(records[0]["palette_colors"], 0)

    def test_a_missing_package_is_an_error_the_command_reports(self):
        with tempfile.TemporaryDirectory() as directory:
            toml = Path(directory) / "manifest.toml"
            toml.write_text('id = "x"\n')
            with self.assertRaises(OSError):
                assets.catalog(toml, Path(directory) / "missing.rvx")
            result = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "assets.py"),
                    str(toml),
                    str(toml.parent / "m.rvx"),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 1)
            self.assertIn("assets:", result.stderr)
