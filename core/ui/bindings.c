/* SPDX-License-Identifier: GPL-3.0-only */

/* The event bindings of the rows extensions add to Apple's menus. The stock
 * rows' bindings live in each screen's CEVT, so when Apple loads a screen's
 * events the core reads one more stream into them: list.pid.<item>.chosen for a
 * menu row, contextualMenu.<handler> for a context menu row, and for a Settings
 * row its delayedselected binding, which shows the row in the preview pane. */

#include "ui/menurows.h"
#include "osos-2.0.5.h"
#include "ui/clones.h"
#include "ui/menus.h"
#include "ui/panes.h"
#include "ui/resources.h"
#include "ui/screens.h"
#include "hooks/sites.h"
#include <stddef.h>

/* One binding per row of the roots a screen serves. */
static void write_bindings(struct core_writer *w, uint32_t screen_id)
{
    uint32_t count = 0, begin = w->at;
    core_write_word(w, 0);
    EACH_ROW(r) {
        if (!core_menus_serves(r->root, screen_id))
            continue;
        char name[40], handler[11], *n;
        core_screens_handler_name(handler, r->item);
        if (r->after) {
            n = core_put_text(name, name + sizeof name, "list.pid.");
            n = core_put_number(n, name + sizeof name, r->item);
            core_put_text(n, name + sizeof name, ".chosen");
        } else {
            n = core_put_text(name, name + sizeof name, "contextualMenu.");
            core_put_text(n, name + sizeof name, handler);
        }
        core_write_event(w, name, handler, NULL, NULL, 0);
        count++;
        if (r->root != RV_MENU_SETTINGS)
            continue;
        /* The preview pane: the row's pane handler, then the shared layout. */
        static const char *const layout[] = {core_panes_layout_name};
        n = core_put_text(name, name + sizeof name, "list.pid.");
        n = core_put_number(n, name + sizeof name, r->item);
        core_put_text(n, name + sizeof name, ".delayedselected");
        core_screens_handler_name(handler, r->item + 2);
        core_write_event(w, name, handler, "navigator.SwitchLayout", layout, 1);
        count++;
    }
    if (w->out)
        for (unsigned b = 0; b < 4; b++)
            w->out[begin + b] = (uint8_t)(count >> (8 * b));
}

/* Adds the rows' bindings to a screen's event map after Apple loaded its own. */
static uint32_t layout_events(uint32_t layout, const struct core_next *next)
{
    uint32_t result = core_next_call(next, layout, 0, 0, 0);
    uint8_t *screen = *(uint8_t **)((uint8_t *)layout + Layout_screen);
    uint32_t screen_id = screen ? core_word_at(screen, LayoutScreen_id) : 0;
    struct core_writer measure = {NULL, 0};
    write_bindings(&measure, screen_id);
    if (measure.at <= 4)
        return result;
    uint8_t *stream = Heap_AllocateZeroed(measure.at);
    if (!stream)
        return result;
    struct core_writer w = {stream, 0};
    write_bindings(&w, screen_id);
    core_clone_read_events((void *)layout, stream, w.at);
    Heap_Free(stream);
    return result;
}

static const struct core_hook hook = {CORE_SITE_UI_LoadLayoutEvents_call, CORE_HOOK_FILTER, 5,
                                     layout_events};

void core_bindings_init(void)
{
    core_hook_bind(&hook);
}
