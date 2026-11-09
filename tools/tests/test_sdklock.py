# SPDX-License-Identifier: GPL-3.0-only
"""A change under sdk/ without an API version bump fails the build."""

import tempfile
import unittest
from pathlib import Path

import sdklock

HEADER = "#define RV_API_MAJOR 1\n#define RV_API_MINOR {minor}\n"


class SdkLockTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.sdk = Path(self.directory.name)
        self.lock = self.sdk / "api.lock"
        (self.sdk / "rv_api.h").write_text(HEADER.format(minor=0))
        (self.sdk / "rv.mk").write_text("all:\n")
        sdklock.update(self.sdk, self.lock)

    def tearDown(self):
        self.directory.cleanup()

    def test_unchanged_passes(self):
        sdklock.check(self.sdk, self.lock)

    def test_change_without_bump_fails_and_names_the_file(self):
        (self.sdk / "rv.mk").write_text("all: ;\n")
        with self.assertRaisesRegex(ValueError, "rv.mk"):
            sdklock.check(self.sdk, self.lock)

    def test_change_with_bump_needs_a_lock_update(self):
        (self.sdk / "rv_api.h").write_text(HEADER.format(minor=1))
        with self.assertRaisesRegex(ValueError, "stale"):
            sdklock.check(self.sdk, self.lock)
        sdklock.update(self.sdk, self.lock)
        sdklock.check(self.sdk, self.lock)
        self.assertEqual(sdklock.api_version(self.sdk / "rv_api.h"), "1.1")
