# SPDX-License-Identifier: GPL-3.0-only
"""Pinned Apple firmware inputs for the single Classic build."""

import hashlib
import json
import re
from pathlib import Path

DIRECTORY = Path(__file__).resolve().parents[1] / "firmware"
PROFILE = DIRECTORY / "apple.json"
# The OSOS build the symbols header, linker script and theme catalog are named after.
NAME = "osos-2.0.5"


def load():
    return json.loads(PROFILE.read_text())


def verify(data, fingerprint, name):
    if (
        len(data) != fingerprint["bytes"]
        or hashlib.sha256(data).hexdigest() != fingerprint["sha256"]
    ):
        raise ValueError(f"Input does not match firmware: {name}")


def build_id(profile=None):
    """RV_FIRMWARE_ID: the first eight hex digits of the pinned osos.bin sha256."""
    profile = profile or load()
    return int(profile["inputs"]["osos.bin"]["sha256"][:8], 16)


MENUS = {"MainMenu": "MAIN", "MusicMenu": "MUSIC", "ExtrasMenu": "EXTRAS", "SettingsMenu": "SETTINGS"}
ROW_PATTERN = re.compile(
    r"^#define RES_(MainMenu|MusicMenu|ExtrasMenu|SettingsMenu)_(\w+)_Item (0x[0-9a-fA-F]+)", re.M
)


def menu_row_name(menu, row):
    """RV_<MENU>_<ROW>: the row's words in upper snake case (iTunesU stays one word)."""
    words = "ItunesU" if row == "iTunesU" else row
    words = re.sub(r"(?<=[a-z0-9])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z])", "_", words)
    return f"RV_{MENUS[menu]}_{words.upper()}"


def menus_header(symbols_header):
    """rv_menus.h for the SDK: the stock rows of the Main, Music, Extras and Settings
    menus, which extensions anchor rows on and move, named by menu and row."""
    rows = ROW_PATTERN.findall(symbols_header)
    if not rows:
        raise ValueError("symbols header has no menu rows")
    names = [(menu_row_name(menu, row), value) for menu, row, value in rows]
    width = max(len(name) for name, _ in names)
    lines = [
        "/* SPDX-License-Identifier: GPL-3.0-only */",
        "/* Generated from the symbols header: the stock rows of the Main, Music, Extras",
        " * and Settings menus, by menu and row. */",
        "#pragma once",
        "",
    ]
    lines += [f"#define {name.ljust(width)} {value}" for name, value in names]
    return "\n".join(lines) + "\n"
