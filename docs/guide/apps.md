# Apps and games

RevolaOS runs apps and games built for Apple's own game runtime, the one behind Extras > Games, from folders of its own. Each is one folder with the program, a `Manifest.plist` and an icon.

| Folder | Where it appears |
|---|---|
| `/Revola/Games/<name>/` | Extras > Games, with the games from iTunes |
| `/Revola/Apps/<name>/` | Extras > Apps |

The installer puts the games and apps of its package there; copy any other folder there with the iPod in disk mode, and delete a folder to remove the app. The Homebrew extension lists these folders and must be on in Settings > Extensions. The games iTunes installed are not affected. An app built for `/Revola/Games/` does not work from `/Revola/Apps/` and the other way round; its author says which.
