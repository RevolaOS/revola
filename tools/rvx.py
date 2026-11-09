#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""rvx-pack: turn an ELF linked at address 0 into a flat .rvx module image.

    rvx-pack module.elf module.rvx --module ext/main --api 1.0 --firmware 0x660eb6ff

The ELF is linked with the SDK linker script and --emit-relocs, so the
R_ARM_ABS32 entries survive; text, rodata, data and bss form one segment.
Ships in the SDK tarball, so it depends on nothing outside this file.
"""

import argparse
import re
import struct
import subprocess
import sys
from pathlib import Path

MAGIC = 0x30585652  # RVX0
FORMAT = 1
HEADER = struct.Struct("<10I")
FIELDS = (
    "magic",
    "format",
    "api",
    "firmware",
    "module",
    "text",
    "data",
    "bss",
    "relocations",
    "entry",
)
SYMBOLS = ("__data_start", "__data_end", "__bss_end", "rvx_entry")
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
}


def fnv1a(text):
    hash_ = 2166136261
    for byte in text.encode():
        hash_ = ((hash_ ^ byte) * 16777619) & 0xFFFFFFFF
    return hash_


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


def pack_image(values, image, offsets, api, firmware, module):
    """Header, text+data and the relocation table for an image whose symbols are
    `values`; `image` is the raw binary from address 0."""
    for name in SYMBOLS:
        if name not in values:
            raise ValueError(f"missing symbol {name}")
    data_start, data_end, bss_end, entry = (values[n] for n in SYMBOLS)
    if not 0 <= data_start <= data_end <= bss_end or data_end % 4 or bss_end % 4:
        raise ValueError("segment layout is not text, data, bss in order")
    if (entry & ~1) >= data_start:
        raise ValueError(f"entry outside text: {entry:#x}")
    for offset in offsets:
        if offset % 4 or offset > data_end - 4:
            raise ValueError(f"relocation outside image: {offset:#x}")
    body = bytes(image).ljust(data_end, b"\0")[:data_end]
    header = HEADER.pack(
        MAGIC,
        FORMAT,
        api,
        firmware,
        module,
        data_start,
        data_end - data_start,
        bss_end - data_end,
        len(offsets),
        entry,
    )
    return header + body + b"".join(struct.pack("<I", o) for o in offsets)


def unpack_header(data):
    if len(data) < HEADER.size:
        raise ValueError("short header")
    header = dict(zip(FIELDS, HEADER.unpack_from(data)))
    if header["magic"] != MAGIC or header["format"] != FORMAT:
        raise ValueError("not an rvx v1 image")
    return header


def pack(prefix, elf, out, api, firmware, module):
    values = symbols(prefix, elf)
    raw = out.with_suffix(".bin")
    run([prefix + "objcopy", "-O", "binary", elf, raw])
    image = raw.read_bytes()
    raw.unlink()
    offsets = relocations(prefix, elf)
    packed = pack_image(values, image, offsets, api, firmware, module)
    out.write_bytes(packed)
    header = unpack_header(packed)
    print(
        f"{out.name}: text {header['text']}, data {header['data']}, "
        f"bss {header['bss']}, {header['relocations']} relocations"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("elf", type=Path)
    parser.add_argument("out", type=Path)
    parser.add_argument("--prefix", default="arm-elf-eabi-")
    parser.add_argument("--api", required=True, help="core API major.minor")
    parser.add_argument(
        "--firmware",
        required=True,
        type=lambda v: int(v, 0),
        help="firmware build id (RV_FIRMWARE_ID)",
    )
    parser.add_argument("--module", required=True, help="<extension id>/<module name>")
    args = parser.parse_args()
    pack(
        args.prefix,
        args.elf,
        args.out,
        api_word(args.api),
        args.firmware,
        fnv1a(args.module),
    )


if __name__ == "__main__":
    sys.exit(main())
