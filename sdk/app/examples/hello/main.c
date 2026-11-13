/* SPDX-License-Identifier: GPL-3.0-only */

/* Draws a greeting, the wheel direction and the time since start; Menu exits. */

#include "rv_app.h"

static char seconds[12];

static void input(const struct rv_app_input_event *event)
{
    if (event->button == RV_APP_BUTTON_MENU && event->type == RV_APP_INPUT_PRESS)
        rv_app_request_exit();
}

static void frame(const struct rv_app_frame *frame)
{
    const struct rv_app_framebuffer *fb = &frame->framebuffer;
    if (!fb->pixels)
        return;
    for (unsigned y = 0; y < fb->height; ++y)
        for (unsigned x = 0; x < fb->width; ++x)
            fb->pixels[y * fb->stride_pixels + x] = 0x0010;

    rv_app_draw_text(fb, 20, 20, "HELLO FROM REVOLAOS", 0xffff);

    uint32_t ticks = rv_app_ticks_ms() / 1000;
    unsigned i = sizeof seconds - 1;
    seconds[i] = 0;
    do {
        seconds[--i] = '0' + ticks % 10;
        ticks /= 10;
    } while (ticks && i);
    rv_app_draw_text(fb, 20, 40, "SECONDS:", 0xffff);
    rv_app_draw_text(fb, 74, 40, seconds + i, 0xffff);

    struct rv_app_wheel wheel = rv_app_read_wheel();
    uint8_t direction = rv_app_wheel_directions(&wheel, RV_APP_WHEEL_TOP_POSITION);
    rv_app_draw_text(fb, 20, 60, "WHEEL:", 0xffff);
    rv_app_draw_text(fb, 62, 60, direction & RV_APP_DIRECTION_UP ? "UP" : "", 0xffff);
    rv_app_draw_text(fb, 80, 60, direction & RV_APP_DIRECTION_DOWN ? "DOWN" : "", 0xffff);
    rv_app_draw_text(fb, 110, 60, direction & RV_APP_DIRECTION_LEFT ? "LEFT" : "", 0xffff);
    rv_app_draw_text(fb, 140, 60, direction & RV_APP_DIRECTION_RIGHT ? "RIGHT" : "", 0xffff);

    rv_app_draw_text(fb, 20, 220, "MENU: EXIT", 0x8410);
}

const struct rv_app rv_app_application = {
    .package_id = "hello",
    .input = input,
    .frame = frame,
};
