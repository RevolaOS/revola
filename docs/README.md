# RevolaOS documentation

## User guide

| Page | Contents |
|---|---|
| [Installing](guide/install.md) | What you need, the installer steps, what is written to the iPod |
| [Uninstalling](guide/uninstall.md) | Removing RevolaOS and restoring Apple's bootloader |
| [Extensions](guide/extensions.md) | Turning extensions on and off, installing one from a file, what Settings > Extensions shows |
| [Recovery](guide/recovery.md) | The error screen, crash reports, starting without extensions |

## Core

| Page | Contents |
|---|---|
| [Layout and boot](core/layout.md) | Directories of the core and the order things run at boot |
| [Build](core/build.md) | Requirements, build commands, outputs, running in the emulator |
| [Hook sites](core/hook-sites.md) | Site kinds, binding, handler calling convention, adding a site |
| [Extension loader](core/loader.md) | The `.rvx` file, boot scan, lifecycle, assets, rescans |
| [Settings store](core/settings-store.md) | File format, flush points, reserved keys |
| [Memory rules](core/memory-rules.md) | Changes to Apple's memory policy and their settings |
| [Failsafe](core/failsafe.md) | Panic screen, crash record, attribution, core-only boot |
| [Settings screens](core/settings-ui.md) | RevolaOS Info and Settings > Extensions |

## SDK

| Page | Contents |
|---|---|
| [Getting started](sdk/README.md) | Files of the SDK, a first extension, building and installing |
| [Modules](sdk/modules.md) | Entry point, descriptor, module kinds, build rules, when to use ARM |
| [User interface](sdk/ui.md) | Declaring screens, rows, settings and entries; canvases; resource limits |
| [Menu placement](sdk/menus.md) | Putting a row into a stock menu |
| [Settings](sdk/settings.md) | Reading, writing and reacting to settings |
| [Files](sdk/files.md) | The extension's data folder |
| [Assets](sdk/assets.md) | Bitmaps and data packed into the `.rvx` |
| [Raw sites](sdk/raw-sites.md) | Hooking code no core site covers |
| [Audio effects](sdk/audio.md) | The output PCM site and the effect stages |
