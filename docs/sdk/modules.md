# Modules

A module is one ELF, linked at 0 with `rvx.lds` and relocated by the loader into the Apple heap. Every module exports `rv_module_entry` (type `rv_module_entry_fn`). The main module returns a `struct rv_descriptor`; another module returns one too when it declares hooks or screens, else NULL.

## Descriptor

| Field | |
|---|---|
| `size` | `sizeof(struct rv_descriptor)` as built, for forward compatibility |
| `hooks`, `hook_count` | `RV_HOOKS(array)`; bound in order, a conflict fails the load with the site named in Settings > Extensions |
| `settings`, `setting_count` | `RV_SETTINGS(array)` |
| `screens`, `screen_count` | `RV_SCREENS(array)` |
| `entries`, `entry_count` | `RV_ENTRIES(array)` |
| `unload` | called before the hooks are unbound; may be NULL |
| `setting_changed` | see [Settings](settings.md); may be NULL |

## Triggers

| `trigger` in the manifest | Loaded | Unloaded |
|---|---|---|
| `always` | while the extension is enabled; only the first module, `main` | when the extension is disabled |
| `screen` | when a push looks up one of the ids in its `screens` list | ten seconds after the last of its screens was popped, unless its `preload` class keeps it |
| `manual` | `api->module_load` | `api->module_unload`, or when Optimize stops keeping it and the extension does not hold it |

A screen module keeps the code of a rarely used page off the heap until it opens, unless Optimize keeps it (below). The core binds the hooks and registers the screens of a non-main module when it loads and drops them when it unloads.

`preload` on a module after `main` (`always`, `optional`, the default, or `never`) says whether the core keeps it loaded: `always`, and `optional` when Settings > Extensions > Optimize prefers performance. A kept module is loaded with `main` and stays loaded while the extension is loaded, so a screen push or `module_load` finds it there and reads nothing from the disk. The trigger says who may ask for the module, not when it goes. A kept module that does not fit in memory stays on disk and loads on request.

For a kept module, `module_load` returns RV_OK at once and runs nothing again; `module_unload` releases the module, and it unloads once no screen of it is open and Optimize does not keep it. A kept module the extension never loaded unloads when Optimize stops keeping it. A module's entry runs once per load, and its statics keep their values across `module_load` calls while the core keeps it; a module that needs a known state per use resets it itself. `module_service` returns the table whenever the module is loaded, so a kept module's table is there before `module_load`; call `module_load` anyway, it is what loads the module when the core does not keep it.

## Services

Modules share no symbols. A module that other modules call points its descriptor's `service` at a table of its functions; `api->module_service(self, module)` returns that pointer, module 0 being `main`, or NULL while the module is not loaded. `main` reaches a manual module between its `module_load` and `module_unload`; the manual module reaches `main`'s table from its own entry, valid for as long as the extension is loaded. One module owns each load and unload pair, since `module_unload` keeps no count.

When the extension unloads, `main`'s `unload` callback runs first, while the other modules are still loaded and may still be loaded with `module_load`; then the other modules unload, highest manifest index first, then `main`.

```c
struct worker_service { int (*run)(const struct job *job); };
/* in the manual module */
static const struct worker_service service = {run};
static const struct rv_descriptor module = {.size = sizeof module, .service = &service};
/* in main, after api->module_load(self, 1) */
const struct worker_service *worker = api->module_service(self, 1);
```

## Build rules

`rv.mk` builds Thumb with `-mlong-calls -fno-pic -fno-jump-tables` and no C library beyond `rv_libc.h`. Only `R_ARM_ABS32` relocations may remain, which `rvx-pack` checks. Memory from `api->alloc` is counted against the extension and shown in Settings > Extensions.

| Variable | |
|---|---|
| `ID` | the manifest id |
| `MODULES` | module names in the manifest's order |
| `<module>_SOURCES` | C and assembly sources, built as Thumb |
| `<module>_ARM_SOURCES` | sources built as ARM with `-marm -fno-optimize-sibling-calls` |

## ARM

Thumb is the default because it is denser, and RAM comes first. Use ARM in two cases only, and say why in a comment:

| Case | Why |
|---|---|
| DSP and hot loops | the ARMv5TE multiply-accumulate and saturating instructions (SMULxy, SMLAxy, QADD) exist only in ARM state |
| Calls into Apple routines that return with `mov pc, lr` | such a return lands in ARM state, so a Thumb caller crashes; the call must come from an ARM function and must not be a tail call |

Mark single functions `RV_ARM`, or list whole files in `<module>_ARM_SOURCES`.

## Hooks

| Declaration | |
|---|---|
| `RV_HOOK_FILTER(site, fn)` | a handler that may call `api->hook_next` to continue |
| `RV_HOOK_FILTER_AT(site, fn, priority)` | the same at an explicit priority; an audio effect on the output PCM site passes its `RV_AUDIO_STAGE_*` value |
| `RV_HOOK_REPLACE(site, fn)` | an exclusive handler; a second claim on the site is a conflict |
| `RV_HOOK_WORD(site, word)` | an exclusive instruction replacement at a word site |
| `RV_HOOK_POINTER(site, object)` | the address of the module's own `object` at a word site that holds one |

`site` is an `RV_SITE_*` id from `rv_sites.h`. A handler receives the Apple arguments followed by `next`, which it passes to `api->hook_next` with the arguments it wants the rest of the chain to see. The calling convention is in [Hook sites](https://github.com/RevolaOS/revola/blob/main/docs/core/hook-sites.md).

`api->string_resource(self, text)` gives a string resource id that resolves to `text`, for a hook that puts the extension's own name into a table Apple reads by resource id, such as a row added with `RV_HOOK_POINTER`.
