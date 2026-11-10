/* SPDX-License-Identifier: GPL-3.0-only */

/* List screens: a Settings menu list controller over the Notes list layout
 * (full width) or the Settings > Main Menu layout (split, the list beside the
 * preview pane), with an ITEM table. A plain list has one row per declared row:
 * Legal-row copies, Sound Check's for toggles. A choice, multi-choice or
 * runtime list has one row that expands into one row per entry (indexed.c):
 * a copy of the Settings > EQ row, the Main Menu row (checkmarks) or the Sound
 * Check row (a detail beside the label, or under it in the two-line style). */

#include "ui/lists.h"
#include "osos-2.0.5.h"
#include "ui/clones.h"
#include "ui/panes.h"
#include "ui/resources.h"
#include "ui/screens.h"
#include <stddef.h>

#define ROW_BYTES 0x98

static void *bank;

static const void *stock(uint32_t type, uint32_t id, uint32_t *bytes)
{
    return ResourceBank_GetResource(bank, type, id, bytes);
}

static const uint8_t *stock_row(uint32_t table, uint32_t id)
{
    uint32_t bytes, row_bytes;
    const void *items = stock(Resource_ITEM, table, &bytes);
    const uint8_t *row = items ? core_block_with_word(items, MenuRow_id, id, &row_bytes) : NULL;
    return row && row_bytes == ROW_BYTES ? row : NULL;
}

static int indexed(const struct core_screen *s)
{
    return core_lists_indexed(s->declaration->kind);
}

/* list.pid.<item>.chosen for `count` items two ids apart, each naming its Rv
 * handler. */
static void write_row_events(struct core_writer *w, uint32_t first_item, uint32_t count)
{
    core_write_word(w, count);
    for (uint32_t i = 0; i < count; i++) {
        char name[40], handler[11], *n = core_put_text(name, name + sizeof name, "list.pid.");
        n = core_put_number(n, name + sizeof name, first_item + 2 * i);
        core_put_text(n, name + sizeof name, ".chosen");
        core_screens_handler_name(handler, first_item + 2 * i);
        core_write_event(w, name, handler, NULL, NULL, 0);
    }
}

uint32_t core_lists_screen_id(const struct core_screen *s, int split)
{
    return split ? s->id + s->ids - CORE_LIST_SPLIT_IDS : s->id;
}

/* A split layout: the Settings > Main Menu layout with the title and the pane
 * rebound (the caption shows the title, the other pane views are hidden), and
 * the list bound by the screen's VSlt (the stock split screens bind their list
 * there, not in the list instance). Takes one id from *next for the pane;
 * the declaration's caption and icon, when set, fill the pane's detail line
 * and icon. */
static int clone_split(struct core_screen *s, uint32_t screen, uint32_t *next, uint8_t *scratch,
                       uint32_t bytes)
{
    struct core_writer w = {scratch, 0};
    const struct rv_screen *d = s->declaration;
    uint32_t caption = CORE_LIST_PANE_CAPTION(screen);
    uint32_t pane_id = (*next)++;
    uint32_t icon = d->bitmap ? caption + 1 : 0, box = d->bitmap ? caption + 2 : 0;
    uint32_t stock_pane = core_panes_write(&w, s->id + 2, d->caption ? caption : 0, icon, box);
    if (!stock_pane)
        return 0;
    for (uint32_t i = 0; i < 3; i++)
        core_set_source(caption + i, caption + i, SourceDestination_Title);
    core_panes_store(pane_id, scratch, w.at, stock_pane);
    uint32_t vslt[5] = {1, 8, ViewBinding_ItemTable, RES_SettingsList_View, s->id + 3};
    core_resource_set(Resource_VSlt, screen, vslt, sizeof vslt);
    struct layout_clone c = {RES_Settings_MainMenu_Layout, screen + 1, RES_StatusBarWhite_Text,
                             s->id + 2, 0, stock_pane, pane_id, 0, NULL};
    return core_clone_layout(&c, next, scratch, bytes);
}

const uint8_t *core_lists_prototype(int value)
{
    return value ? stock_row(RES_SettingsMenu_Items, RES_SettingsMenu_SoundCheck_Item)
                 : stock_row(RES_SettingsMenu_Items, RES_SettingsMenu_Legal_Item);
}

/* The row a list screen's rows are copied from. */
static const uint8_t *prototype(const struct core_screen *s, uint32_t i, int *value)
{
    const struct rv_screen *d = s->declaration;
    uint8_t row_kind = d->kind == RV_SCREEN_LIST ? d->list.rows[i].kind : 0;
    *value = d->kind == RV_SCREEN_RUNTIME_LIST || (d->kind == RV_SCREEN_LIST &&
             (row_kind == RV_ROW_TOGGLE || row_kind == RV_ROW_SETTING));
    if (*value)
        return core_lists_prototype(1);
    switch (d->kind) {
    case RV_SCREEN_CHOICE:
        return stock_row(RES_Settings_EQ_Items, RES_Settings_EQ_Item);
    case RV_SCREEN_MULTI_CHOICE:
        return stock_row(RES_Settings_MainMenu_Items, RES_Settings_MainMenu_Item);
    default:
        return core_lists_prototype(0);
    }
}

/* The shared part of a list: the title source and, per row, the item id (its
 * title property) and the source id (its value property) in the ITEM table. */
static int write_rows(struct core_screen *s, uint8_t *scratch)
{
    const struct rv_screen *d = s->declaration;
    uint32_t rows = CORE_SCREEN_ROWS(s), count = indexed(s) ? 1 : d->list.count;
    core_set_source(s->id + 2, s->id + 2, 1);
    struct core_writer w = {scratch, 0};
    core_write_word(&w, count);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t item = rows + 2 * i, source = item + 1;
        int value;
        const uint8_t *row = prototype(s, i, &value);
        if (!row)
            return 0;
        /* A runtime list with the detail below draws the inline value under the
         * label; with a chevron the model gives the style (indexed.c), as the
         * chevron shows only for a style the model answers. */
        uint8_t detail = d->kind == RV_SCREEN_RUNTIME_LIST ? d->runtime.detail : RV_DETAIL_BESIDE;
        uint32_t words[6] = {MenuRow_id, item, MenuRow_title, source, MenuRow_style,
                             detail == RV_DETAIL_BELOW_SUBMENU ? MenuRowStyle_ModelLayout
                                                               : MenuRowStyle_TwoLine};
        uint32_t begin = core_write_block_begin(&w, 0);
        uint32_t at = w.at;
        core_write_copy(&w, row, ROW_BYTES, words, detail != RV_DETAIL_BESIDE ? 3 : 2);
        scratch[at + MenuRow_title_from_model] = 1;
        core_write_block_end(&w, begin);
        if (value)
            core_set_source2(source, item, source, SourceDestination_InlineValue);
        else if (d->kind == RV_SCREEN_MULTI_CHOICE)
            core_set_source2(source, item, item, SourceDestination_RowBitmap);
        else
            core_set_source(source, item, 1);
    }
    /* A table left from the owner's earlier load stays: a list view that still
     * holds it would read a freed copy if it were replaced. */
    uint32_t bytes;
    if (!stock(Resource_ITEM, s->id + 3, &bytes))
        core_resource_set(Resource_ITEM, s->id + 3, scratch, w.at);
    return 1;
}

/* The layouts and the screen of a list whose rows are in place: the layout
 * clones, the layout events (`layout_events`, else Menu pops) and the screen
 * with `events` as its CEVT. Ids: id screen, +1 layout, +2 title, +3 items, +4..
 * layout clones, then per row the item id and its source id (an indexed list has
 * one such row), then the split layout's screen, layout, pane and layout clones. */
static int clone_layouts(struct core_screen *s, int split, const void *events,
                         uint32_t event_bytes, const void *layout_events,
                         uint32_t layout_event_bytes, uint8_t *scratch, uint32_t scratch_bytes)
{
    uint32_t screen = core_lists_screen_id(s, split), next = split ? screen + 2 : s->id + 4;
    uint32_t last = split ? CORE_LIST_PANE_CAPTION(screen) : CORE_SCREEN_ROWS(s);
    struct layout_clone c = {RES_Notes_List_Screen_Alt_Default, screen + 1, RES_StatusBarWhite_Text,
                             s->id + 2, s->id + 3, 0, 0, 0, NULL};
    if (!(split ? clone_split(s, screen, &next, scratch, scratch_bytes)
                : core_clone_layout(&c, &next, scratch, scratch_bytes)) ||
        next > last)
        return 0;
    struct core_writer w = {scratch, 0};
    if (!layout_events) {
        core_clone_layout_events(&w);
        layout_events = scratch;
        layout_event_bytes = w.at;
    }
    core_resource_set(Resource_SEVT, screen + 1, layout_events, layout_event_bytes);
    return core_clone_screen(screen, screen + 1, RES_SettingsMenu_Main_Screen,
                             split ? RES_Settings_MainMenu_Screen : RES_Notes_List_Screen_Alt,
                             events, event_bytes, scratch, scratch_bytes);
}

/* Room for the row events: list.pid.<item>.chosen naming its Rv handler. */
#define ROW_EVENT_BYTES 52

int core_clone_list(struct core_screen *s, int split)
{
    uint32_t count = indexed(s) ? 1 : s->declaration->list.count;
    if (count > CORE_MAX_LIST_ROWS)
        return 0;
    /* The ITEM table, the layout clones and the SLst copy each fit the scratch
     * or the table's own size, whichever is larger; the row events go first. */
    uint32_t bytes = 4 + count * (8 + ROW_BYTES), events = 4 + count * ROW_EVENT_BYTES;
    if (bytes < CORE_CLONE_SCRATCH_BYTES)
        bytes = CORE_CLONE_SCRATCH_BYTES;
    uint8_t *scratch = Heap_AllocateZeroed(events + bytes);
    if (!scratch)
        return 0;
    int built = s->built || write_rows(s, scratch);
    struct core_writer w = {scratch, 0};
    write_row_events(&w, CORE_SCREEN_ROWS(s), count);
    built = built && clone_layouts(s, split, scratch, w.at, NULL, 0, scratch + events, bytes);
    Heap_Free(scratch);
    return built;
}

int core_clone_list_with(struct core_screen *s, int split, const void *items, uint32_t item_bytes,
                         const void *events, uint32_t event_bytes, const void *layout_events,
                         uint32_t layout_event_bytes)
{
    uint8_t *scratch = Heap_AllocateZeroed(CORE_CLONE_SCRATCH_BYTES);
    if (!scratch)
        return 0;
    core_set_source(s->id + 2, s->id + 2, 1);
    core_resource_set(Resource_ITEM, s->id + 3, items, item_bytes);
    int built = clone_layouts(s, split, events, event_bytes, layout_events, layout_event_bytes,
                              scratch, CORE_CLONE_SCRATCH_BYTES);
    Heap_Free(scratch);
    return built;
}

void core_lists_init(void *resource_bank)
{
    bank = resource_bank;
}
