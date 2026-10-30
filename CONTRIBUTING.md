# Contributing

## Reports

Open an issue with what you did, what you expected and what happened, and your system. After a
crash, attach `hyperion_crash.log` from Hyperion's `Data` folder. The start-up log beside it
(`hyperion_startup.log`) helps when Hyperion does not start.

## Code

- Read the area's doc in `engine/docs/modules/` first; `overview.md` maps every file to one.
- Run the tests before sending a change: `python tools/run_tests.py`, from `engine/` after a
  build (`BUILDING.md`, Tests). They need none of the game's files.
- A change that is not meant to change the game's simulation must leave every replay identical.
  The replay comparison that checks it needs the game's files and a replay collection
  (`engine/docs/replay-test.md`); it runs before each release, so say in the pull request when
  a change may touch the simulation.
- Engine files are CRLF (`.gitattributes`).
- Comments are plain sentences: what the code does, and why when it is not obvious. The original
  game is "classic".
- No Blizzard files: no art, sound, video, archives or programs, and nothing extracted from them.

## Translations

The menus' text is in `engine/locale/`, one gettext catalog a language. The catalogs other than
English were drafted and need people who speak the language. `docs/translating.md` says how to
correct one or add a language.
