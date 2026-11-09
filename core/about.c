/* SPDX-License-Identifier: GPL-3.0-only */

/* Four rows after Legal in Settings: RevolaOS version, core resident bytes, heap
 * used and heap free. While Apple builds the Settings ITEM list the core copies the
 * Legal row as it goes past, gives each copy its own id and a title source bound to
 * a Settings model property, and appends it. The property handler formats the text
 * on each request, so the numbers are current every time Settings opens. Apple keeps
 * pointers to the rows, so they live in core memory for good. */

#include "osos-2.0.5.h"
#include "sites.h"

#define ROWS 4
#define ROW_BYTES 0x98
#define ROW_ID 0x30
#define ROW_TITLE 0x68
#define ROW_MODEL_TITLE 0x94 /* byte: the title comes from a model property */
#define TEXT_BYTES 40

/* Ids from the core's block; property ids stay outside Apple's 0x8900-0x892f. */
#define ITEM_ID(i) (0x0cf00001u + (i))
#define SOURCE_ID(i) (0x0cf00011u + (i))
#define PROPERTY_ID(i) (0x0cf00021u + (i))

extern char __core_start[], __core_end[];

static uint32_t rows[ROWS][ROW_BYTES / 4];
static char text[ROWS][TEXT_BYTES];

static char *put_text(char *out, const char *end, const char *value)
{
    while (*value && out < end - 1)
        *out++ = *value++;
    *out = 0;
    return out;
}

/* Decimal with thousands separators. */
static char *put_number(char *out, const char *end, uint32_t value)
{
    char digits[14];
    int n = 0;
    do {
        if (n && n % 4 == 3)
            digits[n++] = ',';
        digits[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (n && out < end - 1)
        *out++ = digits[--n];
    *out = 0;
    return out;
}

/* MiB with one decimal. */
static char *put_mib(char *out, const char *end, uint32_t bytes)
{
    uint32_t tenths = (bytes + 52429u) / 104858u; /* bytes / (MiB / 10), rounded */
    out = put_number(out, end, tenths / 10);
    char fraction[] = {'.', (char)('0' + tenths % 10), ' ', 'M', 'i', 'B', 0};
    return put_text(out, end, fraction);
}

static const char *row_text(uint32_t index)
{
    char *out = text[index], *end = out + TEXT_BYTES;
    switch (index) {
    case 0:
        put_text(out, end, "RevolaOS " CORE_VERSION);
        break;
    case 1:
        out = put_text(out, end, "Core ");
        out = put_number(out, end, (uint32_t)(__core_end - __core_start));
        put_text(out, end, " B");
        break;
    case 2:
        out = put_text(out, end, "Heap used ");
        put_mib(out, end, *(uint32_t *)((uint8_t *)MainHeap + MainHeap_used_bytes));
        break;
    default:
        out = put_text(out, end, "Heap free ");
        put_mib(out, end, *(uint32_t *)((uint8_t *)MainHeap + MainHeap_free_bytes));
        break;
    }
    return text[index];
}

static uint32_t append_rows(uint32_t list, uint32_t data, uint32_t bytes, uint32_t metadata,
                            const struct core_next *next)
{
    uint32_t result = core_next_call(next, list, data, bytes, metadata);
    const uint32_t *row = (const uint32_t *)data;
    if (bytes != ROW_BYTES || row[ROW_ID / 4] != RES_SettingsMenu_Legal_Item)
        return result;
    for (uint32_t i = 0; i < ROWS; i++) {
        for (uint32_t w = 0; w < ROW_BYTES / 4; w++)
            rows[i][w] = row[w];
        rows[i][ROW_ID / 4] = ITEM_ID(i);
        rows[i][ROW_TITLE / 4] = SOURCE_ID(i);
        ((uint8_t *)rows[i])[ROW_MODEL_TITLE] = 1;
        core_next_call(next, list, (uint32_t)rows[i], bytes, metadata);
    }
    return result;
}

static uint32_t property(uint32_t model, uint32_t type, uint32_t id, uint32_t out,
                         const struct core_next *next)
{
    if (type == Property_String && id - PROPERTY_ID(0) < ROWS) {
        *(const char **)out = row_text(id - PROPERTY_ID(0));
        return 1;
    }
    return core_next_call(next, model, type, id, out);
}

static const struct core_hook hooks[] = {
    {CORE_SITE_ResourceBlockList_AppendBorrowedBlock_call, CORE_HOOK_FILTER, 0, append_rows},
    {CORE_SITE_Settings_Property_slot, CORE_HOOK_FILTER, 0, property},
};

/* Title sources: one SORC block {service, property, 1} per row, copied into Apple's
 * override map. Runs once Apple's override map exists. */
void core_about_init(void *bank)
{
    for (uint32_t i = 0; i < ROWS; i++) {
        uint32_t source[6] = {1, 12, Resource_SORC, Service_Settings, PROPERTY_ID(i), 1};
        ResourceBank_SetResource(bank, Resource_SORC, SOURCE_ID(i), source, sizeof source);
    }
    core_hook_bind(&hooks[0]);
    core_hook_bind(&hooks[1]);
}
