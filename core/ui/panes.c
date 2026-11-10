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
#include "ui/bitmaps.h"
#include "ui/clones.h"
#include "ui/lists.h"
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
#define BOX (layout + 6)
#define IDS 7

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
                return core_word_at(instance.data, SsinInstance_layout_events_id);
    }
    return 0;
}

uint32_t core_panes_write(struct core_writer *w, uint32_t caption, uint32_t detail, uint32_t icon,
                          uint32_t box)
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
        uint32_t source = view.kind == RES_SettingsPreview_Caption_Text      ? caption
                          : view.kind == RES_Settings_InfoDetail_View        ? detail
                          : view.kind == RES_Settings_Icon_View              ? icon
                          : view.kind == RES_SettingsPreview_Background_View ? box
                                                                             : 0;
        /* The box's bitmap comes from the template; the stock layouts only show
         * or hide it, so a source for it is an option of its own. */
        int add_source = view.kind == RES_SettingsPreview_Background_View && box;
        uint32_t begin = core_write_block_begin(w, view.kind);
        core_write_word(w, core_blocks_count(view.data) + (uint32_t)add_source);
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
        if (add_source) {
            uint32_t b = core_write_block_begin(w, Resource_SORC);
            core_write_word(w, box);
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
    core_resource_set(Resource_VLyt, id, data, bytes);
    if (events) /* the layout's event tables are loaded by this id too */
        core_resource_set(Resource_TEVT, id, events, events_bytes);
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
    uint32_t stock_pane = core_panes_write(&w, CAPTION, DETAIL, ICON, BOX);
    int built = stock_pane != 0;
    if (built) {
        core_panes_store(PANE, scratch, w.at, core_panes_stock_layout(RES_SettingsMenus_Legal_Layout));
        core_set_source(CAPTION, CAPTION, SourceDestination_Title);
        core_set_source(DETAIL, DETAIL, SourceDestination_Title);
        core_set_source(ICON, ICON, SourceDestination_Title);
        core_set_source(BOX, BOX, SourceDestination_Title);
        uint32_t next = SSIN;
        struct layout_clone c = {RES_SettingsMenus_Legal_Layout, layout, 0, 0, 0,
                                 core_panes_stock_layout(RES_SettingsMenus_Legal_Layout), PANE,
                                 0, NULL};
        built = core_clone_layout(&c, &next, scratch, scratch_bytes) && next <= PANE;
    }
    if (built) {
        core_resource_set(Resource_SEVT, layout, events, events_bytes);
        /* The stock SLst with one more block, a copy of the Legal layout's. */
        w.at = 0;
        core_write_word(&w, core_blocks_count(slst) + 1);
        core_write_bytes(&w, (const uint8_t *)slst + 4, slst_bytes - 4);
        uint32_t begin = core_write_block_begin(&w, 0);
        uint32_t words[2] = {0, layout};
        core_write_copy(&w, entry, entry_bytes, words, 1);
        core_write_block_end(&w, begin);
        core_resource_set(Resource_SLst, RES_SettingsMenu_Main_Screen, scratch, w.at);
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
    core_screens_notify(BOX);
}

/* The box behind an icon, or a transparent pixel for a row without one: an
 * image view keeps its last bitmap when it is answered NULL. */
static const void *box_for(const struct rv_entry *e)
{
    static uint32_t clear[(BMap_payload + 4) / 4];
    if (e->bitmap && !e->plain_icon)
        return ResourceService_GetBitmap(UI_GetResourceService(),
                                         RES_SettingsPreview_Background_Image);
    if (!clear[0]) {
        uint8_t *b = (uint8_t *)clear;
        *(uint16_t *)(b + BMap_format) = BMapFormat_BGRA8888;
        *(uint16_t *)(b + BMap_stride) = 4;
        *(uint16_t *)(b + BMap_depth) = 32;
        ((int32_t *)(b + BMap_bounds))[2] = ((int32_t *)(b + BMap_bounds))[3] = 1;
        *(uint32_t *)(b + BMap_payload_bytes) = 4;
    }
    return clear;
}

int core_panes_property(uint32_t type, uint32_t id, uint32_t out)
{
    const struct rv_entry *e;
    struct rv_extension_record *owner;
    if (!layout || id - CAPTION >= 4 || !(e = core_menus_entry(current, &owner)))
        return 0;
    if (id == ICON && type == Property_Bitmap)
        *(const void **)out = core_bitmap(owner, e->bitmap);
    else if (id == BOX && type == Property_Bitmap)
        *(const void **)out = box_for(e);
    else if (id == CAPTION && type == Property_String)
        *(const char **)out = core_row_label(&e->row);
    else if (id == DETAIL && type == Property_String)
        *(const char **)out = e->caption ? e->caption : "";
    else
        return 0;
    return 1;
}

int core_panes_list_property(struct core_screen *s, uint32_t type, uint32_t id, uint32_t out)
{
    const struct rv_screen *d = s->declaration;
    if (!core_lists_kind(d->kind))
        return 0;
    uint32_t caption = CORE_LIST_PANE_CAPTION(core_lists_screen_id(s, 1));
    if (id == caption && type == Property_String)
        *(const char **)out = d->caption ? d->caption : "";
    else if (id == caption + 1 && type == Property_Bitmap)
        *(const void **)out = core_bitmap(s->owner, d->bitmap);
    else if (id == caption + 2 && type == Property_Bitmap)
        *(const void **)out = ResourceService_GetBitmap(UI_GetResourceService(),
                                                        RES_SettingsPreview_Background_Image);
    else
        return 0;
    return 1;
}

void core_panes_init(void *resource_bank)
{
    bank = resource_bank;
}
