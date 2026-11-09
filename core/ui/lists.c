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

/* The VLyt the stock split layout gives its preview pane. */
static uint32_t stock_pane_layout(void)
{
    uint32_t bytes;
    const void *slyt = stock(Resource_SLyt, RES_Settings_MainMenu_Layout, &bytes);
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

/* The pane's VLyt with the caption bound to the screen title and every other
 * view hidden. Every pane view is bound: template views keep what the last
 * screen applied. TODO: a pane icon and caption per screen. */
static int write_pane(struct core_writer *w, const void *vlyt, uint32_t title)
{
    uint32_t views = core_blocks_count(vlyt);
    core_write_word(w, views);
    for (uint32_t v = 0; v < views; v++) {
        struct core_block view, option;
        core_block_at(vlyt, v, &view);
        uint32_t begin = core_write_block_begin(w, view.kind);
        core_write_word(w, core_blocks_count(view.data));
        for (uint32_t o = 0; core_block_at(view.data, o, &option); o++) {
            uint32_t b = core_write_block_begin(w, option.kind);
            int caption = view.kind == RES_SettingsPreview_Caption_Text;
            if (option.kind == Resource_SORC && caption)
                core_write_word(w, title);
            else if (option.kind == ViewOption_Visible && !caption)
                core_write_bytes(w, "", 1);
            else
                core_write_bytes(w, option.data, option.bytes);
            core_write_block_end(w, b);
        }
        core_write_block_end(w, begin);
    }
    return 1;
}

/* A split layout: the Settings > Main Menu layout with the title and the pane
 * rebound, and the list bound by the screen's VSlt (the stock split screens
 * bind their list there, not in the list instance). Takes one id from *next for
 * the pane. */
static int clone_split(struct core_screen *s, uint32_t *next, uint8_t *scratch, uint32_t bytes)
{
    uint32_t pane_bytes, stock_pane = stock_pane_layout();
    const void *pane = stock_pane ? stock(Resource_VLyt, stock_pane, &pane_bytes) : NULL;
    if (!pane)
        return 0;
    struct core_writer w = {scratch, 0};
    uint32_t pane_id = (*next)++;
    write_pane(&w, pane, s->id + 2);
    ResourceBank_SetResource(bank, Resource_VLyt, pane_id, scratch, w.at);
    uint32_t events_bytes;
    const void *events = stock(Resource_TEVT, stock_pane, &events_bytes);
    if (events)
        ResourceBank_SetResource(bank, Resource_TEVT, pane_id, events, events_bytes);
    uint32_t vslt[5] = {1, 8, ViewBinding_ItemTable, RES_SettingsList_View, s->id + 3};
    ResourceBank_SetResource(bank, Resource_VSlt, s->id, vslt, sizeof vslt);
    struct layout_clone c = {RES_Settings_MainMenu_Layout, s->id + 1, RES_StatusBarWhite_Text,
                             s->id + 2, 0, stock_pane, pane_id, 0, NULL};
    return core_clone_layout(&c, next, scratch, bytes);
}

/* The row a list screen's rows are copied from. */
static const uint8_t *prototype(const struct core_screen *s, uint32_t i, int *value)
{
    const struct rv_screen *d = s->declaration;
    *value = d->kind == RV_SCREEN_RUNTIME_LIST ||
             (d->kind == RV_SCREEN_LIST && d->list.rows[i].kind == RV_ROW_TOGGLE);
    if (*value)
        return stock_row(RES_SettingsMenu_Items, RES_SettingsMenu_SoundCheck_Item);
    switch (d->kind) {
    case RV_SCREEN_CHOICE:
        return stock_row(RES_Settings_EQ_Items, RES_Settings_EQ_Item);
    case RV_SCREEN_MULTI_CHOICE:
        return stock_row(RES_Settings_MainMenu_Items, RES_Settings_MainMenu_Item);
    default:
        return stock_row(RES_SettingsMenu_Items, RES_SettingsMenu_Legal_Item);
    }
}

/* Ids of a list: id screen, +1 layout, +2 title, +3 items, +4.. layout clones,
 * then per row the item id (its title property) and its source id (its value
 * property). An indexed list has one such row. */
static int clone_list(struct core_screen *s, uint8_t *scratch, uint32_t scratch_bytes)
{
    const struct rv_screen *d = s->declaration;
    uint32_t next = s->id + 4, rows = CORE_SCREEN_ROWS(s);
    uint32_t count = indexed(s) ? 1 : d->list.count;
    core_set_source(bank, s->id + 2, s->id + 2, 1);
    struct core_writer w = {scratch, 0};
    core_write_word(&w, count);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t item = rows + 2 * i, source = item + 1;
        int value;
        const uint8_t *row = prototype(s, i, &value);
        if (!row)
            return 0;
        /* A stacked runtime list draws the inline value under the label. */
        int stacked = d->kind == RV_SCREEN_RUNTIME_LIST && d->runtime.stacked;
        uint32_t words[6] = {MenuRow_id, item, MenuRow_title, source, MenuRow_style,
                             MenuRowStyle_TwoLine};
        uint32_t begin = core_write_block_begin(&w, 0);
        uint32_t at = w.at;
        core_write_copy(&w, row, ROW_BYTES, words, stacked ? 3 : 2);
        scratch[at + MenuRow_title_from_model] = 1;
        core_write_block_end(&w, begin);
        if (value)
            core_set_source2(bank, source, item, source, SourceDestination_InlineValue);
        else if (d->kind == RV_SCREEN_MULTI_CHOICE)
            core_set_source2(bank, source, item, item, SourceDestination_RowBitmap);
        else
            core_set_source(bank, source, item, 1);
    }
    ResourceBank_SetResource(bank, Resource_ITEM, s->id + 3, scratch, w.at);
    int split = d->layout == RV_LAYOUT_SPLIT;
    struct layout_clone c = {RES_Notes_List_Screen_Alt_Default, s->id + 1, RES_StatusBarWhite_Text,
                             s->id + 2, s->id + 3, 0, 0, 0, NULL};
    if (!(split ? clone_split(s, &next, scratch, scratch_bytes)
                : core_clone_layout(&c, &next, scratch, scratch_bytes)) ||
        next > rows)
        return 0;
    w.at = 0;
    core_clone_layout_events(&w);
    ResourceBank_SetResource(bank, Resource_SEVT, s->id + 1, scratch, w.at);
    w.at = 0;
    write_row_events(&w, rows, count);
    return core_clone_screen(s->id, s->id + 1, RES_SettingsMenu_Main_Screen,
                             split ? RES_Settings_MainMenu_Screen : RES_Notes_List_Screen_Alt,
                             scratch, w.at, scratch + w.at, scratch_bytes - w.at);
}

int core_clone_list(struct core_screen *s)
{
    uint32_t count = indexed(s) ? 1 : s->declaration->list.count;
    if (count > CORE_MAX_LIST_ROWS)
        return 0;
    /* The ITEM table, the row events and the SLst copy each fit the scratch
     * or the table's own size, whichever is larger. */
    uint32_t bytes = 4 + count * (8 + ROW_BYTES);
    if (bytes < CORE_CLONE_SCRATCH_BYTES)
        bytes = CORE_CLONE_SCRATCH_BYTES;
    uint8_t *scratch = Heap_AllocateZeroed(bytes);
    if (!scratch)
        return 0;
    int built = clone_list(s, scratch, bytes);
    Heap_Free(scratch);
    return built;
}

void core_lists_init(void *resource_bank)
{
    bank = resource_bank;
}
