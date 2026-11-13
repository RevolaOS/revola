# SPDX-License-Identifier: GPL-3.0-only
"""rv_menus.h names the stock rows of the five menus extensions place rows in."""

import unittest

import firmware

HEADER = """#define ADDR_Foo 0x08000000
#define RES_SettingsMenu_Legal_Item 0x0dad0c7b
#define RES_MainMenu_CoverFlow_Item 0x0dad09f0
#define RES_MainMenu_iTunesU_Item 0x0dad09f1
#define RES_MainMenu_TVShows_Item 0x0dad09f2
#define RES_VideosMenu_MusicVideos_Item 0x0dad081a
#define RES_Settings_EQ_Item 0x0dad0f47
#define RES_Menu_Checked_Bitmap 0x0dad0c08
"""


class MenusHeaderTests(unittest.TestCase):
    def test_rows_are_named_by_menu_and_row(self):
        text = firmware.menus_header(HEADER)
        self.assertIn("#define RV_SETTINGS_LEGAL      0x0dad0c7b", text)
        self.assertIn("#define RV_MAIN_COVER_FLOW     0x0dad09f0", text)
        self.assertIn("#define RV_MAIN_ITUNES_U       0x0dad09f1", text)
        self.assertIn("#define RV_MAIN_TV_SHOWS       0x0dad09f2", text)
        self.assertIn("#define RV_VIDEOS_MUSIC_VIDEOS 0x0dad081a", text)
        self.assertTrue(text.startswith("/* SPDX-License-Identifier: GPL-3.0-only */"))

    def test_only_menu_rows_are_listed(self):
        text = firmware.menus_header(HEADER)
        self.assertNotIn("EQ", text)
        self.assertNotIn("Bitmap", text)
        self.assertNotIn("ADDR_", text)

    def test_header_without_rows_is_rejected(self):
        with self.assertRaises(ValueError):
            firmware.menus_header("#define RES_Settings_EQ_Item 0x1\n")
