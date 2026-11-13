# Extensions

An extension is one file on the iPod, `/Revola/Extensions/<id>.rvx`. Installing, sharing and removing one is copying or deleting that file. An extension that is off is not loaded and uses no memory.

## Turning extensions on and off

Settings > Extensions lists Usage, Optimize and every installed extension. The second line of an extension shows its state and, while it runs, its memory use. Select one to read its description and change Enabled. Usage opens Memory Usage. Optimize chooses between Performance, which keeps the code and artwork extensions mark for it (the default) in memory, so using them does not spin the disk up, and Memory, which loads them from the disk when needed and uses the least memory; Auto picks Performance on a hard disk and Memory on flash.

An extension turned on loads at once. An extension turned off unloads within a second and its memory returns to the system. An extension you copy onto the iPod yourself starts off; most of the extensions the installer ships start on.

## Installing an extension from a file

1. Connect the iPod to the computer. It enters disk mode as usual.
2. Copy the `.rvx` file into `/Revola/Extensions/`.
3. Eject the iPod. RevolaOS reads the folder again as disk mode ends: new files load, removed ones unload, changed ones reload.

An extension must be built for the installed version of RevolaOS. The ones from the [extensions](https://github.com/RevolaOS/extensions) repository come with each release.

## States in Settings > Extensions

| State | Meaning |
|---|---|
| On, with memory use | loaded and running |
| Off | installed, not loaded |
| Off after crash | turned off because the last crash was attributed to it; turn it on again to retry |
| Needs update | built for a newer RevolaOS than the one installed |
| Incompatible | built for another RevolaOS or firmware version; replace the file with a matching build |
| Failed | the load failed for another reason |
| Unreadable | the file is truncated or damaged; copy it again |
| Duplicate | another file with the same extension id is loaded; remove one |
| Conflict | wants a hook, or a content folder such as `/Revola/Themes/`, that an enabled extension holds exclusively; the holder is named |
| Site mismatch | the firmware code it expected is not there |
| No memory | the heap was full, or the extension declared more screens than the resource budget allows |

Incompatible and Unreadable files have no Enabled control. Their detail page says how to replace the file.

## Settings of an extension

Extensions keep their settings in RevolaOS's settings store, by extension id, so renaming the file keeps them. Settings > Reset All Settings empties the store for the core and every extension. Extensions that add rows to the Main or Music menu can be shown or hidden in Settings > Main Menu and Settings > Music Menu, as the stock rows can.
