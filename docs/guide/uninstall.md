# Uninstalling RevolaOS

The installer removes RevolaOS and puts Apple's bootloader back. Your music and Apple's software are untouched. No firmware file is needed.

## Steps

1. Open the installer and choose Uninstall.
2. Put the iPod into DFU mode: unlock the Hold switch, then hold Menu and Center together for about 12 seconds, past the Apple logo, until the screen turns black again.
3. The installer checks the device, backs up the boot flash if RevolaOS's bootloader is installed, removes the RevolaOS files and sends the uninstaller, which moves Apple's bootloader back into place.
4. The iPod restarts into Apple's software. Disconnect USB once it has restarted.

If a file cannot be deleted, the installer says so and restores Apple's bootloader anyway. The leftover files are harmless; delete the `Revola` folder in disk mode to be rid of them.

## What is removed

| Path | |
|---|---|
| `/Revola/Firmware/` | firmware and loader |
| `/Revola/` | extensions, settings, data and crash reports |
| Boot flash | the RevolaOS bootloader, replaced by Apple's |

The files go first because the RevolaOS bootloader stops when they are missing.
