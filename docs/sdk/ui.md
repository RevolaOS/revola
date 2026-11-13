# User interface

An extension describes its UI; the core draws it as stock screens. The descriptor lists settings, screens and entries, each one line with `rv_ui.h`. A macro is named after what it builds: `RV_ROW_PUSH` builds a row of kind `RV_ROW_KIND_PUSH`, `RV_SCREEN_LIST` a screen of kind `RV_SCREEN_KIND_LIST`. Nothing about the firmware's own resources appears in a UI declaration; the symbols header is for hooks.

## Rows

| Row | What it does |
|---|---|
| `RV_ROW_PUSH(label, screen)` | opens one of the extension's screens |
| `RV_ROW_ACTION(label, fn)` | calls `fn` |
| `RV_ROW_SETTING(label, key)` | shows a setting's value; flips a bool or opens the setting's screen |
| `RV_ROW_INFO(label, text)` | a label and a value |

## Settings

`RV_SETTING_BOOL`, `RV_SETTING_INT`, `RV_SETTING_CHOICE`, `RV_SETTING_SLIDER` and `RV_SETTING_MULTI` declare a setting with its labels. An `RV_ROW_SETTING` row shows it, and a choice, multi-choice or slider setting gets a screen the core makes. See [Settings](settings.md).

## Screens

| Screen | What the user sees |
|---|---|
| `RV_SCREEN_LIST(id, title, rows)` | a menu, full width or beside the Settings preview pane as the screen it opens from decides; `.layout` overrides |
| `RV_SCREEN_LIST_PANES(id, title, rows, panes)` | the same with an `rv_pane` per row: the caption and icon the pane shows while that row is highlighted, static or from `.caption_text` and `.bitmap_for`; a row without a bitmap shows a caption alone |
| `RV_SCREEN_CHOICE`, `RV_SCREEN_MULTI_CHOICE`, `RV_SCREEN_SLIDER` | a screen over a setting with a title of its own; beside Settings a choice screen's pane names the highlighted choice under its `.bitmap`, as Settings > EQ does |
| `RV_SCREEN_TEXT(id, title, paragraphs)` | a text page |
| `RV_SCREEN_DYNAMIC_LIST(id, title, count, row, select)` | rows from callbacks, with an inline or stacked detail |
| `RV_SCREEN_CANVAS(id, title, draw, input)` | the extension draws and reads wheel and key input |
| `RV_SCREEN_CONTEXT(rows)` | a long-press menu on a list or dynamic list |

Screen ids are local to the extension. `api->ui_push(self, id, arg)` opens one from any code on the UI task; `ui_pop` closes the extension's own top screen. Labels and paragraphs are read from the module while a screen is open, never copied. An unloaded extension's screens and records are freed.

## Entries

`RV_MENU_ENTRY(menu, after, row)` puts a row into a stock menu after a stock row from `rv_menus.h`. See [Menu placement](menus.md).

The menus are `RV_MENU_MAIN`, `RV_MENU_MUSIC`, `RV_MENU_EXTRAS` and `RV_MENU_SETTINGS`, and the context menus `RV_MENU_NOW_PLAYING_CONTEXT`, `RV_MENU_SONG_CONTEXT`, `RV_MENU_ALBUM_CONTEXT`, `RV_MENU_ARTIST_CONTEXT`, `RV_MENU_PODCAST_EPISODE_CONTEXT`, `RV_MENU_ITUNES_U_EPISODE_CONTEXT`, `RV_MENU_AUDIOBOOK_CONTEXT`, `RV_MENU_MOVIE_CONTEXT`, `RV_MENU_TV_EPISODE_CONTEXT` and `RV_MENU_VIDEO_PLAYLIST_CONTEXT`. A row in a context menu receives the item the menu opened on in its `rv_context`.

| Entry field | |
|---|---|
| `.caption`, `.bitmap` | fill the Settings preview pane; `.caption_text` and `.bitmap_for` give them as they are now |
| `.shown_by` | a bool setting that shows the row; Settings > Main Menu and Music Menu flip it |
| `.available` | decides each time a context menu opens whether the row is offered |

## Callbacks and `rv_context`

Every UI callback receives a `struct rv_context`: row actions and text, canvas draw and input, dynamic list rows, a screen's `.opened` and `.closed`.

| Field | |
|---|---|
| extension | the handle |
| controller | the Apple controller |
| argument | what the screen was pushed with |
| `item`, `row`, `list` | in a context menu's action: the song, episode, audiobook or video, the model row and the list controller (`list` is NULL for Now Playing) |

## Dynamic lists

| | |
|---|---|
| `.dynamic.initial` | the row the list opens on |
| `.dynamic.status(ctx, index)` | `RV_ROW_STATUS_NONE` or `RV_ROW_STATUS_PLAYING`; the core supplies the stock speaker icons |
| `.dynamic.empty` | a centred message when the row count is zero |
| `api->ui_refresh(self, id)` | re-reads the rows and the empty message |
| `api->ui_select(self, id, row)` | highlights a row, clamped to the count, now or when a covered screen becomes active again |

`api->ui_redraw(self)` redraws everything on screen, for a change to colours or artwork the stock views have already drawn.

An empty list has no selection.

## Context menus

`RV_SCREEN_CONTEXT(rows)` of `RV_CONTEXT_ROW(row, .available = predicate)` attaches to a list or dynamic list. Action and push rows receive the highlighted index in `ctx->row`. Unavailable rows are hidden and Cancel closes the menu.

## Canvases

A canvas draws with the `rv_gfx` table it is given: fills, text in the stock fonts placed or aligned in a rectangle, colour roles (which one extension at a time may replace for every canvas with `api->ui_set_colors`, as a theme does; a theme replaces menu bitmaps the same way through the [asset provider](assets.md#asset-provider)) and mixes between them, wrapped paragraphs, list rows, bitmaps whole, in part or tiled, and the Settings > About card the core's Usage page uses. The [canvas kit](canvas.md) builds an editor's palette, plots and value cells over it. Input is wheel and key events, plus `RV_INPUT_HOLD` when the hold switch moves. The input callback returns 1 for an event it consumed; a release goes where its press went, whatever the callback returns for it. A fill with an alpha below 0xff blends over what is there, and `bmap_blend` draws a bitmap at an opacity, for a fade. A canvas may redraw part of itself with `ui_invalidate_rect`; `gfx->clip()` tells a draw which part of the canvas it covers, so a canvas with many pictures draws only the ones that meet it, and a picture it computes itself is a BMap made with the [canvas kit](canvas.md#pictures). Its `.closed` runs after the last draw of its slide-out, so that is where what the canvas draws with is freed.

## Resource ids

Screens and entries are limited by the heap and by the extension's 4,096 resource ids. A load that runs out shows No memory in Settings > Extensions.

| Declaration | Ids |
|---|---|
| list | 19 + 2 per row |
| choice, multi-choice or dynamic list | 21 |
| context menu | 5 + 1 per context row |
| text page | 8 + 1 per paragraph |
| slider | 10 |
| canvas | 8 (held bitmaps, in total across open canvases) |
| menu entry | 2, or 3 under Settings |
