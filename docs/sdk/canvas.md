# Canvas kit

`rv_canvas.h` is what an editor canvas draws with, over the `rv_gfx` table a draw callback receives: a palette from the stock colour roles, a plot with its grid, ticks and curves, a row of value cells, and the Select and Menu input that moves through them. `rv.mk` links `rv_canvas.c` into every module; a module carries only what it calls. Settings > Channels, Compressor, Crossfeed and Edit EQ are drawn with it.

## Palette

`rv_canvas_begin(&canvas, gfx, width, height)` fills a `struct rv_canvas` with the palette and clears the canvas to the background. `canvas.width` and `canvas.height` are the drawable area under the title bar; size a bottom row from `canvas.height` so it reaches the edge of the screen. The palette is rebuilt on every draw, since a theme may change the roles.

| Colour | What it is |
|---|---|
| `RV_CANVAS_BACKGROUND`, `RV_CANVAS_TEXT` | the stock roles |
| `RV_CANVAS_ACCENT` | the accent as text: a curve, the selected label |
| `RV_CANVAS_FILL`, `RV_CANVAS_ON_FILL` | the accent as a surface, and text on it |
| `RV_CANVAS_MUTED`, `RV_CANVAS_INACTIVE`, `RV_CANVAS_ZERO`, `RV_CANVAS_BORDER`, `RV_CANVAS_GRID`, `RV_CANVAS_TRACK` | greys between the background and the text, nearest the background first |
| `RV_CANVAS_SELECTION` | the background tinted toward the accent |

## Drawing

| Call | Draws |
|---|---|
| `rv_canvas_rect(&canvas, x, y, width, height, color)` | a filled rectangle |
| `rv_canvas_text(&canvas, x, y, width, height, text, large, align, color)` | one line in the box, vertically centred; `large` asks for the large font and falls back to the text font when the text does not fit |
| `rv_canvas_header(&canvas, title, legend, count)` | the title at the left of the header line and, from the right, a swatch and label per `struct rv_canvas_legend` |
| `rv_canvas_cells(&canvas, y, height, titles, values, states, count)` | a row of cells across the canvas, a muted title over a large value each, with a border above and dividers between |
| `rv_canvas_fields(&canvas, y, height, titles, values, states, count)` | the same row with the title at the left of each cell and the value at the right, for a row too low for two lines |

A cell's state is `RV_CELL_IDLE`, `RV_CELL_SELECTED` (framed in the accent: the wheel would edit it) or `RV_CELL_EDITING` (filled with the accent: the wheel edits it), with `RV_CELL_MUTED` added when the value plays no part in what is chosen and reads inactive.

## Plots

A `struct rv_plot` names the columns of a plot, `points` from `left`, and the values at its `top` and `bottom` rows, in whatever unit the curve is in.

| Call | Draws |
|---|---|
| `rv_plot_y(&plot, value)` | the row of a value, clamped to the plot |
| `rv_plot_grid(&canvas, &plot, scale, lines)` | horizontal grid lines evenly spaced from top to bottom, each with its `scale` label left of the plot |
| `rv_plot_ticks(&canvas, &plot, labels, count, y, height)` | vertical grid lines evenly spaced across the plot, each with its label under it at `y`; the last label is right-aligned to the canvas edge |
| `rv_plot_curve(&canvas, &plot, values, thickness, color)` | a curve through one value per column |

## Input

`rv_editor_input(&editor, input)` is the Select and Menu handling of an editor whose wheel works at `editor.depths` levels: two for choosing a value and editing it, three with a level above, such as the EQ's bands. Select goes one level deeper, from the deepest back one; Menu goes back one and at level 0 leaves the key to the system, which pops the screen.

| Result | What the canvas does |
|---|---|
| `RV_EDITOR_IGNORED` | returns 0 from its input callback |
| `RV_EDITOR_CONSUMED` | returns 1; nothing changed (the hold switch) |
| `RV_EDITOR_WHEEL` | moves what `editor.depth` selects by `input->delta`, then redraws |
| `RV_EDITOR_DEEPER`, `RV_EDITOR_BACK` | redraws; `editor.depth` has changed |

```c
static int input(const struct rv_context *ctx, const struct rv_input *input)
{
    switch (rv_editor_input(&editor.input, input)) {
    case RV_EDITOR_IGNORED:
        return 0;
    case RV_EDITOR_CONSUMED:
        return 1;
    case RV_EDITOR_WHEEL:
        wheel(input->delta);
        break;
    default:
        break;
    }
    api->ui_invalidate(self);
    return 1;
}
```

## Value labels

`rv_libc.h` formats the values the cells show: `rv_format_int` ("100 %"), `rv_format_tenths` ("-0.5 dB", "+12.0 dB") and `rv_format_hz` ("700 Hz", "1.5k Hz"). They write digits themselves, so a module that uses them alone does not carry `rv_format`.
