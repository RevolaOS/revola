#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the package path rules and the directory and file lifecycle on the host,
once per root: python3 tests/run.py [SYMBOLS_HEADER]."""

import os
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
headers = (root.parent / "osos-2.0.5.h", root.parents[1] / "firmware/osos-2.0.5.h")
header = (
    Path(sys.argv[1])
    if len(sys.argv) > 1
    else next((p for p in headers if p.is_file()), None)
)
if header is None or not header.is_file():
    sys.exit("osos-2.0.5.h not found; pass its path")
with tempfile.TemporaryDirectory(prefix="rv-app-tests-") as name:
    work = Path(name)
    for package_root in ("/Revola/Games/", "/Revola/Apps/"):
        executable = work / "files_test"
        subprocess.run(
            [
                os.environ.get("HOST_CC", "cc"),
                "-std=c11",
                "-g",
                "-O1",
                "-fsanitize=address,undefined",
                "-fno-sanitize-recover=all",
                "-Wall",
                "-Wextra",
                "-Werror",
                f'-DRV_APP_ROOT="{package_root}"',
                "-I" + str(root),
                "-I" + str(root / "runtime"),
                "-I" + str(header.parent),
                str(root / "tests/files_test.c"),
                str(root / "runtime/files.c"),
                str(root / "runtime/directory.c"),
                "-o",
                str(executable),
            ],
            check=True,
        )
        subprocess.run([str(executable), package_root], check=True)
print("eApp SDK path and lifecycle checks passed")
