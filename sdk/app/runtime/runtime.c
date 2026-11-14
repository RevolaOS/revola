/* SPDX-License-Identifier: GPL-3.0-only */
/* Adapted from RepriseOS game-sdk/runtime/game_runtime.c by lgreitis. */

#include "platform.h"
#include "runtime.h"

void rv_app_start(void);
uint32_t rv_app_reserved_entry(void);
extern const uint8_t rv_app_imports[];

const struct rv_app_eapp_header rv_app_eapp_header __attribute__((section(".eapp"), used)) = {
    .magic = GameImage_Magic,
    .version = GameImage_MaxVersion,
    .entry_count = RV_APP_EAPP_SLOT_COUNT,
    .imports = (uint32_t)rv_app_imports,
    .entries = {
        [RV_APP_EAPP_SLOT_INIT] = (uint32_t)rv_app_start,
        [RV_APP_EAPP_SLOT_SHUTDOWN] = (uint32_t)rv_app_runtime_shutdown,
        [2] = (uint32_t)rv_app_reserved_entry,
        [3] = (uint32_t)rv_app_reserved_entry,
        [RV_APP_EAPP_SLOT_FRAME] = (uint32_t)rv_app_runtime_frame,
    },
};

static int exit_requested;

void rv_app_request_exit(void)
{
    exit_requested = 1;
}

void rv_app_runtime_init(void)
{
    exit_requested = 0;
    if (rv_app_application.init)
        rv_app_application.init();
}

void rv_app_runtime_shutdown(void)
{
    if (rv_app_application.shutdown)
        rv_app_application.shutdown();
    rv_app_files_close_all();
    rv_app_directories_close_all();
    rv_app_saves_close_all();
}

void rv_app_runtime_frame(const struct rv_app_eapp_frame_input *input,
                          struct rv_app_eapp_frame_output *output)
{
    if (!input || !output)
        return;

    if (exit_requested || input->state == GameFrameState_ShuttingDown ||
        input->state == GameFrameState_ShutDown) {
        output->state = GameFrameState_Done;
        return;
    }

    uint32_t address = input->events;
    while (address) {
        const struct rv_app_eapp_input_event *native =
            (const struct rv_app_eapp_input_event *)(uintptr_t)address;
        struct rv_app_input_event event = {
            .button = native->button,
            .type = native->type,
            .timestamp_ms = native->timestamp,
        };
        address = native->next;
        if (rv_app_application.input)
            rv_app_application.input(&event);
        if (exit_requested) {
            output->state = GameFrameState_Done;
            return;
        }
    }

    struct rv_app_frame frame = {0};
    void *surface = rv_app_platform_begin_frame(input, &frame.framebuffer);
    if (rv_app_application.frame)
        rv_app_application.frame(&frame);
    if (surface)
        rv_app_platform_present(surface);
    if (exit_requested)
        output->state = GameFrameState_Done;
}
