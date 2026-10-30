# The maps Hyperion plays

Hyperion plays every map 1.17 plays, as 1.17 plays
it, and maps past 1.17's limits from its own format, `.hxm`
(`../../docs/hxm-format.md`).

## Size

Up to 1,024 tiles a side, wide or tall, any size from 1 up: `MAP_SIDE_MAX` in
`src117/h/maplimits.h`, with what derives from it. 1.17 stops at 256. A Hyperion game keeps
its positions in 31 bits, wrapping as 1.17's 16 do; the screen and the net commands'
coordinates still hold 16 bits, which reach 2,047 tiles, the bound for the next step.

A map within 256 tiles a side keeps 1.17's paths with 1.17's numbers throughout, so its
games, saves and replays are 1.17's; the replay suite holds that. A larger map differs
where 1.17's numbers do not reach:

| What | 1.17's maps | Past 256 tiles |
|---|---|---|
| positions | 16 bits, as 1.17 | 31 bits, wrapping the same way |
| tile maps, sprite rows, repulse grid | 1.17's contents, in tables sized for 1,024 | the same tables |
| minimap | 2:1 at 192 and 256 | 4:1 to 512, 8:1 to 1,024 |
| pathing rows | 256 tiles, a matrix row of 0x400 bytes | 1,024 tiles, 0x1000 bytes |
| contours | 1.17's records of 8 bytes | points and counts of 4 bytes |
| raw regions | 5,000, carved from boxes up to 8 by 7 | 8,176 at a time, boxes up to 16 by 14, carved in stages |
| regions after the merge | under 2,500 | under 2,500, so coarser |
| saving the game | as 1.17 | the pathing tables and contours whole, under a `PATH` version 1.17 refuses |

The 2,500 is the AI's: a captain per nation and area, `nation * 2500 + area`
(`TAI_Captain.cpp`). A larger map whose regions do not merge under it, or whose carve
cannot go on, is refused with 1.17's "too many obstructions" message.

## Players

Up to 64 players, in slots 0 to 10 and 12 to 64, on a map whose HXPL section names
players past 7. `PLAYERS_MAX` in `src117/h/playerlimits.h` is the count;
the tables are sized from it and their types hold 64; `tools/hxmap/convert.py` reads it
from there. Engine code reaches a player's entry of a per-player global by its slot as before:
the global is a player table, 1.17's entries at its address and the new slots' beside
them (`playertables.h`), and `.a` is 1.17's array where code takes it whole. Loops over
the players run to `PlayerSlots()` or `PlayerLobbySlots()` and test `PlayerIsLobby()`;
each is 1.17's bound in a game without the new slots, and the game's own slots in one with
them (one past the last its map gives an owner), so a game plays the same whatever the
build's count. Vision for the slots past 7 is in
`playervision.h`.

A player of an `.hxm` takes `"rgb": [r, g, b]`, a colour in full RGB (stage G). For slots
0 to 7 it goes in CRGB with `"color_mode": "rgb"`, as Remastered's maps have it; for the
slots past 7 in HXRG, version 1, beside HXPL: a DWORD version, the slot count as HXPL's,
then four bytes a slot from 8 on, 1 where it has a colour and its red, green and blue
(`src117/port/gameplay/port_players.cpp`, `tools/hxmap/convert.py`). Such a slot's ramp for the 8-bit
drawing is the nearest to it; the full colour draws the colour itself.

The AI's areas are 2,500 a nation, and its pools of towns, workers and buildings are
1.17's, now shared by up to 64 computers; with every nation's turn in the round-robins,
each computer acts half as often as with 8 for 16 players, a quarter as often for 32 and
an eighth as often for 64.

## Open

- Maps past 1,024 tiles: the screen, the dialogs and the net commands' WORD coordinates
 , and pathing that scales with the map rather than coarsening (stage F; BGH
  tiled 8 by 8 merges to some 2,500 regions, each about 420 tiles).
- The multiplayer game description packs the size in nibbles (`netdesc.cpp`); the new
  multiplayer will carry it whole, and the players past 7.
- The alliance, vision and score dialogs past 8 rows. (Trigger player codes for the slots
  past 11 are in.)
