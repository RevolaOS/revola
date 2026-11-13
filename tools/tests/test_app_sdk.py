# SPDX-License-Identifier: GPL-3.0-only
"""The eApp runtime's path rules and file lifecycle, on the host."""

import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "firmware/osos-2.0.5.h"


class AppSdkTests(unittest.TestCase):
    def test_files_and_directories(self):
        if "File_GetLegacyHandleSize" not in HEADER.read_text():
            self.skipTest("the eApp host routines are not in the symbols header yet")
        subprocess.run(
            [sys.executable, ROOT / "sdk/app/tests/run.py", HEADER], check=True
        )
