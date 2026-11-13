# RevolaOS SDK

What extensions and apps build against, for one revola release. Nothing in it reaches into the core.

| Directory | For |
|---|---|
| `extension/` | Extensions: `rv_api.h`, the declaration helpers, the C library, the module linker script, `rv.mk` and `rvx-pack` |
| `app/` | Apps and games on Apple's eApp host: `rv_app.h`, the runtime, `rv_app.mk` and `eapp-pack` |
| the root | What both share: the symbols header and linker script of the Apple firmware, `rv_firmware.h` and `rv_firmware.mk` |

The guides ship in `docs/` of the SDK tarball and live at [docs/sdk](https://github.com/RevolaOS/revola/tree/main/docs/sdk) in the repository: [Getting started](https://github.com/RevolaOS/revola/blob/main/docs/sdk/README.md) for an extension, [Apps and games](https://github.com/RevolaOS/revola/blob/main/docs/sdk/apps.md) for an app.
