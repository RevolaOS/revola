/* SPDX-License-Identifier: GPL-3.0-only */

/* The resources behind a screen, cloned from stock ones on its first push. A
 * plain list is the Notes list layout over a Settings menu controller with an
 * ITEM table of Legal-row copies (Sound Check's for toggles); a text page is the
 * Legal screen over a core-built template of paragraph views. Titles and
 * paragraphs are Settings model properties the screen registry answers, so the
 * clones hold ids, never text. Everything stored through SetResource is copied
 * by Apple, so the buffers here are transient. */

#include "ui/clones.h"
#include "osos-2.0.5.h"
#include "ui/resources.h"
#include "ui/screens.h"
#include "hooks/sites.h"
#include <stddef.h>

#define ROW_BYTES 0x98
/* Transient buffer for one resource at a time; a list's ITEM table may need more. */
#define SCRATCH_BYTES 1536
/* Copied beside a VCvs when the stock instance has one. TODO: symbols header. */
#define Resource_VCrv 0x56437276u

static void *bank;

static const void *stock(uint32_t type, uint32_t id, uint32_t *bytes)
{
    return ResourceBank_GetResource(bank, type, id, bytes);
}

static void set(uint32_t type, uint32_t id, const void *data, uint32_t bytes)
{
    ResourceBank_SetResource(bank, type, id, data, bytes);
}

uint32_t core_clone_ids(const struct rv_screen *d)
{
    switch (d->kind) {
    case RV_SCREEN_LIST:
        /* screen, layout, title, items, 4 for layout clones, 2 per row */
        return 8 + 2 * d->list.count;
    case RV_SCREEN_TEXT:
        /* screen, layout, title, 5 for layout clones, one source per paragraph */
        return 8 + (d->text.count < CORE_MAX_PARAGRAPHS ? d->text.count : CORE_MAX_PARAGRAPHS);
    default:
        return 8;
    }
}

/* A copy of a stock resource with words changed; `words` are {offset, value}
 * pairs. Writes through `w`, so a NULL writer only measures. */
static void copy_with(struct core_writer *w, const void *data, uint32_t bytes,
                      const uint32_t *words, uint32_t count)
{
    uint32_t start = w->at;
    core_write_bytes(w, data, bytes);
    if (w->out)
        for (uint32_t i = 0; i < count; i++)
            for (unsigned b = 0; b < 4; b++)
                w->out[start + words[2 * i] + b] = (uint8_t)(words[2 * i + 1] >> (8 * b));
}

static void put_word(uint8_t *at, uint32_t value)
{
    for (unsigned b = 0; b < 4; b++)
        at[b] = (uint8_t)(value >> (8 * b));
}

/* Sources for `count` consecutive views of a template, applied to the instance
 * that uses the template so the views are rebound on every display. */
struct view_sources {
    uint32_t view;   /* first view id */
    uint32_t source; /* first source id */
    uint32_t count;
};

/* The VCvs of one instance with the title view's source and the item table
 * rebound and `extra` sources appended: a block per view holding option blocks,
 * or one ViewBinding_ItemTable block naming the ITEM table. `vcvs` may be NULL
 * when the instance has none. Returns the bytes written, 0 when nothing changed. */
static uint32_t rebind_views(struct core_writer *w, const void *vcvs, uint32_t title,
                             uint32_t items, const struct view_sources *extra)
{
    uint32_t start = w->at, changed = 0, count = vcvs ? core_blocks_count(vcvs) : 0;
    core_write_word(w, count + (extra ? extra->count : 0));
    for (uint32_t i = 0; i < count; i++) {
        struct core_block view;
        if (!core_block_at(vcvs, i, &view))
            break;
        uint32_t begin = core_write_block_begin(w, view.kind);
        if (view.kind == ViewBinding_ItemTable) {
            uint32_t words[2] = {ItemTableBinding_item_table, items};
            copy_with(w, view.data, view.bytes, words, items && view.bytes >= 8);
            changed |= items != 0;
            core_write_block_end(w, begin);
            continue;
        }
        core_write_word(w, core_blocks_count(view.data));
        for (uint32_t o = 0;; o++) {
            struct core_block option;
            if (!core_block_at(view.data, o, &option))
                break;
            uint32_t b = core_write_block_begin(w, option.kind);
            if (option.kind == Resource_SORC && view.kind == RES_StatusBarWhite_Text && title) {
                core_write_word(w, title);
                changed = 1;
            } else {
                core_write_bytes(w, option.data, option.bytes);
            }
            core_write_block_end(w, b);
        }
        core_write_block_end(w, begin);
    }
    for (uint32_t i = 0; extra && i < extra->count; i++) {
        uint32_t begin = core_write_block_begin(w, extra->view + i);
        core_write_word(w, 1);
        uint32_t option = core_write_block_begin(w, Resource_SORC);
        core_write_word(w, extra->source + i);
        core_write_block_end(w, option);
        core_write_block_end(w, begin);
        changed = 1;
    }
    return changed ? w->at - start : 0;
}

/* Clones a stock layout: every SSin list and the VCvs of instances whose
 * bindings change; template `from` is replaced by `to` (0 for none) and its
 * instance gets the `extra` sources. Uses ids from `next`. The clone is stored
 * as it is built, so the scratch holds one layout, one SSin and one VCvs. */
static int clone_layout(uint32_t stock_layout, uint32_t layout, uint32_t title, uint32_t items,
                        uint32_t from, uint32_t to, const struct view_sources *extra,
                        uint32_t *next, uint8_t *scratch, uint32_t scratch_bytes)
{
    uint32_t bytes, lists;
    const void *slyt = stock(Resource_SLyt, stock_layout, &bytes);
    if (!slyt || bytes > scratch_bytes)
        return 0;
    uint8_t *layout_copy = scratch;
    for (uint32_t i = 0; i < bytes; i++)
        layout_copy[i] = ((const uint8_t *)slyt)[i];
    lists = core_blocks_count(slyt);
    for (uint32_t l = 0; l < lists; l++) {
        struct core_block list;
        if (!core_block_at(layout_copy, l, &list))
            return 0;
        uint32_t ssin_bytes;
        const void *ssin = stock(Resource_SSin, core_word_at(list.data, 0), &ssin_bytes);
        if (!ssin || bytes + ssin_bytes > scratch_bytes)
            return 0;
        uint8_t *ssin_copy = scratch + bytes;
        for (uint32_t i = 0; i < ssin_bytes; i++)
            ssin_copy[i] = ((const uint8_t *)ssin)[i];
        for (uint32_t n = 0;; n++) {
            struct core_block instance;
            if (!core_block_at(ssin_copy, n, &instance))
                break;
            uint8_t *words = (uint8_t *)instance.data;
            uint32_t view_id = core_word_at(words, SsinInstance_view_bindings), vcvs_bytes;
            const void *vcvs = view_id ? stock(Resource_VCvs, view_id, &vcvs_bytes) : NULL;
            const struct view_sources *sources =
                from && core_word_at(words, SsinInstance_template) == from ? extra : NULL;
            if (vcvs || sources) {
                struct core_writer measure = {NULL, 0};
                if (rebind_views(&measure, vcvs, title, items, sources)) {
                    uint8_t *out = scratch + bytes + ssin_bytes;
                    if (bytes + ssin_bytes + measure.at > scratch_bytes)
                        return 0;
                    struct core_writer w = {out, 0};
                    rebind_views(&w, vcvs, title, items, sources);
                    uint32_t id = (*next)++;
                    set(Resource_VCvs, id, out, w.at);
                    uint32_t vcrv_bytes;
                    const void *vcrv = vcvs ? stock(Resource_VCrv, view_id, &vcrv_bytes) : NULL;
                    if (vcrv)
                        set(Resource_VCrv, id, vcrv, vcrv_bytes);
                    put_word(words + SsinInstance_view_bindings, id);
                }
            }
            for (uint32_t offset = SsinInstance_template; offset <= SsinInstance_substitute;
                 offset += 4)
                if (from && core_word_at(words, offset) == from)
                    put_word(words + offset, to);
        }
        uint32_t list_id = (*next)++;
        set(Resource_SSin, list_id, ssin_copy, ssin_bytes);
        put_word((uint8_t *)list.data, list_id);
    }
    set(Resource_SLyt, layout, layout_copy, bytes);
    return 1;
}

/* The screen's own resources: SCST from `controller_screen`, SLst from
 * `layouts_screen` with the first layout replaced, CEVT from `events`. */
static int set_screen(uint32_t screen, uint32_t layout, uint32_t controller_screen,
                      uint32_t layouts_screen, const void *events, uint32_t event_bytes,
                      uint8_t *scratch, uint32_t scratch_bytes)
{
    uint32_t bytes;
    const void *scst = stock(Resource_SCST, controller_screen, &bytes);
    if (!scst)
        return 0;
    set(Resource_SCST, screen, scst, bytes);
    const void *slst = stock(Resource_SLst, layouts_screen, &bytes);
    if (!slst || bytes > scratch_bytes)
        return 0;
    struct core_writer w = {scratch, 0};
    uint32_t words[2] = {12, layout}; /* word 0 of the first block: count, bytes, kind before it */
    copy_with(&w, slst, bytes, words, 1);
    set(Resource_SLst, screen, scratch, bytes);
    set(Resource_CEVT, screen, events, event_bytes);
    return 1;
}

/* Menu pops the screen, holding it returns to the main menu. */
static void write_layout_events(struct core_writer *w)
{
    core_write_word(w, 2);
    core_write_event(w, "button.menu.up", "", "navigator.PopTopScreen", NULL, 0);
    core_write_event(w, "button.menu.pressandhold", "", "navigator.PopToMainScreen", NULL, 0);
}

/* list.pid.<item>.chosen for every item, each naming its Rv handler. */
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

void core_clone_read_events(void *layout, const void *stream_bytes, uint32_t bytes)
{
    /* TODO: CppString and StringInputStream sizes from the symbols header. */
    uint32_t string[8], stream[31], allocator = 0;
    CppString_InitBytes(string, stream_bytes, bytes, &allocator);
    StringInputStream_Init(stream, string, 0xc);
    CppString_Destroy(string);
    UIEventMap_ReadStream(stream, (uint8_t *)layout + Layout_events);
    StringInputStream_Destroy(stream, 2, 0);
}

/* Templates Apple enumerates once at UI start, so the core builds its own then:
 * one text page of CORE_MAX_PARAGRAPHS paragraph views. Ids from the core's
 * block: the template, then one view id per paragraph, which is also the id of
 * the empty string source the view binds at boot; a text screen rebinds the
 * views to its own sources through its VCvs each time it is displayed. */
static uint32_t text_template;
static void *template_data;
static uint32_t template_bytes;

/* Views: the stock template's second block is a paragraph, its last the bottom
 * margin; each paragraph hangs below the one before. View offsets count from the
 * screen top, so the first paragraph starts below the 20 px title bar with the
 * stock page's 15 px top margin. */
#define TEXT_TOP 35
#define PARAGRAPH_GAP 12
static void build_text_template(void)
{
    uint32_t bytes;
    const void *views = stock(Resource_View, RES_Settings_Legal_Template, &bytes);
    struct core_block paragraph, margin;
    text_template = core_resource_take(0, 2 + CORE_MAX_PARAGRAPHS); /* + the margin view */
    if (!text_template || !views || !core_block_at(views, 1, &paragraph) ||
        !core_block_at(views, core_blocks_count(views) - 1, &margin))
        return;
    struct core_writer w = {NULL, 0};
    for (int pass = 0; pass < 2; pass++) {
        w.at = 0;
        core_write_word(&w, CORE_MAX_PARAGRAPHS + 1);
        uint32_t previous = 0;
        for (uint32_t i = 0; i < CORE_MAX_PARAGRAPHS; i++) {
            uint32_t id = text_template + 1 + i; /* view id and its property */
            uint32_t words[10] = {ViewDescriptor_top_offset, previous ? PARAGRAPH_GAP : TEXT_TOP,
                                  ViewDescriptor_anchor, previous,
                                  ViewDescriptor_anchor_mode, previous ? AnchorMode_Bottom : 0,
                                  ViewDescriptor_id, id, TextViewDescriptor_source, id};
            uint32_t begin = core_write_block_begin(&w, paragraph.kind);
            copy_with(&w, paragraph.data, paragraph.bytes, words, 5);
            core_write_block_end(&w, begin);
            /* Bound when the template is built at boot, before the Settings model
             * exists, so the boot source is an empty string resource. */
            if (pass) {
                uint32_t source[6] = {1, 12, Resource_SORC, Service_String, id, SourceDestination_Title};
                set(Resource_SORC, id, source, sizeof source);
                set(Resource_String, id, "", 1);
            }
            previous = id;
        }
        uint32_t words[4] = {ViewDescriptor_anchor, previous, ViewDescriptor_id,
                             text_template + 1 + CORE_MAX_PARAGRAPHS};
        uint32_t begin = core_write_block_begin(&w, margin.kind);
        copy_with(&w, margin.data, margin.bytes, words, 2);
        core_write_block_end(&w, begin);
        if (!pass) {
            w.out = Heap_AllocateZeroed(w.at);
            if (!w.out)
                return;
        }
    }
    template_data = w.out;
    template_bytes = w.at;
    /* The enumeration yields the views once; the child views are built from the
     * resource looked up by id. */
    set(Resource_View, text_template, w.out, w.at);
    const void *tmlt = stock(Resource_TMLT, RES_Settings_Legal_Template, &bytes);
    if (tmlt)
        set(Resource_TMLT, text_template, tmlt, bytes);
}

/* Apple's template enumeration walks the ROM index only; the core's templates
 * follow it. Called again with the last id handed out, so the next one follows. */
static uint32_t next_template(uint32_t resource_bank, uint32_t id, uint32_t bytes,
                              const struct core_next *next)
{
    uint32_t result = core_next_call(next, resource_bank, id, bytes, 0);
    if (result || !template_data || *(uint32_t *)id == text_template)
        return result;
    *(uint32_t *)id = text_template;
    *(uint32_t *)bytes = template_bytes;
    return (uint32_t)template_data;
}

/* Ids of a list: id screen, +1 layout, +2 title, +3 items, +4.. layout clones,
 * then per row the item id and its source. */
static int clone_list(struct core_screen *s, uint8_t *scratch, uint32_t scratch_bytes)
{
    const struct rv_screen *d = s->declaration;
    uint32_t next = s->id + 4, rows = CORE_SCREEN_ROWS(s);
    core_set_source(bank, s->id + 2, s->id + 2, 1);

    uint32_t table_bytes;
    const void *table = stock(Resource_ITEM, RES_SettingsMenu_Items, &table_bytes);
    uint32_t row_bytes, toggle_bytes;
    const uint8_t *plain = table ? core_block_with_word(table, MenuRow_id, RES_SettingsMenu_Legal_Item, &row_bytes) : NULL;
    const uint8_t *toggle = table ? core_block_with_word(table, MenuRow_id, RES_SettingsMenu_SoundCheck_Item, &toggle_bytes) : NULL;
    if (!plain || !toggle || row_bytes != ROW_BYTES || toggle_bytes != ROW_BYTES)
        return 0;
    struct core_writer w = {scratch, 0};
    core_write_word(&w, d->list.count);
    for (uint32_t i = 0; i < d->list.count; i++) {
        uint32_t item = rows + 2 * i, source = item + 1;
        int is_toggle = d->list.rows[i].kind == RV_ROW_TOGGLE;
        uint32_t words[4] = {MenuRow_id, item, MenuRow_title, source};
        uint32_t begin = core_write_block_begin(&w, 0);
        uint32_t at = w.at;
        copy_with(&w, is_toggle ? toggle : plain, ROW_BYTES, words, 2);
        scratch[at + MenuRow_title_from_model] = 1;
        core_write_block_end(&w, begin);
        /* The item id is the title property, the source id the value's. */
        if (is_toggle)
            core_set_source2(bank, source, item, source);
        else
            core_set_source(bank, source, item, 1);
    }
    set(Resource_ITEM, s->id + 3, scratch, w.at);
    if (!clone_layout(RES_Notes_List_Screen_Alt_Default, s->id + 1, s->id + 2, s->id + 3, 0, 0, NULL,
                      &next, scratch, scratch_bytes) ||
        next > rows)
        return 0;
    w.at = 0;
    write_layout_events(&w);
    set(Resource_SEVT, s->id + 1, scratch, w.at);
    w.at = 0;
    write_row_events(&w, rows, d->list.count);
    return set_screen(s->id, s->id + 1, RES_SettingsMenu_Main_Screen, RES_Notes_List_Screen_Alt, scratch,
                      w.at, scratch + w.at, scratch_bytes - w.at);
}

int core_clone_list(struct core_screen *s)
{
    uint32_t count = s->declaration->list.count;
    if (count > CORE_MAX_LIST_ROWS)
        return 0;
    /* The ITEM table, the row events and the SLst copy each fit SCRATCH_BYTES
     * or the table's own size, whichever is larger. */
    uint32_t bytes = 4 + count * (8 + ROW_BYTES);
    if (bytes < SCRATCH_BYTES)
        bytes = SCRATCH_BYTES;
    uint8_t *scratch = Heap_AllocateZeroed(bytes);
    if (!scratch)
        return 0;
    int built = clone_list(s, scratch, bytes);
    Heap_Free(scratch);
    return built;
}

/* Ids of a text page: id screen, +1 layout, +2 title, +3.. layout clones, +8..
 * one source per paragraph, bound to the text template's views by the layout. */
int core_clone_text(struct core_screen *s)
{
    uint32_t next = s->id + 3, rows = CORE_SCREEN_ROWS(s);
    if (!template_data) /* a VCvs block naming a view that does not exist panics */
        return 0;
    uint8_t *scratch = Heap_AllocateZeroed(SCRATCH_BYTES);
    if (!scratch)
        return 0;
    struct view_sources paragraphs = {text_template + 1, rows, s->ids - 8};
    core_set_source(bank, s->id + 2, s->id + 2, 1);
    for (uint32_t i = 0; i < paragraphs.count; i++)
        core_set_source(bank, rows + i, rows + i, 1);
    uint32_t none = 0; /* the Legal controller handles Menu itself: no layout events */
    int built = clone_layout(RES_Settings_Legal_Layout, s->id + 1, s->id + 2, 0,
                             RES_Settings_Legal_Template, text_template, &paragraphs, &next,
                             scratch, SCRATCH_BYTES) &&
                next <= rows;
    if (built) {
        set(Resource_SEVT, s->id + 1, &none, 4);
        built = set_screen(s->id, s->id + 1, RES_Settings_Legal_Screen, RES_Settings_Legal_Screen,
                           &none, 4, scratch, SCRATCH_BYTES);
    }
    Heap_Free(scratch);
    return built;
}

static const struct core_hook next_template_hook = {CORE_SITE_ResourceBank_NextTemplate_call,
                                                    CORE_HOOK_FILTER, 0, next_template};

void core_clones_init(void *resource_bank)
{
    bank = resource_bank;
    build_text_template();
    core_hook_bind(&next_template_hook);
}
