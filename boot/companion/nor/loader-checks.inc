/* SPDX-License-Identifier: GPL-3.0-only */

/* The pad file's header after its name: checksum, type 0xf0, attributes, size
   and state. The checksum covers the name, so the name itself is not listed. */
native_expect apple_loader, 0x1a728, \
    0x4f, 0x5a, 0xf0, 0x00, 0xf8, 0x4d, 0x00, 0xf8
