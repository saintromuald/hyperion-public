# The Hyperion map format (.hxm)

A Hyperion map is a small tree of files: a JSON document, `map.json`, that says everything
about the map but its per-tile data, and the per-tile layers beside it. It is meant to be
written by hand, by an editor or by a generator (an AI included) as easily as it is read,
and every StarCraft map converts to it: the classic `.scm` and `.scx`, the maps
Remastered and ScmDraft 2 save, the campaign's, and the map inside a replay.

It is Hyperion's own format. Beyond 1.17's limits it holds
bigger maps and more player slots than 1.17's, and its numbers are not capped by 1.17's
sections: a side is any size up to 65,535 tiles, and the players are a list of any length.
The engine sets the limits it plays (`engine/docs/maps.md`): 1,024 tiles a side and 64
players, with 512 and 32 the target for now. An editor's guide to the format, with what is
planned for it, is `hxm-for-editors.md`.

`tools/hxmap` reads and writes the format and converts:

    python -m tools.hxmap convert "(8)Big Game Hunters.scm" bgh.hxm
    python -m tools.hxmap convert bgh.hxm bgh/            # unpacked, to edit
    python -m tools.hxmap convert bgh/ bgh.scx            # back to a classic map
    python -m tools.hxmap info bgh.hxm
    python -m tools.hxmap roundtrip <maps or folders>     # check the conversion

## The package

    map.json              the document
    layers/<name>.bin     each layer map.json lists
    files/<path>          the map's other files (its sounds), by their path in the map,
                          with slashes for backslashes

A `.hxm` file is a zip of that tree (deflated or stored). The same tree in a folder is the
unpacked form, for editing and version control. Both read the same.

## Conventions

- Names are lower case with underscores (`terran_marine`, `jungle`, `at_least`). Wherever
  the document takes a name for one of the game's numbers it also takes the number, so a
  value outside the lists is never lost. `tools/hxmap/names.py` has every list; the engine
  has the same (`engine/src117/port/data/port_hxm_names.h`).
- Coordinates are pixels from the map's top left; a tile is 32 by 32 pixels.
- Text is plain JSON text. The game's colour and formatting codes are the control
  characters they always were (`\u0003` and so on).
- A field left out takes its default, given below. The converter writes only what differs
  from the defaults.

## Players

Player slots are counted from 0 and named by their number. The neutral player is not a
slot: it is `"neutral"`. Triggers name groups of players with these names:

| Name | Who |
|---|---|
| `current_player` | the player the trigger runs for |
| `foes`, `allies` | the current player's enemies, allies |
| `neutral_players` | players neutral to the current player |
| `all_players` | every player |
| `force_1` .. `force_4` | a force's players |
| `non_allied_victory_players` | players not allied for victory |
| `neutral` | the neutral player |
| `unused_12`, `unused_22` .. `unused_25`, `group_N` | codes 1.17 does not use; kept as they are |

A 1.17 map's slots 1 to 11 are this format's 0 to 10, and its player 12 is `"neutral"`.
Slots 8 to 10, which no 1.17 lobby offers, are written only when the map uses them.

A map whose players from list entry 8 on include a `human` or a `computer` is a Hyperion
map, which only Hyperion plays. Its slots past neutral's are 12 and up, and the `players`
list leaves out slot 11: entry 11 is slot 12, entry 12 slot 13, and so on. A unit's or
doodad's `owner` is the slot's number (0 to 10, then 12 and up) or `"neutral"`. Triggers
name any slot by its number, and the groups reach every slot (Trigger, below). The
compiled map carries those slots in HXPL and their RGB colours in HXRG
(`engine/docs/maps.md`).

## The document

| Field | Type | Default | Meaning |
|---|---|---|---|
| `format` | `"hyperion-map"` | required | |
| `version` | 1 | required | this document's version (see Versions) |
| `name`, `description` | text | none | the map's title and description |
| `tileset` | name or 0-7 | required | `badlands`, `platform`, `installation`, `ashworld`, `jungle`, `desert`, `ice`, `twilight` |
| `size` | `[width, height]` in tiles | required | |
| `expansion` | bool | `true` | the map needs Brood War |
| `text_encoding` | `utf-8` or `cp1252` | `utf-8` | how text goes into a 1.17 map: `cp1252` writes each string in 1.17's code page when it can, as the classic editors did |
| `players` | list of player | | the slots, in order |
| `neutral` | player | inactive | the neutral player's slot record |
| `forces` | list of force | | up to 4 for a 1.17 map |
| `units` | list of unit | `[]` | units placed at the start |
| `doodads` | list of doodad | `[]` | the sprites and unit doodads the map places |
| `locations` | list of location | `[]` | |
| `triggers` | list of trigger | `[]` | |
| `briefing` | list of trigger | none | the mission briefing's triggers |
| `unit_properties` | list | `[]` | the property sets triggers create units with |
| `settings` | object | none | unit, upgrade and tech costs and stats |
| `availability` | object | none | what each player may build and research |
| `sounds` | object | none | the map's sound list: slot to the sound's path in the map |
| `switches` | object | none | switch number (from 1) to its name |
| `layers` | object | | written by the tools: each layer's file and element type |
| `files` | list | | written by the tools: the paths of the map's other files |
| `sections` | list | none | sections of a converted map this format has no field for, as `{"tag", "data"}` (base64), written back as they were |
| `editor` | object | none | `tiles_same_as_game`: the editor's tile layer equals the game's; an editor's own data under its name (`"stardraft": {...}`), which Hyperion never reads |
| `compat` | object | none | what a converted map needs to come back the same: its CHK `chk_version`, `type`, `iver`, `ive2`, `vcod` (`"standard"` or base64), `strx`, `string_count`, and the byte counts of sections that had an unusual size |

### Player

| Field | Default | Meaning |
|---|---|---|
| `slot` | `inactive` | `human` (an open slot for a person), `computer`, `rescuable`, `neutral`, `closed`, `unused`, `inactive`, and 1.17's two in-game kinds `human_occupied`, `computer_game` |
| `race` | `zerg` | `zerg`, `terran`, `protoss`, `user_select`, `random`, `independent`, `neutral`, `inactive` |
| `force` | 0 | the force's index |
| `color` | the slot's own | a colour name (`red`, `blue`, `teal`, `purple`, `orange`, `brown`, `white`, `yellow`, `green`, `pale_yellow`, `tan`, `azure`, Remastered's eleven more, and from 23 on Hyperion's own for the players past 7, `tools/hxmap/names.py`) or number. Players 0 to 7 take 1.17's and Remastered's; past 7 any of them, and with none a player from slot 12 on takes Hyperion's in turn |
| `color_mode`, `rgb` | none | Remastered's colour choice: `random`, `player_choice`, `rgb` with `rgb: [r, g, b]`, or a predefined colour's number |
| `editor_slot` | `slot` | the slot the editor shows, when it differs |

### Force

`{"name": text, "flags": [...]}` with flags from `random_start`, `allies`,
`allied_victory`, `shared_vision`.

### Unit

| Field | Default | Meaning |
|---|---|---|
| `type` | required | a unit name or number |
| `x`, `y` | required | the unit's centre, in pixels |
| `owner` | 0 | a player or `"neutral"` |
| `id` | its index | the unit's number, which `link` refers to |
| `link`, `relation` | 0 | a nydus canal's exit or an add-on's building: `link` is the other unit's `id`; `relation` 0x200 for a nydus link, 0x400 for an add-on |
| `hp`, `shields`, `energy` | 100 | percentages |
| `resources` | 0 | a mineral field's or geyser's amount |
| `hangar` | 0 | |
| `state` | 0 | bits: 1 cloaked, 2 burrowed, 4 lifted, 8 hallucinated, 16 invincible |
| `valid_state`, `valid_props` | 0x1F, 0x3F | which of the state bits and properties the map sets |
| `unused` | 0 | |

A `start_location` unit owned by a player is where that player starts.

### Doodad

`{"sprite": n, "x", "y", "owner"}` for a sprite, or `{"unit": name, "x", "y", "owner"}`
for a unit placed as a doodad, with optional `flags` (bit 0x8000 a disabled doodad) and
`pad`.

### Location

`{"id": n, "name": text, "left", "top", "right", "bottom", "elevation"}`. Ids count from
1; 1.17 maps have 64 (version 59) or 255, and 64 is "Anywhere". `elevation` is the bit set
of elevations the location leaves out.

### Trigger

| Field | Meaning |
|---|---|
| `players` | the players and groups it runs for |
| `conditions` | up to 16 |
| `actions` | up to 64 |
| `flags` | 1.17's trigger flags (bit 2 disabled, among others), when not 0 |
| `player_bytes`, `current_action` | raw values of a converted map, when unusual |

A condition is `{"type": name, ...}` with any of `location` (an id), `player` (a player
or group), `amount`, `unit`, `comparison` (`at_least`, `at_most`, `exactly`), `resource`
(the resource, score kind or switch number, by the type), `flags`, `mask`. An action is
`{"type": name, ...}` with any of `location`, `text`, `wav` (a sound's path), `time` (ms),
`player`, `number` (a second location, an amount, a state, or for
`give_units_to_player` the player receiving them), `unit`, `modifier` (a count or how to
change a value), `flags`, `pad`, `mask`. The names are 1.17's, in `names.py`:
`CONDITIONS`, `ACTIONS`, and `BRIEFING_ACTIONS` for the briefing. A text field that a
converted map pointed at no string is kept as `{"string_id": n}`.

On a Hyperion map, a trigger's `players` and a condition's or action's `player` (and
`give_units_to_player`'s `number`) may be a slot from 12 up, by its number, as for slots
0 to 10. A trigger's `players` takes slots up to 63. Added 2026-10-13 within version 1:
such values were refused before, so no map changes meaning. The compiled map writes a
condition's or action's slot past 11 as the player code 0x100 + slot, which 1.17 never
writes and only a Hyperion game reads, and the slots past 11 a trigger runs for in a
section of its own, HXTE, version 1: a DWORD version, a DWORD count of TRIG's triggers,
then a QWORD for each, bit n for slot n. Briefings run only for slots 0 to 7.

### Unit properties

`[{"slot": 1-64, "valid_state", "valid_props", "owner", "hp", "shields", "energy",
"resources", "hangar", "state", "used": true}]`, each field 0 when left out.

### Settings

`settings.units` maps a unit to `{"hp", "shields", "armor", "build_time", "minerals",
"gas", "name"}` and `settings.weapons` a weapon number to `[damage, bonus]` (the whole
table, whenever a unit is listed). `settings.upgrades` maps an upgrade to `{"minerals",
"minerals_factor", "gas", "gas_factor", "time", "time_factor"}`, and `settings.techs` a
tech to `{"minerals", "gas", "time", "energy"}`. Only what a map overrides is listed; the
rest keeps the game's values. The `*_classic` forms are the ones a map for the original
StarCraft carries.

### Availability

`availability.units`: `global` (unit to 0 or 1, for those not 1) and per player
`available` and `use_global`. `availability.upgrades`: `global_max`, `global_start` and per
player `max`, `start`, `use_global`. `availability.techs`: `global_available`,
`global_researched` and per player `available`, `researched`, `use_global`. Values left out
are the game's defaults: everything available, upgrade levels 0, nothing researched,
players using the global settings.

## Layers

| Layer | Element | Meaning |
|---|---|---|
| `tiles` | u16 per tile, rows top to bottom | the terrain: each value is a tile group times 16 plus the tile's index in it, from the tileset's CV5 |
| `fog` | u8 per tile | the bit set of players who start with the tile unexplored |
| `isom` | u16 | the editor's isometric terrain, as StarEdit and ScmDraft keep it; the game does not read it |
| `editor_tiles` | u16 per tile | the editor's own copy of the terrain, when it differs from `tiles` |
| `editor_doodads` | bytes | the editor's doodad list (DD2) |

All little-endian. The layers are binary because a 512 by 512 map has 262,144 tiles; a
generator writes them with a few lines of code (`tools/hxmap` does it with Python's
`struct`).

## Versions

Version 1 is frozen as of 2026-10-13: every field above keeps its meaning. A field may be
added to version 1 when leaving it out means what version 1 meant; a change to what an
existing field means raises the version. Hyperion and `tools/hxmap` refuse a document of a
newer version than they read, saying so, and ignore fields they do not know.
`Hyperion.exe --hxm-check <map.hxm>` checks a map an editor wrote (`hxm-for-editors.md`).

## Conversion

`from_chk` reads a CHK as the game does (each handler over every copy of its section, in
file order: the last copy wins for most, MTXM and MASK overlay copy by copy, UNIT, THG2,
TRIG and MBRF add up), so a protected map converts to what the game plays. `to_chk` writes
a CHK any 1.17 tool reads when the map is within 1.17's limits: 256 tiles a side, 11 slots
and neutral, 255 locations, 4 forces. String ids are renumbered on the way back; nothing
in the game depends on an id's value.

A map past those limits, more than 256 tiles a side, players from list entry 8 on who take
part, triggers for slots past 11 or a mod's units, gets a VER of its own: 1206, or 1064 when
`expansion` is false. No Blizzard version uses either, so a map site or an editor tells such
a CHK from a classic one by its VER alone, whatever the file is called. Hyperion reads them
as Remastered's 206 and 64, with STR or STRx. A map within the limits keeps the classic
versions (59, 63, 64, 205, 206), and one converted back from 1206 or 1064 gets 205 or 206,
63 or 64.

`python -m tools.hxmap roundtrip` checks every map it is given: the sections the game
reads come back byte for byte, and those with text come back with the same document. The
replay check in `tools/hxmap/check.py` goes further: it plays the suite's replays with
their maps converted to the format and back, and compares every frame with the original.
