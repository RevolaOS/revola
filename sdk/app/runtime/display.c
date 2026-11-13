/* SPDX-License-Identifier: GPL-3.0-only */
/* Adapted from RepriseOS game-sdk/runtime/display.c by lgreitis. */

#include "platform.h"
#include "rv_app_abi.h"

#define FIELD(base, offset, type) (*(const type *)((const uint8_t *)(base) + (offset)))

void *rv_app_platform_begin_frame(const struct rv_app_eapp_frame_input *input,
                                  struct rv_app_framebuffer *framebuffer)
{
    void *surface = EApp_GetSurface(EAppSurface_Draw);
    if (!surface ||
        FIELD(surface, EAppSurface_color_mode, uint8_t) != EAppSurfaceColorMode_RGB565 ||
        FIELD(surface, EAppSurface_width, uint32_t) != 320 ||
        FIELD(surface, EAppSurface_height, uint32_t) != 240 || !input->pixels ||
        FIELD(surface, EAppSurface_pixels, uint32_t) != input->pixels)
        return NULL;

    framebuffer->pixels = (uint16_t *)(uintptr_t)input->pixels;
    framebuffer->width = 320;
    framebuffer->height = 240;
    framebuffer->stride_pixels = 320;
    return surface;
}

void rv_app_platform_present(void *surface)
{
    EApp_PresentSurface(0, surface);
}
