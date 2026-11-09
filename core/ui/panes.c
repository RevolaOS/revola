/* SPDX-License-Identifier: GPL-3.0-only */

/* The Settings preview pane. Every stock Settings row has a layout of its own
 * whose pane instance binds a caption, an icon and a detail line, and the row's
 * delayedselected binding switches to it. Rows extensions add share one layout
 * instead: their binding names the row's pane handler, which makes the row the
 * pane's current one, and switches to the shared layout, whose pane views are
 * bound to properties answered from that row (label, caption, bitmap). The
 * layout is built when the first such row registers, so it costs nothing until
 * an extension adds a Settings row. */

#include "ui/panes.h"
#include "osos-2.0.5.h"
#include "ui/clones.h"
#include "ui/menus.h"
#include "ui/resources.h"
#include "ui/screens.h"
#include "hooks/sites.h"
#include <stddef.h>

const char core_panes_layout_name[] = "RvSettingsPane";

static void *bank;
static uint32_t layout;  /* the shared layout; its ids follow */
static uint32_t current; /* item id of the row the pane shows */

#define SSIN (layout + 1)
#define PANE (layout + 2)
#define CAPTION (layout + 3)
#define DETAIL (layout + 4)
#define ICON (layout + 5)
#define IDS 6

static const void *stock(uint32_t type, uint32_t id, uint32_t *bytes)
{
    return ResourceBank_GetResource(bank, type, id, bytes);
}

uint32_t core_panes_stock_layout(uint32_t layout_id)
{
    uint32_t bytes;
    const void *slyt = stock(Resource_SLyt, layout_id, &bytes);
    for (uint32_t l = 0; slyt && l < core_blocks_count(slyt); l++) {
        struct core_block list, instance;
        core_block_at(slyt, l, &list);
        const void *ssin = stock(Resource_SSin, core_word_at(list.data, 0), &bytes);
        for (uint32_t i = 0; ssin && core_block_at(ssin, i, &instance); i++)
            if (core_word_at(instance.data, SsinInstance_template) == RES_SettingsPreview_Template)
                return core_word_at(instance.data, SsinInstance_substitute);
    }
    return 0;
}

uint32_t core_panes_write(struct core_writer *w, uint32_t caption, uint32_t detail, uint32_t icon)
{
    uint32_t bytes, stock_pane = core_panes_stock_layout(RES_Settings_MainMenu_Layout);
    const void *vlyt = stock_pane ? stock(Resource_VLyt, stock_pane, &bytes) : NULL;
    if (!vlyt)
        return 0;
    uint32_t views = core_blocks_count(vlyt);
    core_write_word(w, views);
    for (uint32_t v = 0; v < views; v++) {
        struct core_block view, option;
        core_block_at(vlyt, v, &view);
        uint32_t source = view.kind == RES_SettingsPreview_Caption_Text ? caption
                          : view.kind == RES_Settings_InfoDetail_View   ? detail
                          : view.kind == RES_Settings_Icon_View         ? icon
                                                                        : 0;
        uint32_t begin = core_write_block_begin(w, view.kind);
        core_write_word(w, core_blocks_count(view.data));
        for (uint32_t o = 0; core_block_at(view.data, o, &option); o++) {
            uint32_t b = core_write_block_begin(w, option.kind);
            if (option.kind == Resource_SORC && source)
                core_write_word(w, source);
            else if (option.kind == ViewOption_Visible)
                core_write_bytes(w, source ? "\1" : "", 1);
            else
                core_write_bytes(w, option.data, option.bytes);
            core_write_block_end(w, b);
        }
        core_write_block_end(w, begin);
    }
    return stock_pane;
}

void core_panes_store(uint32_t id, const void *data, uint32_t bytes, uint32_t stock_pane)
{
    uint32_t events_bytes;
    const void *events = stock(Resource_TEVT, stock_pane, &events_bytes);
    ResourceBank_SetResource(bank, Resource_VLyt, id, data, bytes);
    if (events) /* the layout's event tables are loaded by this id too */
        ResourceBank_SetResource(bank, Resource_TEVT, id, events, events_bytes);
}

/* The shared layout: the Legal row's layout with its pane replaced, listed in
 * the Settings screen's SLst (read on every push) and named for the bindings'
 * navigator.SwitchLayout. */
static int build(void)
{
    uint32_t slst_bytes, events_bytes, entry_bytes, id = core_resource_take(0, IDS);
    const void *slst = stock(Resource_SLst, RES_SettingsMenu_Main_Screen, &slst_bytes);
    const void *events = stock(Resource_SEVT, RES_SettingsMenus_Legal_Layout, &events_bytes);
    const uint8_t *entry =
        slst ? core_block_with_word(slst, 0, RES_SettingsMenus_Legal_Layout, &entry_bytes) : NULL;
    uint32_t scratch_bytes = slst_bytes + 16 > 1024 ? slst_bytes + 16 : 1024;
    uint8_t *scratch = id && events && entry ? Heap_AllocateZeroed(scratch_bytes) : NULL;
    if (!scratch)
        return 0;
    layout = id;
    struct core_writer w = {scratch, 0};
    uint32_t stock_pane = core_panes_write(&w, CAPTION, DETAIL, ICON);
    int built = stock_pane != 0;
    if (built) {
        core_panes_store(PANE, scratch, w.at, core_panes_stock_layout(RES_SettingsMenus_Legal_Layout));
        core_set_source(bank, CAPTION, CAPTION, SourceDestination_Title);
        core_set_source(bank, DETAIL, DETAIL, SourceDestination_Title);
        core_set_source(bank, ICON, ICON, SourceDestination_Title);
        uint32_t next = SSIN;
        struct layout_clone c = {RES_SettingsMenus_Legal_Layout, layout, 0, 0, 0,
                                 core_panes_stock_layout(RES_SettingsMenus_Legal_Layout), PANE,
                                 0, NULL};
        built = core_clone_layout(&c, &next, scratch, scratch_bytes) && next <= PANE;
    }
    if (built) {
        ResourceBank_SetResource(bank, Resource_SEVT, layout, events, events_bytes);
        /* The stock SLst with one more block, a copy of the Legal layout's. */
        w.at = 0;
        core_write_word(&w, core_blocks_count(slst) + 1);
        core_write_bytes(&w, (const uint8_t *)slst + 4, slst_bytes - 4);
        uint32_t begin = core_write_block_begin(&w, 0);
        uint32_t words[2] = {0, layout};
        core_write_copy(&w, entry, entry_bytes, words, 1);
        core_write_block_end(&w, begin);
        ResourceBank_SetResource(bank, Resource_SLst, RES_SettingsMenu_Main_Screen, scratch, w.at);
        uint32_t name[CppString_bytes / 4], allocator = 0;
        CppString_InitBytes(name, core_panes_layout_name, sizeof core_panes_layout_name - 1,
                            &allocator);
        *ResourceNames_FindOrInsertSlot(ResourceNameMap, name) = layout;
        CppString_Destroy(name);
    }
    Heap_Free(scratch);
    if (!built)
        layout = 0;
    return built;
}

int core_panes_prepare(void)
{
    return layout || build();
}

void core_panes_show(uint32_t item)
{
    current = item;
    core_screens_notify(CAPTION);
    core_screens_notify(DETAIL);
    core_screens_notify(ICON);
}

int core_panes_property(uint32_t type, uint32_t id, uint32_t out)
{
    const struct rv_entry *e;
    if (!layout || id - CAPTION >= 3 || !(e = core_menus_entry(current)))
        return 0;
    if (id == ICON && type == Property_Bitmap)
        *(const void **)out = e->bitmap;
    else if (id != ICON && type == Property_String)
        *(const char **)out = id == CAPTION ? e->row.label : e->caption ? e->caption : "";
    else
        return 0;
    return 1;
}

void core_panes_init(void *resource_bank)
{
    bank = resource_bank;
}
