/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef CORE_RECOVERY_H
#define CORE_RECOVERY_H
#include "panic/panic.h"
#include "osos-2.0.5.h"
#define CORE_RECOVERY_BYTES 512u
struct core_crash_record {
    uint32_t commit, checksum, firmware, build, report_id, owner;
    uint32_t exception_kind, exception_address;
    struct panic_frame frame;
    char name[64], file[36], id[33];
    uint32_t backlight[4];
};
#define CORE_CRASH_RECORD ((volatile struct core_crash_record *)(IRAM0_END - CORE_RECOVERY_BYTES))
void core_recovery_prepare(void);
void core_recovery_backlight(const uint32_t result[4]);
int core_recovery_center(unsigned *released, uint32_t packet);
void core_recovery_capture(const struct panic_frame *frame, uint32_t owner);
int core_recovery_valid(void);
void core_recovery_clear(void);
uint32_t core_recovery_owner(void);
void core_recovery_path(char *out, uint32_t id);
void core_recovery_wait(void) __attribute__((noreturn));
extern uint32_t core_recovery_report_id;
extern uint32_t core_recovery_log_status;
#endif
