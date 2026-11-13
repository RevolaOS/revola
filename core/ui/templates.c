/* SPDX-License-Identifier: GPL-3.0-only */

/* View templates the core adds to Apple's: a text page of paragraph views, a
 * slider with the Brightness bar and value text, a canvas of one core view, an
 * empty one, and the copies of stock pane templates the menu panes take. Each
 * is built the first time a screen or pane needs it, as UI_InitializeViewTemplates
 * builds the stock ones at UI start, and stays for the session, so a kind no
 * screen opens costs nothing. All screens of a kind share the template's views;
 * a screen rebinds them to its own sources through its VCvs each time it is
 * displayed. */

#include "ui/templates.h"
#include "osos-2.0.5.h"
#include "ui/resources.h"
#include "rv_api.h"
#include <stddef.h>

static void *bank;
static const void *copy_second; /* views appended to the stock ones by write_copy */
static int copy_replaced;       /* view replaced by an image view in its box, -1 for none */

static const void *stock(uint32_t type, uint32_t id, uint32_t *bytes)
{
    return ResourceBank_GetResource(bank, type, id, bytes);
}

static void set(uint32_t type, uint32_t id, const void *data, uint32_t bytes)
{
    core_resource_set(type, id, data, bytes);
}

/* The empty string the text views of the text page and the empty message are
 * bound to until a screen rebinds them; made for the first template that
 * needs it. */
static uint32_t empty_string(void)
{
    static uint32_t id;
    if (!id && (id = core_resource_take(0, 1)) != 0) {
        uint32_t source[6] = {1, 12, Resource_SORC, Service_String, id, SourceDestination_Title};
        set(Resource_SORC, id, source, sizeof source);
        set(Resource_String, id, "", 1);
    }
    return id;
}

/* Writes the View resource of a template whose first id is `id`; 0 when a
 * stock view it copies is missing. */
typedef int (*template_writer)(struct core_writer *w, uint32_t id, const void *views);

/* A TMLT's record for layout kind `kind`, or NULL. */
static uint8_t *style_record(const void *tmlt, uint32_t kind)
{
    struct core_block block;
    if (!tmlt || !core_block_at(tmlt, 0, &block) ||
        block.bytes < TemplateResource_styles + (kind + 1) * TemplateStyle_bytes)
        return NULL;
    return (uint8_t *)block.data + TemplateResource_styles + kind * TemplateStyle_bytes;
}

int16_t core_templates_enter_style(uint32_t template, uint32_t kind)
{
    uint32_t bytes;
    const uint8_t *record = style_record(stock(Resource_TMLT, template, &bytes), kind);
    if (!record)
        return -1;
    return (int16_t)(record[TemplateStyle_enter] | record[TemplateStyle_enter + 1] << 8);
}

/* Stock firmware gives the Brightness template enter and leave styles only for
 * a split list (the window slides in from the right and back out); template
 * `id`'s copy gets them for every list kind a core screen opens from. The
 * bank's copy is changed in place, before the template is built from it. */
static void slide_like_brightness(uint32_t id)
{
    static const uint8_t kinds[] = {LayoutKind_Menu, LayoutKind_SplitList, LayoutKind_List};
    uint32_t bytes;
    const uint8_t *from =
        style_record(stock(Resource_TMLT, RES_Settings_Brightness_Template, &bytes), LayoutKind_SplitList);
    const void *copy = stock(Resource_TMLT, id, &bytes);
    struct core_block block;
    if (!from || !copy || !core_block_at(copy, 0, &block))
        return;
    for (uint32_t i = 0; i < sizeof kinds; i++) {
        uint8_t *to = style_record(copy, kinds[i]);
        if (!to)
            return;
        for (uint32_t b = 0; b < 2; b++) {
            to[TemplateStyle_enter + b] = from[TemplateStyle_enter + b];
            to[TemplateStyle_leave + b] = from[TemplateStyle_leave + b];
        }
    }
    ((uint8_t *)block.data)[TemplateResource_has_styles] = 1;
}

/* Builds the template of View `id` as UI_InitializeViewTemplates builds each
 * stock one: a zeroed options block whose edge offsets are the owner's bounds
 * (its parent's when it has one), the owner as event target when it has one,
 * and the standard kind. The owner's children are walked in order when a
 * template is looked up, so one appended now is found. 0 when Apple hands no
 * template view back. */
static int construct(uint32_t id)
{
    uint8_t *owner = RV_FIELD(UI_GetLayoutManager(), LayoutManager_template_owner, uint8_t *);
    uint8_t *parent = RV_FIELD(owner, View_parent, uint8_t *);
    const uint8_t *bounds = (parent ? parent : owner) + View_bounds;
    uint32_t *options = Heap_AllocateZeroed(ViewOptions_bytes);
    if (!options)
        return 0;
    options[ViewOptions_left_offset / 4] = RV_FIELD(bounds, Rect_left, uint32_t);
    options[ViewOptions_top_offset / 4] = RV_FIELD(bounds, Rect_top, uint32_t);
    options[ViewOptions_bottom_offset / 4] = RV_FIELD(bounds, Rect_bottom, uint32_t);
    options[ViewOptions_right_offset / 4] = RV_FIELD(bounds, Rect_right, uint32_t);
    int (*has_target)(void *) =
        RV_FIELD(RV_FIELD(owner, 0, void **), View_has_event_target_method, void *);
    void *built = UI_CreateTemplateFromResource(RV_FIELD(owner, TemplateOwner_context, void *), owner,
                                                has_target(owner) ? owner : NULL, options, id, 0);
    Heap_Free(options);
    uint8_t *view = built ? Object_GetInterface(built, Interface_Template) : NULL;
    if (!view)
        return 0;
    view[TemplateView_kind] = TemplateKind_Standard;
    return 1;
}

/* Takes `ids` ids, writes the View with `write` (measured first), stores it
 * with the stock template's TMLT and builds the template from them. The bank
 * keeps the only copy of the View: the template builds its child views from
 * it. 0 when a stock resource is missing or the heap is full. */
static uint32_t build(uint32_t stock_template, uint32_t ids, template_writer write)
{
    uint32_t bytes;
    const void *views = stock(Resource_View, stock_template, &bytes);
    struct core_writer w = {NULL, 0};
    if (!views || !write(&w, 0, views))
        return 0;
    uint32_t id = core_resource_take(0, ids);
    if (!id)
        return 0;
    w.out = Heap_AllocateZeroed(w.at);
    if (!w.out)
        return 0;
    w.at = 0;
    write(&w, id, views);
    set(Resource_View, id, w.out, w.at);
    Heap_Free(w.out);
    const void *tmlt = stock(Resource_TMLT, stock_template, &bytes);
    if (tmlt)
        set(Resource_TMLT, id, tmlt, bytes);
    return id;
}

/* Text page: the stock Legal template's second block is a paragraph, its last
 * the bottom margin; each paragraph hangs below the one before. View offsets
 * count from the screen top, so the first paragraph starts below the 20 px title
 * bar with the stock page's 15 px top margin. */
#define TEXT_TOP 35
#define PARAGRAPH_GAP 12
static int write_text(struct core_writer *w, uint32_t t, const void *views)
{
    struct core_block paragraph, margin;
    uint32_t source = empty_string();
    if (!source || !core_block_at(views, 1, &paragraph) ||
        !core_block_at(views, core_blocks_count(views) - 1, &margin))
        return 0;
    core_write_word(w, CORE_MAX_PARAGRAPHS + 1);
    uint32_t previous = 0;
    for (uint32_t i = 0; i < CORE_MAX_PARAGRAPHS; i++) {
        uint32_t id = t + 1 + i;
        uint32_t words[10] = {ViewDescriptor_top_offset, previous ? PARAGRAPH_GAP : TEXT_TOP,
                              ViewDescriptor_top_reference_view, previous,
                              ViewDescriptor_top_anchor_mode, previous ? AnchorMode_Bottom : 0,
                              ViewDescriptor_id, id, TextViewDescriptor_source, source};
        uint32_t begin = core_write_block_begin(w, paragraph.kind);
        core_write_copy(w, paragraph.data, paragraph.bytes, words, 5);
        core_write_block_end(w, begin);
        previous = id;
    }
    uint32_t words[4] = {ViewDescriptor_top_reference_view, previous, ViewDescriptor_id,
                         t + 1 + CORE_MAX_PARAGRAPHS};
    uint32_t begin = core_write_block_begin(w, margin.kind);
    core_write_copy(w, margin.data, margin.bytes, words, 2);
    core_write_block_end(w, begin);
    return 1;
}

/* The Calendar empty-day message without its background image. */
static int write_empty(struct core_writer *w, uint32_t t, const void *views)
{
    uint32_t bytes;
    const void *text = core_block_with_word(views, ViewDescriptor_id, RES_Calendar_Empty_Text, &bytes);
    uint32_t source = empty_string();
    if (!text || !source)
        return 0;
    core_write_word(w, 1);
    uint32_t words[] = {ViewDescriptor_id, t + 1, TextViewDescriptor_source, source};
    uint32_t begin = core_write_block_begin(w, 0);
    core_write_copy(w, text, bytes, words, 2);
    core_write_block_end(w, begin);
    return 1;
}

/* Slider: the Brightness bar and value text with new ids. They keep the stock
 * sources, the brightness properties Apple's own template binds at boot. */
static int write_slider(struct core_writer *w, uint32_t t, const void *views)
{
    static const uint32_t stock_views[2] = {RES_Settings_Brightness_Bar,
                                            RES_Settings_Brightness_Value_Text};
    static const uint32_t types[2] = {ViewType_Progress, ViewType_Text};
    core_write_word(w, 2);
    for (uint32_t i = 0; i < 2; i++) {
        uint32_t bytes;
        const uint8_t *view = core_block_with_word(views, ViewDescriptor_id, stock_views[i], &bytes);
        if (!view || core_word_at(view, ViewDescriptor_type) != types[i])
            return 0;
        uint32_t words[2] = {ViewDescriptor_id, t + 1 + i};
        uint32_t begin = core_write_block_begin(w, types[i]);
        core_write_copy(w, view, bytes, words, 1);
        core_write_block_end(w, begin);
    }
    return 1;
}

/* Canvas: the full-screen image view with the core's view type, below the
 * 20 px title bar. Zero edge offsets with mode 0 fill the parent. */
#define CANVAS_TOP 20
static int write_canvas(struct core_writer *w, uint32_t t, const void *views)
{
    struct core_block image;
    if (!core_block_at(views, 0, &image) || image.bytes < View_options_bytes)
        return 0;
    uint32_t words[6] = {ViewDescriptor_top_offset, CANVAS_TOP, ViewDescriptor_id, t + 1,
                         ViewDescriptor_type, CORE_VIEW_CANVAS};
    core_write_word(w, 1);
    uint32_t begin = core_write_block_begin(w, CORE_VIEW_CANVAS);
    core_write_copy(w, image.data, image.bytes, words, 3);
    core_write_block_end(w, begin);
    return 1;
}

/* View `i` of the stock template, then of the second one. */
static int copied_view(const void *views, uint32_t i, struct core_block *view)
{
    uint32_t count = core_blocks_count(views);
    const void *from = i < count ? views : copy_second;
    return from && core_block_at(from, i < count ? i : i - count, view) &&
           view->bytes >= ViewDescriptor_id + 4;
}

/* Every view of the stock template, then of the second one, with the id after
 * `t` that its position has; an edge anchored to another copied view stays
 * anchored to its copy. The replaced view becomes the Games pane's image view
 * in the replaced view's box: its four edges, each an offset, a reference view
 * and an anchor mode. */
static int write_copy(struct core_writer *w, uint32_t t, const void *views)
{
    static const uint8_t edges[4][3] = {
        {ViewDescriptor_left_offset, ViewDescriptor_left_reference_view, ViewDescriptor_left_anchor_mode},
        {ViewDescriptor_top_offset, ViewDescriptor_top_reference_view, ViewDescriptor_top_anchor_mode},
        {ViewDescriptor_bottom_offset, ViewDescriptor_bottom_reference_view,
         ViewDescriptor_bottom_anchor_mode},
        {ViewDescriptor_right_offset, ViewDescriptor_right_reference_view,
         ViewDescriptor_right_anchor_mode},
    };
    struct core_block view, other, image;
    uint32_t bytes, count = core_blocks_count(views) + (copy_second ? core_blocks_count(copy_second) : 0);
    const void *games = stock(Resource_View, RES_ExtrasPreview_Games_Template, &bytes);
    if (copy_replaced >= 0 && !(games && core_block_at(games, 0, &image)))
        return 0;
    core_write_word(w, count);
    for (uint32_t i = 0; i < count; i++) {
        if (!copied_view(views, i, &view))
            return 0;
        uint32_t words[26];
        words[0] = ViewDescriptor_id;
        words[1] = t + 1 + i;
        for (uint32_t e = 0; e < 4; e++)
            for (uint32_t f = 0; f < 3; f++) {
                uint32_t value = core_word_at(view.data, edges[e][f]);
                for (uint32_t j = 0; f == 1 && value && j < count; j++)
                    if (copied_view(views, j, &other) &&
                        core_word_at(other.data, ViewDescriptor_id) == value)
                        value = t + 1 + j;
                words[2 + 2 * (3 * e + f)] = edges[e][f];
                words[3 + 2 * (3 * e + f)] = value;
            }
        const struct core_block *from = (int)i == copy_replaced ? &image : &view;
        uint32_t begin = core_write_block_begin(w, from->kind);
        core_write_copy(w, from->data, from->bytes, words, 13);
        core_write_block_end(w, begin);
    }
    return 1;
}

uint32_t core_templates_copy(uint32_t stock_template, uint32_t second_template, int replaced)
{
    uint32_t bytes, ids;
    const void *views = stock(Resource_View, stock_template, &bytes);
    copy_second = second_template ? stock(Resource_View, second_template, &bytes) : NULL;
    copy_replaced = replaced;
    if (!views || (second_template && !copy_second))
        return 0;
    ids = 1 + core_blocks_count(views) + (copy_second ? core_blocks_count(copy_second) : 0);
    uint32_t id = build(stock_template, ids, write_copy);
    return id && construct(id) ? id : 0;
}

/* Templates of the core's screens, each with the stock template it is written
 * from, the ids it takes and whether it slides in like Brightness. */
static const struct {
    uint32_t stock, ids;
    template_writer write;
    uint8_t slides;
} templates[CORE_TEMPLATES] = {
    [CORE_TEMPLATE_TEXT] = {RES_Settings_Legal_Template, 2 + CORE_MAX_PARAGRAPHS, write_text, 0},
    [CORE_TEMPLATE_SLIDER] = {RES_Settings_Brightness_Template, 3, write_slider, 1},
    [CORE_TEMPLATE_CANVAS] = {RES_FullScreenImage_Template, 2, write_canvas, 1},
    [CORE_TEMPLATE_EMPTY] = {RES_Calendar_Empty_Template, 2, write_empty, 0},
};
static uint32_t built[CORE_TEMPLATES]; /* 0 until built */

uint32_t core_template(enum core_template which)
{
    if (built[which])
        return built[which];
    uint32_t id = build(templates[which].stock, templates[which].ids, templates[which].write);
    if (id && templates[which].slides)
        slide_like_brightness(id);
    if (id && construct(id))
        built[which] = id;
    return built[which];
}

void core_templates_init(void *resource_bank)
{
    bank = resource_bank;
}
