# revola

Core firmware for RevolaOS: the patches that make Apple's iPod Classic 2.0.5 firmware load extensions, the extension loader and lifecycle, the settings store, the UI runtime and the SDK that extensions build against.

Supported devices: iPod Classic 6G, 6.5G and 7G on Apple firmware 2.0.5. FAT32 storage.

## Layout

| Path | Contents |
|---|---|
| `core/` | Firmware payload linked at `CORE_BASE`: install-time patches (`boot.c`), hook site table and dispatcher (`sites.def`, `sites.c`, `stubs.S`), panic screen |
| `firmware/` | Pinned Apple fingerprints (`apple.json`) and the symbols linker script and header from the reversing repository |
| `boot/` | Companion loader (`companion/`), Rockbox bootloader patches and the pinned Rockbox commit |
| `tools/` | Recipe builder, build and release scripts, tests |

Directories for the SDK and extension loader are created when their code lands.

## Build

Requires Python 3.11+, Rockbox's `arm-elf-eabi-` GCC 9.5.0 (`firmware/apple.json` pins the version), and decrypted 2.0.5 inputs produced by the installer tooling. TODO: the recipe assembler is still the RepriseOS `reprise` CLI on PATH until the installer repository ships revola's.

```sh
python3 tools/build.py osos --inputs /path/to/firmware-2.0.5 --cross-prefix /path/to/arm-elf-eabi-
python3 tools/build.py loader-recipe
cd tools && python3 -m unittest discover -s tests
```

`osos` writes `build/osos.json`, `build/osos.data` and the assembled `build/osos-cfw.bin`; `osos-recipe` stops before assembly and needs no Apple inputs, so it skips the checks of the site table against the image. `--selftest` compiles the dispatcher self-test into the core (results in `core_selftest`, read with `ipodvm peek`). Boot the image in the emulator with `ipodvm launch --firmware build/osos-cfw.bin --disk <test disk>` from this directory.

The build prints the core's resident bytes; the link fails above `CORE_BYTES` (96 KiB) in `core/layout.h`.

Apple symbols: `firmware/osos-2.0.5.ld` is passed to the linker with `--just-symbols`, so C code calls Apple functions by the names declared in `firmware/osos-2.0.5.h`. Both files are copied from the reversing repository; never edit them here.

## Hook sites

Apple code runs from DRAM, so the core patches it at boot. `core/sites.def` lists every site the core can patch: address, kind (`CALL`, `VTABLE`, `ENTRY`, `ENTRY_NORETURN`, `WORD`), what an unbound site does, the Apple words expected there, whether several handlers may chain, and the argument count. Each site has one pointer slot and one stub in the core. Call and vtable sites are patched when the first handler binds and restored when the last unbinds; entry sites are written once by `core_boot`, before Apple starts its tasks, and fall through to a replay stub while unbound. Every write runs with IRQ and FIQ masked and is followed by `core_cache_sync`.

A handler receives the Apple arguments followed by `next` (in `r[argc]`, or as the first stack argument for four-argument sites). A `filter` continues with `core_next_call`; a `replace` handler is exclusive and a second claim is reported as a conflict. With one handler bound the slot holds the handler itself; with more it holds a chain entry that calls them in priority order. Handler lists are published with a single store and the retired list is freed at the next `core_grace_tick`. TODO: the grace tick is not yet driven by a UI idle site.

The build tool reads the table from the linked core, rejects entry sites whose displaced words use `pc`, are conditional, branch, or are the target of a branch at `entry+4`, checks every expected word against the pinned image when Apple inputs are present, and records the same expectations in the recipe for the installer.

The install-time recipe keeps only what must hold before the core runs: the heap-base literals, the panic entries and the one call that bootstraps `core_boot`.

## What drives development

1. **RAM first.** 64 MiB in total, about 20 MiB of heap shared with Apple's music database. Every pull request reports resident and peak bytes from the emulator heap profile. An increase needs a stated reason. A disabled extension costs zero bytes. Reuse what exists before adding anything.
2. **Performance second.** No work in audio callbacks beyond the DSP itself. No redraw of unchanged pixels. No disk reads for things that can be computed. No per-draw checks when a one-time hook can decide. Hot paths bind directly; the dispatcher runs only when two handlers share a site.
3. **Optimise before merge.** Measure, remove what measurement does not justify, and stop when the next step costs more clarity than it saves. "Works" is not the bar.
4. **Complexity matches need.** No abstraction for one caller, no layer that only forwards. Defer anything with one user, delete anything with none. A person must be able to reason about the code.
5. **Clean structure.** One concern per directory. Extensions never reach into core internals. Apple addresses come only from the generated symbols header.
6. **Measure, do not guess.** Emulator heap profile for every RAM claim; hardware for audio, cache and storage behaviour.

## Core or extension

Something is core only if the loader, SDK or settings store needs it, if it must act before the loader can run, or if the boot cannot recover without it. Everything else is an extension, however small. One extension per concern. If one feature cannot work without another, it is a module of that extension, not a dependency.


## License

GPL-3.0-only. See COPYING and NOTICE.
