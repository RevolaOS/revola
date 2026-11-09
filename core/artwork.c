/* SPDX-License-Identifier: GPL-3.0-only */

/* iTunes writes one ArtworkDB image per track, each with its own chain of thumbnail
 * records, even when every track on an album has the same cover, and Apple keeps all
 * of them in heap. While the ArtworkDB loads, an image whose chain matches one
 * already loaded is dropped: its thumbnails and its image record go back to their
 * pools, and the iTunesDB load then gives its tracks the id of the first image.
 *
 * Sharing is safe because the ArtworkDB never changes after import: readers only
 * walk an image's chain, the per-thumbnail destructor runs only for photo databases,
 * and teardown frees the pools as a whole. A shared thumbnail's image pointer names
 * the first image, which has the same database and name cache.
 *
 * Every track reaches its image by id, or by DBID when it has no id. Dropped ids are
 * rewritten as the mhit parser stores them, before anything reads them. A dropped
 * image's DBID stays resolvable only if a track without an id needs it.
 *
 * Adapted from RepriseOS perf/memory payload/artwork_thumbs.c (804e67e, f59dfdc). */

#include "osos-2.0.5.h"
#include "settings.h"
#include "sites.h"
#include <stddef.h>

#define FIELD(base, offset, type) (*(type *)((uint8_t *)(base) + (offset)))
#define INITIAL_CAPACITY 1024u
#define INITIAL_DUPLICATES 256u

struct canonical {
    uint32_t hash;
    void *image;
};

struct duplicate {
    uint32_t id;
    void *image;
    uint32_t dbid[2]; /* DBIDs are 32-bit aligned; ldrd would need 64-bit alignment */
};

static void *active_db;
static struct canonical *entries;
static uint32_t capacity, used;
static int disabled;
static void *last_kept;

/* Dropped images in load order; after the load, orders over them by id and by DBID. */
static struct duplicate *duplicates;
static uint32_t *by_id, *by_dbid;
static uint32_t duplicate_count, duplicate_capacity;
static void *duplicate_db;
static int remapping, dbid_needed;

static uint64_t dbid_at(const void *base, uint32_t offset)
{
    return (uint64_t)FIELD(base, offset + 4, uint32_t) << 32 | FIELD(base, offset, uint32_t);
}

static uint64_t key(uint32_t index, int dbid)
{
    return dbid ? dbid_at(duplicates[index].dbid, 0) : duplicates[index].id;
}

static void sift(uint32_t *order, uint32_t root, uint32_t count, int dbid)
{
    for (;;) {
        uint32_t child = root * 2 + 1;
        if (child >= count)
            return;
        if (child + 1 < count && key(order[child], dbid) < key(order[child + 1], dbid))
            child++;
        if (key(order[root], dbid) >= key(order[child], dbid))
            return;
        uint32_t swap = order[root];
        order[root] = order[child];
        order[child] = swap;
        root = child;
    }
}

static void sort(uint32_t *order, int dbid)
{
    uint32_t count = duplicate_count;
    for (uint32_t i = count / 2; i-- > 0;)
        sift(order, i, count, dbid);
    for (uint32_t i = count; i-- > 1;) {
        uint32_t swap = order[0];
        order[0] = order[i];
        order[i] = swap;
        sift(order, 0, i, dbid);
    }
}

static struct duplicate *find(const uint32_t *order, int dbid, uint64_t value)
{
    uint32_t low = 0, high = duplicate_count;
    while (low < high) {
        uint32_t middle = (low + high) / 2;
        uint64_t k = key(order[middle], dbid);
        if (k == value)
            return &duplicates[order[middle]];
        if (k < value)
            low = middle + 1;
        else
            high = middle;
    }
    return NULL;
}

static void forget_duplicates(void)
{
    if (duplicates)
        Heap_Free(duplicates);
    duplicates = NULL;
    by_id = by_dbid = NULL;
    duplicate_count = duplicate_capacity = 0;
    duplicate_db = NULL;
    remapping = dbid_needed = 0;
}

/* One allocation holds the records and both orders. */
static int grow_duplicates(void)
{
    if (duplicate_count < duplicate_capacity)
        return 1;
    uint32_t size = duplicate_capacity ? duplicate_capacity * 2 : INITIAL_DUPLICATES;
    if (size > 0xffffffffu / (sizeof(struct duplicate) + 8))
        return 0;
    struct duplicate *table = Heap_AllocateZeroed(size * (sizeof(struct duplicate) + 8));
    if (!table)
        return 0;
    for (uint32_t i = 0; i < duplicate_count; i++)
        table[i] = duplicates[i];
    if (duplicates)
        Heap_Free(duplicates);
    duplicates = table;
    by_id = (uint32_t *)(table + size);
    by_dbid = by_id + size;
    duplicate_capacity = size;
    return 1;
}

/* Removes a just-loaded duplicate image from the database. It is the list tail and
 * the last kept image precedes it; its slot is the next one the pool hands out. */
static int drop(void *image, void *canonical)
{
    void *db = active_db;
    if (!last_kept || FIELD(last_kept, 0, void *) != image || FIELD(image, 0, void *) ||
        FIELD(db, ArtworkDatabase_last_image, void *) != image)
        return 0;
    uint8_t *chunk = FIELD(FIELD(db, ArtworkDatabase_image_pool, uint8_t *), FixedPool_chunks, uint8_t *);
    while (chunk && ((uint8_t *)image < FIELD(chunk, FixedPoolChunk_first, uint8_t *) ||
                     (uint8_t *)image > FIELD(chunk, FixedPoolChunk_last, uint8_t *)))
        chunk = FIELD(chunk, FixedPoolChunk_next, uint8_t *);
    if (!chunk || !grow_duplicates())
        return 0;
    struct duplicate *d = &duplicates[duplicate_count];
    d->id = FIELD(image, ArtworkImage_id, uint32_t);
    d->image = canonical;
    d->dbid[0] = FIELD(image, ArtworkImage_dbid, uint32_t);
    d->dbid[1] = FIELD(image, ArtworkImage_dbid + 4, uint32_t);
    duplicate_count++;
    FIELD(last_kept, 0, void *) = NULL;
    FIELD(db, ArtworkDatabase_last_image, void *) = last_kept;
    FIELD(db, ArtworkDatabase_image_count, uint32_t)--;
    FIELD(image, 0, void *) = FIELD(chunk, FixedPoolChunk_free_list, void *);
    FIELD(chunk, FixedPoolChunk_free_list, void *) = image;
    FIELD(chunk, FixedPoolChunk_free, uint32_t)++;
    return 1;
}

static uint32_t mix(uint32_t hash, uint32_t value)
{
    hash ^= value;
    hash *= 0x01000193;
    return hash ^ (hash >> 15);
}

static uint32_t thumb_word(const void *thumb, uint32_t offset)
{
    return FIELD(thumb, offset, uint32_t);
}

static int same_thumb(const void *a, const void *b)
{
    for (uint32_t offset = ArtworkThumb_format; offset < ArtworkThumb_size + 4; offset += 4)
        if (thumb_word(a, offset) != thumb_word(b, offset))
            return 0;
    return thumb_word(a, ArtworkThumb_file) == thumb_word(b, ArtworkThumb_file) &&
           ArtworkThumb_GetNameCache((void *)a) == ArtworkThumb_GetNameCache((void *)b);
}

static uint32_t chain_hash(const void *thumb)
{
    uint32_t hash = 0x811c9dc5;
    for (; thumb; thumb = FIELD(thumb, ArtworkThumb_next, void *)) {
        for (uint32_t offset = ArtworkThumb_format; offset <= ArtworkThumb_file; offset += 4)
            hash = mix(hash, thumb_word(thumb, offset));
        hash = mix(hash, (uint32_t)ArtworkThumb_GetNameCache((void *)thumb));
    }
    return hash;
}

static int same_chain(const void *a, const void *b)
{
    for (; a && b; a = FIELD(a, ArtworkThumb_next, void *), b = FIELD(b, ArtworkThumb_next, void *))
        if (!same_thumb(a, b))
            return 0;
    return !a && !b;
}

static void *image_thumbs(const void *image)
{
    return FIELD(image, ArtworkImage_thumbs, void *);
}

static struct canonical *slot(struct canonical *table, uint32_t size, uint32_t hash,
                              const void *image)
{
    uint32_t index = hash & (size - 1);
    while (table[index].image &&
           (table[index].hash != hash ||
            (image && !same_chain(image_thumbs(table[index].image), image_thumbs(image)))))
        index = (index + 1) & (size - 1);
    return &table[index];
}

static void clear(void)
{
    if (entries)
        Heap_Free(entries);
    entries = NULL;
    capacity = used = 0;
    active_db = last_kept = NULL;
    disabled = 0;
}

static int grow(void)
{
    if (capacity && used < capacity * 3 / 4)
        return 1;
    if (capacity > 0xffffffffu / 2 / sizeof(*entries))
        return 0;
    uint32_t size = capacity ? capacity * 2 : INITIAL_CAPACITY;
    struct canonical *table = Heap_AllocateZeroed(size * sizeof(*table));
    if (!table)
        return 0;
    /* Canonical chains are distinct, so rehashing needs no chain comparison. */
    for (uint32_t i = 0; i < capacity; i++)
        if (entries[i].image)
            *slot(table, size, entries[i].hash, NULL) = entries[i];
    if (entries)
        Heap_Free(entries);
    entries = table;
    capacity = size;
    return 1;
}

/* Returns whether the image was dropped. */
static int share(void *image)
{
    void *thumbs = image_thumbs(image);
    if (!thumbs)
        return 0;
    if (!grow()) {
        disabled = 1;
        return 0;
    }
    uint32_t hash = chain_hash(thumbs);
    struct canonical *entry = slot(entries, capacity, hash, image);
    if (!entry->image) {
        entry->hash = hash;
        entry->image = image;
        used++;
        return 0;
    }
    /* The destructor unlinks each thumbnail from this image and returns it to the pool. */
    while (thumbs) {
        void *next = FIELD(thumbs, ArtworkThumb_next, void *);
        ArtworkThumb_Destroy(thumbs);
        thumbs = next;
    }
    FIELD(image, ArtworkImage_thumbs, void *) = image_thumbs(entry->image);
    FIELD(image, ArtworkImage_thumb_count, uint16_t) =
        FIELD(entry->image, ArtworkImage_thumb_count, uint16_t);
    return drop(image, entry->image);
}

static uint32_t import(uint32_t db, uint32_t owner, uint32_t path, uint32_t a3,
                       const struct core_next *next, uint32_t a4, uint32_t a5)
{
    clear();
    forget_duplicates();
    active_db = (void *)db;
    uint32_t status = core_next_call6(next, db, owner, path, a3, a4, a5);
    clear();
    if (status || !duplicate_count) {
        forget_duplicates();
    } else {
        for (uint32_t i = 0; i < duplicate_count; i++)
            by_id[i] = by_dbid[i] = i;
        sort(by_id, 0);
        sort(by_dbid, 1);
        duplicate_db = (void *)db;
        remapping = 1;
    }
    return status;
}

static uint32_t image_thumbs_read(uint32_t reader, uint32_t image, uint32_t count,
                                  uint32_t flags, const struct core_next *next)
{
    uint32_t status = core_next_call(next, reader, image, count, flags);
    void *db = FIELD((void *)reader, ArtworkReader_database, void *);
    if (!status && active_db && db == active_db && (disabled || !share((void *)image)))
        last_kept = (void *)image;
    return status;
}

/* ArtworkDB photo albums name images by id while the database loads. */
static uint32_t album_image(uint32_t db, uint32_t id, const struct core_next *next)
{
    uint32_t image = core_next_call(next, db, id, 0, 0);
    for (uint32_t i = 0; !image && (void *)db == active_db && i < duplicate_count; i++)
        if (duplicates[i].id == id)
            image = (uint32_t)duplicates[i].image;
    return image;
}

/* In place of the mhit parser's store of the image id: r0 = id, r4 = track. */
static uint32_t track_image(uint32_t id, uint32_t track, const struct core_next *next)
{
    (void)next;
    if (remapping) {
        struct duplicate *d;
        if (id) {
            if ((d = find(by_id, 0, id)))
                id = FIELD(d->image, ArtworkImage_id, uint32_t);
        } else if (find(by_dbid, 1, dbid_at((void *)track, TrackData_dbid))) {
            dbid_needed = 1;
        }
    }
    FIELD((void *)track, TrackData_artwork_id, uint32_t) = id;
    return 0;
}

/* After the iTunesDB tracks are parsed, ids are final. */
static uint32_t tracks_imported(uint32_t reader, const struct core_next *next)
{
    uint32_t status = core_next_call(next, reader, 0, 0, 0);
    remapping = 0;
    if (!dbid_needed)
        forget_duplicates();
    return status;
}

static uint32_t image_by_dbid(uint32_t db, uint32_t a1, uint32_t dbid_low, uint32_t dbid_high,
                              const struct core_next *next, uint32_t sorted)
{
    uint32_t image = core_next_call6(next, db, a1, dbid_low, dbid_high, sorted, 0);
    if (!image && db && (void *)db == duplicate_db) {
        struct duplicate *d = find(by_dbid, 1, (uint64_t)dbid_high << 32 | dbid_low);
        if (d)
            image = (uint32_t)d->image;
    }
    return image;
}

static const struct core_hook hooks[] = {
    {CORE_SITE_ArtworkDatabase_Import_call, CORE_HOOK_FILTER, 0, import},
    {CORE_SITE_ArtworkDatabase_ReadImageThumbs_call, CORE_HOOK_FILTER, 0, image_thumbs_read},
    {CORE_SITE_ArtworkDatabase_FindById_PhotoAlbum_call, CORE_HOOK_FILTER, 0, album_image},
    {CORE_SITE_TrackData_ArtworkId_word, CORE_HOOK_REPLACE, 0, track_image},
    {CORE_SITE_ITunesDB_ImportTrackDataset_call, CORE_HOOK_FILTER, 0, tracks_imported},
    {CORE_SITE_ArtworkDatabase_FindByDbid_0804581c_call, CORE_HOOK_FILTER, 0, image_by_dbid},
    {CORE_SITE_ArtworkDatabase_FindByDbid_08046494_call, CORE_HOOK_FILTER, 0, image_by_dbid},
};

void core_artwork_bind(void)
{
    if (core_settings_get_int(CORE_SETTINGS_OWNER_CORE, CORE_KEY_ARTWORK_SHARE, 1) == 0)
        return;
    for (uint32_t i = 0; i < sizeof hooks / sizeof hooks[0]; i++)
        core_hook_bind(&hooks[i]);
}
