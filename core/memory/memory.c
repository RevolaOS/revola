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

#include "memory/memory.h"
#include "osos-2.0.5.h"
#include "settings.h"
#include "hooks/sites.h"

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
#define ORDER_LIMIT 0x30 /* order slots in a MediaList */
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

/* Sorted orders of the library that Apple builds itself during the import: 2 copies
 * the database order, and 22, 26 and 29 serve the playlist import. Nothing reads them
 * while browsing and a later request rebuilds the same order, so the import's end
 * releases them (4 bytes per track each). Orders read from the iTunesDB stay: a
 * rebuild would sort them with Apple's collation instead of the order iTunes wrote.
 * RepriseOS perf/memory f2a9c67. */
static void release_import_orders(uint8_t *owner)
{
    static const uint8_t built_orders[] = {2, 22, 26, 29};
    struct MediaList **slot = owner ? FIELD(owner, MediaBrowser_library, struct MediaList **) : 0;
    struct MediaList *library = slot ? *slot : 0;
    if (!library)
        return;
    for (uint32_t i = 0; i < sizeof built_orders; i++)
        MediaList_InvalidateOrderCache(library, built_orders[i]);
}

/* Runs once the import has built every list, so it also drops what the import alone used. */
static uint32_t block_cache_budget(uint32_t heap_total, uint32_t import_cost, uint32_t owner,
                                   const struct core_next *next)
{
    (void)next;
    release_import_orders((uint8_t *)owner);
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

/* Apple rounds fixed-pool slots to 16 bytes; 8 keeps the 36-byte ArtworkDB thumbnail
 * records in 40-byte slots instead of 48. */
static const struct core_hook pool_hooks[] = {
    {CORE_SITE_FixedPool_SlotRoundAdd_word, CORE_HOOK_REPLACE, 0, (void *)0xe2840007u},
    {CORE_SITE_FixedPool_SlotRoundMask_word, CORE_HOOK_REPLACE, 0, (void *)0xe3c00007u},
};

/* Blocks to add to a cache of total blocks so that request blocks fit beside the
 * kept blocks of clients that do not share theirs, leaving GROW_HEAP_MARGIN of
 * heap_free unused. */
uint32_t core_block_cache_shortfall(uint32_t total, uint32_t kept, uint32_t request,
                                    uint32_t heap_free, uint32_t block_bytes)
{
    if (!block_bytes || kept + request <= total)
        return 0;
    uint32_t wanted = kept + request - total;
    uint32_t spare =
        heap_free > GROW_HEAP_MARGIN ? (heap_free - GROW_HEAP_MARGIN) / block_bytes : 0;
    return wanted < spare ? wanted : spare;
}

/* Client ring: each node {next, previous, client **}. Clients with a weight share
 * their blocks with the rebalancer; the others keep max(held, desired). */
static uint32_t kept_blocks(uint8_t *cache, uint8_t *skip)
{
    uint32_t block_bytes = FIELD(cache, BlockCache_block_bytes, uint32_t);
    uint32_t kept = 0;
    void **ring = FIELD(cache, BlockCache_clients, void **);
    for (void **node = ring[0]; node != ring; node = node[0]) {
        uint8_t *client = *(uint8_t **)node[2];
        if (!client || client == skip || FIELD(client, BlockClient_weight, uint32_t))
            continue;
        uint32_t desired =
            (FIELD(client, BlockClient_desired_bytes, uint32_t) + block_bytes - 1) / block_bytes;
        uint32_t held = FIELD(client, BlockClient_block_count, uint32_t);
        kept += held > desired ? held : desired;
    }
    return kept;
}

static int grown_for_game;

/* Before Apple's acquire: grow the cache so the game's blocks fit beside what the
 * other clients keep, as far as the heap allows. */
static uint32_t game_blocks_acquire(uint32_t manager, const struct core_next *next)
{
    uint8_t *cache = BlockCache_Get();
    uint32_t block_bytes = cache ? FIELD(cache, BlockCache_block_bytes, uint32_t) : 0;
    if (block_bytes) {
        uint8_t **client_slot = FIELD((uint8_t *)manager, EAppBlockManager_client, uint8_t **);
        Mutex_Lock(cache + BlockCache_lock);
        uint32_t total = FIELD(cache, BlockCache_total_blocks, uint32_t);
        uint32_t kept = kept_blocks(cache, client_slot ? *client_slot : 0);
        Mutex_Unlock(cache + BlockCache_lock);
        uint32_t request = FIELD((uint8_t *)manager, EAppBlockManager_request_blocks, uint32_t);
        uint32_t add = core_block_cache_shortfall(
            total, kept, request, FIELD(MainHeap, MainHeap_free_bytes, uint32_t), block_bytes);
        if (add) {
            BlockCache_Grow((total + add) * block_bytes);
            grown_for_game = 1;
        }
    }
    return core_next_call(next, manager, 0, 0, 0);
}

/* After Apple's release: free unassigned blocks above the import size, as
 * BlockCache_Trim does but without waiting for pending requests, which the
 * rebalancer is still serving while the game's blocks come back. */
static uint32_t game_blocks_release(uint32_t manager, const struct core_next *next)
{
    uint32_t result = core_next_call(next, manager, 0, 0, 0);
    uint8_t *cache = BlockCache_Get();
    uint32_t block_bytes = cache ? FIELD(cache, BlockCache_block_bytes, uint32_t) : 0;
    if (!grown_for_game || !block_bytes)
        return result;
    grown_for_game = 0;
    Mutex_Lock(cache + BlockCache_lock);
    uint32_t keep = import_cache_bytes / block_bytes;
    uint32_t total = FIELD(cache, BlockCache_total_blocks, uint32_t);
    uint32_t extra = total > keep ? total - keep : 0;
    uint32_t free_before = FIELD(cache, BlockCache_free_count, uint32_t);
    if (extra > free_before)
        extra = free_before;
    void *list = cache + BlockCache_free_blocks;
    void **node = FIELD(cache, BlockCache_free_blocks + 4, void **); /* list.first */
    for (uint32_t i = 0; i < extra && (void *)node != list; i++)
        BlockList_Erase((void **)&node, list, node);
    FIELD(cache, BlockCache_total_blocks, uint32_t) -=
        free_before - FIELD(cache, BlockCache_free_count, uint32_t);
    BlockCache_PostRebalance(cache, 0);
    Mutex_Unlock(cache + BlockCache_lock);
    return result;
}

static const struct core_hook game_hooks[] = {
    {CORE_SITE_EAppBlocks_Acquire_call, CORE_HOOK_FILTER, 0, game_blocks_acquire},
    {CORE_SITE_EAppBlocks_Release_call, CORE_HOOK_FILTER, 0, game_blocks_release},
};

/* Replaces Apple's position lookup, which keeps an index of (entry, position) pairs
 * sorted by address beside each order it is asked about: 8 bytes per entry, as large
 * as a play-all queue, asked twice per track change. Scanning the order gives the
 * same position. Returns the position (-1 if absent) in r0 and the entry in r1, as
 * Apple's function does. RepriseOS perf/memory c6468c0. */
static uint64_t entry_position(uint32_t entry, uint32_t order, uint32_t reverse_arg,
                               const struct core_next *next)
{
    (void)next;
    uint64_t result = (uint64_t)entry << 32;
    uint8_t *list = entry ? FIELD((uint8_t *)entry, MediaEntry_list, uint8_t *) : 0;
    if (!list || !FIELD((uint8_t *)entry, MediaEntry_parent, void *) || !order)
        return result | UINT32_MAX;
    uint8_t reverse = (uint8_t)reverse_arg;
    MediaList_ResolveOrderMode((struct MediaList *)list, &order, &reverse);
    if (!order || order > ORDER_LIMIT || MediaList_BuildOrderIndex((struct MediaList *)list, order))
        return result | UINT32_MAX;
    uint8_t *entries = FIELD(list, MediaList_order_index + order * 4, uint8_t *);
    uint32_t count = FIELD(entries, MediaOrder_count, uint32_t);
    uint32_t *items = (uint32_t *)(entries + MediaOrder_entries);
    for (uint32_t i = 0; i < count; i++)
        if (items[i] == entry)
            return result | (reverse ? count - i - 1 : i);
    return result | UINT32_MAX;
}

static const struct core_hook position_hook = {CORE_SITE_MediaList_GetEntryOrderIndex_entry,
                                               CORE_HOOK_REPLACE, 0, entry_position};

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
    if (enabled(CORE_KEY_POOL_SLOTS))
        bind_all(pool_hooks, 2);
    if (enabled(CORE_KEY_GAME_BLOCKS))
        bind_all(game_hooks, 2);
    if (enabled(CORE_KEY_POSITION_SCAN))
        core_hook_bind(&position_hook);
}
