# SPDX-License-Identifier: GPL-3.0-only
"""The core's lz decoder, built for the host, against rvx-pack's encoder."""

import random
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

import rvx

CORE = Path(__file__).resolve().parents[2] / "core"
SDK = Path(__file__).resolve().parents[2] / "sdk"
# Feeds core_lz_decode from stdin as a file (in chunks) or from memory.
DRIVER = r"""
#include "loader/file.h"
#include "loader/lz.h"
#include <stdio.h>
#include <stdlib.h>
void (*volatile core_trace_probe)(uint32_t kind, uint32_t argument);
int core_file_read(void *handle, void *buffer, uint32_t bytes, uint32_t *got)
{
    *got = (uint32_t)fread(buffer, 1, bytes, handle);
    return 1;
}
int main(int argc, char **argv)
{
    uint32_t stored = (uint32_t)atol(argv[2]), bytes = (uint32_t)atol(argv[3]);
    uint8_t *out = malloc(bytes + 1), *stream = malloc(stored + 1);
    struct core_lz_input in;
    if (argv[1][0] == 'f') {
        core_lz_from_file(&in, stdin, stored);
    } else {
        if (fread(stream, 1, stored, stdin) != stored)
            return 2;
        core_lz_from_memory(&in, stream, stored);
    }
    if (!core_lz_decode(&in, out, bytes))
        return 1;
    fwrite(out, 1, bytes, stdout);
    return 0;
}
"""


@unittest.skipUnless(shutil.which("cc"), "no host C compiler")
class CoreLzTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.scratch = tempfile.TemporaryDirectory()
        directory = Path(cls.scratch.name)
        (directory / "driver.c").write_text(DRIVER)
        cls.binary = directory / "lz"
        subprocess.run(
            [
                "cc",
                "-std=c11",
                "-Wall",
                "-Werror",
                "-I",
                CORE,
                "-I",
                SDK,
                directory / "driver.c",
                CORE / "loader/lz.c",
                "-o",
                cls.binary,
            ],
            check=True,
        )

    @classmethod
    def tearDownClass(cls):
        cls.scratch.cleanup()

    def decode(self, source, stream, size):
        result = subprocess.run(
            [self.binary, source, str(len(stream)), str(size)],
            input=stream,
            capture_output=True,
        )
        return result.stdout if result.returncode == 0 else None

    def test_decodes_what_rvx_pack_encodes(self):
        rng = random.Random(2)
        samples = [
            b"",
            b"z",
            bytes(rng.randrange(256) for _ in range(3000)),
            bytes(rng.choice(b"\0\0\0\x01\xff") for _ in range(9000)),
            b"\0" * 300 + b"x" + b"\0" * 5000,
        ]
        for data in samples:
            stream = rvx.lz_encode(data)
            for source in ("file", "memory"):
                with self.subTest(size=len(data), source=source):
                    self.assertEqual(self.decode(source, stream, len(data)), data)

    def test_refuses_malformed_streams(self):
        data = b"hello hello hello hello"
        stream = rvx.lz_encode(data)
        cases = {
            "ends early": (stream[:-1], len(data)),
            "trailing bytes": (stream + b"\0", len(data)),
            "distance before the start": (b"\x01\x00\x05", 3),
            "match past the output": (b"\x02a\x10\x00", 3),
            "longer output asked": (stream, len(data) + 1),
            "length extension cut": (b"\x02a\xf0\x00", 40),
        }
        for name, (bad, size) in cases.items():
            for source in ("file", "memory"):
                with self.subTest(name, source=source):
                    self.assertIsNone(self.decode(source, bad, size))
