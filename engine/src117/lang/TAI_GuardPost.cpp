// TAI_GuardPost.cpp -- guard-post records for the AI: the pool, the per-player
// lists and the aiscript entry points. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playertables.h"  // Hyperion: player slots past 11
#include "../port/data/port_formats.h"  // Hyperion: PortSave117Compress, PortSave117Decompress
#include "../gen/save117.h"
#include "../h/platform.h"
#include "../h/CUnit.h"
#include "../h/CSprite.h"
#include "../h/CTarget.h"
#include "../h/CUnitCombat.h"
#include "../h/CUnitSearches.h"
#include "../h/sai.h"
#include "../h/SAI_Banker.h"
#include "../h/SAI_Build.h"
#include "../h/SAI_Main.h"
#include "../h/SAI_Scripts.h"
#include "../h/net_cmds.h"
#include "../h/saveload.h"
#include "../h/TAI_Captain.h"
#include "../h/TAI_GuardPost.h"
#include "../gen/globals117.h"
#include "../h/handicap.h"  // Hyperion: a player's handicap

// Hyperion: banker_demand_guard_unit and banker_captain_for_request read a guard through SAI_Request
// (SAI_Banker.h), on both widths.
static_assert(offsetof(SAI_Request, pParent) == offsetof(TGuard, pUnit), "SAI_Request is TGuard");
static_assert(offsetof(SAI_Request, wType) == offsetof(TGuard, wType), "SAI_Request is TGuard");
static_assert(offsetof(SAI_Request, sX) == offsetof(TGuard, wPosX), "SAI_Request is TGuard");
static_assert(offsetof(SAI_Request, sY) == offsetof(TGuard, wPosY), "SAI_Request is TGuard");

// Globals this TU reaches, dual-spelled, one #ifdef per name.

// gGuards_g -- TGuardPool @0x6B6908, sc-refs/sheets/data_naming_1.17.csv.
#ifdef OC_DLL_MODE
#define gGuards_g (*reinterpret_cast<TGuardPool *>(OC_VA(0x6B6908u)))
#else
extern TGuardPool gGuards_g;
#endif

// gPlayerGuards_g -- TPlayerGuards[8] @0x6BE610, same sheet; the extent is the
// distance to the next sheet name, 0x6BE650.
#ifdef OC_DLL_MODE
#define gPlayerGuards_g (reinterpret_cast<TPlayerGuards *>(OC_VA(0x6BE610u)))
#else
extern PlayerTable<TPlayerGuards, PlayerTagGuards, 8> gPlayerGuards_g;
#endif

// guwUnitTimeToBuild -- WORD[228] @0x5238B0, same sheet, extent to
// guwUnitDepIndex @0x523A78.
#ifdef OC_DLL_MODE
#define guwUnitTimeToBuild (reinterpret_cast<WORD *>(OC_VA(0x5238B0u)))
#else
extern WORD guwUnitTimeToBuild[228] OC_UNALIGNED;
#endif

// gubUnitAIData -- BYTE[228] @0x521578, same sheet, extent to gulUnitTypeFlags.
#ifdef OC_DLL_MODE
#define gubUnitAIData (reinterpret_cast<BYTE *>(OC_VA(0x521578u)))
#else
extern BYTE gubUnitAIData[228] OC_UNALIGNED;
#endif

// True when the unit's soldier record is a live type-4 record that points back at
// it and the unit type lacks flag 0x01. The four rejects share one `xor eax, eax`,
// so they are one `||` chain.
BOOL guard_unit_is_soldier(CUnit *pUnit)
{
  const TSoldier *pSoldier =
      reinterpret_cast<const TSoldier *>(pUnit->dwUnion134);

  if (pSoldier == NULL || pSoldier->bType != 4 || pSoldier->pUnit != pUnit ||
      (gulUnitTypeFlags[pUnit->wType] & 0x01) != 0)
    return 0;
  return 1;
}

// True when the unit is a computer player's and still wants a guard record.
void CUnit::AttachAiRecord() {
  if (PlayerIsLobby(this->bPlayer)) {  // Hyperion: bPlayer < 8 in 1.17
    banker_register_medic(this);

    if (gubUnitAIData[this->wType] & 2)
      town_add_unit_to_first_town((intptr_t)this);
    else
      this->MakeGuardPost();
  }
}

// Write the pool and the per-player lists out in their index form and put the
// live pointers back afterwards. The result is the pool's own compress result,
// masked to zero when the list write fails.
int sai_write_guard_data(intptr_t nFile) {
  gGuards_g.PreSave();

  // Hyperion: 1.17's pool whole, or a grown pool's records and head
  int nResult = AiPoolSave<TGuard, AI_RECORDS_117>(SAVE117_GuardPool, SAVE117_Guard, gGuards_g.aNodes,
                                                   g_aiPools.records, gGuards_g.pFree, (void *)nFile);

  gGuards_g.PostLoad();

  for (int i = 0; i < PlayerLobbySlots(); i++) {
    gPlayerGuards_g[i].pHead =
        gPlayerGuards_g[i].pPool->ToIndex(gPlayerGuards_g[i].pHead);
    gPlayerGuards_g[i].pPool = NULL;
  }

  nResult = PortSave117Compress(SAVE117_PlayerGuards, gPlayerGuards_g.a, 8, (void *)nFile)
                ? nResult
                : 0;
  // Hyperion: the nations past 7, and every nation in the loops
  if (g_hxPlayers && !PortSave117Compress(SAVE117_PlayerGuards, gPlayerGuards_g.Extra(), gPlayerGuards_g.kExtraCount, (void *)nFile))
    nResult = 0;

  for (int i = 0; i < PlayerLobbySlots(); i++) {
    intptr_t nId = (intptr_t)gPlayerGuards_g[i].pHead;

    gPlayerGuards_g[i].pPool = &gGuards_g;
    gPlayerGuards_g[i].pHead = guard_ptr((int)nId);
  }

  return nResult;
}

// Read the guard pool and the per-player lists back, then turn every saved
// 1-based index into a pointer. guard_ptr inlines here, which is where the
// biased base 0x6B68E8 comes from.
int sai_read_guard_data(intptr_t nFile) {
  // Hyperion: 1.17's pool whole, or a grown pool's records and head
  if (!AiPoolLoad<TGuard, AI_RECORDS_117>(SAVE117_GuardPool, SAVE117_Guard, gGuards_g.aNodes, g_aiPools.records,
                                          &gGuards_g.pFree, (void *)nFile))
    return 0;

  gGuards_g.PostLoad();

  if (!PortSave117Decompress(SAVE117_PlayerGuards, gPlayerGuards_g.a, 8, (void *)nFile))
    return 0;
  // Hyperion: the nations past 7, and every nation in the loops
  if (g_hxPlayers && !PortSave117Decompress(SAVE117_PlayerGuards, gPlayerGuards_g.Extra(), gPlayerGuards_g.kExtraCount, (void *)nFile))
    return 0;

  for (int i = 0; i < PlayerLobbySlots(); i++) {
    intptr_t nId = (intptr_t)gPlayerGuards_g[i].pHead;

    gPlayerGuards_g[i].pPool = &gGuards_g;
    gPlayerGuards_g[i].pHead = guard_ptr((int)nId);
  }

  return 1;
}

// Give a computer player's unit a guard post where it stands, reusing an
// unmanned post already there. Both paths share the tail that binds the unit.
// The pop needs its own `pFree` local: assigning straight into `p` lets MSVC
// merge the two null tests and loses 4 B.
void CUnit::MakeGuardPost() {
  int nPlayer = this->bPlayer;

  if (gPlayerData[nPlayer].nType == 1) {
    int nUnitType = this->wType;

    if ((gubUnitAIData[nUnitType] & 2) == 0) {
      CSprite *pSprite = this->pSprite;
      int nX = PosS(pSprite->wPosX);
      int nY = PosS(pSprite->wPosY);
      TGuard *p = gPlayerGuards_g[nPlayer].pHead;

      while (p != NULL) {
        if (p->pUnit == NULL && p->wType == nUnitType &&
            PosS(p->wHomeX) == nX && PosS(p->wHomeY) == nY)
          break;

        p = p->pNext;
      }

      if (p == NULL) {
        TGuardPool *pPool = gPlayerGuards_g[nPlayer].pPool;
        TGuard *pFree = pPool->pFree;

        if (pFree != NULL) {
          pPool->pFree = pFree->pNext;

          if (pPool->pFree != NULL)
            pPool->pFree->pPrev = NULL;
        }

        p = pFree;

        if (p != NULL) {
          p->pPrev = NULL;
          p->pNext = gPlayerGuards_g[nPlayer].pHead;

          if (gPlayerGuards_g[nPlayer].pHead != NULL)
            gPlayerGuards_g[nPlayer].pHead->pPrev = p;

          gPlayerGuards_g[nPlayer].pHead = p;

          p->bType = 1;
          p->wType = (WORD)nUnitType;
        }
      }

      if (p != NULL) {
        p->pUnit = this;
        p->wPosX = PosU(nX);
        p->wPosY = PosU(nY);
        p->wHomeX = PosU(nX);
        p->wHomeY = PosU(nY);
        p->bTries = 0;
        this->dwUnion134 = (DWORD_PTR)p;
      }
    }
  }
}

// Detach a guard record from its unit; the retry count saturates at 100.
BOOL TAI_GuardPost_RemoveUnit(CUnit *pUnit) {
  TGuard *pGuard = (TGuard *)pUnit->dwUnion134;

  if (pGuard == NULL)
    return 0;

  if (pGuard->bType != 1)
    return 0;

  BYTE bTries = pGuard->bTries;

  pGuard->pUnit = NULL;

  if (bTries < 100)
    pGuard->bTries = bTries + 1;

  return 1;
}

// Restore a guard's post to the position it was created at, and bind the unit.
void guard_crew_post(TGuard *pGuard, CUnit *pUnit) {
  pGuard->wPosX = pGuard->wHomeX;
  pGuard->pUnit = pUnit;
  pGuard->wPosY = pGuard->wHomeY;
  pUnit->dwUnion134 = (DWORD_PTR)pGuard;
}

// True when the unit is fit to be sent back to its guard post, and sends it
// unless the caller asked otherwise. The failure block is shared and has to
// stay textually last, so every test is written in the positive sense.
BOOL __fastcall guard_return_to_post(CUnit *pUnit, int nControl) {
  TGuard *pGuard = (TGuard *)pUnit->dwUnion134;

  if (pGuard != NULL && pGuard->bType == 1) {
    if (pUnit->GetRace() != 1 ||
        (gulUnitTypeFlags[pUnit->wType] & 0x40000000) == 0 ||
        pUnit->lHitPoints == HandicapLife(gxUnitLife[pUnit->wType], pUnit->bPlayer)) { // Hyperion: at the owner's handicap
      if (pUnit->dwUnion7C == 0) {
        // GetRegion_xy has to be the right operand: it is evaluated first and
        // kept in SI, which is what `cmp ax, si` needs.
        if (GetRegion_CUnit(pUnit) != GetRegion_xy(PosS(pGuard->wPosX),
                                                   PosS(pGuard->wPosY))) {
          int nGuardY = PosS(pGuard->wPosY);
          int nGuardX = PosS(pGuard->wPosX);

          if (!reinterpret_cast<CTarget *>(pUnit)->XYInRadius(0xC0, nGuardX, nGuardY) &&
              (pUnit->pOrderTarget == NULL || !pUnit->IsUnitInWeaponRange(NULL)) &&
              (pUnit->pOrderQueueHead == NULL ||
               pUnit->GetFirstQueuedOrder() != 0x98)) {
            if (nControl == 0)
              pUnit->SetOrder_xy(0xA3, PosS(pGuard->wPosX), PosS(pGuard->wPosY));

            return 1;
          }
        }
      }
    }
  }

  return 0;
}

// Move a unit's guard post, if it holds one.
void __fastcall guard_move_post(CUnit *pUnit, int nX, int nY) {
  TGuard *pGuard = (TGuard *)pUnit->dwUnion134;

  if (pGuard != NULL && pGuard->bType == 1) {
    pGuard->wPosX = PosU(nX);
    pGuard->wPosY = PosU(nY);
  }
}

// True unless the unit holds a guard post it has wandered off.
BOOL guard_is_at_post(CUnit *pUnit) {
  TGuard *pGuard = (TGuard *)pUnit->dwUnion134;

  if (pGuard == NULL)
    return 1;

  if (pGuard->bType != 1)
    return 1;

  CSprite *pSprite = pUnit->pSprite;

  if (pGuard->wPosX == pSprite->wPosX && PosS(pGuard->wPosY) == pSprite->wPosY)
    return 1;

  return 0;
}

// guard_ptr -- the 1-based id to the record; 0 is the null id.
TGuard *guard_ptr(int nId) {
  if (nId == 0)
    return NULL;

  return &gGuards_g.aNodes[nId - 1];
}

// guard_id -- the inverse. The difference is taken unsigned; the oracle's
// divide is `shr`, not `sar`.
int guard_id(TGuard *pGuard) {
  if (pGuard == NULL)
    return 0;

  // Hyperion: 1.17's stride of 32 is sizeof(TGuard), which x64 widens
  //.
  return (int)(((DWORD_PTR)pGuard - (DWORD_PTR)&gGuards_g) / sizeof(TGuard)) + 1;
}

// Walk one player's guards and re-arm every unmanned post whose type matches.
// Type 30 is answered by 5.
void guard_wake_posts_of_type(int nPlayer, WORD wType) {
  for (TGuard *p = gPlayerGuards_g[nPlayer].pHead; p != NULL; p = p->pNext) {
    if (p->pUnit == NULL && p->dwFlag != 0) {
      int nType = p->wType;

      if (nType == 30)
        nType = 5;

      if (nType == (int)wType)
        p->dwFlag = 1;
    }
  }
}

// True when the player has an unmanned post of this type in a region the AI
// still wants.
BOOL guard_needs_type(int nPlayer, int nType) {
  for (TGuard *p = gPlayerGuards_g[nPlayer].pHead; p != NULL; p = p->pNext) {
    if (p->pUnit == NULL && p->wType == nType) {
      if (tai_captain_region_is_calm(nPlayer, GetRegion_xy(PosS(p->wHomeX), PosS(p->wHomeY))))
        return 1;
    }
  }

  return 0;
}

// True when the player holds a post within 0x120 of the given point.
int guard_post_near(int nPlayer, int nX, int nY) {
  for (TGuard *p = gPlayerGuards_g[nPlayer].pHead; p != NULL; p = p->pNext) {
    if ((DWORD)veclen(PosS(p->wPosX), nX, PosS(p->wPosY), nY) <= 0x120)
      return 1;
  }

  return 0;
}

// Take a guard record off the pool's free list and put it at the head of the
// player's list, posted at (nX, nY). The pop is spelled through `pPool->pFree`
// while the insert is guarded on the local, so the two conditions are different
// expressions and MSVC leaves the oracle's second null test standing.
void guard_create_post(int nPlayer, int nType, int nX, int nY) {
  TGuardPool *pPool = gPlayerGuards_g[nPlayer].pPool;
  TGuard *p = pPool->pFree;

  if (pPool->pFree != NULL) {
    pPool->pFree = pPool->pFree->pNext;

    if (pPool->pFree != NULL)
      pPool->pFree->pPrev = NULL;
  }

  if (p != NULL) {
    p->pPrev = NULL;
    p->pNext = gPlayerGuards_g[nPlayer].pHead;

    if (gPlayerGuards_g[nPlayer].pHead != NULL)
      gPlayerGuards_g[nPlayer].pHead->pPrev = p;

    gPlayerGuards_g[nPlayer].pHead = p;

    p->bType = 1;
    p->bTries = 0;
    p->pUnit = NULL;
    p->wType = (WORD)nType;
    p->wPosX = PosU(nX);
    p->wPosY = PosU(nY);
    p->wHomeX = PosU(nX);
    p->wHomeY = PosU(nY);
  }
}

// Return every unmanned guard record of one player to the free list. The loop
// is bounded by the list head rather than by the cursor, and tests the cursor
// again at the top.
void guard_free_unmanned(int nPlayer) {
  TGuard *p = gPlayerGuards_g[nPlayer].pHead;

  while (gPlayerGuards_g[nPlayer].pHead != NULL) {
    if (p == NULL)
      break;

    TGuard *pNext = p->pNext;

    if (p->pUnit == NULL) {
      if (gPlayerGuards_g[nPlayer].pHead == p)
        gPlayerGuards_g[nPlayer].pHead = p->pNext;

      if (p->pPrev != NULL)
        p->pPrev->pNext = p->pNext;

      if (p->pNext != NULL)
        p->pNext->pPrev = p->pPrev;

      TGuardPool *pPool = gPlayerGuards_g[nPlayer].pPool;

      p->pPrev = NULL;
      p->pNext = pPool->pFree;

      if (pPool->pFree != NULL)
        pPool->pFree->pPrev = p;

      pPool->pFree = p;
    }

    p = pNext;
  }
}

// aiscript `place_guard`: find a spot in the town and post a guard there.
void TAI_GuardPost_aiscript_place_guard(int nPlayer, intptr_t nTown, int nType, int nArg3) {
  int nX;
  int nY;

  if (town_place_point((TTown *)nTown, nArg3, &nX, &nY))
    guard_create_post(nPlayer, nType, nX, nY);
}

// Lay the whole pool out as one free list. The middle nodes are a loop of
// 998; the two ends are written out because their links are one-sided.
// Hyperion: the game's records, 1000 in 1.17.
void TSaveNode<TGuard,1000>::Init() {
  const int n = g_aiPools.records;
  aNodes[0].pPrev = NULL;
  aNodes[0].pNext = &aNodes[1];
  aNodes[0].Clear();

  for (int i = 1; i < n - 1; i++) {
    aNodes[i].pPrev = &aNodes[i - 1];
    aNodes[i].pNext = &aNodes[i + 1];
    aNodes[i].Clear();
  }

  aNodes[n - 1].pPrev = &aNodes[n - 2];
  aNodes[n - 1].pNext = NULL;
  aNodes[n - 1].Clear();

  pFree = &aNodes[0];
}

// Turn the pool into its save form: wipe the free nodes and replace every
// pointer with a 1-based index.
void TSaveNode<TGuard,1000>::PreSave() {
  for (TGuard *p = pFree; p != NULL; p = p->pNext)
    p->Clear();

  TGuard *q = aNodes;

  for (int i = 0; i < g_aiPools.records; i++) {  // Hyperion: 1000 in 1.17
    q->PreSave();
    q->pNext = ToIndex(q->pNext);
    q->pPrev = ToIndex(q->pPrev);
    q++;
  }

  pFree = ToIndex(pFree);
}

// The inverse of PreSave, less the wipe.
void TSaveNode<TGuard,1000>::PostLoad() {
  TGuard *q = aNodes;

  for (int i = 0; i < g_aiPools.records; i++) {  // Hyperion: 1000 in 1.17
    q->PostLoad();
    q->pNext = FromIndex(q->pNext);
    q->pPrev = FromIndex(q->pPrev);
    q++;
  }

  pFree = FromIndex(pFree);
}

// Build the pool and point every player's list at it.
void tai_guardpost_init() {
  gGuards_g.Init();

  for (int i = 0; i < PlayerLobbySlots(); i++) {  // Hyperion
    gPlayerGuards_g[i].pPool = &gGuards_g;
    gPlayerGuards_g[i].pHead = NULL;
  }
}

// Re-crew one guard post from a captain's spare unit, or re-request it.
// nOrderType is a WORD so its int argument is a widening copy (`mov edx, ecx`).
static __forceinline void guard_recrew(int nPlayer, TGuard *p) {
  WORD nOrderType = p->wType;
  CUnit *pNew;

  if (nOrderType == 0x25 || nOrderType == 0x2F)
    pNew = tai_captain_find_spare_unit_near(nPlayer, nOrderType, PosS(p->wHomeX), PosS(p->wHomeY));
  else
    pNew = NULL;

  if (pNew != NULL) {
    SAI_RemoveUnit(pNew, 0);

    p->wPosX = p->wHomeX;
    p->pUnit = pNew;
    p->wPosY = p->wHomeY;
    pNew->dwUnion134 = (DWORD_PTR)p;
    pNew->SetOrder(0x9C);
  } else {
    banker_demand_guard_unit(nPlayer, (SAI_Request *)p);
    p->dwFlag = g_dwElapsedTime;
  }
}

// Service one player's guard posts: retire the ones the AI has given up on,
// recycle the ones whose unit is gone, re-crew or re-request the rest. The
// unlink-and-recycle block is written twice because the oracle has it twice.
void guard_update_posts(int nPlayer) {
  TGuard *pNext;

  for (TGuard *p = gPlayerGuards_g[nPlayer].pHead;
       gPlayerGuards_g[nPlayer].pHead != NULL; p = pNext) {
    if (p == NULL)
      break;

    pNext = p->pNext;

    if (aiscript_has_campaign(nPlayer) && p->bTries >= 3) {
      if (gPlayerGuards_g[nPlayer].pHead == p)
        gPlayerGuards_g[nPlayer].pHead = p->pNext;

      if (p->pPrev != NULL)
        p->pPrev->pNext = p->pNext;

      if (p->pNext != NULL)
        p->pNext->pPrev = p->pPrev;

      TGuardPool *pPool = gPlayerGuards_g[nPlayer].pPool;

      p->pPrev = NULL;
      p->pNext = pPool->pFree;

      if (pPool->pFree != NULL)
        pPool->pFree->pPrev = p;

      pPool->pFree = p;
      continue;
    }

    CUnit *pUnit = p->pUnit;

    if (pUnit != NULL) {
      if ((pUnit->dwFlags & 0x40000000) == 0)
        continue;

      if (gPlayerGuards_g[nPlayer].pHead == p)
        gPlayerGuards_g[nPlayer].pHead = p->pNext;

      if (p->pPrev != NULL)
        p->pPrev->pNext = p->pNext;

      if (p->pNext != NULL)
        p->pNext->pPrev = p->pPrev;

      TGuardPool *pPool = gPlayerGuards_g[nPlayer].pPool;

      p->pPrev = NULL;
      p->pNext = pPool->pFree;

      if (pPool->pFree != NULL)
        pPool->pFree->pPrev = p;

      pPool->pFree = p;
      continue;
    }

    DWORD dwWhen = p->dwFlag;

    if (dwWhen != 0) {
      int nUnitType = p->wType;

      if (nUnitType == 30)
        nUnitType = 5;

      DWORD dwBuild = guwUnitTimeToBuild[nUnitType];

      switch (nUnitType) {
        case 68:
          dwBuild += guwUnitTimeToBuild[67];
          break;

        case 63:
          dwBuild += guwUnitTimeToBuild[61];
          break;

        case 44:
        case 62:
          dwBuild += guwUnitTimeToBuild[43];
          break;

        case 103:
          dwBuild += guwUnitTimeToBuild[38];
          break;
      }

      if (p->bTries != 0)
        dwWhen += 300;
      else
        dwWhen += 5 + dwBuild / 15;

      if (g_dwElapsedTime <= dwWhen)
        continue;
    }

    if (!tai_captain_region_is_calm(nPlayer, GetRegion_xy(PosS(p->wHomeX), PosS(p->wHomeY))))
      continue;

    // Both arms keep dwWhen live past the region check, which keeps the
    // cmov's 5 out of EBX; MSVC merges them after allocation.
    if (dwWhen)
      guard_recrew(nPlayer, p);
    else
      guard_recrew(nPlayer, p);
  }
}
