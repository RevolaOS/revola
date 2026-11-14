# SPDX-License-Identifier: GPL-3.0-only
"""The release build's extension and theme inputs."""

import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import release


class ThemeTests(unittest.TestCase):
    def checkout(self, root, folders):
        themes = root / "themes"
        for name, body in folders.items():
            (themes / "themes" / name).mkdir(parents=True)
            (themes / "themes" / name / "theme.toml").write_text(body)
        return themes

    def test_themes_are_compiled_with_their_versions_and_classic_is_skipped(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            themes = self.checkout(
                root,
                {
                    "classic": 'id = "classic"\nversion = "1.0"\n',
                    "flat": 'id = "flat"\nname = "Flat"\nversion = "1.2"\n',
                    "mono": 'id = "night"\nversion = "3"\n',
                },
            )
            out = root / "out"
            out.mkdir()
            commands = []

            def compile_theme(command, cwd):
                commands.append((list(map(str, command)), cwd))
                folder, dist = Path(command[4]), Path(command[6])
                identifier = {"flat": "flat", "mono": "night"}[folder.name]
                (dist / identifier).mkdir(parents=True)
                (dist / identifier / f"{identifier}.rvt").write_bytes(b"RVT0")

            with patch("release.run", side_effect=compile_theme):
                packages = release.build_themes(themes, out)
            self.assertEqual(
                packages,
                {
                    "flat": {
                        "format": "revola-theme-v1",
                        "version": "1.2",
                        "file": str(out / "themes/flat/flat.rvt"),
                    },
                    "night": {
                        "format": "revola-theme-v1",
                        "version": "3",
                        "file": str(out / "themes/night/night.rvt"),
                    },
                },
            )
            self.assertEqual([cwd for _, cwd in commands], [themes, themes])
            self.assertEqual(
                commands[0][0][1:],
                [
                    "-m",
                    "rvt",
                    "compile",
                    str(themes / "themes/flat"),
                    "--out",
                    str(out / "themes"),
                ],
            )

    def test_missing_id_version_or_output_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            out = root / "out"
            out.mkdir()
            themes = self.checkout(root, {"flat": 'version = "1"\n'})
            with patch("release.run"), self.assertRaises(ValueError):
                release.build_themes(themes, out)
            for body in ['id = "flat"\n', 'id = "flat"\nversion = 3\n']:
                (themes / "themes/flat/theme.toml").write_text(body)
                with patch("release.run"), self.assertRaises(ValueError):
                    release.build_themes(themes, out)
            (themes / "themes/flat/theme.toml").write_text(
                'id = "flat"\nversion = "1"\n'
            )
            with patch("release.run"), self.assertRaises(ValueError):
                release.build_themes(themes, out)

    def test_specification_carries_extensions_themes_homebrew_and_the_core(self):
        extensions = {"eq": {"format": "revola-extension-v1"}}
        themes = {"flat": {"format": "revola-theme-v1"}}
        games = {"2048": {"format": "revola-eapp-v1"}}
        apps = {"clock": {"format": "revola-eapp-v1"}}
        core = {"api": 1, "firmware_build_id": 1, "catalog_fingerprint": "00" * 8}
        with patch("release.firmware.load", return_value={"compatibility": {}}):
            spec = release.specification(
                "1.0.0",
                "1.0.0",
                Path("out"),
                Path("helper"),
                extensions,
                themes,
                games,
                apps,
                core,
            )
        self.assertEqual(spec["extensions"], extensions)
        self.assertEqual(spec["themes"], themes)
        self.assertEqual(spec["games"], games)
        self.assertEqual(spec["apps"], apps)
        self.assertEqual(spec["core"], core)

    def test_catalog_fingerprint_comes_from_the_themes_catalog(self):
        with tempfile.TemporaryDirectory() as directory:
            themes = Path(directory)
            data = themes / "rvt/data"
            data.mkdir(parents=True)
            catalog = data / "osos-2.0.5.json"
            catalog.write_text('{"fingerprint": "4f2e84ff031616d7"}')
            self.assertEqual(release.catalog_fingerprint(themes), "4f2e84ff031616d7")
            for bad in ["4f2e84ff031616d", "4f2e84ff031616d7ff", "zz2e84ff031616d7"]:
                catalog.write_text(json.dumps({"fingerprint": bad}))
                with self.assertRaises(ValueError):
                    release.catalog_fingerprint(themes)


if __name__ == "__main__":
    unittest.main()
