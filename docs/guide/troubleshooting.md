# Troubleshooting

What to do when the installer or the iPod does not behave as the other guides describe.

## The installer does not find the iPod

- Use a 30-pin cable plugged straight into the computer. Hubs and docks often drop the DFU connection.
- Enter DFU again: unlock the Hold switch, hold Menu and Center for about 12 seconds, through the Apple logo, until the screen is black. A black screen with the backlight on is not DFU; keep holding.
- Windows: the installer uses the DFU driver that iTunes installs when it cannot talk to the iPod directly. Install iTunes from Apple's site, then reconnect.
- Linux: the installer asks for administrator access once to install its USB rule. If that prompt never appears, install polkit and a desktop authentication agent, then try again.
- Close other programs that claim the iPod, such as iTunes, Finder, Music or a virtual machine.

## "No RevolaOS release has been published yet"

The installer looks for the newest package on the [revola releases](https://github.com/RevolaOS/revola/releases). Check the connection, or download the package there and choose it as a local file.

## The package is rejected

An older installer refuses a package made for a newer one. Update the installer from the [installer releases](https://github.com/RevolaOS/installer/releases). A signature error on a downloaded package means the download is damaged or altered; download it again.

## The installation stopped part way

The installer says which step failed and where its backups and logs are. Put the iPod into DFU mode again and run the installer again; every step can be repeated. Use "Save debug ZIP" and attach it when you [open an issue](https://github.com/RevolaOS/installer/issues).

## The iPod shows "File read failed" or a Rockbox error at startup

The RevolaOS bootloader could not read `/Revola/Firmware/revola.bin`. Hold Menu while the iPod starts to use Apple's software, then run the installer again, which writes the files anew. The music is not affected.

## The iPod does not start at all

Hold Menu and Center until it restarts. If it stays dark, charge it for a while first; the bootloader waits for enough battery. If it restarts into the error screen, see [Recovery](recovery.md).

## After an uninstall, files are left on the iPod

If a file could not be deleted, the installer lists it and restores Apple's bootloader anyway. The leftovers do nothing with Apple's bootloader. Connect the iPod in disk mode and delete the `Revola` folder to remove them.

## Where the installer keeps its files

Backups of the boot flash, the decrypted Apple firmware and the logs of every run are in the installer's application data folder under `org.revolaos.installer`. The Success screen and every error message show the exact path.
