# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the loader's C rejection and quarantine paths with failing storage."""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "core/loader/loader.c").read_text()


def function(name):
    match = re.search(
        r"^(?:static )?[\w *]+\b" + name + r"\([^;]*?\)\n\{", SOURCE, re.M
    )
    start = match.start()
    at = match.end()
    depth = 1
    while depth:
        depth += (SOURCE[at] == "{") - (SOURCE[at] == "}")
        at += 1
    return SOURCE[start:at] + "\n"


PREAMBLE = r"""
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "loader/loader.h"
#include "loader/file.h"
#include "loader/rvx.h"
#include "settings.h"
#include "storage.h"
#include "ui/bitmaps.h"
#include "ui/panes.h"
#include "rv_api.h"
#define CORE_FIRMWARE_ID 0x660eb6ff
"""
DEFINITIONS = SOURCE[
    SOURCE.index("#define ROOT ") : SOURCE.index("static struct block *retiring;")
]
FOUND = SOURCE[SOURCE.index("struct found {") : SOURCE.index("/* The length of `name`")]
CATALOG = SOURCE[
    SOURCE.index("/* Streams catalog entries") : SOURCE.index("static int catalog_start")
]
STUBS = r"""
struct package { char name[33]; uint32_t data[300]; uint32_t bytes, cursor; };
static struct package packages[2];
static unsigned package_count, entered, clears, set_calls;
static int fail_set, fail_flush;
static uint32_t retained_owner;
uint32_t core_recovery_log_status;
struct setting { uint32_t owner; uint16_t key; int32_t value; };
static struct setting settings[16], persisted[16];
static unsigned settings_count, persisted_count;
uint32_t core_settings_owner(const char *s) {
 uint32_t hash = 2166136261u;
 while (*s) hash = (hash ^ (unsigned char)*s++) * 16777619u;
 return hash;
}
int32_t core_settings_get_int(uint32_t owner, uint16_t key, int32_t fallback) {
 for (unsigned i = 0; i < settings_count; i++)
  if (settings[i].owner == owner && settings[i].key == key) return settings[i].value;
 return fallback;
}
int core_settings_set_int(uint32_t owner, uint16_t key, int32_t value) {
 if (++set_calls == (unsigned)fail_set) return -1;
 unsigned i = 0;
 for (; i < settings_count; i++) if (settings[i].owner == owner && settings[i].key == key) break;
 assert(i < 16); settings[i] = (struct setting){owner,key,value};
 if (i == settings_count) settings_count++;
 return 0;
}
void core_settings_flush(void) {
 if (fail_flush) return;
 memcpy(persisted, settings, sizeof settings); persisted_count = settings_count;
}
static uint32_t unbundled; /* the one test package that is not in bundled.def */
static int default_on(uint32_t owner) { return owner != unbundled; }
static uint32_t load_order[4]; static unsigned loads;
uint32_t core_recovery_owner(void) { return retained_owner; }
void core_recovery_clear(void) { retained_owner = 0; clears++; }
int core_file_open(const char *path, int mode, void **out) {
 assert(mode == CORE_FILE_READ);
 for (unsigned i = 0; i < package_count; i++) {
  char expected[96]; snprintf(expected, sizeof expected, "/Revola/Extensions/%s.rvx", packages[i].name);
  if (!strcmp(path, expected)) { packages[i].cursor = 0; *out = &packages[i]; return 1; }
 }
 return 0;
}
int core_file_read(void *handle, void *out, uint32_t bytes, uint32_t *got) {
 struct package *p = handle;
 if (bytes > p->bytes - p->cursor) bytes = p->bytes - p->cursor;
 memcpy(out, (char *)p->data + p->cursor, bytes); p->cursor += bytes; *got = bytes; return 1;
}
void core_file_close(void *h) { (void)h; }
static void *allocate(struct rv_extension_record *owner, uint32_t bytes) {
 (void)owner; struct block *b = calloc(1, sizeof *b + bytes); assert(b); return b + 1;
}
void core_loader_charge(struct rv_extension_record *owner, int32_t bytes) { (void)owner; (void)bytes; }
void *core_heap_allocate(struct rv_extension_record *owner, uint32_t bytes) { (void)owner; return calloc(1, bytes); }
static void release(struct rv_extension_record *owner, void *p) { (void)owner; free(p); }
static uint32_t load_index[4];
static int load_module(struct rv_extension_record *r, uint32_t index) {
 if (loads < 4) { load_index[loads] = index; load_order[loads++] = r->owner; }
 entered++;
 return index == 0; /* a kept module that does not fit */
}
int core_file_seek(void *handle, uint32_t at) { struct package *p = handle; if (at > p->bytes) return 0; p->cursor = at; return 1; }
static uint32_t held_buttons(void) { return 0; }
static const struct core_hook disk_mode_exit_hook = {0};
int core_hook_bind(const struct core_hook *h) { (void)h; return 0; }
int core_storage_is_flash(const uint16_t *identify) { (void)identify; return 0; }
uint16_t ATA_Identify[256];
void core_panes_refresh(void) {}
int core_bitmaps_replacement(uint32_t owner, uint32_t name, struct rv_asset_stream *stream) { (void)owner; (void)name; (void)stream; return 0; }
int core_bitmaps_replacement_read(uint32_t owner, uint32_t name, void *out, uint32_t stored) { (void)owner; (void)name; (void)out; (void)stored; return 0; }
"""
SCAN = r"""
static struct found *scan(uint32_t *count, uint8_t *buffer) {
 *count = package_count; struct found *list = calloc(*count, sizeof *list); assert(list);
 for (unsigned i = 0; i < *count; i++) strcpy(list[i].file, packages[i].name);
 pick(list, *count, buffer); return list;
}
"""
TESTS = r"""
static void package(unsigned n, const char *name) {
 struct package *p = &packages[n]; memset(p, 0, sizeof *p); strcpy(p->name, name);
 struct rvx_header *file = (void *)p->data;
 struct manifest_header *h = (void *)(file + 1);
 *h = (struct manifest_header){.magic=MANIFEST_MAGIC,.version=MANIFEST_VERSION,.api=RV_API_MAJOR<<16,
  .flags=0,.module_count=1,.strings_bytes=(uint32_t)strlen(name)+1};
 /* Zero text offsets point to the ID, a valid bounded string for every field. */
 strcpy((char *)h + tables_bytes(h), name);
 *file = (struct rvx_header){.magic=RVX_MAGIC,.format=RVX_FORMAT,.api=RV_API_MAJOR<<16,
  .firmware=CORE_FIRMWARE_ID,.manifest_bytes=tables_bytes(h)+h->strings_bytes};
 p->bytes = sizeof *file + file->manifest_bytes;
}
/* Appends a content folder name to package 0's strings, without its NUL when `nul` is 0. */
static void content(const char *name, int nul) {
 struct package *p = &packages[0]; struct rvx_header *file = (void *)p->data;
 struct manifest_header *h = (void *)(file + 1); char *strings = (char *)h + tables_bytes(h);
 memcpy(strings + h->strings_bytes, name, strlen(name) + 1);
 h->content = (uint16_t)h->strings_bytes; h->content_count = 1;
 h->strings_bytes += (uint32_t)strlen(name) + (nul ? 1 : 0);
 file->manifest_bytes = tables_bytes(h) + h->strings_bytes; p->bytes = sizeof *file + file->manifest_bytes;
}
static void free_extensions(void) {
 while (core_extensions) { struct rv_extension_record *r = core_extensions; core_extensions = r->next; free(header_of(r)); }
}
static void boot(void) {
 free_extensions(); entered = 0; set_calls = 0;
 memcpy(settings, persisted, sizeof settings); settings_count = persisted_count;
 core_loader_boot();
}
int main(void) {
 package_count = 1; package(0, "test");
 uint32_t buffer[CORE_MANIFEST_MAX/4]; uint8_t failure;
 struct rvx_header *file = (void *)packages[0].data;
 struct manifest_header *h = (void *)(file + 1);
 assert(read_manifest("test", (void *)buffer, &failure) && failure == CORE_EXTENSION_OK);
 content("Themes", 1);
 assert(read_manifest("test", (void *)buffer, &failure) && failure == CORE_EXTENSION_OK);
 for (unsigned mode = 0; mode < 12; mode++) {
  package(0, "test");
  switch (mode) {
   case 0: file->format++; break;
   case 1: file->api += 1<<16; break;
   case 2: file->firmware ^= 1; break;
   case 3: h->version++; break;
   case 4: h->api += 1<<16; break;
   case 5: packages[0].bytes = sizeof *file - 1; break;
   case 6: h->text[0] = h->strings_bytes; break;
   case 7: h->content_count = 5; break;
   case 8: h->content_count = 1; h->content = (uint16_t)h->strings_bytes; break;
   case 9: content("themes", 1); break;
   case 10: content("Data", 1); break;
   case 11: content("Themes", 1); h->content_count = 2; break;
  }
  assert(!read_manifest("test", (void *)buffer, &failure));
  assert(failure == (mode < 5 ? CORE_EXTENSION_INCOMPATIBLE : CORE_EXTENSION_UNREADABLE));
  assert(!load_extension("test") && !entered);
  struct core_installed *rows; assert(core_loader_installed(&rows) == 1);
  assert(rows[0].failure == failure && !rows[0].owner && !rows[0].name[0]); free(rows);
 }
 package_count = 2; package(0, "test"); package(1, "other");
 for (unsigned mode = 0; mode < 3; mode++) {
  persisted_count = settings_count = clears = 0;
  retained_owner = core_settings_owner("test"); core_recovery_log_status = 2;
  fail_set = mode < 2 ? (int)mode + 1 : 0; fail_flush = mode == 2;
  boot(); assert(entered == 1 && core_extensions->owner == core_settings_owner("other"));
  assert(retained_owner && !clears);
  fail_set = fail_flush = 0;
  boot(); assert(entered == 1 && retained_owner && !clears);
  boot(); assert(entered == 1 && !retained_owner && clears == 1);
 }
 /* Bundled extensions load first whatever the file names. */
 package_count = 2; package(0, "a"); package(1, "b");
 unbundled = core_settings_owner("a"); loads = 0;
 core_settings_set_int(unbundled, 0, 1); /* the listener turned it on */
 core_settings_flush();
 boot(); assert(loads == 2 && load_order[0] == core_settings_owner("b") && load_order[1] == unbundled);
 unbundled = 0;
 free_extensions();
 puts("PASS: rejected packages remain visible; quarantine survives allocation and flush failures");
}
"""


KEPT_TESTS = r"""
static void entry(struct rvx_entry *e, uint8_t kind, uint8_t preload, uint32_t offset) {
 *e = (struct rvx_entry){.name = offset, .kind = kind, .codec = RVX_RAW, .preload = preload,
                         .offset = offset, .stored = 20, .bytes = 20};
}
int main(void) {
 /* Three modules, main always then optional then never, and one always asset. */
 struct package *p = &packages[0];
 struct rv_extension_record r = {0};
 struct core_module modules[3] = {0};
 struct { struct manifest_header header; struct manifest_module modules[3]; } manifest = {
  .header = {.module_count = 3},
  .modules = {{.preload = RVX_PRELOAD_ALWAYS}, {.trigger = TRIGGER_MANUAL, .preload = RVX_PRELOAD_OPTIONAL},
              {.trigger = TRIGGER_SCREEN, .preload = RVX_PRELOAD_NEVER}}};
 r.manifest = (uint8_t *)&manifest;
 r.modules = modules; r.module_count = 3; r.entry_count = 4; r.manifest_bytes = sizeof manifest;
 modules[0].loaded = 1;
 assert(module_record(&r, 1)->preload == RVX_PRELOAD_OPTIONAL && module_record(&r, 2)->preload == RVX_PRELOAD_NEVER);
 /* Performance keeps the optional module and loads it after main. */
 preload_limit = RVX_PRELOAD_OPTIONAL;
 assert(!kept(&r, 0) && kept(&r, 1) && !kept(&r, 2));
 keep_modules(&r);
 assert(loads == 1 && load_index[0] == 1);
 /* Memory keeps nothing optional. */
 preload_limit = RVX_PRELOAD_ALWAYS; loads = 0;
 assert(!kept(&r, 1));
 keep_modules(&r);
 assert(loads == 0);
 /* Nothing loads while main is not. */
 preload_limit = RVX_PRELOAD_OPTIONAL; modules[0].loaded = 0;
 keep_modules(&r);
 assert(loads == 0);
 /* Catalog rules: every module entry carries always, the assets sort by class. */
 uint32_t catalog = sizeof(struct rvx_header) + 48, payloads = catalog + 4 * sizeof(struct rvx_entry);
 r.manifest_bytes = 48;
 struct rvx_entry e;
 entry(&e, RVX_MODULE, RVX_PRELOAD_OPTIONAL, payloads);
 assert(!entry_valid(&r, &e, 0, 0) && !entry_valid(&r, &e, 1, 0));
 entry(&e, RVX_MODULE, RVX_PRELOAD_ALWAYS, payloads);
 assert(entry_valid(&r, &e, 1, 0));
 entry(&e, RVX_DATA, RVX_PRELOAD_ALWAYS, payloads);
 assert(entry_valid(&r, &e, 3, 0));
 entry(&e, RVX_DATA, RVX_PRELOAD_OPTIONAL, payloads);
 assert(!entry_valid(&r, &e, 3, RVX_PRELOAD_NEVER) && entry_valid(&r, &e, 3, RVX_PRELOAD_OPTIONAL));
 /* A walk over a file in that shape reaches the end. */
 struct rvx_entry *entries = (struct rvx_entry *)((uint8_t *)p->data + catalog);
 entry(&entries[0], RVX_MODULE, RVX_PRELOAD_ALWAYS, payloads);
 entry(&entries[1], RVX_MODULE, RVX_PRELOAD_ALWAYS, payloads + 20);
 entry(&entries[2], RVX_MODULE, RVX_PRELOAD_ALWAYS, payloads + 40);
 entry(&entries[3], RVX_DATA, RVX_PRELOAD_OPTIONAL, payloads + 60);
 p->bytes = payloads + 80;
 struct catalog c;
 assert(catalog_start(&c, &r, p, 1, 4));
 for (unsigned i = 1; i < 4; i++) { const struct rvx_entry *next = catalog_next(&c); assert(next && next->preload == entries[i].preload); }
 assert(!catalog_next(&c) && c.index == c.end);
 puts("PASS: kept modules follow the manifest class and the preload limit; the catalog rules hold");
 return 0;
}
"""


def compile_and_run(source):
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory)
        (path / "test.c").write_text(source)
        subprocess.run(
            [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-Wno-unused-function",
                "-Wno-unused-const-variable",
                "-fsanitize=address,undefined",
                "-I",
                str(ROOT / "core"),
                "-I",
                str(ROOT / "firmware"),
                "-I",
                str(ROOT / "sdk/extension"),
                str(path / "test.c"),
                "-o",
                str(path / "test"),
            ],
            check=True,
        )
        subprocess.run([str(path / "test")], check=True)


class LoaderRecoveryTests(unittest.TestCase):
    def test_kept_modules_and_catalog_classes(self):
        functions = "".join(
            function(name)
            for name in [
                "fnv1a",
                "copy",
                "catalog_offset",
                "read_all",
                "entry_valid",
                "catalog_start",
                "catalog_next",
                "module_record",
                "kept",
                "keep_modules",
            ]
        )
        compile_and_run(
            PREAMBLE + "\n#include <stdio.h>\n" + DEFINITIONS + FOUND + STUBS + CATALOG + functions + KEPT_TESTS
        )

    def test_rejected_packages_and_quarantine(self):
        functions = "".join(
            function(name)
            for name in [
                "fnv1a",
                "copy",
                "length",
                "same_name",
                "header_of",
                "tables_bytes",
                "strings",
                "content_folder_valid",
                "manifest_valid",
                "open_extension",
                "read_manifest",
                "pick",
                "load_extension",
                "find_file",
                "load_found",
            ]
        )
        source = (
            PREAMBLE
            + "\n#include <stdio.h>\n"
            + DEFINITIONS
            + FOUND
            + STUBS
            + "static void keep_modules(struct rv_extension_record *record);\n"
            + functions
            + SCAN
            + function("core_loader_boot")
            + function("core_loader_installed")
            + "static void keep_modules(struct rv_extension_record *record) { (void)record; }\n"
            + TESTS
        )
        compile_and_run(source)
