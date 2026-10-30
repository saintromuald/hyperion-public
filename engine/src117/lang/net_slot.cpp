// net_slot.cpp -- the lobby's slot table: the per-player slot records the game
// and chat-room screens share, the legality predicates over slot values, the
// user-selectable-race mask, the counting family, and the swap and sync the
// chat-room command receivers drive. Functions are in oracle VA order.
//
// A callee defined here can be spliced into a caller here by /Ob2, which the
// oracle (WPO off) never does across a TU; that is what the
// __declspec(noinline) markers are for. SyncSlots and IsLocalSeatRaceSelectable
// are other bodies of this TU inlined, so those callees must stay here.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playertables.h"  // Hyperion: player slots past 11
#include <string.h>        // memset/memcpy -- the race-mask rows

#include "../h/net_slot.h"
#include "../h/net_cmds.h" // PlayerData -- the 36-byte per-player slot record
#include "../h/net_rand.h" // set_rand_enabled -- slot_set_race's tail jump
#include "../h/TrigAct.h"  // IsNamedComputerPlayerType
#include "../h/Teams.h"    // IsTeamGame, GetEmptiestTeam, GetTeamFreeSlot, PrepareTeamSlots
#include "../h/gametype.h" // IsScenarioGame
#include "../h/CUnitCloak.h" // ShuffleColorSlots
#include "../h/replay.h"  // replay_set_selectable_races -- set_selectable_races's tail call
#include "../h/forces.h"  // IsForceRandomStartLocations -- ShuffleForceStartSlots
#include "../h/net_chrm.h"     // sub_471A40 -- sub_479840
#include "../h/CUnitTSpells.h" // net_randbetween -- sub_47A030, AssignRandomRaces
#include "../h/CSprite.h" // GetMaxPlayersPerTeam -- AssignTeamsBySlot, sub_479CE0
#include "../port/gameplay/port_teams.h"  // Hyperion: TeamsStartFor, TeamsStartTaken
#include "../gen/globals117.h"

// The globals this TU reaches, dual-spelled, one #ifdef per name; gPlayerData
// and g_LocalNationID come from gen/globals117.h. There are two slot arrays of
// 36-byte PlayerData records: gPlayerData @0x6726C8, the game slots, and
// gNetPlayerData_g @0x673D48, the 12 chat-room slots. Every walk addresses them
// by .nType (base + 8), so it ends at 0x6726D0 or 0x673D50; maphdr.cpp's
// gPlayers_g at 0x673D50 is the same storage biased by +8.

#ifndef OC_NET_SLOT_gNetPlayerData_g
#define OC_NET_SLOT_gNetPlayerData_g
#ifdef OC_DLL_MODE
#define gNetPlayerData_g (reinterpret_cast<PlayerData *>(OC_VA(0x673D48u)))
#else
extern PlayerTable<PlayerData, PlayerTagNetData> gNetPlayerData_g;
#endif
#endif

// sgbUserSelectableRaces @0x673F08, 8 bytes: every accessor bounds its index with
// `cmp cl, 8h` and clear_selectable_races zeroes eight bytes with one `movq`.
#ifndef OC_NET_SLOT_sgbUserSelectableRaces
#define OC_NET_SLOT_sgbUserSelectableRaces
#ifdef OC_DLL_MODE
#define sgbUserSelectableRaces (reinterpret_cast<BYTE *>(OC_VA(0x673F08u)))
#else
extern BYTE sgbUserSelectableRaces[8];
#endif
#endif

// Globals only sub_47A030 reaches. 0x673F00 is dword_673EF8[2], not a name of
// its own: `mov dword ptr [esi*4+673EF8h], 1h` with ESI in 0..2.
#ifndef OC_NET_SLOT_dword_673EF8
#define OC_NET_SLOT_dword_673EF8
#ifdef OC_DLL_MODE
#define dword_673EF8 (reinterpret_cast<int *>(OC_VA(0x673EF8u)))
#else
extern int dword_673EF8[3];
#endif
#endif

#ifndef OC_NET_SLOT_dword_673F04
#define OC_NET_SLOT_dword_673F04
#ifdef OC_DLL_MODE
#define dword_673F04 (*reinterpret_cast<int *>(OC_VA(0x673F04u)))
#else
extern int dword_673F04;
#endif
#endif

// The bodies, in oracle VA order.

BYTE CountHumanSlots()
{
  BYTE bCount = 0;
  int nSlot = 8;

  while (nSlot--) {
    if (gPlayerData[nSlot].nType == 6)
      ++bCount;
  }

  return bCount;
}

BYTE CountValidSlots()
{
  BYTE bCount = 0;
  int nSlot = 8;

  while (nSlot--) {
    BYTE bType = gNetPlayerData_g[nSlot].nType;

    if (bType == 6 || bType == 5 || bType == 3 || bType == 4 || bType == 7)
      ++bCount;
  }

  return bCount;
}

void set_selectable_race(BYTE bRace)
{
  if (bRace < 8)
    sgbUserSelectableRaces[bRace] = 1;
}

void get_selectable_races(void *pDst, size_t nLen)
{
  memcpy(pDst, sgbUserSelectableRaces, nLen);
}

BOOL IsHumanPlayerType(BYTE bType)
{
  return bType == 2;
}

BOOL IsLegalGameSlotVal(BYTE bVal)
{
  return bVal == 2 || bVal == 1 || bVal == 3 || bVal == 4 || bVal == 7;
}

int is_race_selectable(BYTE bRace)
{
  if (bRace >= 8)
    return 0;

  return sgbUserSelectableRaces[bRace];
}

void clear_selectable_races()
{
  memset(sgbUserSelectableRaces, 0, 8);
}

int slot_set_race(BYTE bRace)
{
  gPlayerData[g_LocalNationID].nRace = bRace;

  set_rand_enabled(1);
  AssignRandomRaces();

  return set_rand_enabled(0);
}

// Re-types the twelve player slots, then
// runs the team, scenario or default setup and the post-setup hook.
// Case 6 and case 8 each store on their own, as in 1.17.
void AssignGameSlots()
{
  int nSlot = PlayerSlots();  // Hyperion: and Hyperion's slots
  int nResult;

  while (nSlot--) {
    switch (gPlayerData[nSlot].nType) {
    case 5:
      gPlayerData[nSlot].nType = 1;
      break;
    case 6:
      gPlayerData[nSlot].nType = 0;
      break;
    case 8:
      gPlayerData[nSlot].nType = 0;
      break;
    }
  }

  nResult = 0;

  if (IsTeamGame())
    PrepareTeamSlots();
  else if (IsScenarioGame(NULL))
    nResult = ShuffleForceStartSlots();
  else
    ShuffleStartSlots(NULL);

  if (gpLoadGameFile == NULL)
    ShuffleColorSlots(nResult);
}

// oracle 0x4794E0 (57 B). An ascending walk with a signed `jl` against the end
// address, so a plain ascending int index; the two `return 8;` paths share one
// block, as an if/else with a common tail does.
int FindLocalSeatSlot()
{
  if (!IsTeamGame()) {
    for (int nSlot = 0; nSlot < 8; nSlot++) {
      if (gPlayerData[nSlot].nType == 6)
        return nSlot;
    }
  } else {
    BYTE bCaptain = GetEmptiestTeam();

    if (bCaptain)
      return GetTeamFreeSlot(bCaptain);
  }

  return 8;
}

// oracle 0x479520 (33 B). gPlayerData[0..7], nType in {1, 5}. A 32-bit
// accumulator (`xor edx, edx` / `inc edx`, then `mov al, dl`), so the counter is
// an int though the return is BYTE; the type is compared twice, so it is held in
// a BYTE local.
BYTE CountDefaultComputerSlots()
{
  int nCount = 0;
  int nSlot = 8;

  while (nSlot--) {
    BYTE bType = gPlayerData[nSlot].nType;

    if (bType == 1 || bType == 5)
      ++nCount;
  }

  return static_cast<BYTE>(nCount);
}

// oracle 0x479550 (25 B). gPlayerData[0..7], nType == 6. CountHumanSlots's walk
// with a 32-bit accumulator counting straight into EAX; the one compare is in
// memory, after the decrement.
BYTE CountDefaultHumanSlots()
{
  int nCount = 0;
  int nSlot = 8;

  while (nSlot--) {
    if (gPlayerData[nSlot].nType == 6)
      ++nCount;
  }

  return static_cast<BYTE>(nCount);
}

// oracle 0x479580 (29 B). The constants are in 1.17's compare order. Declared in
// src117/h/TrigAct.h.
int IsNamedComputerPlayerType(BYTE nType)
{
  return nType == 1 || nType == 3 || nType == 4 || nType == 7;
}

// oracle 0x4795E0 (34 B). The chat-room legality list.
BOOL IsLegalChatroomSlotVal(BYTE bVal)
{
  return bVal == 6 || bVal == 5 || bVal == 3 || bVal == 4 || bVal == 7;
}

// oracle 0x478EA0 (34 B). The chat-room array (`cmp ecx, 673D50h`), nType in
// {1, 5}. An 8-bit accumulator (`xor dl, dl` / `inc dl`), so the counter is a BYTE.
BYTE CountComputerSlots()
{
  BYTE bCount = 0;
  int nSlot = 8;

  while (nSlot--) {
    BYTE bType = gNetPlayerData_g[nSlot].nType;

    if (bType == 1 || bType == 5)
      ++bCount;
  }

  return bCount;
}

// oracle 0x479BF0 (65 B). ResetSlotFromLobby inlined into a 12-iteration
// descending walk, so ResetSlotFromLobby must stay in this TU. The rotated
// `while (n--)` tests `dec`'s flags across the whole copy.
void SyncSlots()
{
  int nSlot = PlayerSlots();  // Hyperion: and Hyperion's slots

  while (nSlot--)
    ResetSlotFromLobby(static_cast<BYTE>(nSlot));
}

// oracle 0x479C40 (57 B). One 36-byte record copy,
// gPlayerData[i] = gNetPlayerData_g[i]. SyncSlots, sub_478C50, sub_478D30 and
// sub_479760 call it, and /Ob2 may splice it into them.
void ResetSlotFromLobby(BYTE bSlot)
{
  gPlayerData[bSlot] = gNetPlayerData_g[bSlot];
}

// oracle 0x479CC0 (29 B). Copies the 8-byte race mask and, outside a replay,
// tail-calls replay_set_selectable_races with the same source. The gPlayback
// read is named in a local before the copy so that the payload build, where the
// two globals may alias, keeps 1.17's order. EDX is set by all three callers and
// never read.
void set_selectable_races(void *pArg0, int nArg1)
{
  DWORD dwPlayback = gPlayback;

  memcpy(sgbUserSelectableRaces, pArg0, 8);

  if (dwPlayback == 0)
    replay_set_selectable_races(pArg0);
}

// oracle 0x47A000 (46 B). A 12-record descending walk over the chat-room array.
// The team clear is unconditional; only the type rewrite sits under the `||`
// chain.
void ResetLobbySlotsOpen()
{
  int nSlot = 12;

  while (nSlot--) {
    BYTE bType = gNetPlayerData_g[nSlot].nType;

    if (bType == 5 || bType == 3 || bType == 4 || bType == 7)
      gNetPlayerData_g[nSlot].nType = 6;

    gNetPlayerData_g[nSlot].nTeam = 0;
  }
}

// oracle 0x479710 (69 B). FindLocalSeatSlot and is_race_selectable inlined, so
// both must stay in this TU. FindLocalSeatSlot's "none found" 8 is folded away:
// is_race_selectable rejects 8, so that path joins the shared `return 0`.
int IsLocalSeatRaceSelectable()
{
  return is_race_selectable(static_cast<BYTE>(FindLocalSeatSlot()));
}

// 0x4793B0 (122 B)
void net_slot_init(DWORD dwLocalId)
{
  int nSlot;

  g_LocalNationID = -1;
  g_LocalHumanID = -1;

  nSlot = 8;
  while (nSlot--) {
    g_StormToNationID[nSlot] = 8;
    g_StormToHumanID[nSlot] = 8;
  }

  nSlot = 8;
  while (nSlot--) {
    if (gPlayerData[nSlot].nType == 2) {
      DWORD dwStormID = gPlayerData[nSlot].dwStormID;

      g_StormToNationID[dwStormID] = nSlot;
      g_StormToHumanID[dwStormID] = nSlot;

      if (dwStormID == dwLocalId) {
        g_LocalNationID = nSlot;
        g_LocalHumanID = nSlot;
      }
    }
  }
}

// 0x478C50 (218 B)
void sub_478C50(BYTE bSlot, int nArg1)
{
  if (gPlayerData[bSlot].nType == 8)
    return;

  BYTE bTeam = gPlayerData[bSlot].nTeam;

  if (nArg1 == bSlot)
    return;

  if (IsTeamGame() && IsComputerTeam(bTeam)) {
    for (int i = 0; i < 8; i++) {
      if (gPlayerData[i].nTeam == bTeam) {
        BYTE bRace = gPlayerData[i].nRace;

        ResetSlotFromLobby((BYTE)i);
        gPlayerData[i].nType = 8;
        gPlayerData[i].nRace = bRace;
      }
    }

    return;
  }

  if (gPlayerData[bSlot].nType != 2) {
    BYTE bRace = gPlayerData[bSlot].nRace;

    ResetSlotFromLobby(bSlot);
    gPlayerData[bSlot].nType = 8;
    gPlayerData[bSlot].nRace = bRace;
  }
}

// 0x479760 (218 B) -- sub_478C50 with the slot type 6 instead of 8.
void sub_479760(BYTE bSlot, int nArg1)
{
  if (gPlayerData[bSlot].nType == 6)
    return;

  BYTE bTeam = gPlayerData[bSlot].nTeam;

  if (nArg1 == bSlot)
    return;

  if (IsTeamGame() && IsComputerTeam(bTeam)) {
    for (int i = 0; i < 8; i++) {
      if (gPlayerData[i].nTeam == bTeam) {
        BYTE bRace = gPlayerData[i].nRace;

        ResetSlotFromLobby((BYTE)i);
        gPlayerData[i].nType = 6;
        gPlayerData[i].nRace = bRace;
      }
    }

    return;
  }

  if (gPlayerData[bSlot].nType != 2) {
    BYTE bRace = gPlayerData[bSlot].nRace;

    ResetSlotFromLobby(bSlot);
    gPlayerData[bSlot].nType = 6;
    gPlayerData[bSlot].nRace = bRace;
  }
}

// 0x478D30 (280 B)
void sub_478D30(BYTE bSlot, int nArg1)
{
  BYTE bType = gPlayerData[bSlot].nType;

  if (bType == 5 || bType == 3 || bType == 4 || bType == 7)
    return;

  BYTE bTeam = gPlayerData[bSlot].nTeam;

  if (IsTeamGame() && gPlayerData[nArg1].nTeam == bTeam)
    return;

  if (!IsTeamGame() && nArg1 == bSlot)
    return;

  if (IsTeamGame()) {
    int i;

    for (i = 0; i < 8; i++) {
      if (gPlayerData[i].nTeam == bTeam && gPlayerData[i].nType == 2)
        return;
    }

    for (i = 0; i < 8; i++) {
      if (gPlayerData[i].nTeam == bTeam) {
        ResetSlotFromLobby((BYTE)i);
        gPlayerData[i].nType = 5;
      }
    }

    return;
  }

  if (gPlayerData[bSlot].nType != 2) {
    ResetSlotFromLobby(bSlot);
    gPlayerData[bSlot].nType = 5;
  }
}

// 0x479630 (210 B)
void swap_slots(BYTE bSlotA, BYTE bSlotB)
{
  BYTE bRaceA = gPlayerData[bSlotA].nRace;
  BYTE bTeamA = gPlayerData[bSlotA].nTeam;
  BYTE bRaceB = gPlayerData[bSlotB].nRace;
  BYTE bTeamB = gPlayerData[bSlotB].nTeam;

    // The slot keeps its own dwPlayerID, nRace and nTeam; only the rest of the
    // record moves. B takes A's record, A takes the chat-room record back.
  gPlayerData[bSlotB] = gPlayerData[bSlotA];
  gPlayerData[bSlotB].nTeam = bTeamB;
  gPlayerData[bSlotB].dwPlayerID = bSlotB;
  gPlayerData[bSlotB].nRace = bRaceB;

  ResetSlotFromLobby(bSlotA);
  gPlayerData[bSlotA].dwPlayerID = bSlotA;
  gPlayerData[bSlotA].nRace = bRaceA;
  gPlayerData[bSlotA].nTeam = bTeamA;

  net_slot_init(g_LocalStormID);
}

// 0x479430 (171 B)
int ShuffleForceStartSlots()
{
  int nResult = 0;

  for (int nForce = 1; nForce <= 4; nForce++) {
    if (IsForceRandomStartLocations((BYTE)nForce)) {
      BYTE bSelectable[8];
      BYTE bOther[8];
      int nSlot;

      memset(bSelectable, 0, 8);
      memset(bOther, 0, 8);

      nSlot = 8;
      while (nSlot--) {
        if (gPlayerData[nSlot].nTeam == nForce) {
          BYTE bType = gNetPlayerData_g[nSlot].nType;

          if (bType == 2 || bType == 1 || bType == 6 || bType == 5) {
            nResult = 1;

            if (is_race_selectable((BYTE)nSlot))
              bSelectable[nSlot] = 1;
            else
              bOther[nSlot] = 1;
          }
        }
      }

      ShuffleStartSlots(bSelectable);
      ShuffleStartSlots(bOther);
    }
  }

  return nResult;
}


// 0x47A030 (259 B)
// Picks a slot index. The int return keeps nPick in ESI, as in 1.17.
int sub_47A030(int nArg0, int nArg1)
{
  if (dword_673F04 == 1) {
    int nCount = 0;

    if (dword_673EF8[0] != 0)
      nCount = (dword_673EF8[1] != 0) + 1;

    dword_673EF8[0] = 0;
    dword_673EF8[1] = 0;
    dword_673EF8[2] = 0;
    dword_673F04 = 3;

    if ((nArg0 == 2 && nArg1 == 3) || (nArg0 == 5 && nArg1 == 6))
      return net_randbetween(0, 2, 0x90);

    return nCount;
  }

  int nPick = net_randbetween(0, dword_673F04 - 1, 0x90);

  if (dword_673F04 == 2) {
    if (nPick == 0) {
      if (dword_673EF8[0] != 0)
        nPick = 1;
    } else if (nPick == 1) {
      if (dword_673EF8[2] == 0)
        nPick = 2;
    }

    if (nArg0 == 1 && nArg1 == 2) {
      if (net_randbetween(0, nArg0 + 6, 0x90) == 4)
        nPick = net_randbetween(0, nArg0 + 1, 0x90);
    }
  }

  dword_673F04--;
  dword_673EF8[nPick] = 1;

  return nPick;
}

// 0x479840 (435 B). In a multiplayer game that is neither a team nor a
// scenario game, retypes slot types 2 and 1 as 6 and 5, packs the saved slots
// back (type 6 first, then 5, 3, 4 and 7) and rebuilds both player tables.
void sub_479840()
{
  if (gbMultiPlayerMode == 0)
    return;

  if (!IsTeamGame() && !IsScenarioGame(NULL)) {
    PlayerData aSaved[8];
    int nCount;
    int i;

    i = 8;
    while (i--) {
      BYTE bType = gPlayerData[i].nType;

      if (bType == 2)
        gPlayerData[i].nType = 6;
      else if (bType == 1)
        gPlayerData[i].nType = 5;
    }

    memcpy(aSaved, gPlayerData.a, sizeof(aSaved));
    memset(gPlayerData.a, 0, 12 * sizeof(PlayerData));

    PlayerData *pDst = gPlayerData.a;
    nCount = 0;

    for (i = 0; i < 8; i++) {
      if (aSaved[i].nType == 6) {
        PlayerData *p = pDst;
        nCount++;
        pDst++;
        *p = aSaved[i];
      }
    }

    for (i = 0; i < 8; i++) {
      BYTE bType = aSaved[i].nType;

      if (bType == 5 || bType == 3 || bType == 4 || bType == 7) {
        gPlayerData[nCount] = aSaved[i];
        sub_471A40((BYTE)nCount, (BYTE)i, 1);
        nCount++;
      }
    }

    {
      PlayerData *p = &gPlayerData.a[12];  // Hyperion: the end of 1.17's 12
      i = 12;
      do {
        --p;
        --i;
        p->dwStormID = -1;
        p->dwPlayerID = i;
        p->nTeam = 0;
      } while (&p->dwStormID != &gPlayerData[0].dwStormID);
    }

    memset(gNetPlayerData_g.a, 0, 12 * sizeof(PlayerData));

    i = 12;
    while (i--) {
      PlayerData &d = gNetPlayerData_g[i];
      d.nRace = gPlayerData[i].nRace;
      d.nType = gPlayerData[i].nType;
      d.dwPlayerID = i;
      d.dwStormID = -1;
      d.nTeam = gPlayerData[i].nTeam;
    }

    return;
  }

  sub_479CE0();
}

#ifndef OC_NET_SLOT_dword_673EF8
#define OC_NET_SLOT_dword_673EF8
#ifdef OC_DLL_MODE
#define dword_673EF8 (reinterpret_cast<int *>(OC_VA(0x673EF8u)))
#else
extern int dword_673EF8[3];
#endif
#endif

#ifndef OC_NET_SLOT_dword_673F04
#define OC_NET_SLOT_dword_673F04
#ifdef OC_DLL_MODE
#define dword_673F04 (*reinterpret_cast<int *>(OC_VA(0x673F04u)))
#else
extern int dword_673F04;
#endif
#endif

// 0x478EE0 (455 B) -- assign the random races.
void AssignRandomRaces()
{
  BYTE bRaces[PLAYER_SLOTS_MAX];  // Hyperion: 8 in 1.17, and its loops run to 8
  int nSwap;
  int nRandom;
  int nSlot;
  int i;
  int j;
  BYTE *pRace;

  dword_673EF8[0] = 0;
  dword_673EF8[1] = 0;
  dword_673EF8[2] = 0;
  dword_673F04 = 3;

  i = 0;
  nRandom = 0;

  nSlot = PlayerLobbySlots();
  while (nSlot--) {
    if (gPlayerData[nSlot].nRace == 6 && nSlot != PLAYER_NEUTRAL) {
      if (gPlayerData[nSlot].nType == 2 || gPlayerData[nSlot].nType == 5)
        ++nRandom;
    }
  }

  nSlot = PlayerLobbySlots();
  while (nSlot--) {
    if (gPlayerData[nSlot].nRace != 6 || nSlot == PLAYER_NEUTRAL)
      continue;

    if (gPlayerData[nSlot].nType == 2 || gPlayerData[nSlot].nType == 5) {
      bRaces[i] = sub_47A030(i, nRandom);
      ++i;
    } else {
      gPlayerData[nSlot].nRace = static_cast<BYTE>(net_randbetween(0, 2, 0x90));
    }

    if (IsTeamGame()) {
      if (gPlayerData[nSlot].nType == 1 || gPlayerData[nSlot].nType == 5) {
        for (j = 0; j < PlayerLobbySlots(); ++j) {
          if (PlayerIsLobby(j) && gPlayerData[j].nTeam == gPlayerData[nSlot].nTeam)
            gPlayerData[j].nRace = gPlayerData[nSlot].nRace;
        }
      }
    }
  }

  for (nSwap = 0; nSwap < 16 && nRandom; ++nSwap) {
    int a = net_randbetween(0, nRandom - 1, 0x90);
    int b = net_randbetween(0, nRandom - 1, 0x90);
    int nTmp = bRaces[a];

    bRaces[a] = bRaces[b];
    bRaces[b] = static_cast<BYTE>(nTmp);
  }

  pRace = bRaces;
  nSlot = PlayerLobbySlots();
  while (nSlot--) {
    if (gPlayerData[nSlot].nRace == 6 && nSlot != PLAYER_NEUTRAL) {
      if (gPlayerData[nSlot].nType == 2 || gPlayerData[nSlot].nType == 5)
        gPlayerData[nSlot].nRace = *pRace++;
    }
  }
}

// gGameHeader, spelled as src117/lang/gametype.cpp spells it. 0x67294A is
// gGameHeader + 0x7A, GameHeader::_unmapped_7A, a flag ShuffleColorSlots,
// PrepareTeamSlots and ShuffleStartSlots test with `cmp byte ptr [0x67294A], 0h`.
#ifndef OC_NET_SLOT_gGameHeader
#define OC_NET_SLOT_gGameHeader
#ifdef OC_DLL_MODE
#define gGameHeader (*reinterpret_cast<GameHeader *>(OC_VA(0x6728D0u)))
#else
extern GameHeader gGameHeader;
#endif
#endif

#ifndef OC_NET_SLOT_gNetPlayerData_g
#define OC_NET_SLOT_gNetPlayerData_g
#ifdef OC_DLL_MODE
#define gNetPlayerData_g (reinterpret_cast<PlayerData *>(OC_VA(0x673D48u)))
#else
extern PlayerTable<PlayerData, PlayerTagNetData> gNetPlayerData_g;
#endif
#endif

// 0x479A00 (485 B)
void ShuffleStartSlots(BYTE *pMask)
{
  // Hyperion: 8 slots in 1.17; with no mask, on a Hyperion map, every lobby slot
  //
  PlayerData tmp[PLAYER_SLOTS_MAX];
  BYTE abFrom[PLAYER_SLOTS_MAX];  // Hyperion: each record's lobby slot
  BYTE bList[PLAYER_SLOTS_MAX];
  BYTE bDefault[PLAYER_SLOTS_MAX];
  int n;
  int i;
  int j;
  int k;
  int m;
  const int nSlots = pMask == NULL ? PlayerLobbySlots() : PLAYER_LOBBY_117;

  memset(bDefault, 1, sizeof bDefault);
  bDefault[PLAYER_NEUTRAL] = 0;

  if (pMask == NULL) {
    pMask = bDefault;

    // The two net_slot_init calls are one block in 1.17, tail-merged.
    if (gGameHeader._unmapped_7A != 0) {
      net_slot_init(g_LocalStormID);
      return;
    }
  }

  memset(tmp, 0, sizeof tmp);

  n = 0;
  for (i = 0; i < nSlots; ++i) {
    BYTE t = gPlayerData[i].nType;

    if ((t == 2 || t == 1 || t == 3 || t == 4 || t == 7) && pMask[i] != 0) {
      abFrom[n] = static_cast<BYTE>(i);
      memcpy(&tmp[n++], &gPlayerData[i], sizeof(PlayerData));
    }
  }

  for (j = 0; j < nSlots; ++j) {
    if (pMask[j] != 0) {
      BYTE b = static_cast<BYTE>(j);

      gPlayerData[b] = gNetPlayerData_g[b];
    }
  }

  // Hyperion: on a Hyperion map the humans keep to slots 0 to 7, where 1.17 seats
  // them (net_slot_init), so they are placed first: the walk takes tmp from its end, and
  // the humans go there
  if (nSlots > PLAYER_LOBBY_117) {
    PlayerData order[PLAYER_SLOTS_MAX];
    BYTE abOrderFrom[PLAYER_SLOTS_MAX];
    int nOrder = 0;
    for (int h = 0; h < 2; ++h)
      for (i = 0; i < n; ++i)
        if ((tmp[i].nType == 2) == (h == 1)) {
          abOrderFrom[nOrder] = abFrom[i];
          order[nOrder++] = tmp[i];
        }
    memcpy(tmp, order, sizeof(PlayerData) * n);
    memcpy(abFrom, abOrderFrom, n);
  }

  while (n--) {
    BYTE bSlot;
    // Hyperion: a start the set-up bound this player to, which the others leave
    // (port_teams.h); with none bound the walk is 1.17's
    const int nBound = TeamsStartFor(abFrom[n], tmp[n].nType);

    for (int bLeave = 1; bLeave >= 0; --bLeave) {
      m = 0;
      memset(bList, 0, sizeof bList);

      for (k = 0; k < nSlots; ++k) {
        BYTE t = gPlayerData[k].nType;

        if (tmp[n].nType == 2 && k >= PLAYER_LOBBY_117)
          break;
        if ((t == 6 || t == 5 || t == 3 || t == 4 || t == 7) && pMask[k] != 0 &&
            (!bLeave || (nBound >= 0 ? k == nBound : !TeamsStartTaken(k)))) {
          bList[m] = static_cast<BYTE>(k);
          ++m;
        }
      }
      if (m != 0)
        break;
    }

    bSlot = nBound >= 0 && m == 1 && bList[0] == nBound ? static_cast<BYTE>(nBound)
                                                        : bList[net_randbetween(0, m - 1, 0x21)];
    gPlayerData[bSlot] = tmp[n];
    gPlayerData[bSlot].dwPlayerID = bSlot;
    PlayersShuffleNote(bSlot, abFrom[n]);  // Hyperion:
  }

  n = nSlots;
  while (n--) {
    BYTE t = gPlayerData[n].nType;

    if (t != 2 && t != 1 && t != 3 && t != 4 && t != 7 && n != PLAYER_NEUTRAL)
      gPlayerData[n].nType = 0;
  }

  net_slot_init(g_LocalStormID);
}

// 0x478A30 (544 B)
void AssignTeamsBySlot()
{
  int nSeats[8];
  int nTeam;
  int i;
  int j;
  int k;
  int nCount;
  int nSubType;

  if (gPlayback != 0)
    return;

  if (!IsTeamGame() && !IsTVBGame())
    return;

  i = 0;
  if (IsTeamGame()) {
    for (nTeam = 0; nTeam < GetNumTeams(); ++nTeam) {
      for (j = 0; j < GetMaxPlayersPerTeam(); ++j) {
        if (i < 8) {
          gNetPlayerData_g[i].nType = 6;
          gNetPlayerData_g[i].nRace = gNetPlayerData_g[0].nRace;
          gNetPlayerData_g[i].nTeam = static_cast<BYTE>(nTeam + 1);
          ++i;
        }
      }
    }
  }

  if (!IsTVBGame())
    return;

  nSubType = gametype_get_subtype();
  k = 0;
  nCount = 0;

  for (; nCount < nSubType; ++k) {
    BYTE t;

    if (k >= 8)
      break;

    t = gNetPlayerData_g[k].nType;
    if (t == 6 || t == 5 || t == 3 || t == 4 || t == 7) {
      nSeats[k] = 1;
      ++nCount;
    }
  }

  for (; k < 8; ++k) {
    BYTE t = gNetPlayerData_g[k].nType;

    if (t == 6 || t == 5 || t == 3 || t == 4 || t == 7)
      nSeats[k] = 2;
  }

  for (j = 0; j < 8; ++j) {
    if (gNetPlayerData_g[j].nType != 0) {
      gNetPlayerData_g[j].nType = 6;
      gNetPlayerData_g[j].nRace = gNetPlayerData_g[0].nRace;
      gNetPlayerData_g[j].nTeam = static_cast<BYTE>(nSeats[j]);
    }
  }
}

// 0x5264AC -- a BYTE[4], team number (1-based) to slot index:
// team_get_slot_captain reads [eax+5264ABh] and nation_find_next bounds the
// index below 4.
#ifndef OC_NET_SLOT_byte_5264AC
#define OC_NET_SLOT_byte_5264AC
#ifdef OC_DLL_MODE
#define byte_5264AC (reinterpret_cast<BYTE *>(OC_VA(0x5264ACu)))
#else
extern BYTE byte_5264AC[4];
#endif
#endif

// 0x4790B0 (649 B)
void SeatLoadedGameSlots()
{
  PlayerData saved[8];
  int nIdx;
  int i;
  BYTE bTeam;
  int m;
  BYTE bCap;

  if (gbMultiPlayerMode == 0)
    return;

  i = 12;
  while (i--) {
    switch (gPlayerData[i].nType) {
    case 5:
      gPlayerData[i].nType = 1;
      break;
    case 6:
      gPlayerData[i].nType = 0;
      break;
    case 8:
      gPlayerData[i].nType = 0;
      break;
    }
  }

  memcpy(saved, gPlayerData.a, sizeof(saved));
  memset(gPlayerData.a, 0, 0x1B0);

  i = 8;
  while (i--) {
    BYTE t = saved[i].nType;

    if (t == 2 || t == 1 || t == 3 || t == 4 || t == 7)
      gPlayerData[GetSaveSlot(static_cast<BYTE>(i))] = saved[i];
  }

  i = 12;
  while (i--) {
    if (gPlayerData[i].nType != 2)
      gPlayerData[i].dwStormID = -1;

    gPlayerData[i].dwPlayerID = i;
  }

  net_slot_init(g_LocalStormID);

  if (!IsTeamGame())
    return;

  for (m = 0; m < 4; ++m) {
    PlayerData t;

    bTeam = static_cast<BYTE>(m + 1);

    if (!IsComputerTeam(bTeam) && !IsFullPlayerTeam(bTeam))
      continue;
    if (IsComputerTeam(bTeam))
      continue;
    if (!IsFullPlayerTeam(bTeam))
      continue;

    nIdx = byte_5264AC[m];
    if (gPlayerData[nIdx].nType == 2)
      continue;

    // bCap is a BYTE, as in 1.17, which keeps the index, not the captain,
    // spilled across the swap.
    bCap = FindTeamCaptain(bTeam);
    nIdx = byte_5264AC[m];

    t = gPlayerData[nIdx];
    gPlayerData[nIdx] = gPlayerData[bCap];
    gPlayerData[bCap] = t;
    gPlayerData[bCap].dwPlayerID = bCap;
    gPlayerData[nIdx].dwPlayerID = nIdx;
  }

  net_slot_init(g_LocalStormID);

  if (gPlayerData[g_LocalNationID].nType == 2)
    g_LocalNationID = byte_5264AC[gPlayerData[g_LocalNationID].nTeam - 1];

  i = 8;
  while (i--)
    g_StormToNationID[i] = 8;

  i = 8;
  while (i--) {
    int p = g_StormToHumanID[i];

    if (gPlayerData[p].nType == 2)
      g_StormToNationID[i] = byte_5264AC[gPlayerData[p].nTeam - 1];
  }
}

// 0x526494 and 0x5264A0 -- BYTE[12] each, one byte per slot; PreparePlayerSlots
// clears both (8 + 4 bytes each), and 0x5264A0 + 12 is byte_5264AC.
// 0x534890 sgbPlayerForce_s -- BYTE[8], forces.cpp's name; sub_479CE0 reads it a
// byte at a time.
#ifndef OC_NET_SLOT_byte_526494
#define OC_NET_SLOT_byte_526494
#ifdef OC_DLL_MODE
#define byte_526494 (reinterpret_cast<BYTE *>(OC_VA(0x526494u)))
#else
extern BYTE byte_526494[24] OC_UNALIGNED;
#endif
#endif

#ifndef OC_NET_SLOT_byte_5264A0
#define OC_NET_SLOT_byte_5264A0
#ifdef OC_DLL_MODE
#define byte_5264A0 (reinterpret_cast<BYTE *>(OC_VA(0x5264A0u)))
#else
extern BYTE byte_5264A0[12];
#endif
#endif

#ifndef OC_NET_SLOT_sgbPlayerForce_s
#define OC_NET_SLOT_sgbPlayerForce_s
#ifdef OC_DLL_MODE
#define sgbPlayerForce_s (reinterpret_cast<BYTE *>(OC_VA(0x534890u)))
#else
extern BYTE sgbPlayerForce_s[8];
#endif
#endif

// 0x479CE0 (789 B)
// Reading the scenario teams through the pSrc local keeps the loop's exit test
// in the payload build, where indexing sgbPlayerForce_s directly loses it.
void sub_479CE0()
{
  PlayerData saved[8];
  int n;
  BYTE bStart;
  int i;
  int j;
  int k;
  int nTeam;

  memcpy(saved, gPlayerData.a, sizeof(saved));
  memset(gPlayerData.a, 0, 0x1B0);

  for (i = 0; i < 12; ++i) {
    BYTE b = byte_526494[i];

    gPlayerData[i].nRace = byte_5264A0[i];
    gPlayerData[i].dwPlayerID = i;
    gPlayerData[i].dwStormID = -1;

    switch (b) {
    case 2:
      gPlayerData[i].nType = 6;
      break;
    case 6:
      gPlayerData[i].nType = 8;
      break;
    default:
      gPlayerData[i].nType = b;
      break;
    }
  }

  if (IsTeamGame()) {
    for (nTeam = 1; nTeam <= GetNumTeams(); ++nTeam) {
      bStart = static_cast<BYTE>(GetMaxPlayersPerTeam() * (nTeam - 1));

      for (j = 0; j < GetMaxPlayersPerTeam(); ++j) {
        if (bStart + j < 8)
          gPlayerData[bStart + j].nTeam = static_cast<BYTE>(nTeam);
      }
    }
  }

  if (IsScenarioGame(NULL)) {
    const BYTE *pSrc = sgbPlayerForce_s;
    i = 8;
    while (i--)
      gPlayerData[i].nTeam = pSrc[i];
  }

  for (k = 0; k < 8; ++k) {
    if (saved[k].nType == 0x0B) {
        // The eight iterations are written out: 1.17 has them fully unrolled and
        // if-converted, and no `for` spelling made MSVC unroll it.
      if (IsTeamGame()) {
        BYTE bTeamK = saved[k].nTeam;

        if (gPlayerData[0].nTeam == bTeamK)
          gPlayerData[0].nType = 8;

        if (gPlayerData[1].nTeam == bTeamK)
          gPlayerData[1].nType = 8;

        if (gPlayerData[2].nTeam == bTeamK)
          gPlayerData[2].nType = 8;

        if (gPlayerData[3].nTeam == bTeamK)
          gPlayerData[3].nType = 8;

        if (gPlayerData[4].nTeam == bTeamK)
          gPlayerData[4].nType = 8;

        if (gPlayerData[5].nTeam == bTeamK)
          gPlayerData[5].nType = 8;

        if (gPlayerData[6].nTeam == bTeamK)
          gPlayerData[6].nType = 8;

        if (gPlayerData[7].nTeam == bTeamK)
          gPlayerData[7].nType = 8;

      }

      if (IsScenarioGame(NULL))
        gPlayerData[k].nType = 8;
    }
  }

  for (k = 0; k < 8; ++k) {
    if (saved[k].nType == 0x0A) {
      IsTeamGame();

      if (IsScenarioGame(NULL))
        gPlayerData[k].nType = 8;
    }
  }

  for (n = 0; n < 8; ++n) {
    BYTE t = saved[n].nType;

    if (t == 1 || t == 3 || t == 4 || t == 7) {
      if (IsTeamGame()) {
        for (j = 0; j < 8; ++j) {
          if (gPlayerData[j].nTeam == saved[n].nTeam)
            sub_471A40(static_cast<BYTE>(j), static_cast<BYTE>(n), 1);
        }
      }

      if (IsScenarioGame(NULL))
        sub_471A40(static_cast<BYTE>(n), static_cast<BYTE>(n), 1);
    }
  }

  memset(gNetPlayerData_g.a, 0, 0x1B0);

  i = 12;
  while (i--) {
    gNetPlayerData_g[i].nRace = gPlayerData[i].nRace;
    gNetPlayerData_g[i].nType = gPlayerData[i].nType;
    gNetPlayerData_g[i].dwPlayerID = i;
    gNetPlayerData_g[i].dwStormID = -1;
    gNetPlayerData_g[i].nTeam = gPlayerData[i].nTeam;
  }
}
