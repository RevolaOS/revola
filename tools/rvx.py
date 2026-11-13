#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""rvx-pack: build an extension's one .rvx file from its manifest.toml, module
ELFs and assets.

    rvx-pack hello.rvx manifest.toml main.elf editor.elf --api 1.7 \
        --firmware 0x660eb6ff

The ELFs come in the manifest's module order, main first. Each is linked at
address 0 with the SDK linker script and --emit-relocs, so the R_ARM_ABS32
entries survive; text, rodata, data and bss form one segment. Asset files are
relative to manifest.toml. The file layout and the lz codec are in the core's
loader/rvx.h. Ships in the SDK tarball with manifest.py and bitmap.py beside it.
"""

import argparse
import re
import struct
import subprocess
import sys
import tomllib
from pathlib import Path

import bitmap
import manifest

MAGIC = 0x30585652  # RVX0
FORMAT = 1
HEADER = struct.Struct("<7I")
ENTRY = struct.Struct("<IBBBBIII")
MODULE = struct.Struct("<5I")
HEADER_FIELDS = (
    "magic",
    "format",
    "api",
    "firmware",
    "manifest",
    "entries",
    "body_hash",
)
ENTRY_FIELDS = (
    "name",
    "kind",
    "codec",
    "preload",
    "reserved",
    "offset",
    "stored",
    "bytes",
)
MODULE_FIELDS = ("text", "data", "bss", "relocations", "entry")
KIND_MODULE, KIND_DATA, KIND_BITMAP = 0, 1, 2
KINDS = ("module", "data", "bitmap")
PRELOAD_ALWAYS, PRELOAD_OPTIONAL, PRELOAD_NEVER = 0, 1, 2
PRELOAD = ("always", "optional", "never")
RAW, LZ = 0, 1
WINDOW = 4096
MIN_MATCH = 3
CHAIN = 48  # earlier positions tried per match search
MANIFEST_MAGIC = 0x464D5652  # RVMF
MANIFEST_VERSION = 3
SYMBOLS = ("__data_start", "__data_end", "__bss_end", "rv_module_entry")
RELOCATION_SECTIONS = {
    ".rel.text",
    ".rel.rodata",
    ".rel.data",
    ".rela.text",
    ".rela.rodata",
    ".rela.data",
}
# Position-independent by construction; nothing to fix up at load time.
RELATIVE_RELOCATIONS = {
    "R_ARM_CALL",
    "R_ARM_JUMP24",
    "R_ARM_THM_CALL",
    "R_ARM_THM_JUMP24",
    "R_ARM_THM_JUMP11",
    "R_ARM_THM_JUMP8",
    "R_ARM_PREL31",
    "R_ARM_NONE",
    "R_ARM_V4BX",  # marks a bx for ARMv4 linkers; libgcc's division helpers carry it
}


def fnv1a(data):
    """FNV-1a: catalog names (the core hashes them the same way) and the
    header's body hash, which the core compares to tell a replaced file from
    the one it loaded."""
    value = 2166136261
    for byte in data:
        value = (value ^ byte) * 16777619 & 0xFFFFFFFF
    return value


def padded(data):
    return data + bytes(-len(data) % 4)


def lz_match(data, i, positions):
    """Longest match for data[i:] among earlier positions in the window."""
    best_length = best_distance = 0
    limit = len(data) - i
    for j in reversed(positions[-CHAIN:]):
        distance = i - j
        if distance > WINDOW:
            break
        length = MIN_MATCH
        while length < limit and data[j + length] == data[i + length]:
            length += 1
        if length > best_length:
            best_length, best_distance = length, distance
            if length == limit:
                break
    return best_length, best_distance


def lz_encode(data):
    """The lz stream for `data`: greedy matching with one step of lookahead."""
    heads = {}
    items = []

    def remember(i):
        if i + MIN_MATCH <= len(data):
            chain = heads.setdefault(data[i : i + MIN_MATCH], [])
            chain.append(i)
            if len(chain) > 2 * CHAIN:
                del chain[:CHAIN]

    def search(i):
        if i + MIN_MATCH > len(data):
            return 0, 0
        return lz_match(data, i, heads.get(data[i : i + MIN_MATCH], []))

    i = 0
    pending = search(0)
    while i < len(data):
        length, distance = pending
        if length:
            remember(i)
            ahead = search(i + 1)
            if ahead[0] > length + 1:
                items.append(bytes((data[i],)))
                i += 1
                pending = ahead
                continue
            extra = length - MIN_MATCH
            nibble = min(extra, 15)
            match = bytes((nibble << 4 | (distance - 1) >> 8, (distance - 1) & 0xFF))
            if nibble == 15:
                extra -= 15
                match += b"\xff" * (extra // 255) + bytes((extra % 255,))
            items.append(match)
            for k in range(i + 1, i + length):
                remember(k)
            i += length
        else:
            items.append(bytes((data[i],)))
            remember(i)
            i += 1
        pending = search(i)
    out = bytearray()
    for group in range(0, len(items), 8):
        chunk = items[group : group + 8]
        out.append(sum(1 << bit for bit, item in enumerate(chunk) if len(item) > 1))
        for item in chunk:
            out += item
    return bytes(out)


def lz_decode(stream, size):
    """`size` bytes decoded from an lz stream, which must end exactly there."""
    out = bytearray()
    at = 0
    try:
        while len(out) < size:
            flags = stream[at]
            at += 1
            for bit in range(8):
                if len(out) == size:
                    break
                if not flags >> bit & 1:
                    out.append(stream[at])
                    at += 1
                    continue
                high, low = stream[at], stream[at + 1]
                at += 2
                length = (high >> 4) + MIN_MATCH
                distance = ((high & 15) << 8 | low) + 1
                if high >> 4 == 15:
                    while True:
                        more = stream[at]
                        at += 1
                        length += more
                        if more != 255:
                            break
                if distance > len(out) or len(out) + length > size:
                    raise ValueError("lz match outside the output")
                for _ in range(length):
                    out.append(out[-distance])
    except IndexError:
        raise ValueError("lz stream ends early") from None
    if at != len(stream):
        raise ValueError("lz stream continues past its output")
    return bytes(out)


def api_word(version):
    major, minor = (int(part) for part in version.split("."))
    return major << 16 | minor


def run(command):
    return subprocess.check_output([str(c) for c in command], text=True)


def symbols(prefix, elf):
    values = {}
    for line in run([prefix + "readelf", "-sW", elf]).splitlines():
        parts = line.split()
        if len(parts) == 8 and parts[0].rstrip(":").isdigit() and parts[6] != "UND":
            values[parts[7]] = int(parts[1], 16)
    return values


def absolute_symbols(symbol_table):
    """Names of SHN_ABS symbols in `readelf -sW` output: the firmware's, from
    --just-symbols. Words that point at them stay as linked."""
    names = set()
    for line in symbol_table.splitlines():
        parts = line.split()
        if len(parts) == 8 and parts[0].rstrip(":").isdigit() and parts[6] == "ABS":
            names.add(parts[7])
    return names


def abs32_offsets(relocation_table, absolute):
    """Offsets of the R_ARM_ABS32 words in `readelf -rW` output that the loader
    must move with the image: every one except those naming an absolute symbol."""
    offsets = []
    section = None
    for line in relocation_table.splitlines():
        match = re.match(r"Relocation section '(\S+)'", line)
        if match:
            section = match.group(1)
            continue
        parts = line.split()
        if len(parts) < 3 or not re.fullmatch(r"[0-9a-f]+", parts[0]):
            continue
        if section not in RELOCATION_SECTIONS:
            continue
        kind = parts[2]
        if kind == "R_ARM_ABS32":
            if len(parts) < 5 or parts[4] not in absolute:
                offsets.append(int(parts[0], 16))
        elif kind not in RELATIVE_RELOCATIONS:
            raise ValueError(f"unsupported relocation {kind} at {parts[0]}")
    return sorted(set(offsets))


def relocations(prefix, elf):
    absolute = absolute_symbols(run([prefix + "readelf", "-sW", elf]))
    return abs32_offsets(run([prefix + "readelf", "-rW", elf]), absolute)


def module_image(values, image, offsets):
    """The payload of one module: struct rvx_module, text and data, then the
    relocation table. `values` are its symbols, `image` the raw binary from
    address 0."""
    for name in SYMBOLS:
        if name not in values:
            raise ValueError(f"missing symbol {name}")
    data_start, data_end, bss_end, entry = (values[n] for n in SYMBOLS)
    if (
        not 0 <= data_start <= data_end <= bss_end
        or data_start % 4
        or data_end % 4
        or bss_end % 4
    ):
        raise ValueError("segment layout is not text, data, bss in order")
    if (entry & ~1) >= data_start:
        raise ValueError(f"entry outside text: {entry:#x}")
    for offset in offsets:
        if offset % 4 or offset > data_end - 4:
            raise ValueError(f"relocation outside image: {offset:#x}")
    body = bytes(image).ljust(data_end, b"\0")[:data_end]
    fields = (
        data_start,
        data_end - data_start,
        bss_end - data_end,
        len(offsets),
        entry,
    )
    return MODULE.pack(*fields) + body + b"".join(struct.pack("<I", o) for o in offsets)


def asset_payload(path, pane=None):
    """Kind and bytes of an asset file: a .png becomes a bitmap, checked as the
    pane icon, picture or badge `pane` names; the rest is data."""
    data = Path(path).read_bytes()
    if Path(path).suffix.lower() == ".png":
        return KIND_BITMAP, bitmap.png_to_bmap(data, pane)
    return KIND_DATA, data


def manifest_modules(compiled):
    """The module count of a compiled manifest, after checking its header."""
    if len(compiled) < 20 or struct.unpack_from("<II", compiled) != (
        MANIFEST_MAGIC,
        MANIFEST_VERSION,
    ):
        raise ValueError(f"not a version {MANIFEST_VERSION} manifest")
    return struct.unpack_from("<I", compiled, 16)[0]


def manifest_module_classes(compiled):
    """The preload class of each module of a compiled manifest, in its order."""
    count, sites = struct.unpack_from("<II", compiled, 16)
    table = manifest.HEADER.size + sites * manifest.SITE.size
    return [
        manifest.MODULE.unpack_from(compiled, table + i * manifest.MODULE.size)[4]
        for i in range(count)
    ]


def pack_extension(compiled, modules, assets, api, firmware):
    """The .rvx bytes and its entries in catalog order. `modules` are
    (name, payload) pairs in manifest order; `assets` are dictionaries with
    name, kind, data, preload (a PRELOAD name) and compress."""
    if len(modules) != manifest_modules(compiled):
        raise ValueError(
            f"the manifest lists {manifest_modules(compiled)} modules, got {len(modules)}"
        )
    entries = [
        {
            "name": name,
            "kind": KIND_MODULE,
            "codec": RAW,
            "stored": payload,
            "bytes": len(payload),
            "preload": PRELOAD_ALWAYS,
        }
        for name, payload in modules
    ]
    for asset in sorted(assets, key=lambda a: PRELOAD.index(a["preload"])):
        data, codec = asset["data"], RAW
        if asset["compress"]:
            packed = lz_encode(data)
            if len(packed) < len(data):
                data, codec = packed, LZ
        entries.append(
            {
                "name": asset["name"],
                "kind": asset["kind"],
                "codec": codec,
                "stored": data,
                "bytes": len(asset["data"]),
                "preload": PRELOAD.index(asset["preload"]),
            }
        )
    seen = {}
    for entry in entries:
        other = seen.setdefault(fnv1a(entry["name"].encode()), entry)
        if other is not entry:
            raise ValueError(
                f"{entry['name']!r} and {other['name']!r} have the same catalog name"
            )
    count = len(modules)
    preloaded = sum(a["preload"] != "never" for a in assets)
    # Payloads in load order: main, the assets that may be preloaded with it,
    # the modules that may be kept with it, the other modules, the other assets.
    classes = manifest_module_classes(compiled)
    kept = [i for i in range(1, count) if classes[i] != PRELOAD_NEVER]
    order = [
        0,
        *range(count, count + preloaded),
        *kept,
        *(i for i in range(1, count) if i not in kept),
        *range(count + preloaded, len(entries)),
    ]
    offset = HEADER.size + len(padded(compiled)) + len(entries) * ENTRY.size
    for index in order:
        entries[index]["offset"] = offset
        offset += len(padded(entries[index]["stored"]))
    catalog = b"".join(
        ENTRY.pack(
            fnv1a(e["name"].encode()),
            e["kind"],
            e["codec"],
            e["preload"],
            0,
            e["offset"],
            len(e["stored"]),
            e["bytes"],
        )
        for e in entries
    )
    payloads = b"".join(padded(entries[index]["stored"]) for index in order)
    body = padded(compiled) + catalog + payloads
    header = HEADER.pack(
        MAGIC,
        FORMAT,
        api,
        firmware,
        len(compiled),
        len(entries),
        fnv1a(body),
    )
    return header + body, entries


def unpack(data):
    """Header and catalog of a .rvx, checked as the core checks them; module
    entries carry their struct rvx_module fields."""
    if len(data) < HEADER.size:
        raise ValueError("short header")
    header = dict(zip(HEADER_FIELDS, HEADER.unpack_from(data)))
    if header["magic"] != MAGIC or header["format"] != FORMAT:
        raise ValueError(f"not an rvx format {FORMAT} file")
    compiled = data[HEADER.size : HEADER.size + header["manifest"]]
    count = manifest_modules(compiled)
    at = HEADER.size + ((header["manifest"] + 3) & ~3)
    end = at + header["entries"] * ENTRY.size
    if header["entries"] < count:
        raise ValueError("catalog does not hold the manifest's modules")
    if end > len(data):
        raise ValueError("catalog past the end of the file")
    entries = []
    for index in range(header["entries"]):
        entry = dict(
            zip(ENTRY_FIELDS, ENTRY.unpack_from(data, at + index * ENTRY.size))
        )
        if (entry["kind"] == KIND_MODULE) != (index < count) or entry[
            "kind"
        ] > KIND_BITMAP:
            raise ValueError(f"entry {index} has the wrong kind")
        if (
            entry["codec"] > LZ
            or entry["codec"] == RAW
            and entry["stored"] != entry["bytes"]
        ):
            raise ValueError(f"entry {index} has a bad codec")
        if (
            entry["offset"] % 4
            or entry["offset"] < end
            or entry["offset"] + entry["stored"] > len(data)
        ):
            raise ValueError(f"entry {index} payload outside the file")
        if index < count:
            if entry["codec"] != RAW or entry["stored"] < MODULE.size:
                raise ValueError(f"module {index} is not a raw module payload")
            module = dict(zip(MODULE_FIELDS, MODULE.unpack_from(data, entry["offset"])))
            if (
                MODULE.size
                + module["text"]
                + module["data"]
                + 4 * module["relocations"]
                != entry["stored"]
            ):
                raise ValueError(f"module {index} size differs from its header")
            entry.update(module)
        if index < count:
            if entry["preload"]:
                raise ValueError(f"module {index} has a preload class")
        elif entry["preload"] > PRELOAD_NEVER or (
            index > count and entry["preload"] < entries[-1]["preload"]
        ):
            raise ValueError(f"entry {index} has a preload class out of order")
        entries.append(entry)
    return header, entries


def payload(data, entry):
    """The decoded bytes of a catalog entry."""
    stored = data[entry["offset"] : entry["offset"] + entry["stored"]]
    return stored if entry["codec"] == RAW else lz_decode(stored, entry["bytes"])


def read_module(prefix, elf, scratch):
    values = symbols(prefix, elf)
    run([prefix + "objcopy", "-O", "binary", elf, scratch])
    image = scratch.read_bytes()
    scratch.unlink()
    return module_image(values, image, relocations(prefix, elf))


def warnings(toml, document):
    if toml.resolve().parent.name not in ("", document.get("id")):
        yield f"directory is not named {document.get('id')}"
    for site in document.get("site", []):
        yield (
            f"raw site {site.get('name')} at {site.get('address', 0):#x};"
            " ask for a core site so others can share it"
        )


def pack(prefix, out, toml, elves, api, firmware):
    document = tomllib.loads(toml.read_text())
    for warning in warnings(toml, document):
        print(f"warning: {warning}", file=sys.stderr)
    names = [module["name"] for module in document["module"]]
    if len(elves) != len(names):
        raise ValueError(
            f"the manifest lists {len(names)} modules, got {len(elves)} ELFs"
        )
    scratch = out.with_suffix(".bin")
    modules = [
        (name, read_module(prefix, elf, scratch)) for name, elf in zip(names, elves)
    ]
    assets = []
    for asset in manifest.assets(document):
        kind, data = asset_payload(toml.parent / asset["file"], asset["pane"])
        assets.append({**asset, "kind": kind, "data": data})
    pictures = {bitmap.bmap_size(a["data"]) for a in assets if a["pane"] == "picture"}
    compiled = manifest.compile_manifest(document, pictures)
    packed, entries = pack_extension(compiled, modules, assets, api, firmware)
    checked_entries = unpack(packed)[1]
    classes = manifest_module_classes(compiled)
    out.write_bytes(packed)
    for index, (entry, checked) in enumerate(zip(entries, checked_entries)):
        if checked["kind"] == KIND_MODULE:
            detail = (
                f"text {checked['text']}, data {checked['data']}, "
                f"bss {checked['bss']}, {checked['relocations']} relocations, "
                f"preload {PRELOAD[classes[index]]}"
            )
        else:
            codec = "lz" if checked["codec"] == LZ else "raw"
            detail = f"{KINDS[checked['kind']]}, preload {PRELOAD[checked['preload']]}, {checked['stored']} bytes stored ({codec}), {checked['bytes']} decoded"
        print(f"{out.name} {entry['name']}: {detail}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("out", type=Path)
    parser.add_argument("toml", type=Path, metavar="manifest.toml")
    parser.add_argument("elves", type=Path, nargs="+", metavar="elf")
    parser.add_argument("--prefix", default="arm-elf-eabi-")
    parser.add_argument("--api", required=True, help="core API major.minor")
    parser.add_argument(
        "--firmware",
        required=True,
        type=lambda v: int(v, 0),
        help="firmware build id (RV_FIRMWARE_ID)",
    )
    args = parser.parse_args()
    try:
        pack(
            args.prefix,
            args.out,
            args.toml,
            args.elves,
            api_word(args.api),
            args.firmware,
        )
    except ValueError as error:
        sys.exit(f"rvx-pack: {error}")


if __name__ == "__main__":
    sys.exit(main())
