# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the patched bootloader's file reads against a host filesystem."""

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

HARNESS = r"""
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <setjmp.h>
#include <assert.h>
#define CFW_STAGE_START 0u
#define CFW_COMPANION_START 200000u
#define CFW_STAGE_END 400000u
#define CFW_BOOTSTRAP_BYTES 0x1f800u
static unsigned char staging[CFW_STAGE_END];
#define S5L8702_UNCACHED_ADDR(p) (staging + (uintptr_t)(p))
static unsigned char io_buffer[16384];
static jmp_buf stopped;
static const char *error;
static void stop(const char *message) { error=message; longjmp(stopped,1); }
static off_t ffilesize(int fd) {struct stat s; assert(!fstat(fd,&s)); return s.st_size;}
static int host_open(const char *p, int flags) {return open(p+1,flags);}
#define open host_open
"""


class BootFilesTests(unittest.TestCase):
    def test_nested_paths_and_missing_or_invalid_files(self):
        patch = (ROOT / "boot/rockbox/0001-cfw-file-boot.patch").read_text()
        section = patch.split("+++ b/bootloader/cfw-file-ipod6g.c\n", 1)[1]
        section = section.split("diff --git", 1)[0]
        source = "\n".join(
            line[1:] for line in section.splitlines() if line.startswith("+")
        )
        reader = source.split("static uint32_t load_file", 1)[1]
        reader = "static uint32_t load_file" + reader.split("void cfw_run_file", 1)[0]
        calls = source.split("void cfw_run_file(void)\n{", 1)[1]
        calls = calls.split("    const void *companion", 1)[0]
        program = HARNESS + reader + "\nint main(int argc, char **argv) {\n"
        program += "assert(argc==2); if(setjmp(stopped)) return strcmp(error,argv[1]) ? 2 : 0;\n"
        program += calls
        program += r"""
assert(firmware_bytes==32769 && companion_bytes==CFW_BOOTSTRAP_BYTES);
for(unsigned i=0;i<firmware_bytes;i++) assert(staging[i]==0x5a);
for(unsigned i=0;i<companion_bytes;i++) assert(staging[CFW_COMPANION_START+i]==0xa5);
return strcmp(argv[1],"ok") ? 3 : 0;
}
"""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            code, binary = root / "boot.c", root / "boot-test"
            code.write_text(program)
            subprocess.run(
                ["cc", "-Wno-int-to-pointer-cast", str(code), "-o", str(binary)],
                check=True,
            )
            folder = root / "Revola/Firmware"
            folder.mkdir(parents=True)
            firmware = folder / "revola.bin"
            loader = folder / "loader.bin"
            cases = [
                (None, None, "File open failed"),
                (32769, None, "File open failed"),
                (0, 0x1F800, "File size out of bounds"),
                (200001, 0x1F800, "File size out of bounds"),
                (32769, 0x1F7FF, "File size out of bounds"),
                (32769, 0x1F800, "ok"),
            ]
            for firmware_size, loader_size, expected in cases:
                with self.subTest(firmware=firmware_size, loader=loader_size):
                    for path, size, byte in (
                        (firmware, firmware_size, 0x5A),
                        (loader, loader_size, 0xA5),
                    ):
                        path.unlink(missing_ok=True)
                        if size is not None:
                            path.write_bytes(bytes([byte]) * size)
                    subprocess.run(
                        [str(binary), expected],
                        cwd=root,
                        check=True,
                        capture_output=True,
                    )
