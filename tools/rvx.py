#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Pack a relocatable ELF linked at address 0 into a flat .rvx module image.

    python3 tools/rvx.py module.elf module.rvx --prefix arm-elf-eabi-

The ELF must be linked with --emit-relocs so the R_ARM_ABS32 entries survive.
"""

import argparse
import re
import struct
import subprocess
import sys
from pathlib import Path

MAGIC = 0x30585652
HEADER = struct.Struct("<6I")


def run(command):
    return subprocess.check_output([str(c) for c in command], text=True)


def symbols(prefix, elf):
    values = {}
    for line in run([prefix + "readelf", "-sW", elf]).splitlines():
        parts = line.split()
        if len(parts) == 8 and parts[0].rstrip(":").isdigit() and parts[6] != "UND":
            values[parts[7]] = int(parts[1], 16)
    return values


def relocations(prefix, elf):
    offsets = []
    section = None
    for line in run([prefix + "readelf", "-rW", elf]).splitlines():
        match = re.match(r"Relocation section '(\S+)'", line)
        if match:
            section = match.group(1)
            continue
        parts = line.split()
        if len(parts) < 3 or not re.fullmatch(r"[0-9a-f]+", parts[0]):
            continue
        kind = parts[2]
        if section in (
            ".rel.text",
            ".rel.rodata",
            ".rel.data",
            ".rela.text",
            ".rela.rodata",
            ".rela.data",
        ):
            if kind == "R_ARM_ABS32":
                offsets.append(int(parts[0], 16))
            elif kind not in (
                "R_ARM_CALL",
                "R_ARM_JUMP24",
                "R_ARM_THM_CALL",
                "R_ARM_THM_JUMP24",
                "R_ARM_PREL31",
                "R_ARM_NONE",
                "R_ARM_THM_JUMP11",
                "R_ARM_THM_JUMP8",
            ):
                raise SystemExit(f"unsupported relocation {kind} at {parts[0]}")
    return sorted(set(offsets))


def pack(prefix, elf, out):
    values = symbols(prefix, elf)
    data_start, data_end, bss_end = (
        values[n] for n in ("__data_start", "__data_end", "__bss_end")
    )
    entry = values["rvx_entry"]
    raw = out.with_suffix(".bin")
    run([prefix + "objcopy", "-O", "binary", elf, raw])
    image = raw.read_bytes().ljust(data_end, b"\0")[:data_end]
    raw.unlink()
    offsets = relocations(prefix, elf)
    for offset in offsets:
        if offset > data_end - 4 or offset % 4:
            raise SystemExit(f"relocation outside image: {offset:#x}")
    header = HEADER.pack(
        MAGIC,
        data_start,
        data_end - data_start,
        bss_end - data_end,
        len(offsets),
        entry,
    )
    out.write_bytes(header + image + b"".join(struct.pack("<I", o) for o in offsets))
    print(
        f"{out.name}: text {data_start}, data {data_end - data_start}, "
        f"bss {bss_end - data_end}, {len(offsets)} relocations"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("elf", type=Path)
    parser.add_argument("out", type=Path)
    parser.add_argument("--prefix", default="arm-elf-eabi-")
    args = parser.parse_args()
    pack(args.prefix, args.elf, args.out)


if __name__ == "__main__":
    sys.exit(main())
