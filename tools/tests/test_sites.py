# SPDX-License-Identifier: GPL-3.0-only
"""Site table decoding and the entry-site rules the dispatcher relies on."""

import struct
import unittest

import sites
from sites import CALL, ENTRY, ENTRY_NORETURN, VTABLE, WORD, Site

BASE = 0x08000000
PUSH_LR = 0xE92D4010  # push {r4, lr}
MOV_R4_R0 = 0xE1A04000
BL_ANY = 0xEB000000


def site(address, kind, word0, word1=0, chain=0, argc=1, original=0):
    return Site(address, original, word0, word1, kind, chain, argc)


def image(words, size=256):
    body = bytearray(size)
    for address, word in words.items():
        struct.pack_into("<I", body, address - BASE, word)
    return bytes(body)


class SiteTests(unittest.TestCase):
    def test_reads_rows_from_the_linked_image(self):
        rows = [
            sites.ROW.pack(BASE + 8, 0x08B3C001, 0xEB000001, 0, CALL, 1, 2, 0),
            sites.ROW.pack(BASE + 16, 0, 0xE1A00000, PUSH_LR, ENTRY, 0, 1, 0),
        ]
        code = struct.pack("<I", 2) + b"".join(rows)
        symbols = {"core_site_count": 0x1000, "core_sites": 0x1004}
        table = sites.read(code, symbols, 0x1000)
        self.assertEqual(
            table,
            [
                Site(BASE + 8, 0x08B3C001, 0xEB000001, 0, CALL, 1, 2),
                Site(BASE + 16, 0, 0xE1A00000, PUSH_LR, ENTRY, 0, 1),
            ],
        )
        self.assertEqual(table[1].expected, struct.pack("<II", 0xE1A00000, PUSH_LR))
        bad = struct.pack("<I", 1) + sites.ROW.pack(BASE, 0, 0, 0, 7, 0, 1, 0)
        with self.assertRaises(ValueError):
            sites.read(bad, symbols, 0x1000)

    def test_matches_expected_words_against_the_firmware(self):
        table = [site(BASE + 8, CALL, 0xEB000001), site(BASE + 16, VTABLE, 0x08123456)]
        good = image({BASE + 8: 0xEB000001, BASE + 16: 0x08123456})
        sites.verify(table, good, BASE)
        with self.assertRaisesRegex(ValueError, "does not match"):
            sites.verify(table, image({BASE + 8: 0xEB000001}), BASE)
        with self.assertRaisesRegex(ValueError, "outside"):
            sites.verify([site(BASE + 512, WORD, 0)], good, BASE)
        for table in (
            [site(BASE + 8, CALL, 0), site(BASE + 8, CALL, 0)],
            [site(BASE + 2, CALL, 0)],
            [site(BASE + 8, WORD, 0, chain=1)],
            [site(BASE + 8, ENTRY_NORETURN, PUSH_LR, MOV_R4_R0, chain=1)],
        ):
            with self.subTest(table=table), self.assertRaises(ValueError):
                sites.verify(table)

    def test_rejects_entry_words_the_replay_stub_cannot_run(self):
        cases = {
            "conditional": 0x01A04000,  # moveq r4, r0
            "branch": BL_ANY,
            "pc as rd": 0xE1A0F000,  # mov pc, r0
            "pc as rn": 0xE59F1010,  # ldr r1, [pc, #16]
            "pc as rm": 0xE1A0400F,  # mov r4, pc
            "pc as rs": 0xE1A04F10,  # mov r4, r0, lsl pc
            "ldm with pc": 0xE8BD8010,  # pop {r4, pc}
            "ldm from pc": 0xE89F0010,  # ldm pc, {r4}
            "coprocessor": 0xEE070F15,
            "swi": 0xEF000000,
        }
        for name, word in cases.items():
            with self.subTest(name), self.assertRaises(ValueError):
                sites.verify([site(BASE + 8, ENTRY, PUSH_LR, word)])
        sites.verify([site(BASE + 8, ENTRY, PUSH_LR, MOV_R4_R0)])
        sites.verify([site(BASE + 8, ENTRY, 0xE3A01000, PUSH_LR)])  # mov r1, #0

    def test_rejects_entries_with_a_branch_into_the_second_word(self):
        entry = BASE + 64
        words = {entry: PUSH_LR, entry + 4: MOV_R4_R0}
        table = [site(entry, ENTRY, PUSH_LR, MOV_R4_R0)]
        sites.verify(table, image(words), BASE)
        # b entry+4 from BASE+128: offset = (entry+4 - (BASE+128) - 8) >> 2
        words[BASE + 128] = 0xEA000000 | (
            (((entry + 4) - (BASE + 128) - 8) >> 2) & 0xFFFFFF
        )
        with self.assertRaisesRegex(ValueError, "entry\\+4"):
            sites.verify(table, image(words), BASE)
        # a blx-immediate (cond 0xF) to the same place is not a branch Apple code resumes from
        words[BASE + 128] |= 0xF0000000
        sites.verify(table, image(words), BASE)
