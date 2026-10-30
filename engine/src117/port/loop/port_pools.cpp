// port_pools.cpp -- the pools' sizes for the game being played (h/poollimits.h).
//
// A game on a 1.17 map, with no player past 7, keeps 1.17's sizes. A Hyperion game takes
// them times k, its players over 8 rounded up: the map's slots, less neutral's (2 for 16
// players, 8 for 64). A game of more than 2,047 units takes wide unit ids, and
// its sprites the draw order's wide key.
//
// The unit table: a game past 1.17's limits, or with a supply cap past 200, holds
// the map's own units and, for each of its players, its supply cap in units and 100 for its
// buildings, rounded up to 1,700s; the unit limit (MutatorSet::nUnits) takes it to at least
// its count. The other pools follow the table in 1.17's proportions, and the AI's pools with
// them (h/aipools.h). A replay and a save record a table other than the slots' (UNTS, HXUN),
// since a save does not read its map again.
#include "../../h/poollimits.h"
#include "../../h/playertables.h"
#include "../../h/pos.h"
#include "../../h/hxrules.h"  // HxLimits
#include "../tools/port_bench.h"  // PortBenchStressGame
#include "../../h/aipools.h"      // g_aiPools
#include "../gameplay/port_mutators.h"  // MutatorsOfGame
#include "../../gen/globals117.h"  // gPlayback, gpLoadGameFile

// Positions wrap to 16 bits as 1.17's do (h/pos.h), and to 31 in a Hyperion game.
int g_posShift = 16;
int g_posMaskU = 0xFFFF;

PoolSizes g_pools = {POOL_UNITS_117, POOL_FINDER_117, POOL_SPRITES_117, POOL_IMAGES_117,
                     POOL_BULLETS_117, POOL_THINGIES_117, POOL_ORDERS_117, UNIT_ID_SHIFT_117,
                     POOL_PATHS_117};

namespace {

// n of a pool 1.17 holds n117 of beside its 1,700 units, for a table of nUnits, rounded up
int Part(int nUnits, int n117) {
  return static_cast<int>((static_cast<long long>(nUnits) * n117 + POOL_UNITS_117 - 1) / POOL_UNITS_117);
}

int g_nMapUnits;                        // the map's UNIT records, as its first pass counts them
int g_nUnitsRecorded;                   // a replay's or save's table, 0 for the slots'
int g_nUnitsOfSlots = POOL_UNITS_117;   // the game's table by its slots alone

// The game's unit table: the slots' nSlots, a replay's or save's own, or the automatic one at
// least the unit limit, for nPlayers players.
int UnitsForGame(int nSlots, int nPlayers, bool bWide) {
  if (gPlayback || gpLoadGameFile != NULL)
    return g_nUnitsRecorded > 0 ? g_nUnitsRecorded : nSlots;
  const MutatorSet &m = MutatorsOfGame();
  if (m.nUnits > 0)
    return m.nUnits > nSlots ? m.nUnits : nSlots;
  // no cap counts as 1,000 supply
  const int nCap = m.nSupplyCap == 0 ? 1000 : m.nSupplyCap;
  if (!bWide && nCap <= 200)
    return nSlots;
  long long ll = g_nMapUnits + static_cast<long long>(nPlayers) * (nCap + 100);
  ll = (ll + POOL_UNITS_117 - 1) / POOL_UNITS_117 * POOL_UNITS_117;
  return ll < nSlots ? nSlots : ll > POOL_UNITS_MAX ? POOL_UNITS_MAX : static_cast<int>(ll);
}

}  // namespace

void PoolsMapBegin() {
  g_nMapUnits = 0;
}

int __stdcall PoolsMapUnit(void *, int nSize, void *) {
  // a UNIT record is 36 bytes; every section of the name counts, as 1.17 reads each
  if (nSize > 0 && g_nMapUnits < POOL_UNITS_MAX)
    g_nMapUnits += nSize / 36;
  return 1;
}

void PoolsUnitsRecorded(int nUnits) {
  g_nUnitsRecorded = nUnits < POOL_UNITS_117 ? 0 : nUnits > POOL_UNITS_MAX ? POOL_UNITS_MAX : nUnits;
}

int PoolsUnitsToRecord() {
  return g_pools.units != g_nUnitsOfSlots ? g_pools.units : 0;
}

PoolSizes PoolsForUnits(int nUnits, bool bWide) {
  if (nUnits < POOL_UNITS_117)
    nUnits = POOL_UNITS_117;
  if (nUnits > POOL_UNITS_MAX)
    nUnits = POOL_UNITS_MAX;
  PoolSizes p;
  p.units = nUnits;
  p.finder = nUnits * 2;
  p.sprites = Part(nUnits, POOL_SPRITES_117);
  p.images = Part(nUnits, POOL_IMAGES_117);
  p.bullets = Part(nUnits, POOL_BULLETS_117);
  p.thingies = Part(nUnits, POOL_THINGIES_117);
  p.orders = Part(nUnits, POOL_ORDERS_117);
  p.idShift = p.units > POOL_UNIT_IDS ? UNIT_ID_SHIFT_WIDE : UNIT_ID_SHIFT_117;
  // a path for every unit that may move: revision 15's 25,600, or a path a unit past that,
  // in a game past 1.17's limits or its unit table
  p.paths = !bWide && nUnits <= POOL_UNITS_117 ? POOL_PATHS_117 : nUnits > POOL_PATHS_HX ? nUnits : POOL_PATHS_HX;
  return p;
}

void PoolsForGame() {
  int k = 1;
  int nPlayers = PLAYER_LOBBY_117;
  if (g_hxPlayers) {
    nPlayers = PlayersMapSlotCount();  // one past the last slot with an owner
    if (nPlayers > PLAYER_NEUTRAL)
      nPlayers--;  // neutral's slot is no player's
    k = (nPlayers + 7) / 8;
    if (k < 1)
      k = 1;
    // a benchmark's stress run takes more than its players do (--bench-pools)
    if (PortBenchStressGame().nPools > k)
      k = PortBenchStressGame().nPools;
    if (k > POOL_SCALE_MAX)
      k = POOL_SCALE_MAX;
  }
  // a Hyperion game (players past 7, a map past 256 tiles a side) keeps 31 bits of a position
  const bool bWide = HxLimits();
  g_nUnitsOfSlots = POOL_UNITS_117 * k;
  g_pools = PoolsForUnits(UnitsForGame(g_nUnitsOfSlots, nPlayers, bWide), bWide);
  // the AI's records: 1.17's in a game of 1.17's limits and table, and past either one of each
  // kind a unit, a town and a script thread for each 17 (revision 17)
  if (bWide || g_pools.units > POOL_UNITS_117)
    g_aiPools = {g_pools.units, Part(g_pools.units, AI_TOWNS_117)};
  else
    g_aiPools = {AI_RECORDS_117, AI_TOWNS_117};
  g_posShift = bWide ? 1 : 16;
  g_posMaskU = bWide ? 0x7FFFFFFF : 0xFFFF;
}
