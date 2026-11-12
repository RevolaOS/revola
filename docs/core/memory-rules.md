# Memory rules

Apple's global memory policy is adjusted at boot. Each rule sits behind a core setting, default on, read from the store in `core_preferences_init`. `core/memory/memory.c` carries the reasoning and the numbers. Ported from RepriseOS perf/memory.

| Setting | Rule |
|---|---|
| `CORE_KEY_CACHE_BUDGET` | Size the block cache after the library import as max(heap total - import cost - reserve, floor): 14/14 MiB on a hard disk as Apple does, 24/6 MiB on flash, detected from the ATA IDENTIFY data. Also releases the four sorted orders the import builds and never reads, 4 bytes per track each |
| `CORE_KEY_READAHEAD_FLOOR` | Below a 14 MiB cache the media player's read-ahead minimum becomes max(cache - 8 MiB, 2 MiB), so a game's 8 MiB still fits beside it |
| `CORE_KEY_POOL_SLOTS` | Fixed-pool slots round to 8 bytes instead of 16; ArtworkDB thumbnail records take 40 bytes instead of 48 |
| `CORE_KEY_GAME_BLOCKS` | When the eApp block manager asks for a game's blocks, the cache first grows by what the other clients keep, leaving 1 MiB of heap; after the release the unassigned blocks above the import size go back to the heap |
| `CORE_KEY_POSITION_SCAN` | List positions come from scanning the order instead of Apple's address index beside it, 8 bytes per entry of a play-all queue |
| `CORE_KEY_ARTWORK_SHARE` | While the ArtworkDB loads, an image whose thumbnail chain matches one already loaded shares that chain and its record goes back to the pool; tracks that named the dropped image get the first image's id (`core/memory/artwork.c`) |

Two changes are not settings:

- The track count of media-list nodes is widened to 32 bits by sixty constant words in `core/memory/trackcount.c`, written by the recipe, so libraries over 65,535 tracks import. The core's own layout of that node (tracks at 0x2c as a word, groups at 0x1e) lives next to those words; the symbols file keeps Apple's.
- The 32 KiB UI stack is not carried until stack-watermark evidence exists on the canvas and loader paths.
