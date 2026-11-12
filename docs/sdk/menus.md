# Menu placement

`RV_MENU_ENTRY(menu, after, row)` places a row in a stock menu. Include `rv_menus.h` and choose an `after` constant from the menu receiving the entry. An `after` of `0` uses the menu's default end anchor; on Main this is Sleep, so the Main Menu editor shows the entry at root level.

Settings > Main Menu places an entry after its Main Menu anchor and uses that stock row's indentation. `RV_MAIN_AUDIOBOOKS` places a row under Music, after Audiobooks and before Search.

## A row on Music and on Main

To offer a Music item on the Main Menu as well, declare both entries, as Apple does for Search. Give each its own `shown_by` bool setting so the two visibility choices stay independent. The Music declaration alone does not create a Main Menu entry.

```c
static const struct rv_menu_entry entries[] = {
    RV_MENU_ENTRY(RV_MENU_MUSIC, RV_MUSIC_AUDIOBOOKS, RV_ROW_PUSH("Queue", "queue"),
                  .shown_by = KEY_IN_MUSIC),
    RV_MENU_ENTRY(RV_MENU_MAIN, RV_MAIN_AUDIOBOOKS, RV_ROW_PUSH("Queue", "queue"),
                  .shown_by = KEY_IN_MAIN),
};

static const struct rv_setting settings[] = {
    RV_SETTING_BOOL(KEY_IN_MUSIC, "Queue in Music", 1),
    RV_SETTING_BOOL(KEY_IN_MAIN, "Queue in Main Menu", 0),
};
```

The example needs a `queue` screen and two distinct, nonzero setting keys, all registered in the descriptor.

For an Extras item offered on Main, declare both entries with the matching anchors, such as `RV_EXTRAS_STOPWATCH` and `RV_MAIN_STOPWATCH`. The Main Menu editor also has stock Video anchors, such as `RV_MAIN_MOVIES`; there is no `RV_MENU_VIDEOS` destination.

## Under Settings

An entry in `RV_MENU_SETTINGS` fills the preview pane with `.caption` and `.bitmap`, a menu bitmap from the extension's [assets](assets.md). `.caption_text` and `.bitmap_for` compute them each time the row is highlighted.
