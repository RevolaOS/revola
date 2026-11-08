# revola

Core firmware for RevolaOS: the patches that make Apple's iPod Classic 2.0.5 firmware load extensions, the extension loader and lifecycle, the settings store, the UI runtime and the SDK that extensions build against.

Supported devices: iPod Classic 6G, 6.5G and 7G on Apple firmware 2.0.5. FAT32 storage.

## Layout

Directories are created when code lands in them.

| Path | Contents |
|---|---|
| `core/` | Firmware payload: hook sites and dispatcher, loader, settings, UI runtime, memory policy |
| `sdk/` | Public headers, linker script and Make include for extensions; versioned |
| `tools/` | Recipe builder, `rvx-pack`, build and release scripts |
| `boot/` | Companion loader and Rockbox bootloader patches |
| `docs/` | How the core works |

## Build

TODO: toolchain setup and build steps once the first core boots.

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
