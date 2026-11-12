# Settings

Settings live in the core's store, under the extension's id. Declare them in the descriptor; the core shows them in rows and screens and keeps them across boots.

| Call | |
|---|---|
| `api->setting_int(self, key)` | the value, with the declared fallback as the Settings rows apply it |
| `api->setting_set_int(self, key, value)` | clamps to the declared range, stores, redraws the rows that show the setting and calls `setting_changed(key)` |
| `api->setting_read(self, key, out, capacity)` | copies at most `capacity` bytes and returns how many, 0 when absent |
| `api->setting_write(self, key, type, data, size)` | stores raw bytes, for strings and blobs; at most 255 |

## `setting_changed`

The descriptor's `setting_changed(key)` runs after the user changes a setting in a row, a choice or a slider, and after the extension's own `setting_set_int`. It runs with `RV_SETTING_KEY_ALL` once the main module has loaded and after Reset All Settings emptied the store. An extension that keeps a copy of a setting, such as DSP coefficients or a cached flag, builds and refreshes it in that one place.

The main module's entry runs before its declarations are known, so a setting read there gets no fallback.

## Visibility

An entry's `.shown_by` names a declared bool setting. The row shows while it is on, and under Main and Music the Main Menu and Music Menu settings flip it.
