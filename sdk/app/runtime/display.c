/* SPDX-License-Identifier: GPL-3.0-only */
/* Adapted from RepriseOS game-sdk/runtime/display.c by lgreitis. */

#include "platform.h"
#include "rv_app_abi.h"

#define FIELD(base, offset, type) (*(const type *)((const uint8_t *)(base) + (offset)))

void *rv_app_platform_begin_frame(const struct rv_app_eapp_frame_input *input,
                                  struct rv_app_framebuffer *framebuffer)
{
    void *surface = GameGraphics_GetCurrentSurface(GameSurface_Draw);
    if (!surface ||
        FIELD(surface, GameSurface_color_mode, uint8_t) != GameSurfaceColorMode_RGB565 ||
        FIELD(surface, GameSurface_width, uint32_t) != 320 ||
        FIELD(surface, GameSurface_height, uint32_t) != 240 || !input->pixels ||
        FIELD(surface, GameSurface_pixels, uint32_t) != input->pixels)
        return NULL;

    framebuffer->pixels = (uint16_t *)(uintptr_t)input->pixels;
    framebuffer->width = 320;
    framebuffer->height = 240;
    framebuffer->stride_pixels = 320;
    return surface;
}

void rv_app_platform_present(void *surface)
{
    GameGraphics_SwapBuffers(NULL, surface);
}
