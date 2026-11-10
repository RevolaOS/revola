/* SPDX-License-Identifier: GPL-3.0-only */

/* View templates the core adds to Apple's. Templates are built once, when the UI
 * starts, from every View resource the enumeration yields, so the core builds
 * its own at boot and yields them after Apple's: a text page of paragraph
 * views, a slider with the Brightness bar and value text, and a canvas of one
 * core view. All screens of a kind share the template's views; a screen
 * rebinds them to its own sources through its VCvs each time it is displayed. */

#include "ui/templates.h"
#include "osos-2.0.5.h"
#include "ui/resources.h"
#include "hooks/sites.h"
#include <stddef.h>

uint32_t core_template_text;
uint32_t core_template_slider;
uint32_t core_template_canvas;

static void *bank;
static uint32_t empty_source;

/* In the order the enumeration yields them. */
#define MAX_TEMPLATES 3
static uint32_t templates[MAX_TEMPLATES];
static uint32_t template_count;

static const void *stock(uint32_t type, uint32_t id, uint32_t *bytes)
{
    return ResourceBank_GetResource(bank, type, id, bytes);
}

static void set(uint32_t type, uint32_t id, const void *data, uint32_t bytes)
{
    core_resource_set(type, id, data, bytes);
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

/* Takes `ids` ids, writes the View with `write` (measured first) and stores it
 * with the stock template's TMLT. The bank keeps the only copy: the
 * enumeration yields it and the template builds its child views from it. */
static uint32_t build(uint32_t stock_template, uint32_t ids, template_writer write)
{
    uint32_t bytes;
    const void *views = stock(Resource_View, stock_template, &bytes);
    uint32_t id = core_resource_take(0, ids);
    struct core_writer w = {NULL, 0};
    if (!views || !id || template_count == MAX_TEMPLATES || !write(&w, id, views))
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
    templates[template_count++] = id;
    return id;
}

/* Text page: the stock Legal template's second block is a paragraph, its last
 * the bottom margin; each paragraph hangs below the one before. View offsets
 * count from the screen top, so the first paragraph starts below the 20 px title
 * bar with the stock page's 15 px top margin. The views are bound when the
 * template is built, before the Settings model exists, so their boot source is
 * an empty string. */
#define TEXT_TOP 35
#define PARAGRAPH_GAP 12
static int write_text(struct core_writer *w, uint32_t t, const void *views)
{
    struct core_block paragraph, margin;
    if (!empty_source || !core_block_at(views, 1, &paragraph) ||
        !core_block_at(views, core_blocks_count(views) - 1, &margin))
        return 0;
    core_write_word(w, CORE_MAX_PARAGRAPHS + 1);
    uint32_t previous = 0;
    for (uint32_t i = 0; i < CORE_MAX_PARAGRAPHS; i++) {
        uint32_t id = t + 1 + i;
        uint32_t words[10] = {ViewDescriptor_top_offset, previous ? PARAGRAPH_GAP : TEXT_TOP,
                              ViewDescriptor_top_reference_view, previous,
                              ViewDescriptor_top_anchor_mode, previous ? AnchorMode_Bottom : 0,
                              ViewDescriptor_id, id, TextViewDescriptor_source, empty_source};
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

/* Apple's template enumeration walks the ROM index only; the core's templates
 * follow it. Called again with the last id handed out, so the next one follows. */
static uint32_t next_template(uint32_t resource_bank, uint32_t id, uint32_t bytes,
                              const struct core_next *next)
{
    uint32_t result = core_next_call(next, resource_bank, id, bytes, 0);
    if (result)
        return result;
    uint32_t i = 0;
    while (i < template_count && templates[i] != *(uint32_t *)id)
        i++;
    i = i < template_count ? i + 1 : 0; /* after ours, or the first after Apple's */
    if (i >= template_count)
        return 0;
    *(uint32_t *)id = templates[i];
    return (uint32_t)stock(Resource_View, templates[i], (uint32_t *)bytes);
}

static const struct core_hook next_template_hook = {CORE_SITE_ResourceBank_NextTemplate_call,
                                                    CORE_HOOK_FILTER, 0, next_template};

void core_templates_init(void *resource_bank)
{
    bank = resource_bank;
    uint32_t id = core_resource_take(0, 1);
    if (id) {
        uint32_t source[6] = {1, 12, Resource_SORC, Service_String, id, SourceDestination_Title};
        set(Resource_SORC, id, source, sizeof source);
        set(Resource_String, id, "", 1);
        empty_source = id;
    }
    core_template_text = build(RES_Settings_Legal_Template, 2 + CORE_MAX_PARAGRAPHS, write_text);
    core_template_slider = build(RES_Settings_Brightness_Template, 3, write_slider);
    core_template_canvas = build(RES_FullScreenImage_Template, 2, write_canvas);
    slide_like_brightness(core_template_slider);
    slide_like_brightness(core_template_canvas);
    core_hook_bind(&next_template_hook);
}
