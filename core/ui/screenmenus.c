/* SPDX-License-Identifier: GPL-3.0-only */

/* The long-press menu of a list or dynamic list screen. The screen's CEVT binds
 * button.select.pressandhold to the long press's handler and
 * contextualMenu.<handler> to each row's; both reach the screen's action slot.
 * Each long press builds the menu from the rows whose `available` agrees, then
 * Apple's Cancel row, and remembers the highlighted row as ctx->row for the row
 * that runs. */

#include "ui/screenmenus.h"
#include "osos-2.0.5.h"
#include "ui/indexed.h"
#include "ui/clones.h"
#include "ui/lists.h"
#include "ui/menurows.h"
#include "ui/screens.h"
#include <stddef.h>

/* The row highlighted when the menu opened. */
static int32_t menu_row = -1;
/* The rows' action names; Apple keeps the pointers while the menu is open. */
static char handlers[CORE_MAX_CONTEXT_ROWS][CORE_HANDLER_BYTES];

uint32_t core_screen_menu_events(struct core_writer *w, const struct core_screen *s)
{
    const struct rv_screen *d = s->declaration;
    if (!d->context_count)
        return 0;
    uint32_t first = CORE_LIST_CONTEXT(s);
    char name[40], handler[CORE_HANDLER_BYTES];
    core_screens_handler_name(handler, first);
    core_write_event(w, "button.select.pressandhold", handler, NULL, NULL, 0);
    for (uint32_t i = 0; i < d->context_count; i++) {
        core_screens_handler_name(handler, first + 1 + i);
        core_put_text(core_put_text(name, name + sizeof name, "contextualMenu."), name + sizeof name,
                      handler);
        core_write_event(w, name, handler, NULL, NULL, 0);
    }
    core_write_event(w, "contextualMenu.HandleCancel", "", NULL, NULL, 0);
    return 2 + d->context_count;
}

int core_screen_menu_build(const struct core_screen *s, uint8_t *scratch, uint32_t bytes)
{
    const struct rv_screen *d = s->declaration;
    uint32_t first = CORE_LIST_CONTEXT(s);
    for (uint32_t i = 0; i < d->context_count; i++) {
        const char *label = d->context[i].row.label;
        uint32_t n = 0;
        while (label[n])
            n++;
        core_resource_set(Resource_String, first + 1 + i, label, n + 1);
    }
    uint32_t screen = first + 1 + d->context_count, layout = screen + 1, next = screen + 2;
    struct layout_clone clone = {RES_ContextMenu_Layout, layout, RES_StatusBarWhite_Text,
                                  s->id + 2, 0, 0, 0, NULL};
    if (!core_clone_layout(&clone, &next, scratch, bytes) || next > screen + 4)
        return 0;
    uint32_t event_bytes, none = 0;
    const void *events = ResourceBank_GetResource(UI_GetResourceBank(), Resource_SEVT,
                                                  RES_ContextMenu_Layout, &event_bytes);
    if (!events)
        return 0;
    core_resource_set(Resource_SEVT, layout, events, event_bytes);
    return core_clone_screen(screen, layout, RES_ContextMenu_Screen, RES_ContextMenu_Screen,
                              &none, sizeof none, scratch, bytes);
}

static void open_menu(const struct core_screen *s, struct rv_context *ctx)
{
    const struct rv_screen *d = s->declaration;
    uint32_t first = CORE_LIST_CONTEXT(s), shown = 0;
    uint8_t items[ContextMenuItems_bytes];
    ctx->row = (int32_t)core_indexed_selected();
    menu_row = ctx->row;
    ContextMenuItems_Init(items, 0);
    for (uint32_t i = 0; i < d->context_count; i++) {
        if (d->context[i].available && !d->context[i].available(ctx))
            continue;
        core_screens_handler_name(handlers[i], first + 1 + i);
        /* Flags 1: dispatched to the screen; Cancel's 0 only closes the menu. */
        ContextMenuItems_Append(items, first + 1 + i, handlers[i], 1);
        shown++;
    }
    if (shown) {
        ContextMenuItems_Append(items, RES_ContextMenu_Cancel_String, "HandleCancel", 0);
        void *model = ContextMenuModel_Get();
        ContextMenuModel_SetItems(model, items);
        uint32_t screen = first + 1 + d->context_count;
        UI_SetScreenState(UI_GetLayoutManager(), screen, 0, 0,
                          ContextMenuModel_GetRememberedIndex(model), 0);
        void *created = UI_CreateScreenController(UI_GetControllerRegistry(), screen, screen + 1, 0);
        if (created) {
            void (*push)(void *, void *) = *(void **)((uint8_t *)*(void **)ctx->controller +
                                                     ScreenController_push_method);
            push(ctx->controller, created);
        }
    }
    ContextMenuItems_Destroy(items);
}

int core_screen_menu_action(struct core_screen *s, void *controller, uint32_t item,
                            uint32_t argument)
{
    const struct rv_screen *d = s->declaration;
    uint32_t first = d->context_count ? CORE_LIST_CONTEXT(s) : 0;
    if (!first || item - first > d->context_count)
        return 0;
    if (argument == ActionSupportQuery)
        return 1;
    struct rv_context ctx;
    core_screens_context(s, &ctx);
    ctx.controller = controller;
    core_screens_caller = controller;
    if (item == first) {
        open_menu(s, &ctx);
    } else {
        const struct rv_row *row = &d->context[item - first - 1].row;
        ctx.row = menu_row;
        struct core_menu_origin menu = {ctx.item, menu_row};
        const struct core_menu_origin *outer = core_screens_menu;
        core_screens_menu = &menu;
        if (row->kind == RV_ROW_KIND_ACTION && row->action) {
            row->action(&ctx);
        } else if (row->kind == RV_ROW_KIND_PUSH) {
            struct core_screen *target = core_loader_screen(s->owner, row->screen);
            if (target)
                core_screens_push(target, controller, NULL);
        }
        core_screens_menu = outer;
    }
    core_screens_caller = NULL;
    return 1;
}
