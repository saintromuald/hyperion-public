# Hyperion release notes

What changed in each version of Hyperion, newest first. The game shows the same under Menu,
Patch Notes. Versions are numbered as `docs/versioning.md` says; this page is written from
`engine/notes/release-notes.json` by `engine/tools/release_notes.py md`.

## v0.7.1-beta, 2026-10-30

**Language support, mod system updates and security fixes.** Hyperion and the game in 13 languages, groundwork for mods to bring their own sounds, art and video, better help when Hyperion can't start or crashes, and security fixes.

### New

- **Language support.** Hyperion and the game can now be played in 13 languages: English, German, French, Italian, Spanish (Spain), Spanish (Mexico), Portuguese (Brazil), Polish, Russian, Japanese, Korean, Chinese (Simplified) and Chinese (Traditional). Hyperion uses your StarCraft's language by default. You can change it in Settings, Interface, Language. You can type chat and names in your language, including Japanese, Korean and Chinese. If you spot a mistake, let us know.
- **Mod system updates.** Groundwork so mods can do much more later. Mods can bring their own sounds and music in common formats (MP3, OGG, FLAC), and their own unit portraits, buttons and unit pictures. Mods can use regular image files (PNG and others) for unit art, drawn sharp and in full colour, with larger and smoother-turning units. Mods can bring their own videos and cinematics in common formats (MP4 and others), up to 4K. The game's own cinematics play in full colour, and the campaign screen lets you watch them again. Mod makers can check a mod for problems before anyone plays it. Maps that go past what a classic map can hold (larger than 256 by 256, more than 8 players, triggers for more players, or a mod's own units) now carry their own version number in the map's header, so map sites and editors can tell them apart from classic maps. Classic maps keep their own version and play as before.
- **Better start-up and crash handling.** If Hyperion can't start, or crashes, a window explains what happened and gives you a short report to copy and send us, for example on Discord. Hyperion keeps a log of each start and a report of each crash in its folder, to help us find the problem.

### Changed

- **The music player, for now.** The music player is out of Hyperion for now. It will come back as an add-on, once Hyperion supports add-ons that anyone can make and turn on.
- **Programs that control Hyperion.** Debug Tools' live channel now only accepts programs that have the key Hyperion makes each time it starts, and turning on a mod that uses the channel asks you first. The tools that come with Hyperion find the key themselves. More changes to how programs and AI tools connect to Hyperion are planned.

### Fixed

- **Changing the HUD during a game.** Changing the HUD design during a game left the old HUD on screen and still in control. The new HUD didn't respond, and in campaign missions the map stopped taking clicks.
- **Portraits after changing the HUD.** The unit portrait stopped animating after changing the HUD during a game.
- **StarCraft in other languages.** StarCraft installed in other languages would not start due to an incompatibility.
- **Campaign screen.** On the campaign screen, pressing Escape just as a mission was starting could change the screen underneath it.
- **Security: files.** As a precaution, Hyperion now checks replays, maps, saved games, mods and videos more carefully, so a damaged or deliberately made file can't crash Hyperion or run code on your computer.
- **Security: rooms on your network.** As a precaution, a room and its browser page only accept what they should, and passwords and keys no longer show in the list of running programs or in crash reports.
- **Watching a room's match.** Watching a match in a room could freeze for good when a lot happened at once.
- **Damaged replays.** A damaged replay could make Hyperion stop responding.

## v0.6.29-beta, 2026-10-27

**Initial release.** Hyperion's first public build: StarCraft and Brood War on an engine of its own, played from the free StarCraft on Battle.net or a classic install. New menus, the game drawn on your graphics card at any size, bigger games, rooms on your network and more.

### New

- **Plays from your StarCraft.** The free StarCraft on Battle.net or a classic install is all Hyperion needs; no StarCraft.exe. Settings, Data and mods, Game files chooses which.
- **New menus.** Home, Campaign, Skirmish, Multiplayer, Replays and Settings, new, with Remastered's settings: mouse sensitivity, voices and subtitles, console skins and the chat filter.
- **Drawn on your graphics card.** Full colour at any resolution through Vulkan or Direct3D 12. Games fill a wide screen, or keep 4:3 or 16:9, and the camera zooms out over a battle.
- **HUD designs.** Mech and Classic, Mech across the foot or down a side, full or small, in three materials, lit in a colour you choose, with a live picture in Settings.
- **Bigger games.** Up to 32 players on maps up to 512 by 512 tiles, and 64 players as an experiment. Selections and control groups of up to 255, and buildings selected together with subgroups to Tab through.
- **Skirmish.** Team matches, computer players from Very Easy to Elite and three Cheater levels, a handicap for each player, mutators, and a new score screen.
- **Hyperion's rules.** New games play by Hyperion's rules, with Remastered's order fixes; the classic rules are a choice. Replays always play as they were recorded.
- **Replays.** Every classic replay plays frame for frame as it did. Replay TV plays replays one after another, and big games can be saved and loaded.
- **Rooms on your network.** Host a room and others join from Hyperion or from a browser on a phone or PC. Watch matches live, a few seconds behind.
- **Your hotkeys.** A profile's own keys, and the command card's keys as Classic or Grid.
- **The music player.** Ctrl+F9 in a game: the game's soundtrack, your own music, or what your other apps play. Off until you turn it on in Settings, Sound.
- **Mods and maps.** Mods add units, races and spells as data. Hyperion's own map format holds bigger maps and more players, and maps from Remastered's editor and EUD maps play as Remastered plays them.
- **Windows, Linux and Mac.** 64-bit Windows, Linux on x86-64 and ARM64, and the Mac on Apple silicon with macOS 13 or later.
- **Works in progress.** The benchmark and the trainer are included to try, and say so.
