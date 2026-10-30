# A mod's data: new units, weapons and the rest

A mod can bring types of its own: units, weapons, flingy, sprites, images, orders, techs,
upgrades, sounds and portraits, with their image scripts and art. They come after the original game's
rows, so a game without the mod cannot see them and plays as it did. A mod can also edit any
row already there, the original's or a mod's it depends on (Edits, below), and build on other
mods (`"depends"`, below). The loader is `engine/src117/port/data/port_moddata.cpp`; the test
mod is `engine/mods/hybrid-test/`.

A mod can also change how the game looks without changing what it does: its console, the
minimap's dots and the selection's marks (`hud.json`, below), and the looks its units are drawn
with (`looks.json`, below). The seams the engine has for
modules as well as mods are listed in `engine/docs/modules/overview.md`.

## Where it goes

A mod's `mod.json` names a folder of its own with `"data"`:

    {
      "id": "hybrid-test",
      "name": "Hybrid Test",
      ...
      "data": "data/"
    }

The folder holds `types.json` (the records), and may hold `iscript.txt` (the image scripts)
and the files the records name: GRPs, sounds, portrait videos, icons and wireframes, and its
own cinematics (Cinematics, below). A mod's art is the player's own business: an `art/` folder anywhere in a mod (`data/art/`, say) is
left out of git and out of the
package (`engine/tools/package.py`).

A game has the mod's types when the run is started with `--mod <id>`, or the `"hyperion"`
`"mod"` setting names it. The data loads at start-up; errors go to `hyperion_mods.log` and
a message box, and the run goes on without the mod. `--mod-check <id>` reports the same
errors before anyone plays, and every file that would not play (Checking a mod, below).

A mod may build on others: `"depends"` names them, each by its id, or with the least version
it needs.

    "depends": [{"id": "hybrid-test", "version": "0.2"}]

Their data loads first, in order, each mod after the mods it depends on, then the mod's own
(`PortModChain`, `port_mods.cpp`). The mod's records name theirs as they name the original's,
take ids after theirs, and may edit them. A dependency that is not installed, is older than the
version named, or leads back to the mod itself stops the load, and the message names it.
Versions compare part by part, so `1.10` is after `1.9`. The fingerprint covers the whole
chain, and a replay or save records the mods under it (In a game, below).

## types.json

One object for each table, `units`, `weapons`, `flingy`, `sprites`, `images`, `orders`,
`techs`, `upgrades`, `sounds`, `portraits`, each a map from a record's name to the record:

    {
      "units": {
        "hybrid_reaver": {
          "base": "zerg_ultralisk",
          "name": "Hybrid Reaver",
          "flingy": "hybrid_reaver",
          "ground_weapon": "hybrid_slash",
          "hit_points": 500,
          "shields_enabled": 1,
          "shields": 200,
          "armor": 2
        }
      },
      "weapons": {
        "hybrid_slash": {"base": 40, "damage": 30, "damage_bonus": 3}
      },
      "flingy": {"hybrid_reaver": {"base": 14, "sprite": "hybrid_reaver"}},
      "sprites": {"hybrid_reaver": {"base": 157, "image": "hybrid_reaver"}},
      "images": {"hybrid_reaver": {"base": 50}}
    }

`base` is the row a record starts from: one of the original's rows by its number or its
name, or another of the mod's records in the same table by its name. Every column the record
does not give is the base's, as the mod's edits leave it. A field that names another type (a
unit's `flingy`, a weapon's `flingy`, an image's `script`, and the others marked below) takes
a number or a name the same way. A number must be a row that exists: one of the original's,
the table's "none" (the row after the original's, for units, weapons, orders, techs and
upgrades), or one of the mod's ids; any other fails the load and the log names the field.

Every original row has a name. Units, techs and upgrades have the names `.hxm` maps use
(`zerg_ultralisk`, `stim_packs`, `terran_infantry_armor`; `docs/hxm-format.md`), and orders
names given by hand (`attack_move`, `cast_psionic_storm`); all are in
`tools/hxmap/names.py`. Weapons, flingy, sprites and images have names made from the
original's own files by `tools/hxmap/gen_typenames.py`, kept in `tools/hxmap/typenames.py`:

- a weapon is its label as the game shows it (`gauss_rifle`); where two share a label, the
  later adds the first unit that fires it (`gauss_rifle_jim_raynor_marine`);
- a flingy, a sprite and its image are named after the unit that moves with them
  (`terran_marine`), else the weapon that fires them (`psionic_storm`);
- a unit's building image is the unit's name with `_construction`;
- a sound is its file's path (`terran_marine_tmayes00`, `misc_buzz`), sound 0 being `none`;
- a portrait is the folder its videos are in (`tmarine`, `tghost`);
- any other image is its file's path (`terran_tmashad`, `thingy_tscglow`), with `_shadow` for
  a shadow drawn from another image's frames;
- a name already taken in its table gets `_2`, `_3` and so on.

The engine reads them from `engine/src117/port/data/port_typenames117.h` (`port_typenames.cpp`),
which `tools/hxmap/gen_cpp.py` writes with the other names, so the engine and the tools agree.

Each record gets the next free id of its table, in the order of the records' names, so a
given version of a mod always has the same ids:

| table | 1.17's rows | a mod's first id | the most ids |
|---|---|---|---|
| units | 0-227 | 256 | 4095 |
| weapons | 0-129 | 131 | 2047 |
| flingy | 0-208 | 209 | 2047 |
| sprites | 0-516 | 517 | 8191 |
| images | 0-998 | 999 | 16383 |
| orders | 0-188 | 190 | 1023 |
| techs | 0-43 | 45 | 1023 |
| upgrades | 0-60 | 62 | 1023 |
| sounds | 0-1143 | 1144 | 16383 |
| portraits | 0-109 | 110 | 4095 |

Units start at 256 because 228 to 232 already mean something to 1.17 (no unit, the
trigger groups), and weapons, orders, techs and upgrades after the row that means none
(130, 189, 44 and 61). A mod's techs and upgrades no longer use 1.17's unused rows, which
an EUD map may use. The limits are in `engine/src117/h/typelimits.h`.

An id past 255 is kept in 16 bits where 1.17 has a byte (a unit's order, a building's
research, a bullet's weapon, units.dat's and orders.dat's columns of them), and the commands
that carry one have wide forms a game sends only for such an id. An EUD map still reads
1.17's byte: the low one.

An image whose `grp` the player does not have is drawn as its base, with its base's script.
Where that script has no animation the unit plays (a spell cast on a unit that never casts),
the image keeps the animation it has.

### The fields

The names are those of the DAT files' columns; the values are the files' own, but for a
unit's `hit_points`, which is in whole points (the file keeps 256 times that).

units: `flingy`*, `subunit`*, `subunit2`*, `infestation`*, `construction_image`*,
`direction`, `shields_enabled`, `shields`, `hit_points`, `elevation`, `movement_flags`,
`rank`, `ai_idle_order`*, `idle_order`*, `return_to_idle_order`*, `attack_unit_order`*,
`attack_move_order`*, `ground_weapon`*, `max_ground_hits`, `air_weapon`*, `max_air_hits`,
`ai_internal`, `flags`, `target_range`, `sight_range`, `armor_upgrade`*, `size`, `armor`,
`right_click`, `ready_sound`*, `what_sound_first`*, `what_sound_last`*, `pissed_sound_first`*,
`pissed_sound_last`*, `yes_sound_first`*, `yes_sound_last`*, `placement_box`,
`addon_position`, `bounds`, `portrait`*, `minerals`, `gas`, `build_time`, `requirements`,
`group_flags`, `supply_provided`, `supply_required`, `space_required`, `space_provided`,
`build_score`, `destroy_score`, `map_string`, `broodwar`, `editor_flags`.

weapons: `label`, `flingy`*, `unused`, `target_flags`, `min_range`, `max_range`,
`damage_upgrade`*, `damage_type`, `behavior`, `remove_after`, `explosion`, `inner_splash`,
`middle_splash`, `outer_splash`, `damage`, `damage_bonus`, `cooldown`, `damage_factor`,
`attack_angle`, `launch_spin`, `forward_offset`, `upward_offset`, `target_error`, `icon`.

flingy: `sprite`*, `top_speed`, `acceleration`, `halt_distance`, `turn_radius`, `unused`,
`movement_control`.

sprites: `image`*, `health_bar`, `unknown`, `visible`, `selection_circle`,
`selection_offset`.

images: `grp`, `turns`, `clickable`, `use_full_iscript`, `draw_if_cloaked`,
`draw_function`, `remapping`, `script`*, `shield_overlay`, `attack_overlay`,
`damage_overlay`, `special_overlay`, `landing_dust_overlay`, `liftoff_dust_overlay`.

orders: `label`, `use_weapon_targeting`, `secondary`, `non_subunit`,
`subunit_inheritance`, `subunit_can_use`, `interruptible`, `stop_moving`, `can_be_queued`,
`keep_target_disabled`, `can_be_obstructed`, `fleeable`, `requires_movable`, `weapon`*,
`tech`*, `animation`, `highlight`, `requirements`, `obscured`*.

techs: `minerals`, `gas`, `research_time`, `energy`, `requirements`, `use_requirements`,
`icon`, `label`, `race`, `researched`, `broodwar`.

upgrades: `minerals`, `minerals_factor`, `gas`, `gas_factor`, `time`, `time_factor` (a
level costs the base and the factor times the levels done), `requirements`, `icon`,
`label`, `race`, `max_level`, `broodwar`.

sounds: `file`, `priority`, `flags`, `length_adjust`, `min_volume`.

portraits: `idle`, `talking`, `idle_chances`.

(* names another type.) A field with an array gives a column of more than one value, as
`bounds` and `placement_box` are. A unit's `flags` may be a number or an array of the flags'
names, bit 0 first: `building`, `addon`, `flyer`, `worker`, `subunit`, `flying_building`,
`hero`, `regenerate`, `animated_idle`, `cloakable`, `two_in_egg`, `single_entity`,
`resource_depot`, `resource_container`, `robotic`, `detector`, `organic`,
`requires_creep`, `unused`, `requires_psi`, `burrowable`, `spellcaster`,
`permanent_cloak`, `pickup_item`, `ignores_supply`, `medium_overlays`, `large_overlays`,
`battle_reactions`, `auto_attack`, `invincible`, `mechanical`, `produces_units`.

Fields of the mod's own, not columns:

- `name` (units): the name the game shows; without it, the record's name.
- `looks` (units): the 1.17 unit whose looks the game borrows for what is not in the DAT
  files: the status panel's layout, and the wireframes, command icon, voices and portrait the
  record does not give itself. Without it, the base's.
- `icon`, `wireframe` and `transport_wireframe` (units): the unit's own command icon and
  wireframes, PNG or GRP files, or a unit whose own it shows (Icons and wireframes, below).
- `buttons` (units): the 1.17 unit whose command card the unit has; without it, its
  looks'.
- `grp` (images): the image's frames, a GRP file in the data folder, or a picture of them
  (Art sheets, below). Without it, the image
  draws its base's frames with its base's overlays. With it, the overlays the base has are
  at the frame's centre, and the image has no damage overlay. A `grp` the folder does not
  have (a player who has not made the art) leaves the image its base's frames and its
  base's script, which those frames fit, and the log says so.
- `requires` (units, techs and upgrades): what a player needs to train the unit, research the tech or upgrade the upgrade, in order, each one met:
  `"broodwar"` (an expansion game), a unit's name (one finished), `{"at": unit}` (the
  unit that trains it) or `{"tech": tech}` (researched); a list among them is
  alternatives. The Hybrid Reaver's is `[{"at": "protoss_gateway"},
  "protoss_templar_archives"]`, as 1.17 writes the Dark Templar's. A unit without it is
  trained nowhere. A tech with it is researched where its `{"at": building}` says, for its
  `minerals`, `gas` and `research_time` (`ResearchAllowed` reads it through
  `ModTechResearchTree`); a new game starts it available to every player and researched by
  none, whatever the map says of the row it took (`ModTechsForGame`). Psionic Lance's is
  `[{"at": "protoss_templar_archives"}]`, 150 minerals, 150 gas and 1,800 frames. An
  upgrade with it is upgraded where it says, a level at a time to its `max_level`
  (`UpgradeAllowed` reads it through `ModUpgradeTree`), and a new game starts it at level
  0 of its `max_level` for every player (`ModTechsForGame`). A weapon's `damage_upgrade` or
  a unit's `armor_upgrade` may name it, and its levels then add the weapon's
  `damage_bonus` or a point of armour each, as 1.17's do. Hybrid Claws is
  `[{"at": "protoss_forge"}]`, three levels, and the Hybrid Reaver's weapon names it.
- `use_requires` (techs): what a unit needs to cast the tech, in the same form, with
  `{"caster": unit}` (the unit casting) and `"researched"` (the tech itself). A mod's tech
  takes one of 1.17's unused tech rows and is available to every player; Psionic
  Shockwave's is `[{"caster": "hybrid_reaver"}]`, so no research is needed, and Psionic
  Lance's `[{"caster": "hybrid_reaver"}, "researched"]`, so its button is greyed until
  the research is done.
- `comment`: anything; the loader skips it.

### Edits

A record with `"edit"` changes a row that is there already, in place of adding one. It names
the row as `base` does, and gives the fields it changes; every other column stays as the row
has it.

    "units": {
      "tougher_marine": {"edit": "terran_marine", "hit_points": 60, "name": "Veteran Marine"}
    },
    "weapons": {
      "heavier_rifle": {"edit": "gauss_rifle", "damage": 8}
    }

The record's own name is only a label. Any column of any table can be edited, as can a
unit's `name`, `icon`, `wireframe` and `transport_wireframe`, and the `requires` and
`use_requires` of units, techs and upgrades. An edit has no `base`, `grp`, `looks` or
`buttons`: those make a new row, and a new record has them.

- An edit of one of the original's rows is written for each game that has the mod, after the
  game reads its tables and before the map's own settings, so a map that sets a unit's hit
  points or an upgrade's levels still has its way. A game without the mod has the original's
  rows: images.dat, read once at start-up, is put back for it, and the other tables are read
  again for each game. An edited upgrade's `max_level` becomes each player's most at the
  game's start, as the original's is.
- An edit's `requires` and `use_requires` take the place of the original's tree only; the
  original's other checks stay, a tech the map leaves unavailable among them. A unit's
  `requires` that starts with `{"at": unit}` also moves an add-on to that building.
- An edit's `name` is the name the game shows for the unit, under a name the map gives it.
- An edit of a record of a mod it depends on changes that record, for every record after it
  that names it. A record of the same `types.json` gives its fields itself, so editing one is
  refused.
- A new record based on an edited row starts from the edit. Two edits of a row apply in turn,
  in the order of the mods and, within one, of their names.

### Cards

`cards`, beside the tables, adds buttons to command cards, by the card's unit, in a game
with the mod. A 1.17 unit's card gets the buttons among its own; a mod's unit named here
gets a card of its own, its looks' card with the buttons, so a 1.17 unit that looks the
same keeps its card.

    "cards": {
      "protoss_forge": [
        {"position": 4, "upgrade": "hybrid_claws", "hotkey": "c", "text": "Upgrade Hybrid <C>laws"}
      ],
      "protoss_templar_archives": [
        {"position": 7, "research": "psionic_lance", "hotkey": "l", "text": "Research Psionic <L>ance"}
      ],
      "protoss_gateway": [
        {"position": 5, "train": "hybrid_reaver", "hotkey": "r",
         "text": "Warp in Hybrid <R>eaver",
         "disabled": "Hybrid Reaver Requires:
   Templar Archives"}
      ]
    }

    "cards": {
      "hybrid_reaver": [
        {"position": 7, "cast": "psionic_shockwave", "ai": "enemy_cluster", "hotkey": "c", "text": "Psionic Sho<c>kwave"},
        {"position": 8, "cast": "psionic_lance", "target": true, "ai": "enemy_cluster", "hotkey": "l", "text": "Psionic <L>ance"}
      ]
    }

`position` is the card's slot, 1 to 9, left to right and top to bottom; `train` the mod's
unit to train, `upgrade` the mod's upgrade to upgrade (shown while its next level is allowed),
`research` the mod's tech to research (one with `requires`; the button
shows while the research is allowed, and goes once it is done, as 1.17's do), or `cast` the
mod's order to give, which the button gives at once at the
caster's own place (the button shows while the order's tech's `use_requires` are met), or
with `"target": true` once the player has aimed it at a unit or a place, as 1.17's spell
buttons do (`order_mod_target`, after the energy is checked as `order_tech` checks it);
`ai` on a cast, how a computer's caster casts it (below);
`hotkey` its key, and `text` the button's text with the key's letter written `<R>`,
drawn highlighted; `disabled` the text while the requirements are not met; `icon` a
command icon, the trained unit's own or its looks', or the tech's, by default. The button is put among the card's by position.

### What a mod's type does not do yet

A mod's order is a spell: 1.17's spell order runs it (`DispatchClickAbility`), which
takes the energy of the order's tech, waits until the target is in its weapon's range,
plays the order's animation, and fires the weapon where the script says `castspell`.
Psionic Shockwave's order starts from Psionic Storm's and names its own weapon and tech;
its weapon appears on the caster (fire type 5) and has the map's range, so the cast
happens where the unit stands. Psionic Lance's is the same order aimed: its weapon appears
at the target's place (fire type 4) with a range of 224, so the caster walks into range of
what it was aimed at and casts there, and its explosion 3 splashes enemies only. A mod's
tech is let through `sub_434130`'s target checks whatever it is aimed at; its weapon's
target flags choose what it hits.

A mod's weapon may use explosion 25, which only a mod reaches: the enemy splash of
explosion 3, then a stun of 8 of the paralysis timer's ticks (Maelstrom's) on every
enemy within its outer splash that is on the ground and is no building.

A computer's unit of a mod casts the casts of its card that have an `ai`, tried in the
card's order where 1.17 tries its units' spells (`ai_update_unit_spells`), when it has the
energy and the tech's `use_requires` are met. The `ai` names how it finds a target: one of
1.17's finders by the spell it serves, `psionic_storm`, `yamato_gun`, `plague`,
`dark_swarm`, `ensnare`, `spawn_broodlings`, `parasite`, `stasis_field`, `emp_shockwave`,
`irradiate`, `defensive_matrix`, `disruption_web`, `restoration`, `optical_flare`,
`feedback`, `mind_control`, `maelstrom`, `lockdown`, `hallucination`; or `enemy_cluster`,
Psionic Storm's finder with the spell's own weapon and without its count of the caster's
side, for a spell whose splash spares it (Psionic Storm's never casts beside its own
melee units). An aimed cast goes at the target found; a cast where the caster stands is
given when a target is inside its weapon's outer splash, where `enemy_cluster` searches.
Both of the test mod's casts use `enemy_cluster`.

A mod's unit has its looks' voices, portrait, command icon and wireframes where its record
gives none of its own (below). A computer trains a mod's unit only as a stand-in its race's `ai` names (Races,
below), and researches no mod tech or upgrade.

### Races

`races`, beside the tables, adds races a player can choose. A race plays one of the three
races' ways, its `base`: their way of building, supply, console, sounds and the number a
replay's header keeps for the player. What it has of its own:

    "races": {
      "hybrid": {
        "name": "Hybrid",
        "base": "protoss",
        "start": [{"unit": "protoss_nexus"}, {"unit": "protoss_probe", "count": 4}, {"unit": "hybrid_reaver"}],
        "units": ["hybrid_reaver"],
        "without": ["protoss_dark_templar"],
        "ai": {"protoss_dark_templar": "hybrid_reaver", "psionic_storm": "psionic_lance",
               "protoss_ground_weapons": "hybrid_claws"}
      }
    }

`name` is the name a player sees and `--race` takes, as does the record's own name.
`start` is what a player of it starts a melee game with, in place of its base's town hall
and workers: a building goes where the town hall would, the rest at the start location,
`count` of each (1 by default); without `start` the race starts as its base does. `units`
are the mod's units that are this race's alone: no other player trains them, while a mod
unit no race names is any player's. `without` are 1.17's units its players do not have,
taken out as a map takes units out, so their buttons are not shown. `ai` is for a computer
of the race, which plays 1.17's AI scripts for its base: where a script names a unit of the
left, the computer makes, waits for and attacks with the unit on the right
(`ModRaceAiUnit`, read by each script command that names a unit of the player's own). Without
it, a script that asks for a unit the race goes without waits for it for ever, and so it does
when the unit on the right cannot be made where the script asks: what it needs must be
built by then, and the gas there. 1.17's Protoss scripts open with Zealots and ask for
Dragoons only after the Cybernetics Core, so the test race keeps its Zealots, and its
Hybrid, which needs the Templar Archives and gas, stands in for the Dark Templar, which come
after both.

`ai` maps techs and upgrades the same way: where a script researches one of 1.17's techs
or upgrades on the left, the computer researches the mod's on the right
(`ModRaceAiTech`, `ModRaceAiUpgrade`, read by the scripts' `tech` and `upgrade`), at the
building its `requires` names, as a player would. 1.17's techs and upgrades go by their
names in lower case with underscores, `psionic_storm`, `protoss_ground_weapons` and so on
(`kTechs117`, `kUpgrades117` in `port_moddata.cpp`). The test race researches Psionic
Lance where its scripts research Psionic Storm, which its Hybrids then cast (U9), and
Hybrid Claws in place of the ground weapons.

A race is chosen with `--race <name>` (`engine/docs/hyperion-exe.md`) or in the front
end's skirmish set-up, for the player and for computers (`--computer-race <name>` gives
every computer one). A computer of a mod's race plays 1.17's AI scripts for its base,
with the race's start units and the stand-ins its `ai` names; the race goes to the computer
of the row that chose it, wherever the game seats it (`PlayersSeatedComputer`). A team game, which only
multiplayer offers (single player lists no team types, as 1.17's did not), has the three
races only for now. The replay records each slot's race in a section of its own
(`RACE`) and the save in `HXRA`, and the game played back or loaded has them.

## Sounds and portraits

`sounds` are sfxdata.dat's rows, the game's sounds by number, and `portraits` portdata.dat's,
the talking heads. Both are heard and seen only: no game's state holds them, so neither a
replay nor the fingerprint has their files.

    "sounds": {
      "trooper_ready": {"file": "sounds/ready.ogg"},
      "trooper_yes": {"base": "terran_marine_tmayes00", "file": "sounds/yes.opus"},
      "louder_ready": {"edit": "terran_marine_tmardy00", "file": "sounds/hello.ogg"}
    },
    "portraits": {
      "trooper": {"idle": "portraits/trooper/idle", "talking": "portraits/trooper/talk",
                  "idle_chances": [85, 5, 5, 5]}
    },
    "units": {
      "trooper": {"base": "terran_marine", "portrait": "trooper", "ready_sound": "trooper_ready",
                  "yes_sound_first": "trooper_yes", "yes_sound_last": "trooper_yes",
                  "requires": [{"at": "terran_barracks"}]}
    }

- A sound's `file` is a path in the data folder: WAV, Ogg Vorbis, Opus, MP3 or FLAC, known by
  its first bytes. A WAV of 8 or 16 bits and one or two channels plays as it is; any other file is decoded
  to one as it loads, more than two channels mixed into two. The browser build plays no Opus.
  A file the folder does not have is named in the log and plays as a missing file of the
  original's does: not at all.
- A sound's `priority`, `length_adjust` (milliseconds the talking portrait runs past it) and
  `min_volume` are sfxdata.dat's. Its `flags` are a number or their names: `preload`,
  `one_per_unit` (a unit type says one of its sounds at a time), `one_at_a_time` (the sound
  plays once at a time), `uninterrupted`. A sound with no `base` needs a `file`.
- A portrait's `idle` and `talking` are paths without their end: the game adds the video's
  number and one of `.webm`, `.mkv`, `.mp4`, `.m4v`, `.mov` or `.smk`
  (`portraits/trooper/idle0.webm`), the first the folder has in that order. It plays idle video 0 to 3 by
  `idle_chances`, the chance in 100 of each, and talking video 0 to 2 at random while the unit
  speaks; a video the folder does not have falls to the one numbered below it, so `idle0` and
  `talk0` are enough. Without `talking`, it talks with its idle videos.
- A portrait's video is Smacker, or VP8, VP9, AV1, H.264 or H.265 in WebM, Matroska, MP4 or
  QuickTime, with Vorbis, Opus or AAC sound if any. H.264, H.265 and AAC play through the
  player's system: on Windows always for H.264 and AAC (except Windows' N editions without the
  Media Feature Pack), and for H.265 with Microsoft's HEVC Video Extensions; on a Mac always;
  on Linux when FFmpeg is installed. Where they cannot, the video is not shown and
  `hyperion_video.log` says what is missing, so AV1 or VP9 is the choice that plays
  everywhere. One larger than the
  console's box (60 by 56) is drawn smaller to fit, its shape kept. On the GPU presenter a
  portrait that is not Smacker plays in full colour at its own size, fitted to the box with its
  shape kept (to the HD console's window when Remastered's art is drawn). The browser build
  plays Smacker portraits only.
- A unit's sound fields and `portrait` name the mod's sounds and portraits or the original's
  (`terran_marine_tmayes00`, `tmarine`). An edit of an original sound or portrait changes it
  for every unit that has it, in a game with the mod.
- `iscript.txt`'s `playsnd`, `playsndbtwn` and `playsndrand` take a sound's number or name.

## Cinematics

A mod replaces a cinematic with a file at the cinematic's path in its data folder:
`smk/Terran1.mkv` in place of the first Terran mission's. The names are the classic game's:
`smk/blizzard`, `smk/starintr`, `smk/starXintr`, `smk/PreTerran_1` to `_4`, `smk/Terran1` to
`4`, `smk/PreZerg_2` to `_4`, `smk/Zerg1` to `4`, `smk/PreProtoss_1` to `_3`, `smk/Protoss1`
to `4`, `smk/xProtoss`, `smk/xTerran` and `smk/xZerg`. It takes the first of `.webm`, `.mkv`,
`.mp4`, `.m4v`, `.mov` and `.smk` there, in the mod's folder first and then in those of the mods
it depends on. The video may be anything a portrait's may be (Sounds and portraits, above), at
any size; one the player's system cannot decode is passed over, and the cinematic plays as it
would without the mod.

With the GPU presenter it plays in full colour at its own depth, up to 16 bits, fitted to the
whole window with its shape kept, so a 4K video is shown at 4K on a 4K screen. A video 320
columns wide or less is shown twice as wide, and twice as tall where 480 rows fit that too, as
the classic player shows its own. On SDL's renderer it is put in 256 colours, fitted to the 640
by 480 screen.

The subtitles are Remastered's for the cinematic of that name, when Settings' Cinematic
subtitles is on. A mod whose video says other words puts its own beside it, as the same path
with `.srt` (`smk/Terran1.srt`), in UTF-8; an empty file shows none.

## Icons and wireframes

A unit's `icon` is its command icon (its button, the selection panel's queues) and its
`wireframe` and `transport_wireframe` are what the status panel and a transport's cargo show of
it. Each is a PNG or GRP file in the data folder, or a unit whose own it shows
(`"wireframe": "zerg_zergling"`). An edit's replace the original unit's.

    "trooper": {"base": "terran_marine", "icon": "art/trooper_icon.png",
                "wireframe": "art/trooper_wire.png", "transport_wireframe": "art/trooper_cargo.grp"}

- A PNG is drawn on a clear background: a pixel of alpha under 128 is clear. An icon takes the
  console's shade nearest each pixel's brightness, as its colour by the button's state; a
  wireframe takes its edge as the outline, in the shields' colours, and its inside as the four
  parts damage colours, a quarter each.
- A GRP's first frame is used as it is, its numbers the console's: an icon's shades 1 to 15
  (1 the brightest), a wireframe's 0xC0 and 0xC1 for the outline and 0xD0 to 0xD3 for the
  parts, as 1.17's `cmdicons.grp` and `wirefram.grp` have them.
- An icon is up to 36 by 34 pixels, a wireframe up to 64 by 64 and a transport wireframe up to
  32 by 32; a larger picture is drawn smaller to fit, its shape kept.
- On the GPU presenter the icon and the wireframe are drawn from the picture at the HD scale
  (2x, or 4x with `HYPERION_HD_SCALE=4`): the icon on the command card and in the selection
  panel's queues, the wireframe in the status panel, each at its size above times the scale.
  A PNG at that size (72 by 68 for an icon at 2x, 128 by 128 for a wireframe) keeps all its
  detail; a smaller one is scaled up. The colours are the console's as above, an icon's by
  its brightness and a wireframe's parts in their damage colours by theirs, the outline as
  deep as the scale. A GRP is drawn as its pixels, each the scale's size. The transport's and
  the group's small wireframes stay as they are drawn above. The live value `hd.mods` 0 draws
  them all as above.
- Without `transport_wireframe`, a transport shows the wireframe at half its size. The
  selection of several units always shows it at half its size, as Hyperion does a building's.
- A command card's button that trains the unit and gives no `icon` shows the unit's.
- The files are read as each game with the mod begins (`ArtBuild`, into the console's groups
  through `ModConsoleGroup`); one that does not read is named in the log and the unit keeps
  its looks'. Like sounds they are not in the fingerprint. Over Remastered's HD console they
  are drawn at the HD scale, tinted as its own icons are.

## Art sheets

An image's `grp` may be a picture of its frames, a sheet, in PNG, WebP or TGA. Hyperion makes
the frames from it as the mod loads.

    "trooper": {"base": "terran_marine", "grp": "art/trooper.png", "team": "art/trooper_team.png",
                "frame": [64, 64], "origin": [32, 42.5], "script": "trooper"}

- `frame`: a frame's width and height in game pixels. Frames run left to right, top to
  bottom; a script's frame numbers are row × 17 + direction, as a GRP's.
- `origin`: where the unit stands in a frame, from its top left; the centre without it.
- `directions`: 17, from up round the right to down, the left side drawn mirrored as
  classic's are; or 32, each its own frame, 32 to a row. A script counts 17 to a row either
  way, so one script serves both.
- `team`: a picture of the same size whose bright, opaque pixels take the player's colour.
- `frames`: the count, when the last row is not full.
- `scale`: the picture is this many times the game's size, 1 to 8; `"scale": 4` with a
  `frame` of 96 is cells of 384 pixels.
- A large sheet may be a list of files stacked top to bottom, `["art/a.png", "art/b.png"]`.
  Levels give one file or list for each scale: `{"1": "art/trooper.png", "4": "art/trooper_4x.png"}`.
- Each pixel takes the nearest of the colours every tileset has, and a team pixel the nearest
  step of the player's colour; alpha 128 or more is drawn. An image drawn as a shadow (draw
  function 10) takes every pixel with any alpha.
- A frame may be larger than 255 pixels a side. By Hyperion's rules sight, building sites and
  clicks use its whole size; classic rules hold them at 255.
- A picture is up to 16,384 pixels a side and 128 million pixels. Every file is in the
  fingerprint; a missing one leaves the image its base's frames and script.
- On the GPU presenter a sheet is drawn in full colour at the HD scale (2x, or 4x with
  `HYPERION_HD_SCALE=4`), from the smallest level at that scale or above, else the largest,
  whether or not Remastered's art is drawn. The team colour takes the place of the hue the team
  pixels were drawn in. Until it is ready, and on SDL's renderer, the image shows its classic
  frames. The live value `hd.mods` 0 shows the classic frames everywhere.

`engine/tools/control/directions.py` shows every direction of an image at once: it adds a
unit for each direction to the mod, places them in a ring on the canvas (wider than it is tall,
so it clears the console), and can make a test sheet whose frames carry their direction's
number.

## Art from StarCraft II

`engine/tools/hxunit.py` makes a unit's GRPs from the sheets the local SC2 sprite tool
renders from a player's own StarCraft II install (`tools/local/sc2-sprites`, never
committed): the unit, frame row * 17 + direction, in the palette indexes every tileset
shares with the team colour on 8 to 15; its shadow; and a first script to finish by hand.
They go in the mod's `data/art/`, which stays on the player's machine:

    python engine/tools/hxunit.py <render folder> engine/mods/hybrid-test/data hybrid_reaver --team-glow 80,0.4 --check

`--team-glow` makes a model's bright, saturated pixels the team colour when it has no
team-colour area of its own, as StarCraft II's hybrids have none.

## iscript.txt

The image scripts, in the text iscript editors write, with a script named instead of
numbered (`port_iscript.cpp`):

    # a comment
    .headerstart
    Name          hybrid_reaver
    Type          12
    Init          HybridReaverInit
    Death         HybridReaverDeath
    Walking       HybridReaverWalk
    .headerend

    HybridReaverInit:
            imgul         hybrid_reaver_shadow 0 0
            playfram      0x00
            goto          HybridReaverLocal

An image's `script` names the header's `Name`. An animation the header leaves out, or names
`[NONE]`, has none. An opcode's image, sprite or flingy argument is a number or the name of
one of the mod's records; `useweapon`'s weapon is a number or a weapon's name, the
original's (`gauss_rifle`) or the mod's, and a sound's (`playsnd`) a number or a sound's name.
The scripts are added after 1.17's, with ids from
1000, and may be of any size: a jump, `call` and `useweapon` are written in wider forms of
Hyperion's own, so a mod's scripts go past the 64 KiB 1.17's offsets reach and name weapons
past 255. 1.17's own scripts keep their bytes and offsets.

## hud.json

The data folder, or the mod's own folder when it names none, may hold `hud.json`, the look of
the console and what is drawn in it, read again as each game's console loads; a mod that only
changes these needs no `types.json`. Every section and key may be left out, and what is left
out or wrong stays as 1.17 has it (`hyperion_mods.log` says what was wrong). All of it is
presentation: neither a replay nor the game's state sees it.

### The console

`"console"` is the console's layout (`engine/src117/port/render/port_console.cpp`):

    {
      "console": {
        "art": {"terran": "art/console_terran.pcx", "replay": "art/console_replay.pcx"},
        "toEdges": true,
        "fullMap": false
      }
    }

- `art`: a console image of the mod's own for `zerg`, `terran`, `protoss` or `replay`, in place
  of 1.17's `game\<race>console.pcx`: a PCX of 640 by 480 at 8 bits a pixel, its numbers the
  game's palette's. Its empty pixels (0) are where the map shows and a click is the map's, so
  the image is the console's shape too. It shows with the HD art on as well, whole in the
  middle of a wide view: Remastered's HD console is cut for 1.17's images.
- `toEdges`: false keeps Remastered's HD console whole in the middle of a wide view, where it
  otherwise moves its left part toward the screen's left edge and its right part toward the
  right edge, the dialogs with them.
- `fullMap`: true draws the map to the screen's foot at 640 wide too, as a wider view always
  does; 1.17 draws its 400 rows, so a hole in an image below them shows black.

The dialogs keep the places 1.17's dialog files give them, and draw over the image there.

### The minimap

`"minimap"` is the minimap's look (`engine/src117/port/render/port_minimap.cpp`): the units'
dots in a colour of the mod's, a palette number, by their owner's relation to the player:

    {
      "minimap": {
        "dots": {"own": 117, "allied": 135, "enemy": 111, "neutral": 81}
      }
    }

A relation left out keeps the colour the game gives it, Settings' team or ally colours
included. In a replay, which has no player of its own, the dots keep theirs. The dots' places
and how many there are stay the game's.

### The selection

`"selection"` is the selection's look (`engine/src117/port/render/port_selection.cpp`): whether
the circles under the units the player selected, the circles under a teammate's selection and
the health bars over selected units are drawn:

    {
      "selection": {"circles": true, "teamCircles": true, "healthBars": true}
    }

A mark left undrawn is still there for the game: what is selected, and what a command does to
it, stay the game's.

## looks.json

The same folder may hold `looks.json`, read as each game begins: looks, which draw a unit's
images in other colours and, in full colour, as a crystal or as gas, with a light on the ground
and a plume. Each look in the file changes the built-in look of its name
(`rich-gold`, `rich-violet`) or is added, and its `units` are the unit types drawn with it. Like
`hud.json` it is presentation only, outside the loader and its fingerprint. The fields and an
example are in `engine/docs/modules/render.md`, "Looks"; the live channel's `look` takes the
same fields and `looks_save` writes the file.

## In a game

- A game with a mod's data plays by Hyperion's rules. Its replay and save
  record the mod's id, a fingerprint of its data and that of the mods it depends on
  (`types.json`, `iscript.txt` and the GRPs, SHA-1, in load order; not the sounds, videos,
  icons and wireframes, which are heard and seen only), and those mods' ids and versions. A run without the mod refuses them ("the mod hybrid-test's data, which this
  run did not load (--mod hybrid-test)"), as does a run with another version of it; where
  the mods under it differ, the refusal names both ("the mod my-mod with hybrid-test 0.2,
  where this run has hybrid-test 0.3"), and the replays screen shows them beside the mod.
- A game without the mod has the original's rows only, whatever mod the run loaded.
- `.hxm` maps place a mod's unit by its record's name (`"type": "hybrid_reaver"`); such a
  map loads only with the mod. `tools/hxmap/hybridmap.py` makes the test map.
- The live channel's `spawn` takes a mod's unit by its number or its record's name.
- The replay test's `h3_hybrid` (`engine/tools/replaytest/hxsuite.json`) plays a game with
  the test mod: its `"mod"` field copies the mod into the test install and passes `--mod`
  to the game, its replay and its save's load. `h5_lt_hybrid_race` plays its Hybrid race
  on Lost Temple against a computer.

## Pushed while a game runs

In a Debug Tools canvas game a mod's data can be sent again while the game runs, to try a
change without restarting (the PRD's live mod editing; 2026-10-15). The live
channel's `mod_push` takes `mod`, one of the mods that bring data (`mods` lists them), and
sends its data folder's `types.json`, `iscript.txt` and art; or `types` (the object, or its
text) and `iscript` given in the message, with the art still from the mod's folder or the
`folder` given. Debug Tools' panel has a Mod data section that does either, the text edited
in the page.

- Each push is a submission. Its records take the next ids of their tables after every
  earlier submission's, so a unit on the map keeps the rows it was made from; a name (the
  channel's `spawn`, a command card's button, another record's field) finds the newest.
  A push resets the mod's techs and upgrades for every player.
- `replace` turns units of earlier submissions into the newest record of their name, as a
  morph turns a unit (`CUnit::TransformType`): `"last"` the submission before, `"all"` every
  earlier one, or a submission's number. Their hit points keep their share of the most.
- A push that does not load (a field no table has, a name nothing has, a full table) is
  refused with the reason, and everything stays as it was.
- The answer names the submission, its units' names and ids, and how many records of each
  table it brought; `mod_submissions` lists the game's pushes, and `units` gives each unit's
  submission.
- A push marks the game as changed, and its data is put away when the game ends, so the menus
  and the next game have the data loaded at start-up: what pushes brought never reaches another
  game, a replay or a save. A push that brings races keeps each player's race by its name. The scripts' ids go on
  from the last pushed, as the image scripts' table keeps every script appended.
- The canvas need not have a mod with data: the first push gives it the mod's types.

## Checking a mod

    Hyperion.exe --mod-check <id> [<report.txt>]

loads the mod's data as start-up does, with the mods it depends on, then reads each file its
records name as a game would: the sounds, the portraits' and the cinematics' videos (the first
frame decoded), the sheets' pictures at every level, the icons, wireframes and GRPs. It plays
no game and opens no window. It reads the game's files as a run does (`--install`, `--data`),
and it may run while Hyperion is open: its logs go to the data folder's `mod-check\`.

Each problem is a line, to the report or to standard output without one:

- `error:` what stops the load, with the message start-up shows, or a file that does not
  decode: a sound that is no WAV, Ogg, MP3 or FLAC, a video Hyperion does not read, a picture
  cut short, a GRP whose frames are past its end.
- `warning:` what a game passes over: a name a field gives that nothing has, where the load
  goes on without it (a unit's `looks`); a file a record names that the folder does not have;
  a file passed over for another of the same name (`.webm` is tried before `.mkv`, `.mp4`,
  `.m4v`, `.mov` and `.smk`); a file in `smk/` that is no cinematic's name; subtitles with no
  cue or no video beside them; a video this system does not decode (H.265 without Microsoft's
  HEVC Video Extensions, say). Then, once each, the videos that play through a system's own
  decoders (H.264, H.265, AAC) and those the browser's build does not play (any video but
  Smacker, and Ogg sounds).
- `note:` what loaded: the mod, the records each table gained (their ids, and the range the
  build holds) and edited, and each video's codec, size, depth and frames.

The last line is `plays: 0 errors, 2 warnings`, `plays with errors: ...`, `refused: its data
does not load; ...` or `refused: no such mod`, which lists the mods there are and the folders
left out with why. The exit code is 0 when there is no error, 1 when there is one, and 2 when
no mod has the id.
