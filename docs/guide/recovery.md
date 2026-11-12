# Recovery

RevolaOS catches crashes, records them and starts again without the extension that caused one.

## The error screen

After a crash the iPod shows "We ran into an error", the name of the extension it attributes the crash to when there is one, and the path the report will be saved under. Press Center once to restart. Pressing Menu and Center resets the hardware instead and may lose the report.

## Crash reports

At the next boot the report is written to `/Revola/Crashes/<report-id>.txt` and the attributed extension is turned off before any extension loads. Settings > Extensions shows it as Off after crash; turn it on again there if you want to retry. Attribution points at a likely cause, not a proven one.

A report holds the extension id, the firmware and build ids, the saved registers and a short piece of the stack. Attach it when you [open an issue](https://github.com/RevolaOS/revola/issues). A hardware reset or a flat battery before the next boot can discard a pending report.

## Starting without extensions

Hold Previous while RevolaOS starts. No extension loads for that boot and Settings > Extensions shows "Core only". To get rid of an extension that keeps failing, connect the iPod to a computer and delete its file from `/Revola/Extensions/`.

## Starting Apple's software

Hold Menu while the iPod starts. Apple's firmware runs as before RevolaOS was installed. Nothing in RevolaOS changes; the next start without a held key returns to it.
