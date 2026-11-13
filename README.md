# revola

Core firmware for RevolaOS: the patches that make Apple's iPod Classic 2.0.5 firmware load extensions, the extension loader and lifecycle, the settings store, the UI runtime and the SDK that extensions build against. Supported devices: iPod Classic 6G, 6.5G and 7G on Apple firmware 2.0.5, FAT32 storage.

## Layout

| Path | Contents |
|---|---|
| `core/` | The firmware payload: boot, hook sites, loader, settings store, UI runtime, memory rules, panic screen |
| `sdk/` | What extensions and apps build against |
| `firmware/` | Pinned Apple fingerprints and the symbols header and linker script copied from the reversing repository |
| `boot/` | Companion loader and the Rockbox bootloader patch |
| `tools/` | Build, recipe, packer and release scripts with their tests |
| `docs/` | User guide, core and SDK documentation |

## Build

Requires Python 3.11+, Rockbox's `arm-elf-eabi-` GCC 9.5.0, decrypted 2.0.5 inputs from the installer tooling and the `revola` CLI from the installer repository on `PATH`.

```sh
python3 tools/build.py firmware --inputs /path/to/firmware-2.0.5 --cross-prefix /path/to/arm-elf-eabi-
python3 tools/build.py sdk
cd tools && python3 -m unittest discover -s tests
```

Targets, outputs and the emulator are in [Building the core](docs/core/build.md).

## Documentation

| Page | Contents |
|---|---|
| [User guide](docs/guide/install.md) | Installing, extensions, apps and games, recovery |
| [Core](docs/README.md#core) | Layout, hook sites, loader, settings store, memory rules, failsafe |
| [SDK](docs/sdk/README.md) | Writing an extension or an app |
| [STYLE.md](STYLE.md) | Principles and rules for every file |

## RevolaOS and RepriseOS

RevolaOS started from the groundwork of [RepriseOS](https://github.com/lgreitis/osos-lab) by lgreitis: the boot chain, the installer's device and bundle code, and the reverse engineering of Apple's 2.0.5 firmware. You can support lgreitis on [Ko-fi](https://ko-fi.com/lgreitis).

Both run Apple's own 2.0.5 firmware with additions, on the iPod Classic 6G, 6.5G and 7G, and both install from the desktop over DFU. RepriseOS is a firmware image: what it contains is settled before it goes onto the iPod. RevolaOS is a small core that loads extensions on the iPod itself.

| | RepriseOS | RevolaOS |
|---|---|---|
| Where features are chosen | Before installing | On the iPod, in Settings |
| Adding or removing a feature | Install a different firmware | Turn an installed extension on or off; off, it is not loaded and uses no RAM |
| Third-party features | Need a modified firmware | Copy the extension's file to `/Revola/Extensions/`; the OS stays as it is |
| Seeing memory use | Not shown | Settings shows free memory and how much each extension uses |
| After a crash | Panic screen | The next boot turns off the extension that crashed; hold Previous to start without extensions |

## Development

Large language models are used for much of the coding on RevolaOS. Their output is checked in the emulator, its memory use comes from the heap profile, and every release is tested on an iPod before it ships. Contributions written with the same tools are welcome and tested in the same way.

## License

GPL-3.0-only. See COPYING and NOTICE.
