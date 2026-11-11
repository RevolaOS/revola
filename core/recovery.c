/* SPDX-License-Identifier: GPL-3.0-only */
#include "recovery.h"
#include "loader/loader.h"
#include "loader/file.h"
#include "patch.h"
#include <stddef.h>
extern void core_cache_sync(const void *start, const void *end);
#define COMMIT 0x31524352u
_Static_assert(sizeof(struct core_crash_record) <= CORE_RECOVERY_BYTES, "Crash reservation");
_Static_assert(CORE_RECOVERY_BYTES <= 0x800, "BootROM copy boundary");
uint32_t core_recovery_report_id;
uint32_t core_recovery_log_status; /* 0 failed, 1 readback, 2 survived a boot */
static const char hex[] = "0123456789ABCDEF";
static uint32_t checksum(void)
{
    const volatile uint8_t *p = (const volatile uint8_t *)CORE_CRASH_RECORD;
    uint32_t hash = 2166136261u;
    for (uint32_t i = 8; i < sizeof *CORE_CRASH_RECORD; i++)
        hash = (hash ^ p[i]) * 16777619u;
    return hash;
}
static void sync_record(void)
{
    core_cache_sync((const void *)CORE_CRASH_RECORD,
                    (const void *)(CORE_CRASH_RECORD + 1));
}
void core_recovery_clear(void)
{
    CORE_CRASH_RECORD->commit = 0;
    sync_record();
}
int core_recovery_valid(void)
{
    return CORE_CRASH_RECORD->commit == COMMIT &&
           CORE_CRASH_RECORD->firmware == CORE_FIRMWARE_ID &&
           CORE_CRASH_RECORD->checksum == checksum();
}
uint32_t core_recovery_owner(void)
{
    return core_recovery_valid() ? CORE_CRASH_RECORD->owner : 0;
}
static void copy_text(volatile char *out, const char *in, uint32_t capacity)
{
    uint32_t i = 0;
    for (; i + 1 < capacity && in && in[i]; i++)
        out[i] = in[i];
    out[i] = 0;
}
void core_recovery_capture(const struct panic_frame *frame, uint32_t owner)
{
    core_recovery_clear();
    volatile uint8_t *p = (volatile uint8_t *)CORE_CRASH_RECORD;
    for (uint32_t i = 4; i < sizeof *CORE_CRASH_RECORD; i++)
        p[i] = 0;
    CORE_CRASH_RECORD->firmware = CORE_FIRMWARE_ID;
    CORE_CRASH_RECORD->build = CORE_BUILD_ID;
    CORE_CRASH_RECORD->report_id = core_recovery_report_id;
    CORE_CRASH_RECORD->owner = owner;
    CORE_CRASH_RECORD->exception_kind = core_panic_exception[0];
    CORE_CRASH_RECORD->exception_address = core_panic_exception[1];
    const uint8_t *from = (const uint8_t *)frame;
    volatile uint8_t *to = (volatile uint8_t *)&CORE_CRASH_RECORD->frame;
    for (uint32_t i = 0; i < sizeof *frame; i++)
        to[i] = from[i];
    /* Only bounded strings from an already-loaded extension are copied. */
    for (const struct rv_extension_record *r = core_extensions; r; r = r->next)
        if (r->owner == owner) {
            copy_text(CORE_CRASH_RECORD->id,
                      core_loader_manifest_text(r->manifest, CORE_MANIFEST_ID),
                      sizeof CORE_CRASH_RECORD->id);
            copy_text(CORE_CRASH_RECORD->file, r->file, sizeof CORE_CRASH_RECORD->file);
            copy_text(CORE_CRASH_RECORD->name,
                      core_loader_manifest_text(r->manifest, CORE_MANIFEST_NAME),
                      sizeof CORE_CRASH_RECORD->name);
            break;
        }
    CORE_CRASH_RECORD->checksum = checksum();
    sync_record();
    CORE_CRASH_RECORD->commit = COMMIT;
    sync_record();
}
void core_recovery_backlight(const uint32_t result[4])
{
    if (!core_recovery_valid())
        return;
    core_recovery_clear();
    for (unsigned i = 0; i < 4; i++)
        CORE_CRASH_RECORD->backlight[i] = result[i];
    CORE_CRASH_RECORD->checksum = checksum();
    sync_record();
    CORE_CRASH_RECORD->commit = COMMIT;
    sync_record();
}
void core_recovery_path(char *out, uint32_t id)
{
    const char *prefix = "/Revola/Crashes/";
    while (*prefix) *out++ = *prefix++;
    for (int shift = 28; shift >= 0; shift -= 4) *out++ = hex[id >> shift & 15];
    const char *suffix = ".txt";
    while ((*out++ = *suffix++)) {}
}
static char *append(char *out, const char *value)
{
    while (*value) *out++ = *value++;
    return out;
}
static char *word(char *out, const char *key, uint32_t value)
{
    out = append(out, key);
    out = append(out, ": ");
    for (int shift = 28; shift >= 0; shift -= 4) *out++ = hex[value >> shift & 15];
    *out++ = '\n';
    return out;
}
static uint32_t format(char *buffer)
{
    char *p = append(buffer, "RevolaOS crash report v1\n");
    p = word(p, "Report", CORE_CRASH_RECORD->report_id);
    p = word(p, "Firmware", CORE_CRASH_RECORD->firmware);
    p = word(p, "Build", CORE_CRASH_RECORD->build);
    p = word(p, "Attributed owner", CORE_CRASH_RECORD->owner);
    p = append(p, "Extension: ");
    for (unsigned i = 0; i < sizeof CORE_CRASH_RECORD->name && CORE_CRASH_RECORD->name[i]; i++)
        *p++ = CORE_CRASH_RECORD->name[i];
    *p++ = '\n';
    p = append(p, "Extension ID: ");
    for (unsigned i = 0; i < sizeof CORE_CRASH_RECORD->id && CORE_CRASH_RECORD->id[i]; i++)
        *p++ = CORE_CRASH_RECORD->id[i];
    *p++ = '\n';
    p = append(p, "File: ");
    for (unsigned i = 0; i < sizeof CORE_CRASH_RECORD->file && CORE_CRASH_RECORD->file[i]; i++)
        *p++ = CORE_CRASH_RECORD->file[i];
    *p++ = '\n';
    p = word(p, "C++ exception kind", CORE_CRASH_RECORD->exception_kind);
    p = word(p, "C++ what address", CORE_CRASH_RECORD->exception_address);
    static const char *const labels[] = {"R0","R1","R2","R3","R4","R5","R6","R7","R8","R9","R10","R11","R12",
        "Exception LR","Type","CPSR","SPSR","DFSR","DFAR","IFSR","Frame valid","SP","Stack words"};
    const volatile uint32_t *w = (const volatile uint32_t *)&CORE_CRASH_RECORD->frame;
    for (unsigned i = 0; i < 23; i++) p = word(p, labels[i], w[i]);
    for (unsigned i = 0; i < PANIC_STACK_WORDS && i < CORE_CRASH_RECORD->frame.stack_count; i++)
        p = word(p, "Stack", CORE_CRASH_RECORD->frame.stack[i]);
    p = word(p, "Backlight (1=on)", CORE_CRASH_RECORD->backlight[0]);
    p = word(p, "I2C clock gates", CORE_CRASH_RECORD->backlight[1]);
    p = word(p, "I2C status", CORE_CRASH_RECORD->backlight[2]);
    p = word(p, "I2C control", CORE_CRASH_RECORD->backlight[3]);
    p = append(p, "Issue: https://github.com/RevolaOS/revola/issues\n");
    return (uint32_t)(p - buffer);
}
enum report_match { REPORT_MISSING, REPORT_SAME, REPORT_DIFFERENT, REPORT_UNREADABLE };

static enum report_match matches(const char *path, const char *expected, uint32_t bytes)
{
    void *handle;
    int status = core_file_open_status(path, CORE_FILE_READ, &handle);
    if (status)
        return (uint32_t)status == FileError_NotFound ? REPORT_MISSING : REPORT_UNREADABLE;
    char chunk[64];
    enum report_match result = REPORT_SAME;
    for (uint32_t at = 0; at < bytes && result == REPORT_SAME;) {
        uint32_t want = bytes - at, got;
        if (want > sizeof chunk) want = sizeof chunk;
        status = core_file_read_status(handle, chunk, want, &got);
        if (status && (uint32_t)status != FileError_EndOfFile) {
            result = REPORT_UNREADABLE;
            break;
        }
        if (got != want) result = REPORT_DIFFERENT;
        for (uint32_t i = 0; result == REPORT_SAME && i < got; i++)
            if (chunk[i] != expected[at + i]) result = REPORT_DIFFERENT;
        at += want;
    }
    /* Only the captured bytes matter; a trailing suffix does not invalidate them. */
    core_file_close(handle);
    return result;
}

/* Report IDs are non-security PRNG values. A failed open is not evidence of
 * absence unless the filesystem explicitly reports FileError_NotFound. */
static uint32_t unused_id(uint32_t seed)
{
    char path[32];
    uint32_t id = seed ? seed : 1;
    for (unsigned attempt = 0; attempt < 64; attempt++) {
        id ^= id << 13; id ^= id >> 17; id ^= id << 5;
        if (core_recovery_valid() && id == CORE_CRASH_RECORD->report_id) continue;
        core_recovery_path(path, id);
        void *handle;
        int status = core_file_open_status(path, CORE_FILE_READ, &handle);
        if (!status) {
            core_file_close(handle);
            continue;
        }
        return (uint32_t)status == FileError_NotFound ? id : 0;
    }
    return 0;
}

void core_recovery_prepare(void)
{
    char path[32];
    core_recovery_log_status = 0;
    int directory = core_file_create_directory("/Revola/Crashes");
    if (core_recovery_valid() && CORE_CRASH_RECORD->report_id) {
        char report[1536];
        uint32_t bytes = format(report);
        core_recovery_path(path, CORE_CRASH_RECORD->report_id);
        enum report_match existing = matches(path, report, bytes);
        if (existing == REPORT_SAME) {
            core_recovery_log_status = 2;
        } else if (directory && existing != REPORT_UNREADABLE) {
            if (existing == REPORT_DIFFERENT) {
                uint32_t id = unused_id(CORE_CRASH_RECORD->report_id);
                if (!id) goto next_report;
                /* Commit the new destination before writing. A reset can leave
                 * a partial file, but the next boot must never overwrite it. */
                core_recovery_clear();
                CORE_CRASH_RECORD->report_id = id;
                CORE_CRASH_RECORD->checksum = checksum();
                sync_record();
                CORE_CRASH_RECORD->commit = COMMIT;
                sync_record();
                core_recovery_path(path, id);
                bytes = format(report);
            }
            void *handle;
            if (core_file_open(path, CORE_FILE_WRITE, &handle)) {
                int ok = core_file_write(handle, report, bytes);
                core_file_close(handle);
                if (ok && matches(path, report, bytes) == REPORT_SAME)
                    core_recovery_log_status = 1;
            }
        }
    }
next_report:
    core_recovery_report_id = unused_id(Clock_GetMicroseconds() ^ CORE_BUILD_ID);
}
int core_recovery_center(unsigned *released, uint32_t packet)
{
    uint32_t buttons;
    if ((packet & WheelPacket_Mask) == WheelPacket_Value)
        buttons = packet >> 8 & 31;
    else if ((packet & WheelPacket_ButtonsMask) == WheelPacket_ButtonsValue)
        buttons = packet >> 16 & 31;
    else return 0;
    if (!buttons) *released = 1;
    return *released && buttons == WheelButton_Center;
}
void core_recovery_wait(void)
{
    volatile uint32_t *status = WheelController + WheelController_status / sizeof *WheelController;
    volatile uint32_t *ack = WheelController + WheelController_acknowledge / sizeof *WheelController;
    volatile uint32_t *packet = WheelController + WheelController_packet / sizeof *WheelController;
    unsigned released = !*(volatile uint32_t *)(WheelState + WheelState_buttons);
    /* A packet already waiting at the crash cannot request a restart. */
    if (*status & WheelController_Ready) {
        uint32_t pending = *packet;
        *ack = WheelController_Acknowledge;
        released = 0;
        core_recovery_center(&released, pending);
    }
    for (;;) {
        if (!(*status & WheelController_Ready)) continue;
        uint32_t value = *packet;
        *ack = WheelController_Acknowledge;
        if (core_recovery_center(&released, value)) {
            SetWarmBootModeTag(WarmBootMode_None);
            ResetSocAndWait();
        }
    }
}
