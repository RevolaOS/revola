# RevolaOS SDK

Headers, linker script, tools and Make include for building extensions against one revola release. Everything an extension needs is in this directory; nothing reaches into the core.

| File | Contents |
|---|---|
| `rv_api.h` | the `rv_api` table the core passes to a module's entry, the descriptor it returns, hook, setting and container declarations; `RV_API_MAJOR`/`RV_API_MINOR` |
| `rv_sites.h` | `RV_SITE_*` ids of every hook site in this core, generated from the core's site table |
| `rv_firmware.h`, `rv_firmware.mk` | `RV_FIRMWARE_ID` of the Apple firmware the symbols describe; the loader refuses a module built for another |
| `osos-2.0.5.h`, `osos-2.0.5.ld` | Apple functions, data, constants and struct offsets by name; modules call Apple directly |
| `rvx.lds` | module layout: linked at 0, text, rodata, data and bss in one segment |
| `rvx-pack` | ELF to `.rvx` (Python 3.11+, needs the `arm-elf-eabi-` binutils) |
| `manifest` | `manifest.toml` to `manifest.bin` |
| `rv.mk` | Make include: set `ID`, `MODULES` and `<module>_SOURCES`, include it, run `make` |

A module exports `rvx_entry` (see `rv_entry_fn`). The main module returns a `struct rv_extension`; its hooks are bound in order and a conflict fails the load with the site named in Settings > Extensions. Install the output directory as `/Revola/Extensions/<id>/`.

Build rules: Thumb, `-mlong-calls`, `-fno-pic -fno-jump-tables`, no C library; only `R_ARM_ABS32` relocations may remain, which rvx-pack checks. Memory from `api->alloc` is counted against the extension and shown in Settings > Extensions.
