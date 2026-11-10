/* SPDX-License-Identifier: GPL-3.0-only */

/* Canvas and slider screens. The canvas template holds one view of the core's
 * type: its vtable is a copy of View_vtable whose draw calls the top canvas
 * screen's callback with a graphics context bound to the view. Canvas and slider
 * controllers are plain resource screens whose vtable copy (controllers.c)
 * sees wheel and key events first; what it leaves goes to Apple's slot, which
 * runs the layout's event bindings. */

#include "ui/canvas.h"
#include "ui/controllers.h"
#include "ui/gfx.h"
#include "osos-2.0.5.h"
#include "ui/screens.h"
#include "ui/templates.h"
#include "settings.h"
#include <stddef.h>

#define FIELD(base, offset, type) (*(type *)((uint8_t *)(base) + (offset)))
#define PREFIX_WORDS (CxxVtable_prefix_bytes / 4)
/* Steps per wheel motion as Apple's menu lists count them. */
#define WHEEL_THRESHOLD 5
#define WHEEL_ACCELERATE 1
#define WHEEL_INTERVAL 350

static void *view_vtable[(CxxVtable_prefix_bytes + View_vtable_bytes) / 4];
/* The template is built once per template owner, so there is more than one
 * canvas view; this is the one last drawn, the one on screen. */
static void *canvas_view;
static void **controller_vtable;
static int (*handle_event)(void *controller, void *event);
static uint32_t wheel[WheelAccumulator_bytes / 4];

/* View */

static void draw(void *view, void *argument, const int32_t *clip)
{
    struct core_screen *s = core_screens_top(RV_SCREEN_KIND_CANVAS);
    int leaving = !s;
    if (leaving)
        s = core_screens_popped_canvas();
    if (!s || !s->declaration || !s->declaration->canvas.draw) {
        View_Draw(view, argument, clip);
        return;
    }
    uint32_t context[GraphicsContext_bytes / 4];
    int32_t bounds[4];
    canvas_view = view;
    GraphicsContext_ConstructDefault(context);
    View_BindGraphicsContext(view, context);
    View_GetLocalContentRect(view, bounds);
    core_gfx_begin(context, bounds, s);
    struct rv_context ctx;
    core_screens_context(s, &ctx);
    s->declaration->canvas.draw(&ctx, &core_gfx, bounds[3] - bounds[1], bounds[2] - bounds[0]);
    if (leaving)
        core_screens_closed(); /* that was the last draw */
}

static void *create(void *owner, void *parent, void *event_target, const void *descriptor)
{
    (void)owner;
    uint32_t options[View_options_bytes / 4];
    View_InitOptionsFromDescriptor(descriptor, options);
    void *view = Runtime_OperatorNew(View_bytes);
    if (!view)
        return NULL;
    View_Construct(view, NULL, parent, event_target, options);
    FIELD(view, 0, void **) = view_vtable + PREFIX_WORDS;
    return view;
}

void core_canvas_invalidate(const struct rv_rect *rect)
{
    if (!canvas_view || !core_screens_top(RV_SCREEN_KIND_CANVAS))
        return;
    /* View rectangles are {top, left, bottom, right} in the root's space. */
    const int32_t *view = (const int32_t *)((uint8_t *)canvas_view + View_bounds);
    int32_t part[4];
    if (rect) {
        part[0] = view[0] + rect->y;
        part[1] = view[1] + rect->x;
        part[2] = part[0] + rect->height;
        part[3] = part[1] + rect->width;
    }
    View_InvalidateRect(UI_GetRootView(), rect ? part : view);
}

/* Slider */

static int32_t range[ControlRange_bytes / 4]; /* the bar copies it when it reads it */

static int32_t slider_value(struct core_screen *s)
{
    const struct rv_screen *d = s->declaration;
    int32_t value = core_screens_setting_value(s->owner, d->choice.key);
    if (value < 0)
        return 0;
    return value < (int32_t)d->choice.count ? value : (int32_t)d->choice.count - 1;
}

int core_slider_property(struct core_screen *s, uint32_t type, uint32_t id, uint32_t out)
{
    uint32_t bar = CORE_SCREEN_ROWS(s);
    if (id == bar && type == Property_Control) {
        range[ControlRange_value / 4] = slider_value(s);
        range[ControlRange_minimum / 4] = 0;
        range[ControlRange_maximum / 4] = (int32_t)s->declaration->choice.count - 1;
        *(int32_t **)out = range;
        return 1;
    }
    if (id == bar + 1 && type == Property_String && s->declaration->choice.count) {
        *(const char **)out = s->declaration->choice.labels[slider_value(s)];
        return 1;
    }
    return 0;
}

static void slide(struct core_screen *s, int steps)
{
    int32_t value = slider_value(s) + steps, last = (int32_t)s->declaration->choice.count - 1;
    value = value < 0 ? 0 : value > last ? last : value;
    if (value == slider_value(s))
        return;
    core_settings_set_int(s->owner ? s->owner->owner : CORE_SETTINGS_OWNER_CORE,
                          s->declaration->choice.key, value);
    core_screens_notify(CORE_SCREEN_ROWS(s));
    core_screens_notify(CORE_SCREEN_ROWS(s) + 1);
    core_screens_setting_changed(s->owner, s->declaration->choice.key);
}

/* Controller */

static int key_of(uint32_t key)
{
    switch (key) {
    case UIKey_Center:
        return RV_BUTTON_CENTER;
    case UIKey_Menu:
        return RV_BUTTON_MENU;
    case UIKey_Previous:
        return RV_BUTTON_PREVIOUS;
    case UIKey_Next:
        return RV_BUTTON_NEXT;
    case UIKey_Play:
        return RV_BUTTON_PLAY;
    default:
        return -1;
    }
}

/* 1 when the screen consumed the event. */
static int input(struct core_screen *s, uint32_t kind, void *event)
{
    const struct rv_screen *d = s->declaration;
    struct rv_input in = {0, 0, 0};
    if (kind == InputEvent_Wheel) {
        WheelEvent_DecodeMotion(event);
        int steps = WheelAccumulator_ConsumeMotion(wheel, FIELD(event, InputEvent_wheel_delta, int32_t),
                                                   WheelEvent_GetSpeed(event));
        if (!steps)
            return 1;
        if (d->kind == RV_SCREEN_KIND_SLIDER) {
            slide(s, steps);
            return 1;
        }
        in.kind = RV_INPUT_WHEEL;
        in.delta = (int16_t)steps;
    } else if (kind == InputEvent_ButtonDown || kind == InputEvent_ButtonUp) {
        int key = key_of(ButtonEvent_GetUIKeyCode(event));
        if (key < 0)
            return 0;
        if (d->kind == RV_SCREEN_KIND_SLIDER) {
            if (key != RV_BUTTON_PREVIOUS && key != RV_BUTTON_NEXT)
                return 0;
            if (kind == InputEvent_ButtonDown)
                slide(s, key == RV_BUTTON_NEXT ? 1 : -1);
            return 1;
        }
        in.kind = kind == InputEvent_ButtonDown ? RV_INPUT_PRESS : RV_INPUT_RELEASE;
        in.button = (uint8_t)key;
    } else {
        return 0;
    }
    struct rv_context ctx;
    core_screens_context(s, &ctx);
    return d->canvas.input && d->canvas.input(&ctx, &in);
}

static int event(void *controller, void *event)
{
    struct core_screen *s = core_screens_of_controller(controller);
    core_screens_caller = controller;
    int consumed = s && s->declaration && input(s, FIELD(event, InputEvent_kind, uint32_t), event);
    core_screens_caller = NULL;
    return consumed ? 1 : handle_event(controller, event);
}

/* The hold switch reaches the canvas on top as an input; the system handles
 * it as before. */
static uint32_t hold_changed(uint32_t system, uint32_t locked, const struct core_next *next)
{
    uint32_t result = core_next_call(next, system, locked, 0, 0);
    struct core_screen *s = core_screens_top(RV_SCREEN_KIND_CANVAS);
    if (s && s->declaration && s->declaration->canvas.input) {
        struct rv_input in = {RV_INPUT_HOLD, 0, (int16_t)(locked != 0)};
        struct rv_context ctx;
        core_screens_context(s, &ctx);
        s->declaration->canvas.input(&ctx, &in);
    }
    return result;
}

static const struct core_hook hold_hook = {CORE_SITE_SystemUI_HandleHoldChange_call,
                                           CORE_HOOK_FILTER, 0, hold_changed};

/* The vtable of the plain resource screen the clones name. */
int core_canvas_prepare(void)
{
    if (!controller_vtable)
        controller_vtable = core_controller_vtable(ScreenController_vtable, ScreenController_event_method,
                                                   event, (void **)&handle_event);
    return controller_vtable != NULL;
}

void core_canvas_attach(void *controller)
{
    core_controller_attach(controller, controller_vtable);
    WheelAccumulator_Init(wheel, WHEEL_THRESHOLD, WHEEL_ACCELERATE, WHEEL_INTERVAL);
}

void core_canvas_init(void)
{
    const uint8_t *from = (const uint8_t *)View_vtable - CxxVtable_prefix_bytes;
    for (uint32_t i = 0; i < sizeof view_vtable / sizeof view_vtable[0]; i++)
        view_vtable[i] = ((void *const *)from)[i];
    view_vtable[PREFIX_WORDS + View_draw_method / 4] = (void *)draw;
    UI_RegisterViewFactory(CORE_VIEW_CANVAS, (void *)create);
    core_hook_bind(&hold_hook);
}
