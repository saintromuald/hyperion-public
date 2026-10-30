// poollimits.h -- the sizes of the game's object pools.
//
// 1.17 sizes each pool with a literal, and writes it again wherever a loop walks the pool.
// Each has one name here. These are 1.17's sizes, which a game on a 1.17 map keeps; a
// Hyperion game sizes its pools for its players.
#ifndef POOLLIMITS_H
#define POOLLIMITS_H

#include <string.h>  // memcpy

#define POOL_UNITS_117 1700    // sgUnitsMem
#define POOL_FINDER_117 (POOL_UNITS_117 * 2)  // the finder's sorted ends, g_pos_sort_x and _y
#define POOL_SPRITES_117 2500  // sgSprites, and the draw order's iterator over them
#define POOL_IMAGES_117 5000   // sgImages
#define POOL_BULLETS_117 100   // sgBullets
#define POOL_THINGIES_117 500  // sgThingies1Arr_g and sgThingies2Arr_g, each
#define POOL_ORDERS_117 2000   // sgOrders
// The paths units walk by (sai_PathPoolBuf, sai_path.cpp), a unit each while it moves: 1.17's
// 1,024, and in a game past its limits 25,600, more than its units can be
// (revision 15). With 1,024 at most that many units could move
// at once, and a unit ordered past them gave its order up and stood.
#define POOL_PATHS_117 1024
#define POOL_PATHS_HX 25600
// The AI's areas a nation: the captains (captains_alloc), the area states (sgRegions), and
// the region count the pathing merge stops under, so that each region is an area. The
// count also stands for "no area" where an area is looked for.
#define AI_AREAS_117 0x9C4

// The most any game takes: 262,144 units (the user's choice of 2026-10-26), and the
// other pools in 1.17's proportions to its units, rounded up (PoolsForUnits). A Hyperion
// game scales 1.17's sizes by its players over 8, and a benchmark's stress run by k up to
// POOL_SCALE_MAX (--bench-pools). The 64-bit build's storage for the pools and the
// arrays tied to them is sized for these (tools/wide_globals.py, GROWN), about 330 MB, taken
// from the system only as a game's pools fill it; the 32-bit build keeps 1.17's.
#define POOL_UNITS_MAX 262144
#define POOL_SCALE_MAX (POOL_UNITS_MAX / POOL_UNITS_117)
#define POOL_FINDER_MAX (POOL_UNITS_MAX * 2)
#define POOL_SPRITES_MAX ((POOL_UNITS_MAX * POOL_SPRITES_117 + POOL_UNITS_117 - 1) / POOL_UNITS_117)
#define POOL_IMAGES_MAX ((POOL_UNITS_MAX * POOL_IMAGES_117 + POOL_UNITS_117 - 1) / POOL_UNITS_117)
#define POOL_BULLETS_MAX ((POOL_UNITS_MAX * POOL_BULLETS_117 + POOL_UNITS_117 - 1) / POOL_UNITS_117)
#define POOL_THINGIES_MAX ((POOL_UNITS_MAX * POOL_THINGIES_117 + POOL_UNITS_117 - 1) / POOL_UNITS_117)
#define POOL_ORDERS_MAX ((POOL_UNITS_MAX * POOL_ORDERS_117 + POOL_UNITS_117 - 1) / POOL_UNITS_117)

// A unit's id: its 1-based index in the table in the low bits, the slot's generation above
// them (CUnit::GetUnitID). 1.17's holds the index in 11 bits, so 2,047 units, and travels in
// 16 bits in the cargo lists and the commands; a game whose table is larger takes wide ids,
// the index in 24 bits.
#define POOL_UNIT_IDS 2047
#define UNIT_ID_SHIFT_117 11
#define UNIT_ID_SHIFT_WIDE 24


// What a pool's storage holds on this build, which its declarations take: the most above
// on 64 bits, 1.17's on 32.
#if defined(_M_IX86)
#define POOL_UNITS_STORE POOL_UNITS_117
#define POOL_FINDER_STORE POOL_FINDER_117
#define POOL_SPRITES_STORE POOL_SPRITES_117
#define POOL_IMAGES_STORE POOL_IMAGES_117
#define POOL_BULLETS_STORE POOL_BULLETS_117
#define POOL_THINGIES_STORE POOL_THINGIES_117
#define POOL_ORDERS_STORE POOL_ORDERS_117
#else
#define POOL_UNITS_STORE POOL_UNITS_MAX
#define POOL_FINDER_STORE POOL_FINDER_MAX
#define POOL_SPRITES_STORE POOL_SPRITES_MAX
#define POOL_IMAGES_STORE POOL_IMAGES_MAX
#define POOL_BULLETS_STORE POOL_BULLETS_MAX
#define POOL_THINGIES_STORE POOL_THINGIES_MAX
#define POOL_ORDERS_STORE POOL_ORDERS_MAX
#endif

// A pool's records in a save (CImage::SaveImages and the like): 1.17 writes the count of
// slots in use and each record's slot in 2 bytes. A pool of more than 65,535 slots writes
// both in 4. A game's pools are sized before its save is read, so the reader
// knows which.
inline unsigned PoolSaveBytes(int nSlots) {
  return nSlots > 0xFFFF ? 4u : 2u;
}
inline unsigned PoolSaveSlot(const void *p, unsigned nBytes) {
  unsigned n = 0;
  memcpy(&n, p, nBytes);
  return n;
}
inline void PoolSaveSlotPut(void *p, unsigned nSlot, unsigned nBytes) {
  memcpy(p, &nSlot, nBytes);
}

// The sizes of the game being played (port_pools.cpp), which every walk over a pool takes,
// the pool's set-up, its save and the state's hash. Two soft limits follow them: weapon
// 0x67 stops at four fifths of the bullets (CBullet.cpp), and the orders count as near
// their limit at nine tenths (COrder.cpp).
struct PoolSizes {
  int units, finder, sprites, images, bullets, thingies, orders;
  int idShift;  // the unit id's index bits: UNIT_ID_SHIFT_117, or _WIDE past 2,047 units
  int paths;    // POOL_PATHS_117, or _HX in a game past 1.17's limits
};
extern PoolSizes g_pools;
// At the start of game_init and game_minimap_init, before the pools are set up.
void PoolsForGame();
// The pools of a game of nUnits units (POOL_UNITS_117 to POOL_UNITS_MAX): 1.17's proportions,
// rounded up, so 1.17's sizes times k for 1,700 times k. bWide: a Hyperion game.
PoolSizes PoolsForUnits(int nUnits, bool bWide);
// The map's units, for the automatic unit table: PoolsMapBegin as a map's first pass
// starts, and PoolsMapUnit its handler for every UNIT section (maphdr.cpp).
void PoolsMapBegin();
int __stdcall PoolsMapUnit(void *pFile, int nSize, void *pDest);
// The unit table a replay (UNTS) or a save (HXUN) records, as it is read, 0 for none: the
// slots'. And the table to record, 0 when it is the slots'.
void PoolsUnitsRecorded(int nUnits);
int PoolsUnitsToRecord();
// Whether the game takes wide unit ids, past 2,047 units.
inline bool PoolsWideIds() {
  return g_pools.idShift != UNIT_ID_SHIFT_117;
}

#endif
