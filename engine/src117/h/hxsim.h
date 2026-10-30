// hxsim.h -- the revision of how a Hyperion game plays.
//
// A game by classic rules plays as 1.17 does, whatever the revision. A game by Hyperion's
// rules (h/hxrules.h: players past 7, a map past 256 tiles a side, or a game started so)
// plays by this build's code where 1.17's numbers do not reach, and that code changes. Its
// replays and saves record the revision, and a build of another refuses them, since they
// would not play the game they recorded.
//
// Raise it with any change to what a Hyperion game does, and note the change here.
//
//   1  2026-10-11  the first recorded: up to 16 players, maps up to 512 tiles a side.
//   2  2026-10-11  the slots past 11 in triggers' player groups (TrigEnum.h, TRIG_SLOT_TAG),
//                  and all players without slots 8 to 10 twice; their units let go of a
//                  target they no longer see (RemoveTargetReferencesForPlayers); a player
//                  index past the last slot reads 1.17's block (playerlimits.h).
//   3  2026-10-11  the banker's staged costs are player tables (playertables.h): a nation
//                  past 11 kept its ore and gas costs over the gas and supply costs of
//                  nations 0 to 3.
//   4  2026-10-11  the pools sized for the map's slots (port_pools.cpp): for 16
//                  players 2,047 units, 5,000 sprites, 10,000 images, 200 bullets, 1,000
//                  thingies a pool and 4,000 orders, and the two soft limits with them.
//   5  2026-10-11  unit ids past 2,047: a game of more than 8 slots takes wide
//                  ids, its unit table is 1,700 times its slots over 8 (3,400 for 16
//                  players), and its commands that name a unit carry 32-bit ids.
//   6  2026-10-11  the pools scale by the players over 8, not the slots (neutral's is no
//                  player's): k is 2 for 16 players, where it was 3. The sprites are no
//                  longer held under 8,191 (the draw order's key widens, stage D).
//   7  2026-10-11  a slot past 7 reads its own bits (stage D, step 2): the AI's harvester test
//                  its explored tiles, the blindness tests the blinders past 7, the first
//                  detection every slot's change; a re-initialised unit clears, and an
//                  archon keeps, the casters past 7; a human's death sight and the vision
//                  command keep the bits past 7 and 15. A new bullet starts from a clear
//                  slot, where 1.17's kept fields of the slot's last bullet, which a save
//                  does not keep.
//   8  2026-10-11  a selection is ordered by the whole of the draw order's key, where the
//                  wide key of revision 6 was cut to 32 bits on one side of a comparison
//                  (usel.cpp); a game loaded keeps its players' selections as saved, where
//                  1.17 sent the local player's again and seated it reversed.
//   9  2026-10-12  positions of 31 bits (h/pos.h): a store wraps to 31 bits where
//                  1.17's wrapped to 16, and a WORD position reads the low 31; maps up to
//                  1,024 tiles a side, with the minimap at 8:1 past 512. The contours'
//                  points and counts widen with them, and their search no longer wraps
//                  the doubled far edge of a map of 512 tiles (16,384), which changes
//                  the games on the 512-tile maps; a larger map's save keeps both.
//  10  2026-10-13  triggers for the slots past 11: a map names them by the codes
//                  0x100 + slot and runs triggers for them from HXTE; the Most and Least
//                  conditions and the leaderboard pass them tagged; the leaderboard keeps a
//                  record a slot, where slot 8 and up wrote past its 8 over the trigger
//                  state; mission objectives are a player table, where slot 12 and up wrote
//                  over the countdown; Give Units gives to them; Set Alliance re-sets every
//                  slot's alliance, not 0 to 11's.
//  11  2026-10-13  the searches' budgets on a map past 256 tiles a side
//                  (h/maplimits.h): the search over regions gets 4,000 nodes and a queue
//                  of 2,000, where 1.17's took 350 and 125, and the search near the unit
//                  1,000 entries and a queue of 600, where 1.17's took 250 and 150. And in a
//                  game a Hyperion save keeps, a unit's slot is cleared as it goes back on
//                  the free list, as a load leaves it, where 1.17 left the last unit's
//                  values for the next unit made there (CUnit::RemoveDead).
//  12  2026-10-14  rule sets (h/hxrules.h): a game plays by classic rules or by
//                  Hyperion's, and Hyperion's can be chosen on a map 1.17 could play. The
//                  fixes of revisions 7, 8 and 11 (a new bullet's and a freed unit's slot
//                  cleared, the selections a save holds kept, the last recorded frame
//                  handed over) follow Hyperion's rules, where some followed players past
//                  7 only, so a large map of 8 players or fewer has them now too. The rule
//                  record names the rules and the game's options, and the searches keep
//                  1.17's budgets on a larger map only when the record says so.
//  13  2026-10-15  a worker with cargo finds its depot among its own player's units, the
//                  nearest by 1.17's distance, a tie to the lower slot (CUnitSearches.cpp,
//                  HxNearestDepot), where 1.17 scans outward through every unit in a column
//                  and a row of the map; on a tie, or where the scan stops short, the two
//                  can choose different depots. And the local path search's box is clamped
//                  to the map before it is narrowed to 16 bits (sai_Contours.cpp): within 64
//                  pixels of a 1,024-tile map's bottom or right edge it wrapped and read
//                  outside the region table, which crashed both 1,024-tile games once the
//                  depots changed their course. Smaller maps are as before.
//  14  2026-10-16  the map's width in pixels is its width alone (cell_init): the dword
//                  read of the width brings the height in its high half, which 1.17's WORD
//                  store dropped and a Hyperion game's 31 bits kept, so the map's right edge
//                  stood at 0x20002000 on a 256-tile map and a bullet or a moving unit was
//                  never held at it. And the map's size is set for the map being played
//                  before its starting units are placed (game_init), where 1.17 had it from
//                  the map's preview: the overlord a Zerg start places beside its hatchery
//                  took its side from the last game's map, or from none, so a session's
//                  second game was not the game its replay played (a classic game too).
//  15  2026-10-21  a game past 1.17's limits has 25,600 paths (sai_PathPoolBuf), a unit's
//                  while it moves, where 1.17 has 1,024 (h/poollimits.h): past
//                  1,024 units moving at once, a unit ordered to move found none, gave its
//                  order up and stood, as the armies of a 64-player stress run did. Its save
//                  holds them all. A game within 1.17's limits keeps 1,024.
//  16  2026-10-22  EUD maps (port_eud.h): a new game on a map whose triggers reach
//                  memory through the death counts plays by Hyperion's rules with HX_OPT_EUD,
//                  and every death count then goes through a map of 1.16.1's addresses, with
//                  the masked forms and an address not served stopping the triggers, where
//                  1.17 dropped every write out of range and read past its table. The map's
//                  writes reach the game: its unlimiter grows the pools to Hyperion's
//                  storage, and a command card it points at buttons of its own shows them.
//      2026-10-22  not a revision but an option, so that the replays and saves of 16 still
//                  play: StarCraft: Remastered's order fixes, in a game that records
//                  HX_OPT_ORDER_QUEUE (hxrules.h), which every
//                  new game by Hyperion's rules does. An order the game gives a unit is
//                  dropped when its queue holds 8 or the order pool is at 90%, as a player's
//                  queued command is, unless the unit is undoing a worker's transparency,
//                  where 1.17 appended it whatever the queue held (1.23.5, replay bit BFIX
//                  0x04); and an SCV putting up a building with a full queue still gets the
//                  order that brings it down from the air (1.23.8), which the limit would
//                  drop (CUnit::SetOrder_Target_type_interrupt).
//  17  2026-10-26  the computer players of a game past 8 slots (h/aipools.h): the
//                  AI's pools, which 1.17 holds at 1,000 soldiers, guards, workers and
//                  buildings and 100 towns and script threads for every computer together,
//                  take a record of each kind for every unit of a game past 1.17's limits and
//                  a town and a script thread for every 17 units, where with 32 to 64
//                  computers they filled within minutes and every unit made after stood where
//                  it was made. Each AI frame plans as many nations and expands as many towns
//                  as there are groups of 8 slots (ai_periodic_timer_tick, PlayerAiTurns), and
//                  the captains' round robin serves 333 per 8 slots
//                  (tai_captain_service_round_robin), where 1.17 took one nation and 333
//                  captains a frame whatever the slots. The unit table goes up to 262,144
//                  (h/poollimits.h). A game of 8 slots or fewer on a 1.17 map keeps 1.17's.
//  18  2026-10-26  a computer's unit on its way to a target more than 16 tiles off takes on
//                  an enemy it meets, where the AI's choice between the two prefers it
//                  (CUnit::AIPickAttackTarget; HxMeetOnTheWay). 1.17's soldiers
//                  kept their captain's target, the greatest threat in its region wherever
//                  they stood, and crossed a large map past every army between.
//  19  2026-10-27  a creep tile's picture is drawn from its cell and the frame
//                  (cover_creep_tile_cb, HxCreepRoll), where 1.17 drew it from the C runtime's
//                  random, which the live game also draws for its units' sounds and a replay
//                  does not, so a replay could draw other creep tiles than its game.
//  20  2026-10-27  a mod's types past 1.17's byte (h/typelimits.h): a unit's
//                  orders, research and upgrade, a bullet's weapon, an order's target and
//                  the DAT columns that name them hold ids of 16 bits, up to 2,047 weapons
//                  and flingy, 1,023 orders, techs and upgrades, 4,095 units, 8,191
//                  sprites and 16,383 images; a mod's orders start at 190, its techs at 45
//                  and its upgrades at 62, past 1.17's "none" rows, where they took 1.17's
//                  unused rows; each slot's state of a mod's techs and upgrades is kept
//                  apart from 1.17's tables (PlayerModTech); and a command whose id passes
//                  255 is sent in a wide form (Action 0x96, Placebox 0x8D, Research 0x97,
//                  Upgrade 0x98). The state hash takes each plane of the unit counts' mod
//                  rows, where it took the first plane's rows four times over.
//  21  2026-10-27  a mod's image scripts past 64 KiB: an image's
//                  offsets into the scripts hold 32 bits, a mod's scripts go after
//                  1.17's iscript.bin whole, with jumps, useweapon and headers of wider
//                  forms, and the state hash takes each offset whole.
//  22  2026-10-28  a sprite's draw extents past 255: a sprite whose main image's group
//                  is wider or higher than 255 has that size where classic clamps it, for
//                  a thingy's fog, a building's site and a click (SpriteGrpWidth), so a
//                  mod's frames past 255, made from its sheets, are seen and built round
//                  by their size. Seven of classic's images are 256 a
//                  side (the Ion Cannon, the Xel'Naga Temple and its shadow, a plasma, two
//                  ice trees and an Installation doodad), which grow by a pixel.
#ifndef HXSIM_H
#define HXSIM_H

#define HX_SIM_REVISION 22

#endif
