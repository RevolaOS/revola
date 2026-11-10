# SPDX-License-Identifier: GPL-3.0-only
"""The rvx v1 image layout and the checks rvx-pack makes before writing one."""

import struct
import unittest

import rvx

SYMBOLS = {
    "__data_start": 0x40,
    "__data_end": 0x50,
    "__bss_end": 0x58,
    "rvx_entry": 0x11,
}


class RvxTests(unittest.TestCase):
    def test_header_then_image_then_relocations(self):
        image = bytes(range(0x48))  # shorter than data_end: padded with zeros
        packed = rvx.pack_image(SYMBOLS, image, [0x44, 0x4C], 0x10002, 0x660EB6FF, 7)
        header = rvx.unpack_header(packed)
        self.assertEqual(
            header,
            {
                "magic": rvx.MAGIC,
                "format": 1,
                "api": 0x10002,
                "firmware": 0x660EB6FF,
                "module": 7,
                "text": 0x40,
                "data": 0x10,
                "bss": 8,
                "relocations": 2,
                "entry": 0x11,
            },
        )
        self.assertEqual(packed[40 : 40 + 0x50], image + bytes(8))
        self.assertEqual(packed[40 + 0x50 :], struct.pack("<II", 0x44, 0x4C))

    def test_rejects_bad_layouts_and_relocations(self):
        cases = {
            "unaligned relocation": (SYMBOLS, [0x42]),
            "relocation past the image": (SYMBOLS, [0x4D]),
            "entry in data": ({**SYMBOLS, "rvx_entry": 0x51}, []),
            "bss before data": ({**SYMBOLS, "__bss_end": 0x4C}, []),
            "missing symbol": (
                {k: v for k, v in SYMBOLS.items() if k != "rvx_entry"},
                [],
            ),
        }
        for name, (values, offsets) in cases.items():
            with self.subTest(name), self.assertRaises(ValueError):
                rvx.pack_image(values, bytes(0x50), offsets, 0, 0, 0)

    def test_api_and_module_identity(self):
        self.assertEqual(rvx.api_word("1.3"), 0x10003)
        self.assertEqual(rvx.fnv1a("core"), 0xDD5E607E)  # CORE_SETTINGS_OWNER_CORE
        with self.assertRaises(ValueError):
            rvx.unpack_header(b"RVX0" + bytes(36))  # format 0

    def test_words_naming_firmware_symbols_stay_as_linked(self):
        symbols = (
            "     2: 000005b4     0 SECTION LOCAL  DEFAULT    3 .data\n"
            "    79: 08259b28     0 NOTYPE  GLOBAL DEFAULT  ABS GraphicsContext_SetColor\n"
        )
        table = (
            "Relocation section '.rel.text' at offset 0x13c1c contains 3 entries:\n"
            " Offset     Info    Type                Sym. Value  Symbol's Name\n"
            "00000014  00000202 R_ARM_ABS32            000005b4   .data\n"
            "0000019c  00004f02 R_ARM_ABS32            08259b28   GraphicsContext_SetColor\n"
            "000001a0  00000102 R_ARM_ABS32            00000000   .text\n"
            "000001a8  00000028 R_ARM_V4BX            \n"
        )
        absolute = rvx.absolute_symbols(symbols)
        self.assertEqual(absolute, {"GraphicsContext_SetColor"})
        self.assertEqual(rvx.abs32_offsets(table, absolute), [0x14, 0x1A0])
