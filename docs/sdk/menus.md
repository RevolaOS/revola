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

For an Extras item offered on Main, declare both entries with the matching anchors, such as `RV_EXTRAS_STOPWATCH` and `RV_MAIN_STOPWATCH`. `RV_MENU_VIDEOS` places a row in Main > Videos, after an `RV_VIDEOS_*` anchor or by default after Settings, its last row; the Main Menu editor's stock Video anchors, such as `RV_MAIN_MOVIES`, place a Main Menu row.

## The pane

Main, Music, Videos, Extras and Settings show a pane beside the list. While an entry's row is highlighted, the pane shows the entry's `.bitmap`, a picture from the extension's [assets](assets.md), with its text: `.caption`, and in the shapes that have them `.lines`, two more lines, and `.badge`, a 20 x 16 picture before the caption. `.caption_text`, `.line_text(ctx, line)`, `.bitmap_for` and `.badge_for` compute them each time the row is highlighted and each time the extension calls `api->pane_refresh(self)` while its row is the one a pane last showed (from a timer, for a pane that counts or ticks). The pane's shape follows the picture's size, as it does for the stock rows (Games shows the same pane under Extras and on Main):

| Picture | Shows | Where |
|---|---|---|
| 78 x 121 mask, `pane = "icon"` | the label, the caption below and the icon on the system's box (or without it, with `.plain_icon`) | Settings only; every Settings row |
| 119 x 119 | the picture with the caption beneath | Main, Music, Videos, Extras; the pane of a row without a picture on Main, Music and Videos |
| 142 x 87 | the picture with the caption beneath | the same; the pane of a row without a picture under Extras |
| 130 x 128 | the label as a title line at the top, the two lines over the picture, the caption beneath | Main, Music, Videos, Extras |
| 146 x 110 | the picture with the caption as the short count line beneath it, about 90 pixels wide, and the badge before it | Main, Music, Videos, Extras |
| 132 x 145 | the picture in the clock's box over the World Clock pane's world map, the caption on the date line | Music, Videos, Extras |

In the 119 x 119, 142 x 87 and 146 x 110 shapes a caption falls back to the label; the others show the label on their title line. A picture of a size the menu has no pane for, such as an icon outside Settings, shows the caption alone in the pane of a row without a picture. The core builds a menu's pane for a picture size the first time a row with such a picture is highlighted and adds it to the menu on screen, so an extension enabled in Settings > Extensions gets its panes at once; a picture served by an [asset provider](assets.md#asset-provider) at another size shows the caption alone. The 132 x 145 shape is not built on Main, whose title the World Clock pane's layout cannot carry. An Extras item mirrored onto Main keeps its one picture.
