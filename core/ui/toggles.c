/* SPDX-License-Identifier: GPL-3.0-only */

/* Extension rows in Settings > Main Menu and Music Menu. Those screens list
 * Apple's rows as one expanded model-backed row: the Settings model answers
 * their count with a fixed number and each row's style, title and checkmark by
 * index from its rows table, with no bounds check, and the selection handler
 * opens Reset for the last index and toggles every other. The core shows its
 * rows where they sit in the menu itself, after the stock row they follow, and
 * Reset last: it maps each list position to an Apple index or a core row,
 * answers for the positions that moved and takes the selection. */

#include "ui/toggles.h"
#include "osos-2.0.5.h"
#include "ui/menus.h"
#include "ui/resources.h"
#include "ui/screens.h"
#include "hooks/sites.h"
#include <stddef.h>

#define FIELD(base, offset, type) (*(type *)((uint8_t *)(base) + (offset)))
#define NONE 0xffffffffu

struct toggle_screen {
    uint8_t root;
    const char *action;   /* the selection handler's name in the screen's CEVT */
    void (*toggle)(void *model, uint32_t index);
    uint32_t item;        /* the screen's one ITEM row, notified to redraw the list */
    uint32_t count;       /* Apple's rows, Reset last */
    const uint8_t *table; /* Apple's rows */
    uint32_t property;
};

static struct toggle_screen screens[2] = {
    {RV_ROOT_MAIN, "HandleMainMenu", SettingsModel_ToggleMainMenuItem, RES_Settings_MainMenu_Item,
     MainMenuSettings_row_count,
     (const uint8_t *)MainMenuSettingsRows, 0},
    {RV_ROOT_MUSIC, "HandleMusicMenu", SettingsModel_ToggleMusicMenuItem,
     RES_Settings_MusicMenu_Item, MusicMenuSettings_row_count,
     (const uint8_t *)MusicMenuSettingsRows, 0},
};

static struct toggle_screen *screen_of(uint32_t property)
{
    for (uint32_t i = 0; i < 2; i++)
        if (screens[i].property && screens[i].property == property)
            return &screens[i];
    return NULL;
}

static uint32_t stock_item(const struct toggle_screen *t, uint32_t index)
{
    return core_word_at(t->table + index * MenuSettingsRow_bytes, MenuSettingsRow_item);
}

/* 1 when core row n follows one of Apple's rows other than Reset. */
static int anchored(const struct toggle_screen *t, uint32_t n)
{
    int visible;
    uint32_t after;
    core_menus_label(t->root, n, &visible, &after);
    for (uint32_t a = 0; a + 1 < t->count; a++)
        if (stock_item(t, a) == after)
            return 1;
    return 0;
}

/* The list position `position` as Apple's index (*stock) or core row (*core);
 * the other is NONE. Core rows follow their anchor, unanchored ones come just
 * before Reset; *anchor is the Apple index a core row follows. */
static void locate(const struct toggle_screen *t, uint32_t position, uint32_t *stock,
                   uint32_t *core, uint32_t *anchor)
{
    uint32_t rows = core_menus_count(t->root), at = 0;
    *stock = *core = NONE;
    if (!rows) {
        *stock = position;
        return;
    }
    for (uint32_t a = 0; a < t->count; a++) {
        if (a + 1 == t->count)
            for (uint32_t n = 0; n < rows; n++)
                if (!anchored(t, n) && at++ == position) {
                    *core = n;
                    *anchor = a - 1;
                    return;
                }
        if (at++ == position) {
            *stock = a;
            return;
        }
        for (uint32_t n = 0; a + 1 < t->count && n < rows; n++) {
            int visible;
            uint32_t after;
            core_menus_label(t->root, n, &visible, &after);
            if (after == stock_item(t, a) && at++ == position) {
                *core = n;
                *anchor = a;
                return;
            }
        }
    }
}

uint32_t core_toggles_property(uint32_t model, uint32_t type, uint32_t id, uint32_t out,
                               const struct core_next *next)
{
    uint32_t result = core_next_call(next, model, type, id, out);
    struct toggle_screen *t = type == Property_Count ? screen_of(id) : NULL;
    if (t && result)
        *(uint32_t *)out += core_menus_count(t->root);
    return result;
}

static uint32_t current_index(void)
{
    void *model = UI_GetSelectionModel();
    uint32_t (*current)(void *) =
        FIELD(FIELD(model, 0, void **), SelectionModel_current_index_method, void *);
    return current(model);
}

static void *bitmap(uint32_t id)
{
    return ResourceService_GetBitmap(UI_GetResourceService(), id);
}

int core_toggles_indexed(uint32_t model, uint32_t type, uint32_t property, uint32_t position,
                         const struct core_next *next, uintptr_t *out, uint32_t *result)
{
    struct toggle_screen *t = screen_of(property);
    uint32_t stock, core, anchor;
    if (!t || !core_menus_count(t->root))
        return 0;
    locate(t, position, &stock, &core, &anchor);
    int selected = position == current_index();
    if (stock != NONE) {
        /* Apple compares the index it is given with the selection, so the
         * checkmarks of moved rows and Reset's icon are the core's. */
        *result = core_next_call6(next, model, type, property, stock, (uint32_t)out, 0);
        if (type == Property_Bitmap && stock + 1 == t->count)
            *out = selected ? (uintptr_t)bitmap(RES_Menu_Reset_Bitmap) : 0;
        else if (type == Property_Bitmap && *out)
            *out = (uintptr_t)bitmap(selected ? RES_Menu_CheckedSelected_Bitmap
                                              : RES_Menu_Checked_Bitmap);
        return 1;
    }
    int visible;
    uint32_t after;
    const char *label = core_menus_label(t->root, core, &visible, &after);
    *result = 1;
    if (!label)
        *result = 0;
    else if (type == Property_String)
        *out = (uintptr_t)label;
    else if (type == Property_Layout) /* drawn like the row it follows: top level or indented */
        *result = core_next_call6(next, model, type, property, anchor, (uint32_t)out, 0);
    else if (type == Property_Bitmap)
        *out = visible ? (uintptr_t)bitmap(selected ? RES_Menu_CheckedSelected_Bitmap
                                                    : RES_Menu_Checked_Bitmap)
                       : 0;
    else
        *result = 0;
    return 1;
}

static int same(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

/* Apple's selection handler opens Reset for the last stock index and toggles
 * every other index; with core rows in the list the core does both by position.
 * Reset is the controller event the layout maps to the confirmation screen. */
static uint32_t select_row(uint32_t controller, uint32_t action, uint32_t argument,
                           const struct core_next *next)
{
    struct toggle_screen *t = &screens[next->site == CORE_SITE_SettingsMusicMenu_Action_slot ||
                                       next->site == CORE_SITE_SettingsMusicMenu_SecondaryAction_slot];
    void *model = core_screens_settings_model();
    if (!same((const char *)action, t->action) || argument == ActionSupportQuery || !model ||
        !core_menus_count(t->root))
        return core_next_call(next, controller, action, argument, 0);
    uint32_t stock, core, anchor;
    locate(t, current_index(), &stock, &core, &anchor);
    if (core != NONE) {
        core_menus_toggle(t->root, core);
        core_screens_notify(t->item);
    } else if (stock + 1 == t->count) {
        static const char reset[] = "GotoScreen_ConfirmCancelResetMenu";
        uint32_t name[CppString_bytes / 4], allocator = 0;
        CppString_InitBytes(name, reset, sizeof reset - 1, &allocator);
        UI_EmitControllerEvent((void *)controller, name);
        CppString_Destroy(name);
    } else if (stock != NONE) {
        t->toggle(model, stock);
    }
    return 1;
}

static const struct core_hook hooks[] = {
    {CORE_SITE_SettingsMainMenu_Action_slot, CORE_HOOK_FILTER, 0, select_row},
    {CORE_SITE_SettingsMainMenu_SecondaryAction_slot, CORE_HOOK_FILTER, 0, select_row},
    {CORE_SITE_SettingsMusicMenu_Action_slot, CORE_HOOK_FILTER, 0, select_row},
    {CORE_SITE_SettingsMusicMenu_SecondaryAction_slot, CORE_HOOK_FILTER, 0, select_row},
};

/* The screens' properties come from their ITEM rows' title sources. */
void core_toggles_init(void *bank)
{
    static const uint32_t tables[2] = {RES_Settings_MainMenu_Items, RES_Settings_MusicMenu_Items};
    for (uint32_t i = 0; i < 2; i++) {
        uint32_t bytes, row_bytes;
        const void *items = ResourceBank_GetResource(bank, Resource_ITEM, tables[i], &bytes);
        const uint8_t *row =
            items ? core_block_with_word(items, MenuRow_id, screens[i].item, &row_bytes) : NULL;
        const void *source =
            row ? ResourceBank_GetResource(bank, Resource_SORC, core_word_at(row, MenuRow_title),
                                           &bytes)
                : NULL;
        struct core_block binding;
        if (source && core_block_at(source, 0, &binding))
            screens[i].property = core_word_at(binding.data, SourceBinding_property);
    }
    for (uint32_t i = 0; i < sizeof hooks / sizeof hooks[0]; i++)
        core_hook_bind(&hooks[i]);
}
