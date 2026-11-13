# Settings screens

The core adds two pages to Apple's Settings menu. Their code is `core/ui/info.c`, `core/ui/extensions.c` and `core/ui/usage.c`.

## RevolaOS Info

A text page: the version with its build id, the project address, the RevolaOS and RepriseOS contributors, third-party credits and the licence. The system font has no `+`, so the version shows its build in brackets.

## Extensions

A list with Usage and Optimize For followed by the installed extensions. The pane says how many are installed. The list is read when opened and released when closed. Each extension's second line shows its state and, while it runs, its memory use; selecting one opens its description and Enabled control. Usage opens Memory Usage. Optimize For is a core-owned choice screen (Auto, Performance, Memory) over `CORE_KEY_OPTIMIZE`, the preload limit of the loader (`docs/core/loader.md`); its row shows the chosen label.

| State | Cause | Enabled control |
|---|---|---|
| On, with bytes | loaded | yes |
| Off | off in the store, or not in the store and not in `loader/bundled.def` | yes |
| Off after crash | the last crash was attributed to it (`CORE_KEY_CRASHED`) | yes |
| Needs update | built for a newer core API minor | yes |
| Failed | any other load failure | yes |
| Incompatible | unsupported package or manifest format, another API major or another firmware id | no |
| Unreadable | truncated or invalid metadata | no |
| Duplicate | another file with the same manifest id loaded | yes |
| Conflict | a replace handler on a site another handler holds, or a content folder another extension owns; the site or the holder is named | yes |
| Site mismatch | a raw site whose address does not hold the expected word | yes |
| No memory | heap exhausted, or more than 4,096 resource ids declared | yes |

Rejected files remain visible by file name. The detail page of an Incompatible or Unreadable file says how to replace the package: build and package extensions with the SDK matching the installed core.
