# Building the core

## Requirements

| Requirement | Notes |
|---|---|
| Python 3.11 or newer | |
| `arm-elf-eabi-` GCC 9.5.0 | Rockbox's toolchain; `firmware/apple.json` pins the version |
| Decrypted 2.0.5 inputs | Produced by the installer tooling from `iPod_38.2.0.5.ipsw` |
| The `revola` CLI | From the installer repository, `cargo build --release -p revola-cli`, on `PATH`; it applies the recipe |

## Commands

```sh
python3 tools/build.py firmware --inputs /path/to/firmware-2.0.5 --cross-prefix /path/to/arm-elf-eabi-
python3 tools/build.py loader-recipe
python3 tools/build.py sdk
python3 tools/build.py examples
cd tools && python3 -m unittest discover -s tests
```

| Target | Writes | Notes |
|---|---|---|
| `firmware` | `build/firmware.json`, `build/firmware.data`, `build/revola.bin` | Checks the site table against the pinned image |
| `firmware-recipe` | `build/firmware.json`, `build/firmware.data` | Stops before assembly, needs no Apple inputs, skips the image checks |
| `loader` | `build/companion.json`, `build/companion.data`, `build/loader.bin` | Needs `--inputs` and `--nor <NOR backup>`, which personalises the companion |
| `loader-recipe` | `build/companion.json`, `build/companion.data` | Stops before assembly |
| `bootloader` | `build/bootloader-ipod6g.ipod` | The patched Rockbox bootloader from the checkout pinned in `boot/dependencies.lock` |
| `all` | everything above except `examples` | The default; needs `--inputs` and `--nor` |
| `sdk` | `build/revola-sdk-<version>.tar.gz` | The staged SDK with the generated headers and `docs/` |
| `examples` | `build/examples/<id>/<id>.rvx` | The fixture extensions under `sdk/examples`, built against a staged SDK |

`--selftest` compiles the dispatcher and loader self-test into the core. Results land in `core_selftest`, read with `ipodvm peek`.

The build prints the core's resident bytes. The link fails above `CORE_BYTES` (96 KiB) in `core/layout.h`.

The build tool reads the site table from the linked core, rejects entry sites whose displaced words use `pc`, are conditional, branch, or are the target of a branch at `entry+4`, checks every expected word against the pinned image when Apple inputs are present, and records the same expectations in the recipe for the installer.

## Running in the emulator

```sh
ipodvm launch --firmware build/revola.bin --disk <test disk>
ipodvm disk --copy <files> ...
```

`ipodvm` comes from the emulator repository. `disk --copy` puts extension files on a disk clone.

## SDK versioning

`sdk/api.lock` records the API version and the hash of every file in `sdk/`. The build and the `sdk` workflow fail when a file under `sdk/` changes at the same version, so an API change is always a visible version bump: `RV_API_MINOR` for additions, `RV_API_MAJOR` when an entry changes meaning. After the bump run `python3 tools/sdklock.py update`. The pages under `docs/sdk/` ship in the tarball but are not part of the lock.
