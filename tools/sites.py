# SPDX-License-Identifier: GPL-3.0-only
"""Read the core's hook site table from its image and check it against the firmware."""

import struct
from array import array
from dataclasses import dataclass

from symbols import require

ROW = struct.Struct("<4I4B")
CALL, VTABLE, ENTRY, ENTRY_NORETURN, WORD = range(5)
LDR_PC = 0xE51FF004
PC = 15


@dataclass(frozen=True)
class Site:
    address: int
    original: int
    word0: int
    word1: int
    kind: int
    chain: int
    argc: int
    copy: int = 0

    @property
    def is_entry(self):
        return self.kind in (ENTRY, ENTRY_NORETURN)

    @property
    def expected(self):
        words = (self.word0, self.word1) if self.is_entry else (self.word0,)
        return b"".join(struct.pack("<I", word) for word in words)


def read(code, symbols, core_start):
    """Rows of core_sites as laid out in the linked image."""
    count_offset = require(symbols, "core_site_count") - core_start
    count = struct.unpack_from("<I", code, count_offset)[0]
    offset = require(symbols, "core_sites") - core_start
    sites = []
    for _ in range(count):
        address, original, word0, word1, kind, chain, argc, copy = ROW.unpack_from(
            code, offset
        )
        if kind > WORD or argc > 4 or chain > 1:
            raise ValueError(f"Malformed site row at {address:#x}")
        if copy and (copy < 4 or copy > 11 or argc < 1 or kind == WORD):
            raise ValueError(
                f"Site {address:#x}: register copy needs r4-r11 and an argument"
            )
        sites.append(Site(address, original, word0, word1, kind, chain, argc, copy))
        offset += ROW.size
    return sites


def registers(word):
    """Registers an ARM instruction reads or writes, or None when it is not allowed
    to be displaced: branches, coprocessor and software interrupt encodings."""
    group = (word >> 25) & 7
    if group in (0, 1):  # data processing, multiply, MRS/MSR, swap, halfword loads
        used = {(word >> 16) & 15, (word >> 12) & 15}
        if group == 0:
            used.add(word & 15)
            if word & 0x10:
                used.add((word >> 8) & 15)
        return used
    if group in (2, 3):  # load/store word or byte
        used = {(word >> 16) & 15, (word >> 12) & 15}
        if group == 3:
            if word & 0x10:
                return None  # undefined space
            used.add(word & 15)
        return used
    if group == 4:  # LDM/STM
        used = {(word >> 16) & 15}
        if word & (1 << PC):
            used.add(PC)
        return used
    return None


def check_entry_word(site, word, image_words=None):
    if word >> 28 != 0xE:
        raise ValueError(
            f"Entry site {site.address:#x}: conditional instruction {word:#010x}"
        )
    used = registers(word)
    if used is None:
        raise ValueError(
            f"Entry site {site.address:#x}: branch or system instruction {word:#010x}"
        )
    if PC in used:
        raise ValueError(
            f"Entry site {site.address:#x}: instruction uses pc {word:#010x}"
        )


def branch_targets_into(image_words, base, target):
    """True when any B/BL in the image lands on `target`."""
    for index, word in enumerate(image_words):
        if (word >> 25) & 7 != 5 or word >> 28 == 0xF:
            continue
        offset = word & 0xFFFFFF
        if offset & 0x800000:
            offset -= 1 << 24
        if base + index * 4 + 8 + (offset << 2) == target:
            return True
    return False


def verify(sites, image=None, base=0x08000000, load_offset=0):
    """Reject rows the dispatcher cannot handle; compare with the firmware if given."""
    seen = set()
    words = None
    if image is not None:
        body = image[load_offset:]
        words = array("I", body[: len(body) - len(body) % 4])
    for site in sites:
        if site.address % 4:
            raise ValueError(f"Unaligned site {site.address:#x}")
        if site.address in seen:
            raise ValueError(f"Duplicate site {site.address:#x}")
        seen.add(site.address)
        if site.is_entry:
            for word in (site.word0, site.word1):
                check_entry_word(site, word)
            if site.chain and site.kind == ENTRY_NORETURN:
                raise ValueError(
                    f"Entry site {site.address:#x}: noreturn sites are exclusive"
                )
        if site.kind == WORD and site.chain:
            raise ValueError(f"Word site {site.address:#x} cannot chain")
        if words is None:
            continue
        offset = site.address - base
        if offset < 0 or offset + len(site.expected) > len(body):
            raise ValueError(f"Site outside firmware: {site.address:#x}")
        if body[offset : offset + len(site.expected)] != site.expected:
            raise ValueError(f"Site {site.address:#x} does not match the firmware")
        if site.is_entry and branch_targets_into(words, base, site.address + 4):
            raise ValueError(f"Entry site {site.address:#x}: a branch targets entry+4")
