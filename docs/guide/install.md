# Installing RevolaOS

RevolaOS installs from a desktop computer over USB. The installer backs up the iPod's boot flash, builds the firmware from Apple's own 2.0.5 firmware file on your computer and writes the result to the iPod. Apple's software stays on the iPod and can be started at any time.

## What you need

| Item | Notes |
|---|---|
| iPod Classic 6G, 6.5G or 7G | The installer checks the connected device |
| The RevolaOS installer | From the [installer releases](https://github.com/RevolaOS/installer/releases) for macOS, Windows and Linux |
| Apple firmware `iPod_38.2.0.5.ipsw` | The one file serves every supported model |
| A 30-pin USB cable | Connected directly to the computer, not through a hub |
| A backup of your music | RevolaOS is beta software; the installer does not touch the music |

## Steps

1. Open the installer and read the disclaimer.
2. Choose the firmware: the IPSW file and the RevolaOS package. The newest package downloads automatically; a local package file can be chosen instead.
3. On Linux, allow USB access when asked.
4. Put the iPod into DFU mode: unlock the Hold switch, then hold Menu and Center together for about 12 seconds, past the Apple logo, until the screen turns black again.
5. The installer checks the device, backs up the NOR flash, assembles the firmware, uploads the files and installs the bootloader. A summary shows what will happen before anything is written.
6. The iPod restarts into RevolaOS. Disconnect USB once it has restarted.

Backups and the assembled firmware are kept in the installer's application data folder, under `org.revolaos.installer`.

## What is on the iPod afterwards

| Path | Contents |
|---|---|
| `/Revola/Firmware/revola.bin` | The RevolaOS firmware |
| `/Revola/Firmware/loader.bin` | The companion loader |
| `/Revola/Extensions/*.rvx` | The extensions of the package |
| `/Revola/Settings/` | Settings of the core and the extensions |
| `/Revola/Themes/*.rvt` | The themes of the package and any you copy there, read by the Appearance extension |
| `/Revola/Games/<id>/`, `/Revola/Apps/<id>/` | The games and apps of the package and any you copy there, listed by the Homebrew extension |
| Boot flash | The RevolaOS bootloader; Apple's bootloader is kept and used when Menu is held |

## Choosing a system at startup

| Hold while the iPod starts | Starts |
|---|---|
| Nothing | RevolaOS |
| Menu | Apple's software |
| Play/Pause | Rockbox, if installed separately under `/.rockbox/` |
| Previous | RevolaOS without extensions, see [Recovery](recovery.md) |

Start with the Hold switch unlocked. The bootloader does not start RevolaOS while it is locked.

## Updating

Run the installer again with the new package. The iPod's files are replaced; the bootloader is kept when it matches the package and replaced after a fresh backup when it does not. The installer writes the package's files and does not delete others, so extensions and settings you added stay.
