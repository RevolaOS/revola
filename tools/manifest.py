#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check an extension's manifest.toml and compile the manifest rvx-pack writes
into the extension's .rvx.

manifest.toml:

    id = "hello_world"           # [a-z][a-z0-9_-]{0,31}, the file name before .rvx
    name = "Hello World"
    description = "What it does" # shown to listeners on its page: one plain sentence
    version = "0.1.0"
    author = "Who"
    license = "GPL-3.0-only"     # GPL-3.0 compatible
    api = "1.0"                  # minimum core API major.minor
    enabled = true               # state after install
    stability = "beta"           # optional: alpha, beta or stable, the author's word
    note = "Adapted from X"      # optional: one line under the version, shown to listeners

    [[module]]                   # the first module is main, trigger always
    name = "main"
    trigger = "always"

    [[module]]
    name = "editor"
    trigger = "screen"           # loaded while one of its screens is on the stack
    screens = ["editor"]         # ids of the extension's screens

    [[site]]                     # a raw hook site, bound as RV_RAW_SITE(0)
    name = "coverflow_clear"     # a label for the site
    address = 0x08123456         # the word to patch, from the pinned firmware
    kind = "call"                # call, vtable or word
    expected = 0xEB001234        # the word there now; the core refuses a mismatch
    argc = 2                     # register arguments, at most 4; 0 for word

    [[asset]]
    name = "eq-icon"             # [a-z0-9_-]{1,31}
    file = "assets/eq.png"       # .png becomes a bitmap, anything else is data
    resident = true              # in RAM, compressed, while the extension is loaded
    menu = true                  # a menu bitmap: 78 x 121 coverage, drawn in the system colour

    [[asset]]
    name = "song"
    file = "song.mp3"
    compress = false             # data only; keeps it raw so asset_read can seek

Raw sites are for code no core site covers yet; first-party extensions use core
sites only.

Ships in the SDK tarball, so it depends on nothing outside this file.
"""

import re
import struct

MAGIC = 0x464D5652  # RVMF
VERSION = 2
FLAG_ENABLED = 1
STABILITY = {"alpha": 1, "beta": 2, "stable": 3}  # flags bits 1-2
HEADER = struct.Struct("<7I8H")  # text offsets, the note's, a reserved half-word
MODULE = struct.Struct("<HBBH")
SITE = struct.Struct("<IIBBH")
SITE_KINDS = {"call": 0, "vtable": 1, "word": 4}  # enum core_site_kind
MAX_SITES = 8
RUNTIME_BASE = 0x08000000
NATIVE_HEAP_BASE = 0x08B3B1B0  # OSOS 2.0.5; raw sites lie below it
TRIGGERS = {"always": 0, "screen": 1, "manual": 2}
TEXT_FIELDS = ("id", "name", "description", "version", "author", "license")
ID_PATTERN = re.compile(r"[a-z][a-z0-9_-]{0,31}")
MODULE_PATTERN = re.compile(r"[a-z0-9_-]+")
ASSET_PATTERN = re.compile(r"[a-z0-9_-]{1,31}")
ASSET_KEYS = {"name", "file", "resident", "compress", "menu"}
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
MAX_MODULES = 15  # main and at most 14 more
MAX_BYTES = 1024  # CORE_MANIFEST_MAX
MANIFEST_KEYS = {*TEXT_FIELDS, "note", "api", "enabled", "stability", "module", "site", "asset"}
MODULE_KEYS = {"name", "trigger", "screens"}
SITE_KEYS = {"name", "address", "kind", "expected", "argc"}


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
    match = re.fullmatch(r"(\d+)\.(\d+)", str(version))
    if not match or int(match[1]) > 0xFFFF or int(match[2]) > 0xFFFF:
        raise ValueError(f"api is major.minor, not {version!r}")
    return int(match[1]) << 16 | int(match[2])


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
    if (
        not isinstance(address, int)
        or address % 4
        or not (RUNTIME_BASE <= address < NATIVE_HEAP_BASE)
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
            raise ValueError(
                f"site {name}: raw sites are exclusive, {field} is not allowed"
            )
    if set(site) - SITE_KEYS:
        raise ValueError(f"site {name}: unknown keys {sorted(set(site) - SITE_KEYS)}")
    return SITE.pack(address, expected, SITE_KINDS[kind], argc, strings.add(name))


def compile_manifest(document):
    """The compiled manifest for a parsed manifest.toml dictionary."""
    unknown = set(document) - MANIFEST_KEYS
    if unknown:
        raise ValueError(f"unknown keys {sorted(unknown)}")
    for field in TEXT_FIELDS:
        if not isinstance(document.get(field), str):
            raise ValueError(f"missing {field}")
    if not isinstance(document.get("note", ""), str):
        raise ValueError("note is a string")
    if not isinstance(document.get("enabled", True), bool):
        raise ValueError("enabled is true or false")
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
    texts.append(strings.add(document.get("note", "")))
    records = []
    for module in modules:
        name = module.get("name", "")
        trigger = module.get("trigger", "")
        screens = module.get("screens", [])
        if set(module) - MODULE_KEYS:
            raise ValueError(
                f"module {name}: unknown keys {sorted(set(module) - MODULE_KEYS)}"
            )
        if not isinstance(screens, list) or not all(
            isinstance(screen, str) and screen for screen in screens
        ):
            raise ValueError(f"module {name}: screens is a list of screen ids")
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
        screens_offset = strings.add(screens[0]) if screens else 0
        for screen in screens[1:]:
            strings.add(screen)
        records.append(
            MODULE.pack(
                name_offset,
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
        (FLAG_ENABLED if document.get("enabled", True) else 0)
        | stability_bits(document) << 1,
        len(records),
        len(site_records),
        len(strings.data),
        *texts,
        0,
    )
    compiled = header + b"".join(site_records) + b"".join(records) + bytes(strings.data)
    if len(compiled) > MAX_BYTES:
        raise ValueError(
            f"the compiled manifest is {len(compiled)} bytes, the core reads {MAX_BYTES}"
        )
    return compiled


def stability_bits(document):
    """0 without a stability field, else its value in STABILITY."""
    if "stability" not in document:
        return 0
    if document["stability"] not in STABILITY:
        raise ValueError(f"stability must be one of {', '.join(STABILITY)}")
    return STABILITY[document["stability"]]


def assets(document):
    """The manifest's assets in declaration order, checked, with defaults filled in."""
    checked = []
    for asset in document.get("asset", []):
        name = asset.get("name", "")
        if not isinstance(name, str) or not ASSET_PATTERN.fullmatch(name):
            raise ValueError(f"bad asset name {name!r}")
        if name in (a["name"] for a in checked):
            raise ValueError(f"repeated asset name {name!r}")
        if set(asset) - ASSET_KEYS:
            raise ValueError(
                f"asset {name}: unknown keys {sorted(set(asset) - ASSET_KEYS)}"
            )
        if not isinstance(asset.get("file"), str) or not asset["file"]:
            raise ValueError(f"asset {name}: file is a path")
        for key in ("resident", "compress", "menu"):
            if not isinstance(asset.get(key, False), bool):
                raise ValueError(f"asset {name}: {key} is true or false")
        bitmap = asset["file"].lower().endswith(".png")
        if bitmap and "compress" in asset:
            raise ValueError(f"asset {name}: bitmaps are always compressed")
        if asset.get("menu") and not bitmap:
            raise ValueError(f"asset {name}: a menu bitmap is a .png")
        checked.append(
            {
                "name": name,
                "file": asset["file"],
                "resident": asset.get("resident", False),
                "compress": asset.get("compress", True),
                "menu": asset.get("menu", False),
            }
        )
    return checked
