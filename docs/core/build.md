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

## Releasing

Nothing in a release contains Apple bytes: the recipes carry our code and short expected words, and the installer copies everything else from the user's own decrypted firmware. The recipes come out the same with or without the decrypted Apple image; the image only lets the build check the site table against it (expected words, and no branch into an entry site's second word). `--inputs` runs that checked build; `--without-image` skips it, which is how a machine without the image, CI included, can build the release once a checked build has passed on the same commit. The installer fetches the newest non-draft release of this repository and expects `classic-manifest.json`, its `.sig` and one `<sha256>.blob` per file as release assets, so every release must carry them.

```sh
git tag v2026.11.1-beta.1
python3 tools/release.py --tag v2026.11.1-beta.1 --minimum-installer-version 2026.11.1-beta.1 \
    --inputs /path/to/firmware-2.0.5 --seed /path/to/bundle.seed --upload
```

| Step | Source |
|---|---|
| Firmware recipe, companion recipe | this checkout, clean, at the tag |
| Rockbox dual-boot installer and uninstaller, `nor.json` | the Rockbox checkout pinned in `boot/dependencies.lock` |
| SDK tarball | the staged SDK |
| Every extension | a clean `../extensions` checkout (`--extensions`), built with its top-level `make` against the staged SDK |
| USB helper | a clean `../installer` checkout (`--installer`), `usb-helper/build.py` |
| Bundle | `revola bundle export`, `sign`, `pack` from the installer CLI, spec `build/release/spec.json` |

`build/release/release/` holds the assets as uploaded, plus `build.json` with the commits of the three checkouts. Versions are calendar versions in SemVer shape, `YYYY.M.N` (year, month without a leading zero, release number within the month) with an optional `-beta.N`; the installer, Cargo and the Tauri updater all compare them as SemVer. `--minimum-installer-version` is the oldest installer allowed to use the bundle; SemVer orders `2026.11.1-beta.1` before `2026.11.1`, so a beta installer needs a beta minimum. Without `--upload` nothing leaves the machine; without `--seed` the bundle is unsigned and only good for a local test through the installer's "Use a local package".

## SDK versioning

`sdk/api.lock` records the API version and the hash of every file in `sdk/`. The build and the `sdk` workflow fail when a file under `sdk/` changes at the same version, so an API change is always a visible version bump: `RV_API_MINOR` for additions, `RV_API_MAJOR` when an entry changes meaning. After the bump run `python3 tools/sdklock.py update`. The pages under `docs/sdk/` ship in the tarball but are not part of the lock.
