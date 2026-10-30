// Teams.cpp -- team and nation queries over the player slot table for team
// melee games: slot counts, captains, the team-game tests and the slot
// preparation pass. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>              // memset -- 0x90 and 0x120 are real calls

#include "../h/Teams.h"
#include "../h/net_cmds.h"      // PlayerData -- gPlayerData indexes it
#include "../h/gametype.h"      // GameHeader, IsTVBGame, team_get_slot_captain
#include "../h/net_error.h"     // err_popup
#include "../h/net_slot.h"      // IsHumanPlayerType, IsLegalGameSlotVal
#include "../h/net_chrm.h"      // sub_4719E0
#include "../h/CUnitTSpells.h"  // net_randbetween
#include "../h/victory.h"       // IsOutOfGame
#include "../h/CSprite.h"       // GetMaxPlayersPerTeam (mis-seated there)
#include "../h/TrigEnum.h"      // nation_find_first / nation_find_next
#include "../gen/globals117.h"

// gGameHeader is declared here rather than in gen/globals117.h: a second
// declaration of the struct conflicts (C2371), as gametype.cpp states.
#ifdef OC_DLL_MODE
#define gGameHeader (*reinterpret_cast<GameHeader *>(OC_VA(0x6728D0u)))
#else
extern GameHeader gGameHeader;
#endif

// 0x5264AC, one captain slot per team. Extent 8 is two-sided: PreparePlayerSlots
// zeroes the two BYTE[12] siblings below it and gwMapWidth @0x5264B4 closes it.
// Team ids are 1-based, so every reader bakes 0x5264AB.
#ifdef OC_DLL_MODE
#define gTeamCaptainSlot (reinterpret_cast<BYTE*>(OC_VA(0x5264ACu)))
#else
extern BYTE gTeamCaptainSlot[4];
#endif

// 0x5264A0, the race chosen in each of the twelve player slots. Extent from
// PreparePlayerSlots, which zeroes 0x5264A0..0x5264AC in one movq plus one
// dword store.
#ifdef OC_DLL_MODE
#define gSlotRace (reinterpret_cast<BYTE*>(OC_VA(0x5264A0u)))
#else
extern BYTE gSlotRace[12];
#endif

// The slot with the lowest Storm id among this team's human and computer slots,
// or 0xFF. Ties go to the lower slot because the walk descends and the compare
// is <=.
BYTE FindTeamCaptain(BYTE bTeam) {
  BYTE nCaptain = 0xFF;
  DWORD dwBest = 0xFFFFFFFF;
  int i = 8;

  do {
    --i;

    if ((gPlayerData[i].nType == 2 || gPlayerData[i].nType == 1) &&
        gPlayerData[i].nTeam == bTeam) {
      DWORD dwStormID = gPlayerData[i].dwStormID;

      if (dwStormID <= dwBest) {
        nCaptain = (BYTE)i;
        dwBest = dwStormID;
      }
    }
  } while (i != 0);

  return nCaptain;
}

// Inlined-only helpers: the oracle has no row for either, and GetEmptiestTeam is the
// only caller. Both walk the slot table the way IsComputerTeam does.
static BYTE CountTeamOpenSlots(BYTE bTeam) {
  BYTE nCount = 0;
  int i = 8;

  do {
    --i;

    if (gPlayerData[i].nTeam == bTeam) {
      BYTE nType = gPlayerData[i].nType;

      if (nType == 6 || nType == 9)
        ++nCount;
    }
  } while (i != 0);

  return nCount;
}

static BYTE CountTeamHumanSlots(BYTE bTeam) {
  int nCount = 0;
  int i = 8;

  do {
    --i;

    if (gPlayerData[i].nTeam == bTeam && gPlayerData[i].nType == 2)
      ++nCount;
  } while (i != 0);

  return (BYTE)nCount;
}

// The team a joining player should be put on: the one with the most open slots,
// with the fewest humans breaking a tie.
BYTE GetEmptiestTeam() {
  BYTE nBestCount = 0;
  BYTE nBest = 0;
  int i;

  for (i = 1; i <= GetNumTeams(); ++i) {
    if (CountTeamOpenSlots((BYTE)i) == nBestCount && nBest != 0) {
      if (CountTeamHumanSlots((BYTE)i) < CountTeamHumanSlots(nBest)) {
        nBest = (BYTE)i;
        nBestCount = CountTeamOpenSlots(nBest);
      }
    }

    if (CountTeamOpenSlots((BYTE)i) > nBestCount) {
      nBest = (BYTE)i;
      nBestCount = CountTeamOpenSlots(nBest);
    }
  }

  return nBest;
}

// Index of the first slot on this team waiting on a downloaded map, or 8.
int GetTeamFreeSlot(BYTE bTeam) {
  int i;

  for (i = 0; i < 8; ++i) {
    if (gPlayerData[i].nTeam == bTeam) {
      BYTE nType = gPlayerData[i].nType;

      if (nType == 6 || nType == 9)
        return i;
    }
  }

  return 8;
}

// Two teams play 4 a side, three play 3, four play 2; anything else is a bug and
// pops the error dialog.
BYTE GetMaxPlayersPerTeam() {
  if (gGameHeader.bNumTeams == 2)
    return 4;

  if (gGameHeader.bNumTeams == 3)
    return gGameHeader.bNumTeams;

  if (gGameHeader.bNumTeams == 4)
    return 2;

  err_popup(0x5D, (intptr_t)error_get_str(0x49), (intptr_t)"Teams.cpp", 0x71, 1);
  return 0;
}

// How many human slots on this team belong to a player still in the game.
BYTE GetNumTeamActivePlayers(BYTE bTeam) {
  BYTE nCount = 0;
  int i = 8;

  do {
    --i;

    if (gPlayerData[i].nTeam == bTeam && gPlayerData[i].nType == 2) {
      if (IsValidPlayer((BYTE)gPlayerData[i].dwStormID))
        ++nCount;
    }
  } while (i != 0);

  return nCount;
}

// How many slots on this team are filled by a human or a computer.
BYTE GetNumTeamOccupiedSlots(BYTE bTeam) {
  int nCount = 0;
  int i = 8;

  do {
    --i;

    if (gPlayerData[i].nTeam == bTeam) {
      BYTE nType = gPlayerData[i].nType;

      if (nType == 2 || nType == 1)
        ++nCount;
    }
  } while (i != 0);

  return (BYTE)nCount;
}

BYTE GetNumTeams() {
  return IsTVBGame() ? 2 : gGameHeader.bNumTeams;
}

// Teams are 1-based, so the read is one element below the table.
BYTE __fastcall team_get_slot_captain(BYTE bTeam) {
  return gTeamCaptainSlot[bTeam - 1];
}

// The oracle's `add cx,12h` needs error_get_str's WORD parameter (error.h).
const char *GetTeamName(BYTE bTeam) {
  WORD wStrId = bTeam;

  wStrId += 0x12;

  return error_get_str(wStrId);
}

// How many of this team's slots picked this race. GetMaxPlayersPerTeam is called
// twice and the second call is the loop bound, re-evaluated every iteration
// because it can pop an error dialog.
BYTE __fastcall team_num_slots(BYTE bTeam, BYTE bRace) {
  BYTE nCount = 0;
  BYTE nBase = (bTeam - 1) * GetMaxPlayersPerTeam();
  int i;

  for (i = 0; i < GetMaxPlayersPerTeam(); ++i) {
    if (nBase + i >= 8) {
      if (gSlotRace[nBase] == bRace)
        ++nCount;
    } else {
      if (gSlotRace[nBase + i] == bRace)
        ++nCount;
    }
  }

  return nCount;
}

// True when any slot on this team is held by a computer. The walk runs from the
// top slot down and tests slot 0 before it stops, because the oracle's loop
// bound is the address of slot 0's nType rather than a count.
int IsComputerTeam(BYTE bTeam) {
  int i = 8;

  do {
    --i;

    if (gPlayerData[i].nTeam == bTeam) {
      BYTE nType = gPlayerData[i].nType;

      if (nType == 1 || nType == 5)
        return 1;
    }
  } while (i != 0);

  return 0;
}

// True when a human occupies a slot on this team.
int IsFullPlayerTeam(BYTE bTeam) {
  int i = 8;

  do {
    --i;

    if (gPlayerData[i].nTeam == bTeam && gPlayerData[i].nType == 2)
      return 1;
  } while (i != 0);

  return 0;
}

// True when no slot on this team is a human or a rescuable passive.
int IsNonPlayerTeam(BYTE bTeam) {
  int nCount = 0;
  int i = 8;

  do {
    --i;

    if (gPlayerData[i].nTeam == bTeam) {
      BYTE nType = gPlayerData[i].nType;

      if (nType == 2 || nType == 5)
        ++nCount;
    }
  } while (i != 0);

  return nCount == 0;
}

BOOL IsTeamGame() {
  if (gbMultiPlayerMode && gGameHeader.bNumTeams)
    return TRUE;

  return FALSE;
}

// Rebuild the slot table for a team game: pull each team's captain out, pack the
// rest down, then hand the captains back a slot apiece.
void PrepareTeamSlots() {
  PlayerData aOthers[8];
  PlayerData aCaptains[4];
  BYTE aFree[8];
  BYTE aCaptainSlot[4];
  PlayerData *p;
  PlayerData *q;
  int nCaptains;
  int nOthers;
  int i;
  int j;

  sub_4719E0();

  i = 4;

  do {
    --i;
    aCaptainSlot[i] = FindTeamCaptain((BYTE)(i + 1));
  } while (i != 0);

  memset(aCaptains, 0, sizeof(aCaptains));
  q = aCaptains;
  nCaptains = 0;

  for (i = 0; i < 4; ++i) {
    BYTE nSlot = aCaptainSlot[i];

    if (nSlot != 0xFF) {
      PlayerData *pDst = q++;

      ++nCaptains;
      *pDst = gPlayerData[nSlot];
      ResetSlotFromLobby(nSlot);
    }
  }

  memset(aOthers, 0, sizeof(aOthers));
  q = aOthers;
  nOthers = 0;

  for (i = 0; i < 8; ++i) {
    if (IsLegalGameSlotVal(gPlayerData[i].nType)) {
      if (gPlayerData[i].nType == 1)
        gPlayerData[i].nType = 0;
      else {
        PlayerData *pDst;

        ++nOthers;
        pDst = q++;
        *pDst = gPlayerData[i];
      }
    }
  }

  memset(gTeamCaptainSlot, 0xFF, 4);
  SyncSlots();

  if (nCaptains > 0) {
    p = aCaptains;

    do {
      int nFree = 0;
      BYTE nSlot;

      memset(aFree, 0, sizeof(aFree));

      for (j = 0; j < 8; ++j)
        if (IsLegalChatroomSlotVal(gPlayerData[j].nType))
          aFree[nFree++] = (BYTE)j;

      if (gGameHeader._unmapped_7A == 0)
        nSlot = aFree[net_randbetween(0, nFree - 1, 0x20)];
      else
        nSlot = aFree[0];

      gPlayerData[nSlot] = *p++;
      gPlayerData[nSlot].dwPlayerID = nSlot;
      gTeamCaptainSlot[gPlayerData[nSlot].nTeam - 1] = nSlot;
      --nCaptains;
    } while (nCaptains != 0);
  }

  if (nOthers > 0) {
    p = aOthers;

    do {
      int nFree = 0;
      BYTE nSlot;

      memset(aFree, 0, sizeof(aFree));

      for (j = 0; j < 8; ++j)
        if (!IsLegalGameSlotVal(gPlayerData[j].nType))
          aFree[nFree++] = (BYTE)j;

      nSlot = aFree[0];
      gPlayerData[nSlot] = *p++;
      gPlayerData[nSlot].dwPlayerID = nSlot;
      --nOthers;
    } while (nOthers != 0);
  }

  i = 8;

  do {
    --i;
    g_StormToNationID[i] = 8;
  } while (i != 0);

  i = 8;

  do {
    --i;

    if (IsHumanPlayerType(gPlayerData[i].nType))
      g_StormToNationID[gPlayerData[i].dwStormID] = i;

    if (gPlayerData[i].dwStormID == g_LocalStormID)
      g_LocalNationID = i;
  } while (i != 0);

  g_LocalHumanID = g_LocalNationID;
  memcpy(g_StormToHumanID, g_StormToNationID, 32);

  i = 8;

  do {
    --i;

    if (!IsLegalGameSlotVal(gPlayerData[i].nType))
      gPlayerData[i].nType = 0;
  } while (i != 0);

  if (gPlayerData[g_LocalNationID].nType == 2)
    g_LocalNationID = gTeamCaptainSlot[gPlayerData[g_LocalNationID].nTeam - 1];

  i = 8;

  do {
    --i;
    g_StormToNationID[i] = 8;
  } while (i != 0);

  i = 8;

  do {
    DWORD dwSlot;

    --i;
    dwSlot = g_StormToHumanID[i];

    // The two tests must read nType afresh: one cached BYTE lets MSVC prove the
    // arms exclusive and jump over the second, which the oracle does not do.
    if (gPlayerData[dwSlot].nType == 2)
      g_StormToNationID[i] = gTeamCaptainSlot[gPlayerData[dwSlot].nTeam - 1];

    if (gPlayerData[dwSlot].nType == 9)
      g_StormToNationID[i] = dwSlot;
  } while (i != 0);
}

DWORD __fastcall nation_find_first(int *pnCursor) {
  *pnCursor = -1;

  return nation_find_next(pnCursor);
}

// The next nation after *pnCursor that is still playing. In a team game the walk
// is over the four team captains, otherwise over the eight slots, and both arms
// end at 8. The cursor is reloaded after each increment because the same value
// indexes the table: a memory-operand compare loses that register.
DWORD __fastcall nation_find_next(int *pnCursor) {
  if (gbMultiPlayerMode && gGameHeader.bNumTeams) {
    for (;;) {
      unsigned int nTeam;

      ++*pnCursor;
      nTeam = (unsigned int)*pnCursor;

      if (nTeam >= 4)
        break;

      BYTE nSlot = gTeamCaptainSlot[nTeam];

      if (PlayerIsLobby(nSlot)) {  // Hyperion: nSlot < 8 in 1.17
        BYTE nType = gPlayerData[nSlot].nType;

        if ((nType == 1 || nType == 3 || nType == 4 || nType == 7 || nType == 2) &&
            !IsOutOfGame(nSlot))
          return nSlot;
      }
    }
  } else {
    for (;;) {
      unsigned int nNation;

      ++*pnCursor;
      nNation = (unsigned int)*pnCursor;

      // Hyperion: on to the last lobby slot, past neutral's
      if (nNation >= (unsigned int)PlayerLobbySlots())
        break;
      if (nNation == PLAYER_NEUTRAL)
        continue;

      BYTE nType = gPlayerData[nNation].nType;

      if ((nType == 1 || nType == 3 || nType == 4 || nType == 7 || nType == 2) &&
          !IsOutOfGame((BYTE)*pnCursor))
        return *pnCursor;
    }
  }

  return PlayerLobbySlots();  // Hyperion: 8 in 1.17
}
