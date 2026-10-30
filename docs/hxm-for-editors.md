# Hyperion maps for map editors

This is for the author of a map editor who wants it to open, save and test Hyperion's
maps. It says what a Hyperion map is, what an editor needs to support it, step by step,
what is built and stable now and what is planned, and how to test a map in Hyperion.
The field-by-field reference is `hxm-format.md` beside this file.

Written 2026-10-13, for format version 1; brought up to date 2026-10-15 for Hyperion v0.3.0-beta.

## What Hyperion is

Hyperion is a 64-bit port of StarCraft 1.17. It plays every map 1.17 plays exactly as
1.17 does, to the frame: the classic `.scm` and `.scx`, the maps Remastered and ScmDraft 2
save, the campaign's. For maps past 1.17's limits it has a format of its own, `.hxm`:
bigger maps, more players, a colour in full RGB for each player, and later the features
listed under "Planned" below.

A Hyperion install is a folder holding the player's own StarCraft 1.17 files with
`Hyperion.exe` beside them. Hyperion reads the game's data from there. It does not need
Remastered, but can use a Remastered install for its HD art.

## The format in short

A `.hxm` file is a zip of:

    map.json              the map as one JSON document
    layers/<name>.bin     the per-tile data, binary, little-endian
    files/<path>          the map's other files (its sounds), by their path in the map

The same tree in a folder is the unpacked form. Hyperion reads the zip; the folder is for
editing by hand and version control.

`map.json` uses names for the game's numbers (`terran_marine`, `jungle`, `at_least`) and
takes the number wherever it takes a name, so nothing outside the lists is lost. Text is
plain UTF-8 JSON text, with the game's colour codes as the control characters they always
were. Strings are written where they are used: there is no string table to keep.

The format was made so that every CHK converts to it and back. If an editor already
models a CHK, an `.hxm` is that model in another form:

| CHK | `.hxm` |
|---|---|
| `DIM`, `ERA`, `VER` (`expansion`) | `size`, `tileset`, `expansion` |
| `SPRP` | `name`, `description` |
| `OWNR`, `SIDE`, `COLR`, `CRGB`, `IOWN` | `players` (`slot`, `race`, `color`, `color_mode`, `rgb`, `editor_slot`) |
| `FORC` | `forces`, and each player's `force` |
| `UNIT` | `units` |
| `THG2` | `doodads` |
| `MRGN` | `locations` |
| `TRIG`, `MBRF` | `triggers`, `briefing` |
| `UPRP`, `UPUS` | `unit_properties` |
| `UNIx`, `UPGx`, `TECx` (and `UNIS`, `UPGS`, `TECS`) | `settings` |
| `PUNI`, `PUPx`, `PTEx` (and `UPGR`, `PTEC`) | `availability` |
| `WAV `, `SWNM` | `sounds`, `switches` |
| `MTXM`, `TILE`, `ISOM`, `MASK`, `DD2 ` | the layers `tiles`, `editor_tiles`, `isom`, `fog`, `editor_doodads` |
| `STR `, `STRx` | the text in place |
| `TYPE`, `IVER`, `IVE2`, `VCOD`, odd section sizes | `compat` |
| any other section | `sections`, kept as they were |

**The CHK's `VER`.** A map past the original's limits (more than 256 tiles a side, players
past 7 who take part, triggers for slots past 11, or a mod's units) compiles to a CHK whose
`VER` is 1206, or 1064 when it does not need Brood War. No Blizzard version uses them, so map
sites and tools tell such a map from a classic one by `VER` alone, whatever the file is
called. An editor that writes a Hyperion map as a CHK, in an `.scx` or alone, writes the same
`VER`. A map within the limits keeps the original's (59, 63, 64, 205 or 206).

## Supporting it, step by step

Each step is useful on its own.

### 1. Open and save `.hxm` within 1.17's limits

A map of up to 256 tiles a side and 8 players. It plays in Hyperion as the same map saved
as `.scx` would, so this step is about the file, not the game.

- Read `map.json` and the layers it lists under `layers`, and the files under `files`.
- Write only what differs from the defaults in `hxm-format.md`; every field has one.
- Keep `compat` and `sections` as they came when the map was converted from a CHK, so it
  converts back to the same bytes. A new map has neither.
- Keep the `isom` layer. Hyperion does not read it, but an editor needs it to paint
  terrain, as with a CHK's ISOM. It has the CHK section's layout at the map's size.

### 2. Larger maps

Hyperion plays maps up to 1,024 tiles a side, wide or tall; **the target for now is 512**.
Nothing in the document changes for a larger map but `size` and the layers' lengths:

- `tiles` holds width times height u16 values, rows top to bottom. A 512 by 512 map has
  262,144 tiles, which is why the layers are binary.
- Positions are pixels from the top left, as in a CHK, up to the map's size times 32.
- A game holds 1,700 units, as 1.17 does, and on a map with players past 7 that many for
  each 8 of its slots (3,400 for 16, 6,800 for 32). The map's preplaced units count.
- Hyperion builds the map's pathing regions when the game starts, as 1.17 does. On a map
  past 256 tiles they are coarser, and a map whose terrain has too many separate pieces
  is refused with 1.17's "too many obstructions" message. Only a game shows this; see
  "Testing" for a quick way to start one.

### 3. More players

Up to 32 players (Hyperion's build plays 64; 32 is the target for now). The rules:

- **The slot numbers.** Slots 0 to 7 are 1.17's lobby slots and 8 to 10 the three no 1.17
  lobby offers. Slot 11 is neutral, which the document keeps apart in `neutral`. The new slots are
  12 and up. The `players` list is in slot order without slot 11: **entry 11 of the list
  is slot 12**, entry 12 slot 13, and so on. A unit's or doodad's `owner` is the slot
  number itself (0 to 10, then 12 and up), or `"neutral"`.
- **What makes it a Hyperion map:** a player from list entry 8 on whose `slot` is `human`
  or `computer`. Such a map has no form the original plays, and its CHK carries Hyperion's
  `VER` (above). A map without one is a 1.17 map.
- **Humans and computers.** For now, humans take slots 0 to 7 as in 1.17, and every
  open slot past 7 gets a computer of the race the map gives it. Hyperion's multiplayer
  is to be rebuilt, and will seat people past 8 then.
- **Forces:** four, as in 1.17. Each player's `force` is 0 to 3.
- **Colours:** a player's `color` takes 1.17's and Remastered's names and numbers (0 to
  22), and from 23 on Hyperion's own (`hxm-format.md`, Player). `"rgb": [r, g, b]` gives a
  player any colour: for slots 0 to 7 with `"color_mode": "rgb"`, as Remastered maps
  have it; for the slots past 7 on its own. Hyperion draws the colour itself, and its
  8-bit drawing takes the nearest palette ramp.
- **Triggers** name any slot by its number, the new ones too, and the groups
  (`all_players`, `foes`, `allies`, `force_1` to `force_4`, and the rest in
  `hxm-format.md`), which reach every slot. A trigger can run for a slot past 11, a
  condition or action can name one, and Give Units can give to one; the leaderboards,
  mission objectives and the Most and Least conditions count every slot (added
  2026-10-13; `hxm-format.md`, Trigger, has how the compiled map carries them). Text,
  sounds and the view still act only for the person playing, slots 0 to 7.
- **Start locations:** one `start_location` unit for each slot a human or a computer can
  take, owned by that slot, as in 1.17.

### 4. Testing in Hyperion

Hyperion's command line has two things for an editor. Pass full paths.

**The check.**

    Hyperion.exe --hxm-check <map.hxm> [<report.txt>]

It reads the map as a game would and reports, a line each, the errors that stop it
playing and the warnings an editor should show: a format version newer than the build, a
size outside 1 to 1,024, a unit outside the map or owned by a slot the map does not have,
a link to no unit, a slot open to a human or a computer with no start location, more
preplaced units than a game holds, a location defined twice, a trigger naming a location
the map does not have, a short tiles layer, and a size or player count past the targets.
The last line is `plays: ...` or `refused: ...` with the counts. The report goes to the
file named, or to standard output without one. The exit code is 0 when the map plays, 1
when it is refused, 2 when the file cannot be read. It does not start the game, needs no
game files (only `SDL3.dll` beside `Hyperion.exe`) and takes well under a second.

**Test play.**

    Hyperion.exe --map <map.hxm> [--race zerg|terran|protoss|random] [--computers <n>]
    Hyperion.exe --map <map.hxm> --ums

The first starts a melee game on the map at once, the player against computers (by
default one in every other start, as Play Custom seats them, and on a map with players
past 7 one in every open slot past 7). The second starts it as Use Map Settings, so the
map's triggers run: the player takes the first `human` slot and every `computer` slot gets
a computer. `.scm` and `.scx` work the same way. After the game Hyperion shows its menus.
When a map cannot be played, Hyperion shows its menus at once and
`hyperion_hxm.log`, in the `Hyperion` folder beside `Hyperion.exe`, says why.

For a quick run with no one watching:

    Hyperion.exe --map <map.hxm> --record=600,<out.rep>

plays the game by itself to frame 600 (about 25 seconds of game time at the fastest speed),
writes its replay and exits. That is the way to see that the pathing regions build and the
game starts.

**Live testing.** With `Hyperion.exe --control` running, an editor can switch the game to a
map without restarting Hyperion: push its tiles (`map_push`) or load the saved file
(`map_load`), each going out and coming in with a fade, and play a scripted cutscene on it.
`http://127.0.0.1:27117/guide`, while Hyperion runs that way, shows and tries each of these
on the game, with the code for it (`live-guide.html` beside this file is the same to read
offline); `live-channel-for-editors.md` has the messages; `tools\hxlive_cpp\hxlive.h` is a
client for C++ and `tools\hxlive.py` one for Python and the command line.

## Versions and what stays stable

- Version 1 is frozen as of 2026-10-13. Every field in `hxm-format.md` keeps its meaning.
- A field may be added in version 1 when leaving it out means what version 1 meant. A
  change to what an existing field means raises the version.
- Hyperion refuses a map of a version newer than it reads, with that reason, rather than
  playing it without the parts it does not know. An editor should do the same.
- Hyperion ignores fields it does not know. An editor's own data, such as view settings
  or layers of its own, goes under `editor`, in an object named after the editor:
  `"editor": {"stardraft": {...}}`. Hyperion never reads or writes it.
- Hyperion does not write `.hxm` files; only editors and tools do.

## EUD maps

Triggers that read and write the game's memory (Deaths and Set Deaths on a player or unit
past the game's own, as euddraft and EUD Editor make them) play as they do in StarCraft:
Remastered: Hyperion serves every address of StarCraft 1.16.1 that Remastered serves, and an
address it does not serve stops the map's triggers, as in Remastered. A
map whose triggers write memory cannot be saved during a game. A map's text may be UTF-8, and
a Korean map's CP949; both are drawn. EUD needs no field in `.hxm`: it is in the triggers.
Native features that do what EUD maps use memory for are planned (below).

## Not supported

- **Tiles Remastered 1.22 added.** A map whose terrain uses them does not play yet, as
  `.scx` or as `.hxm`.

## Planned, not in version 1

None of these are in the format yet. What they will look like is not fixed: the notes
say what each will do, so an editor can plan for it. Each will arrive as fields with
defaults or a new version, as above, and this document will say so.

- **Rule sets in the map.** A game plays by a named set of rules, and two exist now:
  Classic, 1.17's exactly, the default for every map 1.17 can play, and Hyperion's own,
  which every map past 1.17's limits plays by and any other map can be played by with
  `--rules hyperion`; the replay and the saved game record which. Later versions of the
  game's balance and fixes, up to Remastered's and any balance patch after it, will be rule
  sets too. What is planned for the format is a map saying which set it is made for.
- **The game base.** A map is for StarCraft or Brood War today (`expansion`); later also
  for a mod that brings its own races or unit settings, which decides the units, tech and
  races a map can use.
- **Mods' data in the map.** A mod can bring units, weapons and other data of its own now,
  with unit types past 1.17's 228 (`mod-data.md` in Hyperion's source), and a game with one
  plays by Hyperion's rules. What is planned for the format is a map made for a mod naming
  it and placing its units by name.
- **Custom races.** New playable races, first as data over the existing three, later with
  mechanics of their own. A map will be able to give a slot such a race.
- **Campaigns.** A campaign will be a set of maps in order, carried by a mod, with what a
  mission carries into the next: surviving units, resources, research, choices. A map
  will be able to say what it takes in and what it passes on.
- **A data fingerprint.** A map saved by an editor may record a hash of the game data it
  was made against, so a game on other data can say so.
- **Maps past 1,024 tiles a side.** Paused for now.

## Test maps

Remakes of web maps at Hyperion's sizes, made with Hyperion's tools from the original
maps:

| Map | Tiles | Players | Units |
|---|---|---|---|
| Big Game Hunters 2x | 256 | 16 | 920 |
| Big Game Hunters 2x rgb test | 256 | 16, 8 of them with an RGB colour | 920 |
| Big Game Hunters 2x 24 | 256 | 24 | 920 |
| Big Game Hunters 3x3 | 384 | 32 | 2,070 |
| Big Game Hunters 4x4 | 512 | 64 | 3,680 |
| Big Game Hunters 8x8 | 1,024 | 64 | 9,856 |
| Big Game Hunters XL | 512 | 16 | 994 |
| Big Game Hunters XL 1800 | 512 | 16 | 1,800 |
| Elderlands 2x | 384 | 16 | 736 |
| Garden of Aiur 2x | 384 | 16 | 624 |
| Trigger Test 32 | 384 | 32, Use Map Settings (`--ums`): triggers for every slot | 2,136 |

Each passes `--hxm-check`; the 4x4 and 8x8 are past the targets and say so, and the
package keeps them in an `Experimental` folder, as the skirmish list will. All but the
24-player map carry an `isom` layer, so an editor can paint their terrain.

## Questions

Through the Hyperion project, who will add what an editor needs.
