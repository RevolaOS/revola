# SPDX-License-Identifier: GPL-3.0-only
"""Pinned Apple firmware inputs for the single Classic build."""

import hashlib
import json
import re
from pathlib import Path

DIRECTORY = Path(__file__).resolve().parents[1] / "firmware"
PROFILE = DIRECTORY / "apple.json"


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


def resources_header(symbols_header):
    """rv_resources.h for the SDK: the RES_* ids of the symbols header as RV_RES_*,
    the names extensions anchor menu rows and native rows by."""
    names = re.findall(r"^#define RES_(\w+) (0x[0-9a-fA-F]+)", symbols_header, re.M)
    if not names:
        raise ValueError("symbols header has no RES_ ids")
    width = max(len(name) for name, _ in names)
    lines = [
        "/* SPDX-License-Identifier: GPL-3.0-only */",
        "/* Generated from the symbols header: Apple resource ids extensions may name. */",
        "#pragma once",
        "",
    ]
    lines += [f"#define RV_RES_{name.ljust(width)} {value}" for name, value in names]
    return "\n".join(lines) + "\n"
