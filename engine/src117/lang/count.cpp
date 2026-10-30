// count.cpp -- per-player unit and supply counters. Bodies are in oracle VA
// order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playertables.h"  // Hyperion: player slots past 11
#include "../h/mutators.h"  // Hyperion: the supply boost and cap
#include "../h/count.h"
#include "../h/CUnit.h"          // the pUnit fields, CUnit::KillScore
#include "../h/CUnitBuild.h"     // count_remove_completion
#include "../h/CUnitZSpells.h"   // count_add_killed
#include "../h/SAI_Banker.h"     // count_food_available/_get_existence/_get_completion
#include "../h/game.h"           // count_init
#include "../h/gametype.h"       // team_get_slot_captain
#include "../h/Teams.h"          // IsTeamGame
#include "../h/victory.h"        // IsOutOfGame
#include <string.h>
#include <intrin.h>              // _ReadWriteBarrier -- count_unit_exists
#include "../gen/globals117.h"

// Globals this TU reaches, dual-spelled, one #ifdef per name. Before coining a
// name, look for an existing one in sc-refs/sheets.

// 0x5290C4 glGameCounts_g -- registered DWORD[28][12], with a type gen cannot
// parse, so this arm has to declare it. Row index is (offset / 48) and the
// player is the inner index. count_init memsets the whole 0x540 in one call.
#ifndef OC_GLGAMECOUNTS_G
#define OC_GLGAMECOUNTS_G
#ifdef OC_DLL_MODE
#define glGameCounts_g (reinterpret_cast<DWORD (*)[12]>(OC_VA(0x5290C4u)))
#else
extern PlayerGameCountsGrid glGameCounts_g;
#endif
#endif

// 0x529604 glCounts_g -- registered DWORD[4][228][12], the spelling SAI_Build.cpp
// and TrigEnum.cpp already carry. Slice [0] is existence, [1] completion, [2]
// the killer's tally and [3] the victim's. 0x52C0C4 is glCounts_g[1], not a
// global of its own.
#ifndef OC_GLCOUNTS_G
#define OC_GLCOUNTS_G
#ifdef OC_DLL_MODE
#define glCounts_g (reinterpret_cast<DWORD (*)[228][12]>(OC_VA(0x529604u)))
#else
extern PlayerUnitCountsGrid glCounts_g;
#endif
#endif

// 0x523E10 gubUnitFoodUsed -- BYTE per unit type, the sheet name banker.cpp
// already carries. count_unit_exists doubles it for a unit whose type flag
// 0x400 is set.
#ifndef OC_GUBUNITFOODUSED
#define OC_GUBUNITFOODUSED
#ifdef OC_DLL_MODE
#define gubUnitFoodUsed (reinterpret_cast<BYTE *>(OC_VA(0x523E10u)))
#else
extern BYTE gubUnitFoodUsed[228] OC_UNALIGNED;
#endif
#endif

// 0x5240C8 guwUnitScoreMake -- WORD[228], the sheet name CUnitZBuild.cpp
// already carries.
#ifndef OC_GUWUNITSCOREMAKE
#define OC_GUWUNITSCOREMAKE
#ifdef OC_DLL_MODE
#define guwUnitScoreMake (reinterpret_cast<WORD *>(OC_VA(0x5240C8u)))
#else
extern WORD guwUnitScoreMake[228] OC_UNALIGNED;
#endif
#endif



// 0x414060. Adds one completed unit of pUnit's type to its owner's tallies.
void count_add_completion(CUnit *pUnit, int nPlayer)
{
  count_unit_complete(1, pUnit, nPlayer);
}

// 0x414070. Adds one existing unit of pUnit's type to its owner's tallies.
void count_add_existence(CUnit *pUnit)
{
  count_unit_exists(1, pUnit);
}

// 0x4142E0. The race's supply cap for one player, zero for an unknown race.
unsigned __fastcall count_food_max(WORD nUnitType, BYTE bPlayer)
{
  const BYTE bInfo = gubUnitCountInfo[nUnitType];

  if (bInfo & 1)
    return glGameCounts_g[20][bPlayer];
  if (bInfo & 2)
    return glGameCounts_g[23][bPlayer];
  if (bInfo & 4)
    return glGameCounts_g[26][bPlayer];
  return 0;
}

// 0x414320. The supply the race's units already consume. An unknown race
// returns zero rather than indexing off the end. The race groups glGameCounts_g
// in threes: 18/19/20, 21/22/23 and 24/25/26 are provided, used and cap.
int count_get_supplies_used(BYTE bRace, BYTE bPlayer)
{
  switch (bRace) {
    case 0:
      return glGameCounts_g[19][bPlayer];
    case 1:
      return glGameCounts_g[22][bPlayer];
    case 2:
      return glGameCounts_g[25][bPlayer];
  }
  return 0;
}

// 0x414360. The supply the race can still spend: the provided total clamped to
// the cap. The clamp is unsigned, which is what puts a cmovb here.
int count_get_supplies_available(BYTE bRace, BYTE bPlayer)
{
  switch (bRace) {
    case 0: {
      const DWORD dwHave = glGameCounts_g[18][bPlayer];
      const DWORD dwMax = glGameCounts_g[20][bPlayer];
      return dwHave < dwMax ? dwHave : dwMax;
    }
    case 1: {
      const DWORD dwHave = glGameCounts_g[21][bPlayer];
      const DWORD dwMax = glGameCounts_g[23][bPlayer];
      return dwHave < dwMax ? dwHave : dwMax;
    }
    case 2: {
      const DWORD dwHave = glGameCounts_g[24][bPlayer];
      const DWORD dwMax = glGameCounts_g[26][bPlayer];
      return dwHave < dwMax ? dwHave : dwMax;
    }
  }
  return 0;
}

// 0x4143C0. How many units of this type the player has, complete or not.
unsigned __fastcall count_get_existence(WORD unitType, BYTE nation)
{
  return glCounts_g[0][unitType][nation];
}

// 0x4143E0. How many completed units of this type the player has.
unsigned __fastcall count_get_completion(int unitType, BYTE nation)
{
  return glCounts_g[1][(WORD)unitType][nation];
}

// 0x414550. The race a unit type belongs to: 0 Zerg, 1 Terran, 2 Protoss, 4
// none. The return is a BYTE, so the ternary's setz can leave the top of EAX
// holding the unit type and still reach `lea eax, [eax*2+2h]`.
BYTE count_race(int nUnitType)
{
  const BYTE bInfo = gubUnitCountInfo[(WORD)nUnitType];

  if (bInfo & 1)
    return 0;
  if (bInfo & 2)
    return 1;
  if (bInfo & 4)
    return 2;
  return 4;
}

// 0x414580. Removes one completed unit of pUnit's type from its owner's tallies.
void count_remove_completion(CUnit *pUnit, int nDelta)
{
  count_unit_complete(-1, pUnit, nDelta);
}

// 0x414590. Removes one existing unit of pUnit's type from its owner's tallies.
void count_remove_existence(CUnit *pUnit)
{
  count_unit_exists(-1, pUnit);
}

// 0x414080. Credits one kill: the victim's loss tallies, then the killer's,
// skipping the killer half for a self-kill, a neutral killer or one already
// out of the game. The type is a WORD local and the flags test reads the
// member again; that second read gives the oracle's EAX/EDI copy pair.
void __fastcall count_add_killed(CUnit *pUnit, int nPlayer)
{
  WORD nType = pUnit->wType;
  const int nVictim = pUnit->bPlayer;
  const BYTE bInfo = gubUnitCountInfo[nType];

  if (pUnit->dwFlags & 0x40000000)
    return;
  if (gulUnitTypeFlags[pUnit->wType] & 0x10)
    return;

  const BYTE bIsUnit = bInfo & 8;
  if (bIsUnit)
    ++glGameCounts_g[3][nVictim];
  else if (bInfo & 0x10)
    ++glGameCounts_g[10][nVictim];

  const BYTE bIsFactory = bInfo & 0x20;
  if (bIsFactory)
    ++glGameCounts_g[16][nVictim];

  ++glCounts_g[3][nType][nVictim];

  if (nPlayer == nVictim)
    return;
  if (!PlayerIsLobby(nPlayer))  // Hyperion: >= 8 in 1.17
    return;
  if (IsOutOfGame((BYTE)nPlayer))
    return;

  if (bIsUnit || nType == 0x23 || nType == 0x24) {
    ++glGameCounts_g[4][nPlayer];
    glGameCounts_g[6][nPlayer] += pUnit->KillScore();
  } else if (bInfo & 0x10) {
    ++glGameCounts_g[11][nPlayer];
    glGameCounts_g[13][nPlayer] += pUnit->KillScore();
  }

  if (bIsFactory)
    ++glGameCounts_g[17][nPlayer];
  ++glCounts_g[2][nType][nPlayer];
}

// 0x414180. How much more supply this unit type's race can still spend for one
// player. The food cheat replaces the clamped total with the raw cap.
unsigned count_food_available(int unitType, BYTE nation)
{
  const BYTE bInfo = gubUnitCountInfo[(WORD)unitType];
  int nMax = 0;
  int nUsed = 0;

  if (bInfo & 1) {
    nMax = count_food_max(unitType, nation) < glGameCounts_g[18][nation]
         ? count_food_max(unitType, nation) : glGameCounts_g[18][nation];
    nUsed = glGameCounts_g[19][nation];
  } else if (bInfo & 2) {
    nMax = count_food_max(unitType, nation) < glGameCounts_g[21][nation]
         ? count_food_max(unitType, nation) : glGameCounts_g[21][nation];
    nUsed = glGameCounts_g[22][nation];
  } else if (bInfo & 4) {
    nMax = count_food_max(unitType, nation) < glGameCounts_g[24][nation]
         ? count_food_max(unitType, nation) : glGameCounts_g[24][nation];
    nUsed = glGameCounts_g[25][nation];
  }

  if (glCheatBits & 0x20000)
    nMax = count_food_max(unitType, nation);

  return nMax - nUsed;
}

// 0x414400. Clears every counter at game start, then seeds the three supply
// caps: 400 per player, or 400 per team slot for the team captain.
void count_init()
{
  PlayerClear(glGameCounts_g);  // Hyperion: and Hyperion's slots
  PlayerClear(glCounts_g);
  const int nCap = MutatorSupplyCap();  // Hyperion: 1.17's 400, or a mutator's

  if (!IsTeamGame()) {
    for (int i = 0; i < PlayerSlots(); i++) {
      glGameCounts_g[20][i] = nCap;
      glGameCounts_g[23][i] = nCap;
      glGameCounts_g[26][i] = nCap;
    }
    return;
  }

  for (int nTeam = 1; nTeam <= 4; nTeam++) {
    const BYTE bCaptain = team_get_slot_captain((BYTE)nTeam);
    if (bCaptain == 0xFF)
      continue;
    glGameCounts_g[20][bCaptain] = team_num_slots((BYTE)nTeam, 0) * nCap < nCap
        ? nCap : team_num_slots((BYTE)nTeam, 0) * nCap;
    glGameCounts_g[23][bCaptain] = team_num_slots((BYTE)nTeam, 1) * nCap < nCap
        ? nCap : team_num_slots((BYTE)nTeam, 1) * nCap;
    glGameCounts_g[26][bCaptain] = team_num_slots((BYTE)nTeam, 2) * nCap < nCap
        ? nCap : team_num_slots((BYTE)nTeam, 2) * nCap;
  }
}

// 0x4145A0. Moves one completed unit into or out of its owner's tallies. The
// egg types are excluded from the units-built score by a switch whose only two
// arms are "skip the increment" and the default.
// Each switch arm spells the score add; MSVC merges the copies after register
// allocation, and the extra pUnit reads keep pUnit in EDX above the byte pair.
void count_unit_complete(int nDelta, CUnit *pUnit, int nFlag)
{
  if (pUnit->dwFlags & 0x40000000)
    return;
  if (gulUnitTypeFlags[pUnit->wType] & 0x10)
    return;
  const BYTE bPlayer = pUnit->bPlayer;
  const WORD wType = pUnit->wType;
  glCounts_g[1][wType][bPlayer] += nDelta;

  // Hyperion: the supply it gives with a mutator's boost
  const BYTE bInfo = gubUnitCountInfo[pUnit->wType];
  if (bInfo & 1)
    glGameCounts_g[18][bPlayer] += MutatorFoodMade(gubUnitFoodMade[wType]) * nDelta;
  else if (bInfo & 2)
    glGameCounts_g[21][bPlayer] += MutatorFoodMade(gubUnitFoodMade[wType]) * nDelta;
  else if (bInfo & 4)
    glGameCounts_g[24][bPlayer] += MutatorFoodMade(gubUnitFoodMade[wType]) * nDelta;

  if (bInfo & 0x20)
    glGameCounts_g[15][bPlayer] += nDelta;

  if (bInfo & 8)
    glGameCounts_g[1][bPlayer] += nDelta;
  else if (bInfo & 0x10)
    glGameCounts_g[8][bPlayer] += nDelta;

  if (nFlag != 0 && bPlayer != 11) {
    if (bInfo & 8) {
      switch (wType) {
        case 0x2C: case 0x3E: case 0x3F: case 0x44: case 0x67:
          glGameCounts_g[5][bPlayer] += guwUnitScoreMake[pUnit->wType] * nDelta;
          break;
        default:
          glGameCounts_g[2][bPlayer] += nDelta;
          glGameCounts_g[5][bPlayer] += guwUnitScoreMake[pUnit->wType] * nDelta;
          break;
      }
    } else if (bInfo & 0x10) {
      switch (wType) {
        case 0x84: case 0x85: case 0x89: case 0x90: case 0x92:
          glGameCounts_g[12][bPlayer] += guwUnitScoreMake[pUnit->wType] * nDelta;
          break;
        default:
          glGameCounts_g[9][bPlayer] += nDelta;
          glGameCounts_g[12][bPlayer] += guwUnitScoreMake[pUnit->wType] * nDelta;
          break;
      }
    }
  }

  // The barrier keeps the tally read at the clamp, after the row stores.
  _ReadWriteBarrier();
  if ((LONG)glCounts_g[1][wType][bPlayer] < 0)
    glCounts_g[1][wType][bPlayer] = 0;
}

// 0x414760. Moves one existing unit into or out of its owner's tallies. An egg
// charges the supply of whatever is queued inside it, and a unit type carrying
// flag 0x400 charges double unless the unit itself is flagged.
// The queued type is read twice through pUnit so pUnit outranks wType for EDI.
void count_unit_exists(int nDelta, CUnit *pUnit)
{
  if (pUnit->dwFlags & 0x40000000)
    return;
  if (gulUnitTypeFlags[pUnit->wType] & 0x10)
    return;
  const WORD wType = pUnit->wType;

  const BYTE bPlayer = pUnit->bPlayer;
  glCounts_g[0][wType][bPlayer] += nDelta;

  const BYTE bInfo = gubUnitCountInfo[wType];
  if (bInfo & 1) {
    BYTE bFood;
    if (wType == 0x24 || wType == 0x3B || wType == 0x61) {
      bFood = gubUnitFoodUsed[pUnit->wBuildQueue[pUnit->bBuildQueueSlot]];
      if (gulUnitTypeFlags[pUnit->wBuildQueue[pUnit->bBuildQueueSlot]] & 0x400)
        bFood += bFood;
    } else {
      bFood = gubUnitFoodUsed[wType];
      if ((gulUnitTypeFlags[wType] & 0x400) && !(pUnit->dwFlags & 1))
        bFood += bFood;
    }
    glGameCounts_g[19][bPlayer] += bFood * nDelta;
  } else if (bInfo & 2) {
    glGameCounts_g[22][bPlayer] += gubUnitFoodUsed[wType] * nDelta;
  } else if (bInfo & 4) {
    glGameCounts_g[25][bPlayer] += gubUnitFoodUsed[wType] * nDelta;
  }

  if (bInfo & 0x20)
    glGameCounts_g[14][bPlayer] += nDelta;

  if (bInfo & 8)
    glGameCounts_g[0][bPlayer] += nDelta;
  else if (bInfo & 0x10)
    glGameCounts_g[7][bPlayer] += nDelta;
  else if (wType == 0x24 || wType == 0x3B || wType == 0x61)
    glGameCounts_g[0][bPlayer] += nDelta;

  // The barrier keeps the tally read at the clamp, after the row stores.
  _ReadWriteBarrier();
  if ((LONG)glCounts_g[0][wType][bPlayer] < 0)
    glCounts_g[0][wType][bPlayer] = 0;
}
