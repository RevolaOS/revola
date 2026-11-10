/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_EXTENSIONS_H
#define CORE_EXTENSIONS_H

struct core_screen;

/* Registers the Settings > Extensions row and its screens; after the RevolaOS
 * Info row. */
void core_extensions_register(void);
/* A core screen left the navigation stack. */
void core_extensions_closed(const struct core_screen *screen);

#endif
