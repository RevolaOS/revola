# SPDX-License-Identifier: GPL-3.0-only
"""PNG to BMap: header, indexed palette and BGRA pixels."""

import struct
import unittest
import zlib

import bitmap


def png(width, height, rgba):
    def chunk(kind, body):
        return (
            struct.pack(">I", len(body))
            + kind
            + body
            + struct.pack(">I", zlib.crc32(kind + body))
        )

    rows = b"".join(
        b"\0" + rgba[y * width * 4 : (y + 1) * width * 4] for y in range(height)
    )
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(rows))
        + chunk(b"IEND", b"")
    )


class BitmapTests(unittest.TestCase):
    def test_few_colours_are_indexed(self):
        red, clear = bytes((255, 0, 0, 255)), bytes((0, 0, 255, 0))
        data = bitmap.png_to_bmap(png(3, 2, red + clear + red + clear + clear + red))
        fmt, _, stride, depth, top, left, bottom, right, size = (
            bitmap.HEADER.unpack_from(data)
        )
        self.assertEqual((fmt, stride, depth), (bitmap.INDEXED8, 4, 8))
        self.assertEqual((top, left, bottom, right), (0, 0, 2, 3))
        payload = data[bitmap.HEADER.size :]
        self.assertEqual(len(payload), size)
        self.assertEqual(
            payload[:12], struct.pack("<I", 2) + bytes((0, 0, 255, 255, 255, 0, 0, 0))
        )
        self.assertEqual(payload[12:], bytes((0, 1, 0, 0, 1, 1, 0, 0)))

    def test_many_colours_are_bgra(self):
        rgba = b"".join(bytes((i, 255 - i, i // 2, 200)) for i in range(256)) + bytes(4)
        data = bitmap.png_to_bmap(png(257, 1, rgba))
        fmt, _, stride, depth, *_, size = bitmap.HEADER.unpack_from(data)
        self.assertEqual(
            (fmt, stride, depth, size), (bitmap.BGRA8888, 257 * 4, 32, 257 * 4)
        )
        self.assertEqual(
            data[bitmap.HEADER.size : bitmap.HEADER.size + 4], bytes((0, 255, 0, 200))
        )

    def test_pane_icon_is_coverage_from_alpha(self):
        rgba = bytearray(bytes((200, 0, 0, 0)) * 78 * 121)
        rgba[4 * 79 : 4 * 80] = bytes((10, 20, 30, 128))  # row 1, column 1
        data = bitmap.png_to_bmap(png(78, 121, bytes(rgba)), pane="icon")
        fmt, _, stride, depth, top, left, bottom, right, size = (
            bitmap.HEADER.unpack_from(data)
        )
        self.assertEqual(
            (fmt, stride, depth, bottom, right, size), (8, 80, 8, 121, 78, 80 * 121)
        )
        self.assertEqual(len(data), 9708)  # a stock Settings pane icon
        pixels = data[bitmap.HEADER.size :]
        self.assertEqual(pixels[80 + 1], 128)
        self.assertEqual(sum(pixels), 128)

    def test_pane_icon_is_coverage_from_gray(self):
        rgba = bytes((90, 90, 90, 255)) * 78 * 121
        pixels = bitmap.png_to_bmap(png(78, 121, rgba), pane="icon")[
            bitmap.HEADER.size :
        ]
        self.assertEqual(set(pixels[i] for i in range(0, len(pixels), 80)), {90})
        self.assertEqual(pixels[78:80], bytes(2))  # row padding

    def test_pane_icon_refuses_colour_and_other_sizes(self):
        for name, (width, height, pixel) in {
            "coloured opaque": (78, 121, (255, 0, 0, 255)),
            "wrong size": (80, 121, (0, 0, 0, 0)),
        }.items():
            with self.subTest(name), self.assertRaises(ValueError):
                bitmap.png_to_bmap(
                    png(width, height, bytes(pixel) * width * height), pane="icon"
                )

    def test_pane_pictures_have_a_pane_size(self):
        sizes = [("picture", size) for size in bitmap.PICTURE_SIZES]
        for pane, (width, height) in sizes + [("badge", bitmap.BADGE_SIZE)]:
            rgba = bytes((10, 20, 30, 255)) * width * height
            with self.subTest(pane, width=width):
                data = bitmap.png_to_bmap(png(width, height, rgba), pane=pane)
                self.assertEqual(bitmap.bmap_size(data), (width, height))
                with self.assertRaises(ValueError):
                    bitmap.png_to_bmap(
                        png(width + 1, height, rgba + rgba[: 4 * height]), pane=pane
                    )
        self.assertEqual(
            bitmap.bmap_size(bitmap.png_to_bmap(png(3, 5, bytes(60)))), (3, 5)
        )

    def test_rejects_an_unknown_pane(self):
        for pane in (True, "settings", "menu"):
            with self.subTest(pane), self.assertRaises(ValueError):
                bitmap.png_to_bmap(png(78, 121, bytes(78 * 121 * 4)), pane=pane)

    def test_rejects_other_files(self):
        with self.assertRaises(ValueError):
            bitmap.png_to_bmap(b"GIF89a")
