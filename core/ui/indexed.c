/* SPDX-License-Identifier: GPL-3.0-only */

/* The rows a choice, multi-choice or dynamic list's one row expands into. As
 * the last model-backed row of its list it becomes as many rows as its title
 * property's Property_Count, and each asks the Settings model's indexed
 * property for its label, detail, style and checkmark by index. All of them
 * share the one row's chosen event; the selection model says which was chosen.
 * A choice controller highlights the current value once its layout is shown,
 * as Settings > EQ does. */

#include "ui/indexed.h"
#include "osos-2.0.5.h"
#include "ui/controllers.h"
#include "ui/lists.h"
#include "ui/resources.h"
#include "ui/screens.h"
#include "ui/toggles.h"
#include "settings.h"
#include "hooks/sites.h"
#include <stddef.h>

#define FIELD(base, offset, type) (*(type *)((uint8_t *)(base) + (offset)))

static void *bank;

static const void *stock(uint32_t type, uint32_t id)
{
    uint32_t bytes;
    return ResourceBank_GetResource(bank, type, id, &bytes);
}

static uint32_t owner_of(const struct core_screen *s)
{
    return s->owner ? s->owner->owner : CORE_SETTINGS_OWNER_CORE;
}

static uint32_t row_count(const struct core_screen *s)
{
    const struct rv_screen *d = s->declaration;
    if (d->kind == RV_SCREEN_KIND_DYNAMIC_LIST) {
        struct rv_context ctx;
        core_screens_context(s, &ctx);
        return d->dynamic.count ? d->dynamic.count(&ctx) : 0;
    }
    return d->kind == RV_SCREEN_KIND_MULTI_CHOICE && d->choice.count > 32 ? 32 : d->choice.count;
}

static int32_t setting(const struct core_screen *s)
{
    return core_screens_setting_value(s->owner, s->declaration->choice.key);
}

/* A multi-choice row asks which row styles to build, then each expanded row's. */
static const uint32_t check_styles[2] = {MenuRowStyle_CheckIndented, 0};
static const uint32_t submenu_styles[2] = {MenuRowStyle_TwoLineSubmenu, 0};

static int submenu_rows(const struct core_screen *s)
{
    return s->declaration->kind == RV_SCREEN_KIND_DYNAMIC_LIST &&
           s->declaration->dynamic.detail == RV_DETAIL_BELOW_SUBMENU;
}

int core_indexed_property(struct core_screen *s, uint32_t type, uint32_t id, uint32_t out)
{
    if (id != CORE_SCREEN_ROWS(s))
        return 0;
    if (type == Property_Count)
        *(uint32_t *)out = row_count(s);
    else if (type == Property_LayoutSet && s->declaration->kind == RV_SCREEN_KIND_MULTI_CHOICE)
        *(const uint32_t **)out = check_styles;
    else if (type == Property_LayoutSet && submenu_rows(s))
        *(const uint32_t **)out = submenu_styles;
    else
        return 0;
    return 1;
}

static uint32_t selected_index(void)
{
    void *model = UI_GetSelectionModel();
    uint32_t (*current)(void *) =
        FIELD(FIELD(model, 0, void **), SelectionModel_current_index_method, void *);
    return current(model);
}

/* Checkmarks as Settings > Main Menu draws them: inverted on the selected row. */
static void *checkmark(uint32_t index)
{
    static void *bitmaps[2];
    int selected = index == selected_index();
    if (!bitmaps[selected])
        bitmaps[selected] = ResourceService_GetBitmap(
            UI_GetResourceService(),
            selected ? RES_Menu_CheckedSelected_Bitmap : RES_Menu_Checked_Bitmap);
    return bitmaps[selected];
}

/* Expanded row `index`: the label from the title property, the detail from the
 * value property, the style and checkmark of a multi-choice row. 0 when the
 * question is not one the row answers. */
static int row_answer(const struct core_screen *s, uint32_t type, uint32_t property,
                      uint32_t index, uintptr_t *out)
{
    const struct rv_screen *d = s->declaration;
    int title = property == CORE_SCREEN_ROWS(s);
    if (index >= row_count(s))
        return 0;
    if (d->kind == RV_SCREEN_KIND_DYNAMIC_LIST) {
        const char *detail = NULL;
        struct rv_context ctx;
        core_screens_context(s, &ctx);
        const char *label = d->dynamic.row ? d->dynamic.row(&ctx, index, &detail) : NULL;
        const char *text = title ? label : detail;
        if (type == Property_Layout && title && submenu_rows(s)) {
            *out = MenuRowStyle_TwoLineSubmenu;
            return 1;
        }
        if (type != Property_String)
            return 0;
        *out = (uintptr_t)(text ? text : "");
        return 1;
    }
    if (!title)
        return 0;
    if (type == Property_String)
        *out = (uintptr_t)d->choice.labels[index];
    else if (type == Property_Layout && d->kind == RV_SCREEN_KIND_MULTI_CHOICE)
        *out = MenuRowStyle_CheckIndented;
    else if (type == Property_Bitmap && d->kind == RV_SCREEN_KIND_MULTI_CHOICE)
        *out = (uint32_t)setting(s) >> index & 1 ? (uintptr_t)checkmark(index) : 0;
    else
        return 0;
    return 1;
}

static uint32_t indexed_property(uint32_t model, uint32_t type, uint32_t property, uint32_t index,
                                 const struct core_next *next, uint32_t out)
{
    struct core_screen *s;
    uint32_t result;
    if (property >> 24 != CORE_RESOURCE_PREFIX &&
        core_toggles_indexed(model, type, property, index, next, (uintptr_t *)out, &result))
        return result;
    if (property >> 24 == CORE_RESOURCE_PREFIX && (s = core_screens_of_id(property)) &&
        core_lists_indexed(s->declaration->kind) && property - CORE_SCREEN_ROWS(s) < 2 &&
        row_answer(s, type, property, index, (uintptr_t *)out))
        return 1;
    return core_next_call6(next, model, type, property, index, out, 0);
}

/* The view the list of a list screen is: the split layout's is named, the
 * Notes layout's is the one its stock item-table binding names. */
static uint32_t list_view(const struct core_screen *s)
{
    static uint32_t notes_view;
    if (s->split)
        return RES_SettingsList_View;
    const void *slyt = stock(Resource_SLyt, RES_Notes_List_Screen_Alt_Default);
    for (uint32_t l = 0; !notes_view && slyt && l < core_blocks_count(slyt); l++) {
        struct core_block list, instance, binding;
        core_block_at(slyt, l, &list);
        const void *ssin = stock(Resource_SSin, core_word_at(list.data, 0));
        for (uint32_t i = 0; !notes_view && ssin && core_block_at(ssin, i, &instance); i++) {
            uint32_t id = core_word_at(instance.data, SsinInstance_view_bindings);
            const void *vcvs = id ? stock(Resource_VCvs, id) : NULL;
            for (uint32_t b = 0; vcvs && core_block_at(vcvs, b, &binding); b++)
                if (binding.kind == ViewBinding_ItemTable)
                    notes_view = core_word_at(binding.data, 0);
        }
    }
    return notes_view;
}

/* Highlights the current choice. The Settings list controller builds its list
 * while it activates, so the row is selected right after that. */
static void select_current(const struct core_screen *s)
{
    void *model = UI_GetSelectionModel();
    void *(*find)(void *, uint32_t) =
        FIELD(FIELD(model, 0, void **), SelectionModel_find_view_method, void *);
    void *view = find(model, list_view(s));
    void *list = view ? Object_GetInterface(view, Interface_List) : NULL;
    int32_t index = setting(s);
    if (!list || index < 0 || (uint32_t)index >= row_count(s))
        return;
    void (*select)(void *, uint32_t) =
        FIELD(FIELD(list, 0, void **), ListInterface_select_method, void *);
    select(list, (uint32_t)index);
}

static void **choice_vtable;
static uint32_t (*activate_original)(void *controller, uint32_t entering);

static uint32_t choice_activate(void *controller, uint32_t entering)
{
    uint32_t result = activate_original(controller, entering);
    struct core_screen *s = core_screens_of_controller(controller);
    if (entering && s && s->declaration)
        select_current(s);
    return result;
}

int core_indexed_prepare(struct core_screen *s)
{
    if (s->declaration->kind != RV_SCREEN_KIND_CHOICE || choice_vtable)
        return 1;
    choice_vtable = core_controller_vtable(SettingsMenuListController_vtable,
                                           ScreenController_activate_method, choice_activate,
                                           (void **)&activate_original);
    return choice_vtable != NULL;
}

void core_indexed_attach(struct core_screen *s, void *controller)
{
    if (s->declaration->kind == RV_SCREEN_KIND_CHOICE)
        core_controller_attach(controller, choice_vtable);
}

void core_indexed_chosen(struct core_screen *s)
{
    const struct rv_screen *d = s->declaration;
    uint32_t index = selected_index();
    if (index >= row_count(s))
        return;
    switch (d->kind) {
    case RV_SCREEN_KIND_CHOICE:
        core_settings_set_int(owner_of(s), d->choice.key, (int32_t)index);
        core_screens_setting_changed(s->owner, d->choice.key);
        core_screens_pop(s->owner);
        break;
    case RV_SCREEN_KIND_MULTI_CHOICE:
        core_settings_set_int(owner_of(s), d->choice.key, setting(s) ^ (int32_t)(1u << index));
        core_screens_notify(CORE_SCREEN_ROWS(s));
        core_screens_setting_changed(s->owner, d->choice.key);
        break;
    default: {
        struct rv_context ctx;
        core_screens_context(s, &ctx);
        if (d->dynamic.select && d->dynamic.select(&ctx, index))
            core_screens_pop(s->owner);
        break;
    }
    }
}

static const struct core_hook indexed_hook = {CORE_SITE_Settings_IndexedProperty_slot,
                                              CORE_HOOK_FILTER, 5, indexed_property};

void core_indexed_init(void *resource_bank)
{
    bank = resource_bank;
    core_hook_bind(&indexed_hook);
}
