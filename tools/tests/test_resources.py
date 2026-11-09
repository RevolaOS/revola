# SPDX-License-Identifier: GPL-3.0-only
"""rv_resources.h carries every RES_ id of the symbols header under RV_RES_."""

import unittest

import firmware

HEADER = """#define ADDR_Foo 0x08000000
#define RES_SettingsMenu_Legal_Item 0x0dad0c7b
#define RES_Menu_Checked_Bitmap 0x0dad0c08
"""


class ResourcesHeaderTests(unittest.TestCase):
    def test_ids_are_renamed_and_kept_in_order(self):
        text = firmware.resources_header(HEADER)
        self.assertIn("#define RV_RES_SettingsMenu_Legal_Item 0x0dad0c7b", text)
        self.assertIn("#define RV_RES_Menu_Checked_Bitmap     0x0dad0c08", text)
        self.assertNotIn("ADDR_", text)
        self.assertTrue(text.startswith("/* SPDX-License-Identifier: GPL-3.0-only */"))

    def test_header_without_ids_is_rejected(self):
        with self.assertRaises(ValueError):
            firmware.resources_header("#define ADDR_Foo 0x1\n")
