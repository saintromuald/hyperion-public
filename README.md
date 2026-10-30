# Hyperion

A source port of **StarCraft: Brood War**: a native 64-bit program for Windows, Mac and Linux
that plays the game as the original engine does, with better performance and room to grow.
Your maps, classic replays and saved games play as they always have. Expanded unit tables
make room for new units and races, and mutators add new twists to skirmish and local
multiplayer matches.

Hyperion is an early beta. Builds are on the [Releases](../../releases) page.

## What it adds

- **Bigger games.** Maps up to 512 × 512 tiles and up to 32 players. Support for map editors
  to come.
- **Modern controls.** Select up to 255 units, with StarCraft: Remastered's order fixes.
- **Any screen.** Widescreen and ultrawide, camera zoom, and HUD designs across the bottom or
  in a column at either side. Players can make their own HUD designs
  (`engine/docs/hud-format.md`), and more tools for it are planned.
- **Skirmish options.** Computer levels from Very Easy to Cheater, handicaps, mutators, and a
  wider range of colours for every player.
- **Replays.** A replay HUD with every player's figures, a slider through the game, and
  computer games to watch.
- **Your network.** Host a room on your local network and play with friends.
- **13 languages.** The game and Hyperion's menus in English, German, French, Italian, Spanish
  (Spain and Mexico), Portuguese (Brazil), Polish, Russian, Japanese, Korean, and Chinese
  (Simplified and Traditional). Hyperion uses your StarCraft's language unless you choose
  another in Settings.

## Planned

- **Mods.** Mods with their own sounds, music, art and video in common formats
  (`docs/mod-data.md`). The framework to make this easier is in progress.

## You need your own game files

Hyperion contains none of StarCraft's files. It reads them from your own install, either
**StarCraft: Remastered** from Battle.net (free) or a classic **Brood War** install with its MPQ
files.

## AI disclosure

Most of Hyperion was built with AI tools, including the reverse engineering of StarCraft 1.16.1
and 1.17 that its game code is based on. The planning, structure and direction of the project
are my own. I use AI to work faster, and I review its work and make the final decision on every
change. No AI image generation is used: Hyperion's own visual design is made in code.

## Building

From `engine/`, with Python 3, first fetch the libraries the build needs (`THIRD-PARTY.md`):

    python tools/get_libraries.py

Then build for your platform:

| Target | Command | Needs |
|---|---|---|
| Windows x64 | `python tools/build_engine_x64.py` | Visual Studio 2026 with the C++ workload |
| Linux x64 | `python tools/build_engine_linux.py` | Docker, or a Linux machine with clang, lld, CMake and Ninja |
| Linux arm64 | `python tools/build_engine_linux.py --arch arm64` | the same |
| Mac (Apple silicon) | `python tools/build_engine_linux.py`, then `python tools/package.py --mac`, on the Mac (`BUILDING.md`) | Xcode's command line tools, CMake and Ninja |
| Browser | `python tools/build_engine_web.py` | Docker (Emscripten) |

On Windows, keep the checkout at a short path such as `C:\hyperion`: some libraries' CMake
builds fail under a long one. `BUILDING.md` has each platform's steps in full, how to start the
build with your game files, and the tests to run. How the program is laid out, area by area:
`engine/docs/modules/overview.md`. Every variable and command line option:
`engine/docs/hyperion-exe.md`.

## Contributing

Bug reports, crash logs and ideas are welcome in the issues. After a crash, Hyperion's `Data`
folder holds `hyperion_crash.log`; attaching it helps. For code and translations, see
`CONTRIBUTING.md`.

## Want to give feedback?

Join the [Discord](https://discord.gg/BtU6qU6Xf) to give feedback and share ideas for features
you'd like to see.

## Acknowledgments and special thanks

- Heinermann and Starsurgical's [StarSource](https://github.com/Starsurgical/StarSource), whose
  reverse engineering databases, guidance and names Hyperion's work drew on. No StarSource code
  is used in Hyperion.
- The BWAPI community, for offsets, function names and years of shared knowledge.
- [AbyssEngine](https://github.com/AbyssEngine/AbyssEngine) and
  [OpenDiablo2](https://github.com/OpenDiablo2/OpenDiablo2), an architectural reference for
  rebuilding Blizzard-era engines.
- Daniel Roster (Nathan Baggs' Gaff Discord), who pointed us at both engines.
- The decompilation community at large, whose methods (from the Super Mario 64 and Ocarina of
  Time projects on) this work follows.
- The authors of the libraries and fonts in `THIRD-PARTY.md`.

## License

Hyperion's own code and documentation are under the MIT license (`LICENSE`). The libraries it
builds with keep their own licenses (`THIRD-PARTY.md`).

Hyperion is a fan project. It is not affiliated with or endorsed by Blizzard Entertainment.
StarCraft and Brood War are trademarks of Blizzard Entertainment.
