/* SPDX-License-Identifier: GPL-3.0-only */

/* The host's device services: the backlight, the power source, the platform,
 * the user's language, the package name and the debug console. */

#include "osos-2.0.5.h"
#include "rv_app.h"

_Static_assert(RV_APP_PLATFORM_IPOD_VIDEO == GamesPlatform_IPodVideo, "video");
_Static_assert(RV_APP_PLATFORM_NANO_3G == GamesPlatform_Nano3G, "nano");
_Static_assert(RV_APP_PLATFORM_CLASSIC == GamesPlatform_Classic, "classic");

void rv_app_set_backlight_level(uint32_t percent)
{
    GameSystem_SetBacklightLevel(percent > GameBacklight_LevelMax ? GameBacklight_LevelMax
                                                                  : (int)percent);
}

void rv_app_set_backlight_dimmed(int on)
{
    GameSystem_SetBacklightDimmed(on != 0);
}

int rv_app_external_power(void)
{
    return GameSystem_IsExternalPowerConnected() != 0;
}

enum rv_app_platform rv_app_platform(void)
{
    uint32_t platform = GameSystem_GetGamesPlatformId();
    return platform <= RV_APP_PLATFORM_CLASSIC ? (enum rv_app_platform)platform
                                               : RV_APP_PLATFORM_UNKNOWN;
}

/* The host's languages in the order of its Language setting, which it
 * reports as an index. */
static const char *const languages[] = {
    "en_US", "da_DK", "de_CH", "de_DE", "es_ES", "fi_FI", "fr_BE", "fr_CH", "fr_FR",
    "it_IT", "ja_JP", "ko_KR", "nl_BE", "nl_NL", "no_NO", "pt_PT", "sv_SE", "zh_CN",
    "zh_TW", "cs_CZ", "el_GR", "hu_HU", "pl_PL", "ru_RU", "tr_TR",
};

const char *rv_app_language(void)
{
    uint32_t index = 0;
    uint32_t size = sizeof index;
    if (GameSettings_GetValue("Language", &index, &size) != 0 ||
        index >= sizeof languages / sizeof languages[0])
        return NULL;
    return languages[index];
}

int rv_app_name(char *out, size_t capacity)
{
    if (!out || capacity == 0 || capacity > 0x7fffffff)
        return -1;
    return GameSystem_GetGameName(out, (int)capacity, 1);
}

void rv_app_log(const char *text)
{
    /* The host's printf writes into its format while it prints, so it
     * cannot be a literal. */
    char format[] = "%s\n";
    if (text)
        GameDebug_Printf(format, text);
}
