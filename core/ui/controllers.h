/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_CONTROLLERS_H
#define CORE_CONTROLLERS_H
#include <stdint.h>

/* A heap copy of a controller class vtable (address point `vtable`) with the
 * slot at `method` (a ScreenController_*_method offset) replaced by `handler`;
 * the replaced slot goes to *original. Slots the dispatcher patches point at
 * their stubs, so their sites still see the copy's controllers. NULL when the
 * heap is full. */
void **core_controller_vtable(void *const *vtable, uint32_t method, void *handler,
                              void **original);
/* Gives a created controller a copy made above. */
void core_controller_attach(void *controller, void **copy);

#endif
