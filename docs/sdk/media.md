# Media items

`rv_media.h` reads the media objects the context menus hand out: `ctx->item` of a context menu's action, the player's current item and the rows of a song model. The helpers are inline over the symbols header, so they cost a module only what it calls. UI task.

A MediaItem is a non-owning view of a library record. One the extension builds itself, with `rv_player_current` or `rv_model_item`, lives in `MediaItem_bytes` of the extension's own storage and is destroyed with `MediaItem_Destroy`.

| Call | |
|---|---|
| `rv_item_valid(item)` | the item views a record that is still in the library |
| `rv_item_same(a, b)` | the two view the same record; the generation tells a record reused after a library reload apart |
| `rv_item_value(item, slot)` | a getter of the item's vtable that returns a word, such as `MediaItem_duration_method` |
| `rv_item_text(item, slot, string)` | a getter that fills a NativeString, such as `MediaItem_title_method`: its UTF-8, NULL when empty; the caller destroys `string` once done with the text |
| `rv_player_lock()`, `rv_player_unlock(player)` | `lock` takes the MediaPlayer's mutex, so the playback task cannot change the current track or the playing list under a read, and returns the player; `unlock` releases it |
| `rv_player_current(player, item)` | the playing item, invalid when nothing plays |
| `rv_model_item(model, row, item)` | a row of a song model |

`rv_copy_utf8` of `rv_libc.h` copies a title into a buffer of the extension's own without cutting inside a UTF-8 sequence.
