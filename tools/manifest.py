#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile an extension's manifest.toml into the manifest.bin the loader reads.

    manifest manifest.toml manifest.bin

manifest.toml:

    id = "hello"                 # [a-z][a-z0-9_-]{0,31}, the directory name
    name = "Hello"
    description = "What it does"
    version = "0.1.0"
    author = "Who"
    license = "GPL-3.0-only"     # GPL-3.0 compatible
    api = "1.0"                  # minimum core API major.minor
    enabled = true               # state after install

    [[module]]                   # the first module is main, trigger always
    name = "main"
    trigger = "always"

    [[module]]
    name = "editor"
    trigger = "screen"           # loaded while one of its screens is on the stack
    screens = ["hello.editor"]

    [[site]]                     # a raw hook site, bound as RV_RAW_SITE(0)
    name = "coverflow_clear"     # a label for the site
    address = 0x08123456         # the word to patch, from the pinned firmware
    kind = "call"                # call, vtable or word
    expected = 0xEB001234        # the word there now; the core refuses a mismatch
    argc = 2                     # register arguments, at most 4; 0 for word

Raw sites are for code no core site covers yet; first-party extensions use core
sites only.

Ships in the SDK tarball, so it depends on nothing outside this file.
"""

import argparse
import re
import struct
import sys
import tomllib
from pathlib import Path

MAGIC = 0x464D5652  # RVMF
VERSION = 2
FLAG_ENABLED = 1
HEADER = struct.Struct("<7I6H")
MODULE = struct.Struct("<HHBBH")
SITE = struct.Struct("<IIBBH")
SITE_KINDS = {"call": 0, "vtable": 1, "word": 4}  # enum core_site_kind
MAX_SITES = 8
RUNTIME_BASE = 0x08000000
NATIVE_HEAP_BASE = 0x08B3B1B0  # OSOS 2.0.5; raw sites lie below it
TRIGGERS = {"always": 0, "screen": 1, "manual": 2}
TEXT_FIELDS = ("id", "name", "description", "version", "author", "license")
ID_PATTERN = re.compile(r"[a-z][a-z0-9_-]{0,31}")
MODULE_PATTERN = re.compile(r"[a-z0-9_-]+")
LICENSES = {
    "GPL-3.0-only",
    "GPL-3.0-or-later",
    "GPL-2.0-or-later",
    "LGPL-2.1-or-later",
    "LGPL-3.0-only",
    "LGPL-3.0-or-later",
    "MIT",
    "BSD-2-Clause",
    "BSD-3-Clause",
    "Apache-2.0",
    "MPL-2.0",
    "0BSD",
    "Zlib",
}
MAX_MODULES = 15  # main and at most 14 more; the installer uploads 16 files


class Strings:
    def __init__(self):
        self.data = bytearray()

    def add(self, text):
        if not isinstance(text, str) or "\0" in text:
            raise ValueError(f"not a string: {text!r}")
        offset = len(self.data)
        self.data += text.encode() + b"\0"
        if len(self.data) > 0xFFFF:
            raise ValueError("strings exceed 64 KiB")
        return offset


def api_word(version):
    major, minor = (int(part) for part in str(version).split("."))
    return major << 16 | minor


def compile_site(site, strings):
    name = site.get("name", "")
    kind = site.get("kind", "")
    address = site.get("address")
    expected = site.get("expected")
    argc = site.get("argc", 0)
    if not MODULE_PATTERN.fullmatch(name):
        raise ValueError(f"bad site name {name!r}")
    if kind not in SITE_KINDS:
        raise ValueError(f"site {name}: kind is call, vtable or word, not {kind!r}")
    if not isinstance(address, int) or address % 4 or not (
        RUNTIME_BASE <= address < NATIVE_HEAP_BASE
    ):
        raise ValueError(f"site {name}: address must be a word inside the OSOS image")
    if not isinstance(expected, int) or not 0 <= expected <= 0xFFFFFFFF:
        raise ValueError(f"site {name}: expected must be a 32-bit word")
    if kind == "call" and expected >> 24 != 0xEB:
        raise ValueError(f"site {name}: a call site's expected word is a BL")
    if not isinstance(argc, int) or not 0 <= argc <= 4 or (kind == "word" and argc):
        raise ValueError(f"site {name}: argc is 0 to 4, and 0 for word")
    for field in ("chain", "mode"):
        if field in site:
            raise ValueError(f"site {name}: raw sites are exclusive, {field} is not allowed")
    return SITE.pack(address, expected, SITE_KINDS[kind], argc, strings.add(name))


def compile_manifest(document):
    """manifest.bin bytes for a parsed manifest.toml dictionary."""
    for field in TEXT_FIELDS:
        if field not in document:
            raise ValueError(f"missing {field}")
    if not ID_PATTERN.fullmatch(document["id"]):
        raise ValueError(f"bad id {document['id']!r}")
    if document["license"] not in LICENSES:
        raise ValueError(f"license {document['license']!r} is not GPL-3.0 compatible")
    modules = document.get("module", [])
    if not modules or len(modules) > MAX_MODULES:
        raise ValueError(f"1 to {MAX_MODULES} modules")
    if modules[0].get("name") != "main" or modules[0].get("trigger") != "always":
        raise ValueError("the first module is main with trigger always")
    names = set()
    strings = Strings()
    texts = [strings.add(document[field]) for field in TEXT_FIELDS]
    records = []
    for module in modules:
        name = module.get("name", "")
        trigger = module.get("trigger", "")
        screens = module.get("screens", [])
        if not MODULE_PATTERN.fullmatch(name) or name in names:
            raise ValueError(f"bad or repeated module name {name!r}")
        names.add(name)
        if trigger not in TRIGGERS:
            raise ValueError(f"module {name}: unknown trigger {trigger!r}")
        if (trigger == "screen") != bool(screens) or len(screens) > 255:
            raise ValueError(f"module {name}: screens go with the screen trigger")
        if trigger == "always" and module is not modules[0]:
            raise ValueError(f"module {name}: only main loads always")
        name_offset = strings.add(name)
        file_offset = strings.add(name + ".rvx")
        screens_offset = strings.add(screens[0]) if screens else 0
        for screen in screens[1:]:
            strings.add(screen)
        records.append(
            MODULE.pack(
                name_offset,
                file_offset,
                TRIGGERS[trigger],
                len(screens),
                screens_offset,
            )
        )
    sites = document.get("site", [])
    if len(sites) > MAX_SITES:
        raise ValueError(f"at most {MAX_SITES} sites")
    if len({site.get("address") for site in sites}) != len(sites):
        raise ValueError("two sites at one address")
    site_records = [compile_site(site, strings) for site in sites]
    header = HEADER.pack(
        MAGIC,
        VERSION,
        api_word(document.get("api", "1.0")),
        FLAG_ENABLED if document.get("enabled", True) else 0,
        len(records),
        len(site_records),
        len(strings.data),
        *texts,
    )
    return header + b"".join(records) + b"".join(site_records) + bytes(strings.data)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("toml", type=Path)
    parser.add_argument("out", type=Path)
    args = parser.parse_args()
    document = tomllib.loads(args.toml.read_text())
    if args.toml.resolve().parent.name not in ("", document.get("id")):
        print(f"warning: directory is not named {document.get('id')}", file=sys.stderr)
    for site in document.get("site", []):
        print(
            f"warning: raw site {site.get('name')} at {site.get('address', 0):#x};"
            " ask for a core site so others can share it",
            file=sys.stderr,
        )
    args.out.write_bytes(compile_manifest(document))


if __name__ == "__main__":
    sys.exit(main())
