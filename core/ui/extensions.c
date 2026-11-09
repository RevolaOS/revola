/* SPDX-License-Identifier: GPL-3.0-only */

/* Settings > Extensions, flattened into the Settings list until the milestone 7
 * UI runtime gives it a screen of its own: a meter row with the bytes extensions
 * hold, then one row per installed extension with its state in the title.
 * Selecting a row toggles the extension. Rows are built the About way (copies of
 * the Legal row appended while Apple builds the list, titles from Settings model
 * properties), so the list of installed extensions is read from disk once, when
 * the list is built, and each row costs its 0x98 bytes plus the entry below. */

#include "ui/about.h"
#include "loader/loader.h"
#include "osos-2.0.5.h"
#include "rv_api.h"
#include "hooks/sites.h"

#define MAX_ROWS 16
#define ROW_BYTES 0x98
#define TEXT_BYTES 40
/* Ids from the core's block, after the About rows' 0x0cf00001 to 0x0cf0002f. */
#define ITEM_ID(i) (0x0cf00041u + (i))
#define SOURCE_ID(i) (0x0cf00061u + (i))
#define PROPERTY_ID(i) (0x0cf00081u + (i))
#define METER 0 /* row 0 is the meter, rows 1.. are extensions */
#define ACTION "RevolaExtension"
#define FIELD(base, offset, type) (*(type *)((uint8_t *)(base) + (offset)))

static struct core_installed *installed; /* heap, one per row after the meter */
static uint32_t (*rows)[ROW_BYTES / 4];  /* heap, MAX_ROWS rows */
static uint32_t row_count;
static char text[TEXT_BYTES];

static char *put_text(char *out, const char *end, const char *value)
{
    while (*value && out < end - 1)
        *out++ = *value++;
    *out = 0;
    return out;
}

static char *put_number(char *out, const char *end, uint32_t value)
{
    char digits[12];
    int n = 0;
    do {
        digits[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (n && out < end - 1)
        *out++ = digits[--n];
    *out = 0;
    return out;
}

static const char *state_text(uint32_t owner)
{
    struct rv_extension_record *record = core_loader_find(owner);
    if (record && record->pending_unload)
        return "Off";
    if (record) {
        switch (record->failure) {
        case CORE_EXTENSION_OK:
            return "On";
        case CORE_EXTENSION_CONFLICT:
            return "Conflict";
        case CORE_EXTENSION_NO_MEMORY:
            return "No memory";
        case CORE_EXTENSION_ID_COLLISION:
            return "Id collision";
        default:
            return "Failed";
        }
    }
    return owner == core_loader_crashed ? "Off after crash" : "Off";
}

static const char *row_text(uint32_t index)
{
    char *out = text, *end = text + TEXT_BYTES;
    if (index == METER) {
        out = put_text(out, end, "Extensions ");
        if (core_loader_core_only)
            return put_text(out, end, "off (core only)"), text;
        out = put_number(out, end, (core_loader_total_bytes() + 512) / 1024);
        put_text(out, end, " KiB");
        return text;
    }
    const struct core_installed *entry = &installed[index - 1];
    out = put_text(out, end, entry->name);
    out = put_text(out, end, ": ");
    out = put_text(out, end, state_text(entry->owner));
    struct rv_extension_record *record = core_loader_find(entry->owner);
    if (record && record->failure == CORE_EXTENSION_OK && !record->pending_unload) {
        out = put_text(out, end, ", ");
        out = put_number(out, end, record->live);
        put_text(out, end, " B");
    }
    return text;
}

/* The rows follow the last About row each time Apple builds the list. They are
 * made from it on the first build, when the installed list is read from disk. */
static uint32_t append_rows(uint32_t list, uint32_t data, uint32_t bytes, uint32_t metadata,
                            const struct core_next *next)
{
    uint32_t result = core_next_call(next, list, data, bytes, metadata);
    const uint32_t *row = (const uint32_t *)data;
    if (bytes != ROW_BYTES || row[MenuRow_id / 4] != CORE_ABOUT_ITEM_ID(CORE_ABOUT_ROWS - 1))
        return result;
    if (!rows) {
        rows = Heap_AllocateZeroed(MAX_ROWS * ROW_BYTES + (MAX_ROWS - 1) * sizeof *installed);
        if (!rows)
            return result;
        installed = (struct core_installed *)(rows + MAX_ROWS);
        row_count =
            1 + (core_loader_core_only ? 0 : core_loader_installed(installed, MAX_ROWS - 1));
        for (uint32_t i = 0; i < row_count; i++) {
            for (uint32_t w = 0; w < ROW_BYTES / 4; w++)
                rows[i][w] = row[w];
            rows[i][MenuRow_id / 4] = ITEM_ID(i);
            rows[i][MenuRow_title / 4] = SOURCE_ID(i);
            ((uint8_t *)rows[i])[MenuRow_title_from_model] = 1;
        }
    }
    for (uint32_t i = 0; i < row_count; i++)
        core_next_call(next, list, (uint32_t)rows[i], bytes, metadata);
    return result;
}

static uint32_t property(uint32_t model, uint32_t type, uint32_t id, uint32_t out,
                         const struct core_next *next)
{
    if (type == Property_String && id - PROPERTY_ID(0) < row_count) {
        *(const char **)out = row_text(id - PROPERTY_ID(0));
        return 1;
    }
    return core_next_call(next, model, type, id, out);
}

/* Row index from "RevolaExtension<n>", 0 for any other action name. A handler
 * binding carries no argument of its own, so the row is in the name. */
static uint32_t action_row(const char *name)
{
    const char *prefix = ACTION;
    while (*prefix && *name == *prefix)
        name++, prefix++;
    uint32_t row = 0;
    if (*prefix || *name < '1' || *name > '9')
        return 0;
    while (*name >= '0' && *name <= '9')
        row = row * 10 + (uint32_t)(*name++ - '0');
    return *name ? 0 : row;
}

/* Toggles the row's extension and refreshes the row and the meter. */
static uint32_t action(uint32_t controller, uint32_t name, uint32_t argument,
                       const struct core_next *next)
{
    uint32_t row = action_row((const char *)name);
    if (!row)
        return core_next_call(next, controller, name, argument, 0);
    if (argument == ActionSupportQuery)
        return 1;
    if (row >= row_count)
        return 0;
    const struct core_installed *entry = &installed[row - 1];
    struct rv_extension_record *record = core_loader_find(entry->owner);
    core_loader_set_enabled(entry->directory, !record || record->pending_unload);
    void *model = FIELD((void *)controller, ScreenController_settings_model, void *);
    void (*notify)(void *, uint32_t, uint32_t) =
        FIELD(FIELD(model, 0, void **), Model_notify_method, void *);
    notify(model, Notify_All, PROPERTY_ID(row));
    notify(model, Notify_All, PROPERTY_ID(METER));
    return 1;
}

/* Serialised event bindings as UIEventMap_ReadStream reads them: u32 count, then
 * per event {str name, the byte '1', str handler, u32 has_action = 0}; str is a u32
 * length and the bytes. list.pid.<item>.chosen names the row by id in decimal. */
/* Words in the stream are not aligned; stored byte by byte. */
static char *put_word(char *out, uint32_t value)
{
    for (unsigned i = 0; i < 4; i++)
        *out++ = (char)(value >> (8 * i));
    return out;
}

static char *put_string(char *out, const char *value)
{
    uint32_t n = 0;
    while (value[n])
        n++;
    out = put_word(out, n);
    while (*value)
        *out++ = *value++;
    return out;
}

static char *put_event(char *out, uint32_t item, uint32_t row)
{
    char name[40], *n = put_text(name, name + sizeof name, "list.pid.");
    n = put_number(n, name + sizeof name, item);
    put_text(n, name + sizeof name, ".chosen");
    out = put_string(out, name);
    *out++ = '1';
    n = put_text(name, name + sizeof name, ACTION);
    put_number(n, name + sizeof name, row);
    out = put_string(out, name);
    return put_word(out, 0);
}

/* Adds the chosen bindings of the rows to the Settings page's event map after
 * Apple loaded its own. The stock rows' bindings live in the screen's CEVT, so
 * the page is recognised by its screen id. */
static uint32_t layout_events(uint32_t layout, const struct core_next *next)
{
    uint32_t result = core_next_call(next, layout, 0, 0, 0);
    uint8_t *screen = FIELD((void *)layout, Layout_screen, uint8_t *);
    if (!screen || FIELD(screen, LayoutScreen_id, uint32_t) != RES_SettingsMenu_Main_Screen ||
        row_count < 2)
        return result;
    char stream_bytes[4 + (MAX_ROWS - 1) * 72];
    char *out = stream_bytes + 4;
    for (uint32_t i = 1; i < row_count; i++)
        out = put_event(out, ITEM_ID(i), i);
    put_word(stream_bytes, row_count - 1);
    uint32_t string[8], stream[31], allocator = 0;
    CppString_InitBytes(string, stream_bytes, (uint32_t)(out - stream_bytes), &allocator);
    StringInputStream_Init(stream, string, 0xc);
    CppString_Destroy(string);
    UIEventMap_ReadStream(stream, (uint8_t *)layout + Layout_events);
    StringInputStream_Destroy(stream, 2, 0);
    return result;
}

static const struct core_hook hooks[] = {
    {CORE_SITE_ResourceBlockList_AppendBorrowedBlock_call, CORE_HOOK_FILTER, 10, append_rows},
    {CORE_SITE_Settings_Property_slot, CORE_HOOK_FILTER, 10, property},
    {CORE_SITE_Settings_Action_slot, CORE_HOOK_FILTER, 0, action},
    {CORE_SITE_UI_LoadLayoutEvents_call, CORE_HOOK_FILTER, 0, layout_events},
};

void core_extensions_init(void *bank)
{
    for (uint32_t i = 0; i < MAX_ROWS; i++) {
        uint32_t source[6] = {1, 12, Resource_SORC, Service_Settings, PROPERTY_ID(i), 1};
        ResourceBank_SetResource(bank, Resource_SORC, SOURCE_ID(i), source, sizeof source);
    }
    for (uint32_t i = 0; i < sizeof hooks / sizeof hooks[0]; i++)
        core_hook_bind(&hooks[i]);
}
