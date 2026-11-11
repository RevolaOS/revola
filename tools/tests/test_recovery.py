# SPDX-License-Identifier: GPL-3.0-only
import subprocess
import tempfile
import unittest
from pathlib import Path

root = Path(__file__).resolve().parents[2]
source = (root / "core/recovery.c").read_text()
source = "\n".join(
    line for line in source.splitlines() if not line.startswith("#include")
)
frame = (root / "core/panic/panic.h").read_text()
frame = frame[
    frame.index("struct panic_frame {") : frame.index("void core_panic_screen")
]
record = (root / "core/recovery.h").read_text()
record = record[
    record.index("struct core_crash_record {") : record.index(
        "#define CORE_CRASH_RECORD"
    )
]
constants = (root / "firmware/osos-2.0.5.h").read_text().splitlines()
constants = "\n".join(
    line
    for line in constants
    if line.startswith("#define WheelPacket_")
    or line.startswith("#define WheelButton_Center")
    or line.startswith("#define WheelController_")
    or line.startswith("#define FileError_")
)
stubs = (
    r"""
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define PANIC_STACK_WORDS 12
#define CORE_RECOVERY_BYTES 512
#define CORE_FIRMWARE_ID 0x660eb6ff
#define CORE_BUILD_ID 0x12345678
#define CORE_FILE_READ 1
#define CORE_FILE_WRITE 2
#define CORE_MANIFEST_ID 0
#define CORE_MANIFEST_NAME 1
#define WarmBootMode_None 0
FRAME
RECORD
CONSTANTS
static struct core_crash_record retained;
#define CORE_CRASH_RECORD (&retained)
struct rv_extension_record { struct rv_extension_record *next; uint32_t owner; const char *file; const void *manifest; };
struct rv_extension_record *core_extensions;
uint32_t core_panic_exception[2];
volatile uint32_t WheelController[8];
uint32_t WheelState[16];
#define WheelState_buttons 0x20
struct file { char path[32], data[2048]; uint32_t bytes, cursor; };
static struct file files[128];
static unsigned file_count, writes, committed_syncs;
static int fail_write, fail_read, fail_open, fail_directory;
static uint32_t partial_write;
int core_recovery_valid(void);
const char *core_loader_manifest_text(const void *m, int field) { (void)m; return field ? "Test extension" : "test-extension"; }
void core_cache_sync(const void *a, const void *b) {
 (void)a; (void)b;
 if (retained.commit) { assert(core_recovery_valid()); committed_syncs++; }
}
uint32_t Clock_GetMicroseconds(void) { return 12345; }
void SetWarmBootModeTag(int mode) { (void)mode; }
void ResetSocAndWait(void) { assert(0); }
int core_file_create_directory(const char *p) { (void)p; return !fail_directory; }
static struct file *find_file(const char *p) {
 for (unsigned i = 0; i < file_count; i++) if (!strcmp(files[i].path, p)) return &files[i];
 return NULL;
}
int core_file_open_status(const char *p, int mode, void **h) {
 if (fail_open) return -99;
 struct file *f = find_file(p);
 if (mode == CORE_FILE_READ && !f) return (int)FileError_NotFound;
 if (mode == CORE_FILE_WRITE) {
  assert(!f); /* Recovery must never open an existing file for replacement. */
  assert(file_count < 128); f = &files[file_count++]; strcpy(f->path, p); writes++;
 }
 f->cursor = 0; *h = f; return 0;
}
int core_file_open(const char *p, int mode, void **h) { return !core_file_open_status(p, mode, h); }
int core_file_read_status(void *h, void *b, uint32_t n, uint32_t *got) {
 struct file *f = h; *got = 0; if (fail_read) return -99;
 if (f->cursor == f->bytes) return (int)FileError_EndOfFile;
 if (n > f->bytes - f->cursor) n = f->bytes - f->cursor;
 memcpy(b, f->data + f->cursor, n); f->cursor += n; *got = n; return 0;
}
int core_file_write(void *h, const void *b, uint32_t n) {
 struct file *f = h; assert(n < sizeof f->data);
 if (fail_write && partial_write < n) n = partial_write;
 memcpy(f->data, b, n); f->bytes = n; return !fail_write;
}
void core_file_close(void *h) { (void)h; }

""".replace("\nFRAME\n", "\n" + frame + "\n")
    .replace("\nRECORD\n", "\n" + record + "\n")
    .replace("\nCONSTANTS\n", "\n" + constants + "\n")
)
tests = r"""
static struct file *report_file(void) {
 char path[32]; core_recovery_path(path, retained.report_id); return find_file(path);
}
static void capture(void) {
 struct panic_frame f = {0}; f.type = 4; f.dfar = 0xdeadbeef; f.stack_count = 0xffffffff;
 core_recovery_prepare(); assert(core_recovery_report_id);
 core_recovery_capture(&f, 99); assert(core_recovery_valid());
}
static void retry_failed_write(uint32_t partial) {
 capture(); fail_write = 1; partial_write = partial;
 core_recovery_prepare(); assert(!core_recovery_log_status); assert(core_recovery_valid());
 struct file *old = report_file(); assert(old && old->bytes == partial);
 uint32_t id = retained.report_id; unsigned before = writes;
 char saved[2048]; memcpy(saved, old->data, sizeof saved);
 fail_write = 0; core_recovery_prepare();
 assert(core_recovery_log_status == 1 && writes == before + 1);
 assert(retained.report_id != id && core_recovery_valid() && retained.owner == 99);
 assert(!memcmp(saved, old->data, sizeof saved) && old->bytes == partial);
 core_recovery_prepare(); assert(core_recovery_log_status == 2);
}
int main(void) {
 struct rv_extension_record ext = {NULL, 99, "test-extension.rvx", NULL}; core_extensions = &ext;
 capture(); assert(!strcmp(retained.id, "test-extension"));
 uint32_t backlight[4] = {1, 0x10, 0x90, 0x184};
 core_recovery_backlight(backlight); assert(core_recovery_valid());
 assert(retained.backlight[0] == 1 && retained.backlight[3] == 0x184);
 uint32_t commit = retained.commit; retained.commit = 0; assert(!core_recovery_valid()); retained.commit = commit;
 retained.frame.dfar ^= 1; assert(!core_recovery_valid()); retained.frame.dfar ^= 1;
 retained.firmware ^= 1; assert(!core_recovery_valid()); retained.firmware ^= 1;
 core_recovery_prepare(); assert(core_recovery_log_status == 1 && writes == 1);
 assert(strstr(report_file()->data, "Extension ID: test-extension\n"));
 assert(strstr(report_file()->data, "DFAR: DEADBEEF\n"));
 assert(core_recovery_report_id != retained.report_id);
 core_recovery_prepare(); assert(core_recovery_log_status == 2 && writes == 1);

 uint32_t id = retained.report_id;
 fail_read = 1; core_recovery_prepare();
 assert(!core_recovery_log_status && writes == 1 && retained.report_id == id);
 fail_read = 0; core_recovery_prepare(); assert(core_recovery_log_status == 2);
 fail_open = 1; core_recovery_prepare();
 assert(!core_recovery_log_status && !core_recovery_report_id && writes == 1 && retained.report_id == id);
 fail_open = 0; core_recovery_prepare(); assert(core_recovery_log_status == 2);

 struct file *old = report_file(); old->data[0] ^= 1;
 uint32_t collision = unused_id(id); char path[32]; core_recovery_path(path, collision);
 void *h; assert(core_file_open(path, CORE_FILE_WRITE, &h)); core_file_close(h);
 unsigned before = writes;
 core_recovery_prepare(); assert(core_recovery_log_status == 1 && writes == before + 1);
 assert(retained.report_id != id && retained.report_id != collision && core_recovery_valid());
 assert(old->data[0] == ('R' ^ 1));
 core_recovery_prepare(); assert(core_recovery_log_status == 2);
 retry_failed_write(0); retry_failed_write(64); retry_failed_write(73);

 capture(); id = retained.report_id; before = writes;
 fail_directory = 1; core_recovery_prepare();
 assert(!core_recovery_log_status && writes == before && retained.report_id == id);
 fail_directory = 0; core_recovery_prepare(); assert(core_recovery_log_status == 1);

 /* Maximum captured strings and stack count fit the formatter's stack buffer. */
 core_recovery_clear();
 memset(retained.name, 'N', sizeof retained.name);
 memset(retained.file, 'F', sizeof retained.file);
 memset(retained.id, 'I', sizeof retained.id);
 retained.checksum = checksum(); retained.commit = COMMIT;
 char formatted[1536]; assert(format(formatted) < sizeof formatted);

 unsigned released = 0;
 assert(!core_recovery_center(&released, WheelPacket_Value | (WheelButton_Center << 8)));
 assert(!core_recovery_center(&released, 0)); assert(!released);
 assert(!core_recovery_center(&released, WheelPacket_Value)); assert(released);
 assert(!core_recovery_center(&released, WheelPacket_Value | (3 << 8)));
 assert(core_recovery_center(&released, WheelPacket_Value | (WheelButton_Center << 8)));
 released = 0; assert(!core_recovery_center(&released, WheelPacket_ButtonsValue));
 assert(core_recovery_center(&released, WheelPacket_ButtonsValue | (WheelButton_Center << 16)));
 assert(committed_syncs >= 10);
 core_recovery_clear(); assert(!core_recovery_valid());
 puts("PASS: record integrity, bounded report, collisions, transient I/O, failed-write retries, fresh Center");
}
"""


class RecoveryTests(unittest.TestCase):
    def test_capture_save_and_center(self):
        with tempfile.TemporaryDirectory() as d:
            c = Path(d) / "test.c"
            c.write_text(stubs + "\n" + source + "\n" + tests)
            binary = Path(d) / "test"
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-fsanitize=address,undefined",
                    str(c),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
