/* SPDX-License-Identifier: GPL-3.0-only */

/* Settings > Extensions, built from the same declarations an extension uses: a
 * Settings entry opens a runtime list with a Usage row (usage.c) and one row
 * per installed extension with its state. Select on an extension opens its
 * page, a canvas with the manifest's description and the loader's figures,
 * whose Enabled line Select flips. The installed list is read from disk when
 * the list opens and the manifest when the page opens; both are freed when
 * their screen closes. */

#include "ui/extensions.h"
#include "loader/loader.h"
#include "osos-2.0.5.h"
#include "ui/page.h"
#include "ui/resources.h"
#include "ui/menus.h"
#include "ui/screens.h"
#include "ui/usage.h"
#include <stddef.h>

#define MAX_LISTED 16

static struct listed {
    uint32_t count;
    struct core_installed installed[MAX_LISTED];
} *listed;

static struct page {
    uint32_t index; /* in listed->installed */
    uint8_t manifest[CORE_MANIFEST_MAX];
    uint32_t manifest_bytes;
} *page;

static char title[sizeof listed->installed[0].name];
/* The Extensions row's icon. */
static const struct rv_bitmap extensions_icon = {"extensions"};
static char meter[24];
static char line[32]; /* a row's second line, formatted when asked */

static int enabled(const struct rv_extension_record *record)
{
    return record && !record->pending_unload;
}

static const char *state_text(const struct core_installed *e)
{
    struct rv_extension_record *record = core_loader_find(e->owner);
    if (e->duplicate)
        return "Duplicate";
    if (!enabled(record))
        return e->owner == core_loader_crashed ? "Off after crash" : "Off";
    switch (record->failure) {
    case CORE_EXTENSION_OK:
        return "On";
    case CORE_EXTENSION_CONFLICT:
        return "Conflict";
    case CORE_EXTENSION_NO_MEMORY:
        return "No memory";
    case CORE_EXTENSION_SITE_MISMATCH:
        return "Site mismatch";
    default:
        return "Failed";
    }
}

/* The list */

/* The list's pane: "4 installed". */
static void update_meter(void)
{
    if (core_loader_core_only) {
        core_put_text(meter, meter + sizeof meter, "Core only");
        return;
    }
    core_put_text(core_page_number(meter, meter + sizeof meter, listed ? listed->count : 0),
                  meter + sizeof meter, " installed");
}

/* Row 0 is Usage, then one row per installed extension. */
static uint32_t list_count(void)
{
    if (!listed && !core_loader_core_only) {
        listed = Heap_AllocateZeroed(sizeof *listed);
        if (listed)
            listed->count = core_loader_installed(listed->installed, MAX_LISTED);
    }
    update_meter();
    return 1 + (listed ? listed->count : 0);
}

static const char *list_row(uint32_t index, const char **detail)
{
    if (index == 0) {
        *detail = core_usage_active(line, line + sizeof line);
        return "Usage";
    }
    if (!listed || index > listed->count) {
        *detail = NULL;
        return "";
    }
    /* The state, and the memory a running extension holds: "On, 6.0 KB" (the
     * system font has no middle dot). */
    const struct core_installed *e = &listed->installed[index - 1];
    struct rv_extension_record *record = core_loader_find(e->owner);
    char *out = core_put_text(line, line + sizeof line, state_text(e));
    if (enabled(record) && record->failure == CORE_EXTENSION_OK)
        core_page_size(core_put_text(out, line + sizeof line, ", "), line + sizeof line,
                        record->live);
    *detail = line;
    return e->name;
}

static int list_select(uint32_t index)
{
    if (index == 0) {
        struct core_screen *usage = core_screens_find(NULL, "core.usage");
        if (usage)
            core_screens_push(usage, core_screens_caller);
        return 0;
    }
    if (!listed || index > listed->count || page)
        return 0;
    page = Heap_AllocateZeroed(sizeof *page);
    if (!page)
        return 0;
    const struct core_installed *e = &listed->installed[index - 1];
    page->index = index - 1;
    page->manifest_bytes = core_loader_manifest(e->file, page->manifest);
    core_put_text(title, title + sizeof title, e->name);
    struct core_screen *s = core_screens_find(NULL, "core.extension");
    if (!s || !core_screens_push(s, core_screens_caller)) {
        Heap_Free(page);
        page = NULL;
    }
    return 0;
}

/* The page. The status bar names the extension; below it the version and
 * author with the stability unless stable, the description, then the memory it
 * holds and, always last, the Enabled row, highlighted since Select flips it.
 * Menu goes back; the playback keys stay the system's. */

/* The rows sit low: the Enabled highlight ends as far above the bottom as the
 * version line's capitals start below the status bar (the font leaves 1 px
 * above them). */
#define TOP 10
#define ROWS_FROM_BOTTOM (2 * CORE_PAGE_ROW + TOP - 1)
#define PROBLEM 0xffc0392bu

static const char *manifest_text(uint32_t field)
{
    return page->manifest_bytes ? core_loader_manifest_text(page->manifest, field) : "";
}

/* The name of the extension holding the site `record` lost, or the core's. */
static const char *conflict_holder(const struct rv_extension_record *record)
{
    for (const struct rv_extension_record *r = core_extensions; r; r = r->next) {
        if (r == record || !r->hooks_bound || !r->descriptor)
            continue;
        for (uint32_t i = 0; i < r->descriptor->hook_count; i++)
            if (r->descriptor->hooks[i].site == record->conflict_site)
                return core_loader_manifest_text(r->manifest, 1);
    }
    return "RevolaOS";
}

/* What keeps an extension that is on from running, or NULL. */
static const char *problem(const struct core_installed *e, const struct rv_extension_record *record,
                           char *out, const char *end)
{
    if (e->duplicate)
        return "Another file has the same id";
    if (!enabled(record))
        return e->owner == core_loader_crashed ? "Turned off after a crash" : NULL;
    switch (record->failure) {
    case CORE_EXTENSION_OK:
        return NULL;
    case CORE_EXTENSION_CONFLICT:
        core_put_text(core_put_text(out, end, "Conflicts with "), end, conflict_holder(record));
        return out;
    case CORE_EXTENSION_NO_MEMORY:
        return "Not enough memory to run";
    case CORE_EXTENSION_SITE_MISMATCH:
        return "Not made for this firmware";
    default:
        return "Could not be loaded";
    }
}

/* Alpha and beta as a pill in the stock highlight blue or the capacity bar's
 * orange, right-aligned on the line at `y`. */
static void stability_pill(const struct rv_gfx *g, int width, int y)
{
    static const char *const names[] = {NULL, "Alpha", "Beta", NULL};
    uint32_t stability = page->manifest_bytes
                             ? CORE_MANIFEST_STABILITY(core_loader_manifest_flags(page->manifest))
                             : 0;
    const char *name = names[stability];
    if (!name)
        return;
    rv_color c = stability == 1 ? 0xffde894eu : 0xff0084dcu;
    int16_t w = (int16_t)(g->measure(RV_FONT_TEXT, name) + 12),
            h = (int16_t)(g->line_height(RV_FONT_TEXT) + 2);
    int16_t x = (int16_t)(width - CORE_PAGE_LEFT - w);
    struct rv_rect across = {x, (int16_t)(y + 1), w, (int16_t)(h - 2)},
                   down = {(int16_t)(x + 1), (int16_t)y, (int16_t)(w - 2), h};
    g->fill(&across, c);
    g->fill(&down, c);
    g->text(x + 6, y + 1, RV_FONT_TEXT, 0xffffffffu, name);
}

static void page_draw(const struct rv_gfx *g, int width, int height)
{
    core_page_clear(g, width, height);
    if (!page || !listed)
        return;
    const struct core_installed *e = &listed->installed[page->index];
    struct rv_extension_record *record = core_loader_find(e->owner);
    char text[64];

    int y = TOP;
    char *out = core_put_text(text, text + sizeof text, "Version ");
    out = core_put_text(out, text + sizeof text, *manifest_text(3) ? manifest_text(3) : "unknown");
    if (*manifest_text(4))
        core_put_text(core_put_text(out, text + sizeof text, " by "), text + sizeof text,
                      manifest_text(4));
    g->text(CORE_PAGE_LEFT, y, RV_FONT_TITLE, CORE_PAGE_TEXT, text);
    stability_pill(g, width, y);
    y += g->line_height(RV_FONT_TITLE) + 10;

    char reason[48];
    const char *why = problem(e, record, reason, reason + sizeof reason);
    y = core_page_text(g, CORE_PAGE_LEFT, y, width - 2 * CORE_PAGE_LEFT, RV_FONT_TEXT,
                       CORE_PAGE_TEXT, manifest_text(2), why ? 2 : 3);
    if (why)
        g->text(CORE_PAGE_LEFT, y, RV_FONT_TITLE, PROBLEM, why);

    int rows = height - ROWS_FROM_BOTTOM;
    if (enabled(record) && record->failure == CORE_EXTENSION_OK) {
        out = core_page_size(text, text + sizeof text, record->live);
        core_page_size(core_put_text(out, text + sizeof text, ", peak "), text + sizeof text,
                       record->peak);
        core_page_row(g, rows, width, "Memory", text, 0);
    }
    core_page_row(g, rows + CORE_PAGE_ROW, width, "Enabled", enabled(record) ? "On" : "Off", 1);
}

static int page_input(const struct rv_input *input)
{
    if (!page || !listed || input->key != RV_KEY_CENTER)
        return 0; /* Menu pops the page; the other keys are the system's */
    if (input->kind == RV_INPUT_PRESS) {
        const struct core_installed *e = &listed->installed[page->index];
        core_loader_set_enabled(e->file, !enabled(core_loader_find(e->owner)));
        struct core_screen *list = core_screens_find(NULL, "core.extensions");
        if (list)
            core_screens_refresh(list);
        core_screens_invalidate();
    }
    return 1;
}

static const struct rv_screen screens[] = {
    {.name = "core.extensions", .title = "Extensions", .kind = RV_SCREEN_RUNTIME_LIST,
     .runtime = {.count = list_count, .row = list_row, .select = list_select,
                 .detail = RV_DETAIL_BELOW_SUBMENU},
     .caption = meter, .bitmap = &extensions_icon},
    {.name = "core.extension", .title = title, .kind = RV_SCREEN_CANVAS,
     .canvas = {.draw = page_draw, .input = page_input}},
};

static const struct rv_entry entry = {
    .in = "settings", .visible = 1, .after = RES_SettingsMenu_Legal_Item,
    .row = {.label = "Extensions", .kind = RV_ROW_PUSH, .screen = "core.extensions"},
    .caption = "Manage Extensions",
    .bitmap = &extensions_icon,
};

void core_extensions_closed(const struct core_screen *s)
{
    if (s->declaration == &screens[1] && page) {
        Heap_Free(page);
        page = NULL;
    } else if (s->declaration == &screens[0] && listed) {
        Heap_Free(listed);
        listed = NULL;
    }
}

void core_extensions_register(void)
{
    core_usage_register();
    core_screens_register(NULL, screens, sizeof screens / sizeof screens[0]);
    core_menus_register(NULL, &entry, 1);
}
