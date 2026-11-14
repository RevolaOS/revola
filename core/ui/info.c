/* SPDX-License-Identifier: GPL-3.0-only */

/* Settings > RevolaOS Info, after Legal: a text page with the version, what
 * RevolaOS is, and the credits and licence from the NOTICE files of everything
 * the release bundle ships. The system font has no "+", ";", ">" or "_", so the
 * text avoids them. */

#include "ui/info.h"
#include "osos-2.0.5.h"
#include "ui/menus.h"
#include "ui/screens.h"
#include <stddef.h>

/* "RevolaOS 0.0.0-dev (ead7b622)": CORE_VERSION with its build in brackets,
 * as the system font has no "+". The pane shows it without "RevolaOS ". */
static char heading[56];

static void format_version(void)
{
    char *out = heading, *end = heading + sizeof heading - 3;
    int build = 0;
    for (const char *c = "RevolaOS " CORE_VERSION; *c && out < end; c++) {
        if (*c == '+' && !build) {
            *out++ = ' ';
            *out++ = '(';
            build = 1;
        } else {
            *out++ = *c;
        }
    }
    if (build)
        *out++ = ')';
    *out = 0;
}

static const char *const paragraphs[] = {
    heading,
    "Custom firmware for iPod classic that runs extensions. github.com/RevolaOS/revola",
    "RevolaOS contributors: paviro.",
    "Derived from RepriseOS by lgreitis and the RepriseOS contributors: lgreitis, "
    "alexballvideos-netizen, RaduTek, Swimming33, brilliant-alien, kayboll.",
    "Rockbox contributors: bootloader, dual-boot installer, hardware drivers, and the "
    "Crossfeed (Thom Johansen, Bertrik Sikken, Michael Sevakis), Channels (Thom Johansen, "
    "Michael Sevakis) and Compressor (Jeffrey Goode) extensions. GPL-2.0-or-later.",
    "2048 from Rockbox by Franklin Wei, high score table by Linus Nielsen Feltzing. "
    "GPL-2.0-or-later.",
    "wInd3x by Serge \"q3k\" Bazanski: BootROM exploit and DFU tooling. GPL-2.0-or-later.",
    "Cupertino Icons (Framework7 Icons) by Vladimir Kharlampidi: Settings icons and the "
    "Flat theme's glyphs. MIT License.",
    "Inter by the Inter Project Authors: the Flat theme's numerals. SIL Open Font License 1.1.",
    "The Flat theme follows \"My own iPodOS edits\" by u/DrawingCompetitive76 on r/IpodClassic.",
    "Provided as is, without warranty of any kind. GPL-3.0-only.",
};

static const struct rv_bitmap icon = {"info"};

static const struct rv_screen screen = {
    .id = "core.info", .title = "RevolaOS Info", .kind = RV_SCREEN_KIND_TEXT,
    .text = {paragraphs, sizeof paragraphs / sizeof paragraphs[0]},
};

static const struct rv_menu_entry entry = {
    .menu = RV_MENU_SETTINGS, .after = RES_SettingsMenu_Legal_Item,
    .row = {.label = "RevolaOS Info", .kind = RV_ROW_KIND_PUSH, .screen = "core.info"},
    .caption = heading + sizeof "RevolaOS",
    .bitmap = &icon,
};

void core_info_register(void)
{
    format_version();
    core_screens_register(NULL, 0, &screen, 1);
    core_menus_register(NULL, &entry, 1);
}
