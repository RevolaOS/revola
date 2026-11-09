/* SPDX-License-Identifier: GPL-3.0-only */

/* Memory rules carried from RepriseOS perf/memory (payload/block_cache.c, 7d7f11e).
 *
 * After the library import Apple grows the shared block cache (audio read-ahead,
 * eApp memory, block-backed heaps) to max(heap total - import cost - 14 MiB, 14 MiB)
 * and never shrinks it. Once the library costs more than about 24 MiB the 14 MiB
 * floor takes the heap the reserve was meant to keep free and the UI fails its
 * allocations. Hard disks keep the native sizes, since a smaller read-ahead means
 * more spin-ups. Flash refills at no spin-up cost, so it keeps a larger reserve of
 * free heap and a 6 MiB floor: 2 MiB of read-ahead, the UI's two blocks, Now
 * Playing art and four blocks for Cover Flow. */

#include "memory.h"
#include "osos-2.0.5.h"
#include "settings.h"
#include "sites.h"

#define MIB (1024u * 1024u)
#define NATIVE_RESERVE (14u * MIB)
#define NATIVE_FLOOR (14u * MIB)
#define FLASH_RESERVE (24u * MIB)
#define FLASH_FLOOR (6u * MIB)
/* The native media-player read-ahead: max(cache - PLAYER_RESERVE, PLAYER_MIN). */
#define PLAYER_RESERVE (10u * MIB)
#define PLAYER_MIN (6u * MIB)
/* The eApp block manager's request for a game. */
#define GAME_BLOCKS (8u * MIB)
/* Read-ahead that still plays ALAC at 1 Mbps with seeking and skipping. */
#define PLAYER_FLOOR (2u * MIB)
/* Heap that growing the cache for a game leaves free for the game and the UI. */
#define GROW_HEAP_MARGIN (1u * MIB)
#define NOP 0xe1a00000u
#define FIELD(base, offset, type) (*(type *)((uint8_t *)(base) + (offset)))

enum {
    ID_CONFIG = 0,
    ID_MODEL = 27,
    ID_MODEL_WORDS = 20,
    ID_COMMAND_SET_2 = 83,
    ID_CF_ADVANCED_TIMING = 163,
    ID_FORM_FACTOR = 168,
    ID_DATA_SET_MANAGEMENT = 169,
    ID_ROTATION_RATE = 217,
};

/* IDENTIFY stores two ASCII characters per word, high byte first. */
static void model_text(const uint16_t *identify, char *model)
{
    for (int i = 0; i < ID_MODEL_WORDS; i++) {
        model[2 * i] = (char)(identify[ID_MODEL + i] >> 8);
        model[2 * i + 1] = (char)identify[ID_MODEL + i];
    }
    model[ID_MODEL_WORDS * 2] = 0;
}

static int contains(const char *text, const char *needle)
{
    for (; *text; text++) {
        int i = 0;
        while (needle[i] && text[i] == needle[i])
            i++;
        if (!needle[i])
            return 1;
    }
    return 0;
}

/* Toshiba (MK1634GAL) and Samsung (HS12YHA) 1.8" drives, the only hard disks Apple
 * fitted to these iPods, optionally after the vendor name. */
static int hard_disk_model(const char *model)
{
    if (contains(model, "TOSHIBA") || contains(model, "SAMSUNG"))
        return 1;
    for (const char *p = model; *p; p++) {
        int word_start = p == model || p[-1] == ' ';
        if (word_start && ((p[0] == 'M' && p[1] == 'K') || (p[0] == 'H' && p[1] == 'S')) &&
            p[2] >= '0' && p[2] <= '9')
            return 1;
    }
    return 0;
}

/* Flash adapters report themselves inconsistently: a reported rotation rate decides
 * first, then the flash markers Rockbox checks, then the known hard-disk models;
 * any other disk in a Classic is an adapter. */
int core_storage_is_flash(const uint16_t *identify)
{
    char model[ID_MODEL_WORDS * 2 + 1];
    model_text(identify, model);
    if (!identify[ID_CONFIG] && !model[0])
        return 0; /* no IDENTIFY data: native behaviour */

    /* Some CF adapters byte-swap the solid-state value. */
    uint16_t rate = identify[ID_ROTATION_RATE];
    if (rate == 0x0001 || rate == 0x0100)
        return 1;
    if (rate >= 0x0401 && rate <= 0xfffe)
        return 0;

    if (identify[ID_CONFIG] == 0x848a || identify[ID_CONFIG] == 0x844a)
        return 1;
    /* Bits 15:14 = 01 mark the word valid; bit 2 is the CompactFlash feature set. */
    if ((identify[ID_COMMAND_SET_2] & 0xc004) == 0x4004)
        return 1;
    if ((identify[ID_FORM_FACTOR] & 0x0f) >= 0x06)
        return 1;
    if (identify[ID_DATA_SET_MANAGEMENT] & 1) /* TRIM */
        return 1;
    if (identify[ID_CF_ADVANCED_TIMING])
        return 1;
    if (contains(model, "iFlash"))
        return 1;
    return !hard_disk_model(model);
}

uint32_t core_block_cache_size(uint32_t heap_total, uint32_t import_cost, int flash)
{
    uint32_t reserve = flash ? FLASH_RESERVE : NATIVE_RESERVE;
    uint32_t floor = flash ? FLASH_FLOOR : NATIVE_FLOOR;
    uint32_t size = heap_total > import_cost && heap_total - import_cost > reserve
                        ? heap_total - import_cost - reserve
                        : 0;
    return size < floor ? floor : size;
}

/* The size chosen after the import. */
static uint32_t import_cache_bytes;

static uint32_t block_cache_budget(uint32_t heap_total, uint32_t import_cost,
                                   const struct core_next *next)
{
    (void)next;
    import_cache_bytes =
        core_block_cache_size(heap_total, import_cost, core_storage_is_flash(ATA_Identify));
    return import_cache_bytes;
}

static const struct core_hook cache_budget_hooks[] = {
    {CORE_SITE_BlockCache_ImportCost_word, CORE_HOOK_REPLACE, 0, block_cache_budget},
    {CORE_SITE_BlockCache_ReserveNative_word, CORE_HOOK_REPLACE, 0, (void *)NOP},
    {CORE_SITE_BlockCache_FloorTest_word, CORE_HOOK_REPLACE, 0, (void *)NOP},
    {CORE_SITE_BlockCache_FloorSet_word, CORE_HOOK_REPLACE, 0, (void *)NOP},
};

/* The media player's read-ahead for a cache of cache_bytes. The native minimum
 * shrinks only where a game would not fit beside it, which only the flash floor
 * reaches: on a hard disk the cache is at least 14 MiB and the result is Apple's. */
uint32_t core_media_read_ahead(uint32_t cache_bytes)
{
    uint32_t minimum = PLAYER_MIN;
    if (cache_bytes < PLAYER_MIN + GAME_BLOCKS)
        minimum = cache_bytes > PLAYER_FLOOR + GAME_BLOCKS ? cache_bytes - GAME_BLOCKS
                                                           : PLAYER_FLOOR;
    return cache_bytes > PLAYER_RESERVE + minimum ? cache_bytes - PLAYER_RESERVE : minimum;
}

/* Called with r0 = cache bytes - 10 MiB, as Apple computes it; may be negative. */
static uint32_t read_ahead(uint32_t over_reserve, const struct core_next *next)
{
    (void)next;
    return core_media_read_ahead(over_reserve + PLAYER_RESERVE);
}

static const struct core_hook read_ahead_hooks[] = {
    {CORE_SITE_MediaPlayer_ReadAheadMinTest_word, CORE_HOOK_REPLACE, 0, read_ahead},
    {CORE_SITE_MediaPlayer_ReadAheadMinSet_word, CORE_HOOK_REPLACE, 0, (void *)NOP},
};

static void bind_all(const struct core_hook *hooks, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
        core_hook_bind(&hooks[i]);
}

static int enabled(uint16_t key)
{
    return core_settings_get_int(CORE_SETTINGS_OWNER_CORE, key, 1) != 0;
}

void core_memory_bind(void)
{
    if (enabled(CORE_KEY_CACHE_BUDGET))
        bind_all(cache_budget_hooks, 4);
    if (enabled(CORE_KEY_READAHEAD_FLOOR))
        bind_all(read_ahead_hooks, 2);
}
