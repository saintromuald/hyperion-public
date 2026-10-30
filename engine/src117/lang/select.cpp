// select.cpp -- the unit-selection layer, oracle 0x49C140-0x49CFF0.

#include "oc_data.h"  // Hyperion: OC_VA
#include <intrin.h>
#include <math.h>            // sqrt -- select_form_calc_offset
#include <string.h>          // memset -- select_init, sub_49C7D0

#include "platform.h"
#include "point16.h"
#include "CUnit.h"
#include "CSprite.h"         // CSprite::flags, wPosX/wPosY
#include "rect16.h"          // rect16_t -- gUnitBoundBox
#include "CUnitPathBuild.h"  // sub_42C930
#include "CUnitBuild.h"      // ClipToMap
#include "CUnitPathUtil.h"   // sub_42ED50
#include "CTarget.h"         // veclen
#include "SAI_Banker.h"      // GetRegion_xy
#include "statbtn.h"         // OrderAllowed, TechUseAllowed
#include "CUnitGUI.h"        // sub_422B40
#include "Teams.h"           // IsTeamGame
#include "sai.h"             // GetRegionPaths
#include "net_cmds.h"        // PlayerData, select_get_next, sub_472100, ...
#include "gamesnd.h"         // gamesnd_select
#include "game.h"            // select_init
#include "select.h"
#include "statselect.h"      // status_change_selection
#include "../gen/globals117.h"
#include "../h/selectlimits.h"  // Hyperion: SELECTION_MAX, SELECTION_HUMANS

// The globals this TU reaches, each spelled once for the standalone build and
// once for the payload.

// 0x6A60A0, the selection cursor select_get_first resets and select_get_next
// walks, a BYTE. It sits exactly at gpNewUnits + 0x30, so a baked 0x6A60A0 can
// also be that array's end pointer; here every access is the byte variable.
#ifdef OC_DLL_MODE
  #ifndef sgbGetUnit
  #define sgbGetUnit (*reinterpret_cast<BYTE *>(OC_VA(0x6A60A0u)))
  #endif
#else
extern BYTE sgbGetUnit;
#endif

// 0x520C68 gubUnitBaseRank, a BYTE[228] indexed by unit type.
#ifdef OC_DLL_MODE
  #ifndef gubUnitBaseRank
  #define gubUnitBaseRank (reinterpret_cast<BYTE *>(OC_VA(0x520C68u)))
  #endif
#else
extern BYTE gubUnitBaseRank[228] OC_UNALIGNED;
#endif

// 0x51B500 order_canBeObstructed, a BYTE per order; the 189 extent is the
// orders-table stride its neighbour order_unk9 @0x51B440 carries.
#ifdef OC_DLL_MODE
  #ifndef order_canBeObstructed
  #define order_canBeObstructed (reinterpret_cast<BYTE *>(OC_VA(0x51B500u)))
  #endif
#else
extern BYTE order_canBeObstructed[189] OC_UNALIGNED;
#endif

// 0x51B800 order_techType, a BYTE per order, with gubOrderWeapon's extent.
#ifdef OC_DLL_MODE
  #ifndef order_techType
  #define order_techType (reinterpret_cast<BYTE *>(OC_VA(0x51B800u)))
  #endif
#else
extern tech_id_t order_techType[189];
#endif

// 0x5210F0 gubUnitOrderAttackTile, a BYTE per unit type, with the 228 extent of
// its sibling gubUnitOrderAttackUnit @0x521008.
#ifdef OC_DLL_MODE
  #ifndef gubUnitOrderAttackTile
  #define gubUnitOrderAttackTile (reinterpret_cast<BYTE *>(OC_VA(0x5210F0u)))
  #endif
#else
extern order_id_t gubUnitOrderAttackTile[228] OC_UNALIGNED;
#endif

// 0x522C38 gUnitBoundBox, a rect16_t[228]: read at stride 8, the four members at
// +0, +2, +4 and +6.
#ifdef OC_DLL_MODE
  #ifndef gUnitBoundBox
  #define gUnitBoundBox (reinterpret_cast<rect16_t *>(OC_VA(0x522C38u)))
  #endif
#else
extern rect16_t gUnitBoundBox[228] OC_UNALIGNED;
#endif

// CUnit +0x8E, the per-unit rank bonus added to gubUnitBaseRank[wType]. It
// falls inside CUnit.h's _unmapped_86 window; the add is byte-wide
// (`add cl, byte ptr [esi+8Eh]`).
static __forceinline BYTE &unit_rank_bonus(CUnit *p) {
  return p->_unmapped_86[8];
}

// Step n 0x20 toward nAt without overshooting it. The ternary gets the oracle's
// `cmov` where an `if` gets a branch, and `+ -0x20` inside the comparison gets
// `add eax, -20h` where `(n) -= 0x20;` canonicalises to `sub eax, 20h`.
#define OC_STEP_TOWARD(n, nAt)                                                \
  do {                                                                        \
    if ((nAt) < (n))                                                          \
      (n) = (nAt) > (n) + -0x20 ? (nAt) : (n) + -0x20;                        \
    else                                                                      \
      (n) = (nAt) < (n) + 0x20 ? (nAt) : (n) + 0x20;                          \
  } while (0)

// Is pUnit disabled: the 0x400 flag or any of the three freeze timers. Both
// tests below call it twice, and the flag stays a shifted value so the second
// test can reuse it.
static BOOL unit_disabled(CUnit *pUnit) {
  return ((pUnit->dwFlags >> 10) & 1) || pUnit->bLockdownTimer ||
         pUnit->bStasisTimer || pUnit->bParalyzeTimer;
}

// Rank two selection candidates: does pCandidate outrank pCurrent? A disabled
// unit loses to an enabled one; between two alike the base rank plus the
// per-unit bonus decides, byte-wide and unsigned.
int __fastcall sub_49C140(CUnit *pCurrent, CUnit *pCandidate) {
  if (pCurrent == NULL)
    return 1;
  if (unit_disabled(pCurrent) && !unit_disabled(pCandidate))
    return 1;
  if (!unit_disabled(pCurrent) && unit_disabled(pCandidate))
    return 0;

  BYTE bCur  = (BYTE)(gubUnitBaseRank[pCurrent->wType] + unit_rank_bonus(pCurrent));
  BYTE bCand = (BYTE)(gubUnitBaseRank[pCandidate->wType] + unit_rank_bonus(pCandidate));
  return bCur < bCand;
}

// Clamp a point to the map in pixels.
void __fastcall sub_49C210(point16_t *pPos) {
  if (pPos->x < 0)
    pPos->x = 0;
  else if (pPos->x >= gwMapPixWidth)
    pPos->x = gwMapPixWidth - 1;

  if (pPos->y < 0)
    pPos->y = 0;
  else if (pPos->y >= gwMapPixHeight)
    pPos->y = gwMapPixHeight - 1;
}

// Is wTarget reachable from wRegion, either by being it or by appearing in its
// path set? The path cursor lives in memory because its address is taken.
BOOL __fastcall sub_49C270(WORD wRegion, WORD wTarget) {
  WORD *pPaths;

  if (wTarget == wRegion)
    return TRUE;

  int nPaths = GetRegionPaths(wRegion, &pPaths);
  int i = 0;
  while (i < nPaths) {
    if (*pPaths == wTarget)
      return TRUE;
    i++;
    pPaths++;
  }
  return FALSE;
}

// Is nPlayer an ally of the local player in a team game?
BOOL __fastcall sub_49C2B0(int nPlayer) {
  if (!IsTeamGame())
    return FALSE;
  if (nPlayer == (int)g_LocalHumanID)
    return FALSE;
  if (nPlayer >= 8)
    return FALSE;
  if (gPlayerData[nPlayer].nType != 2)
    return FALSE;
  if (gPlayerData[g_LocalHumanID].nType != 2)
    return FALSE;
  return gPlayerData[g_LocalHumanID].nTeam == gPlayerData[nPlayer].nTeam;
}

// Work out where the formation select_target is building should sit: bounding
// box, centre of mass and spread (the square root of twice the footprint area)
// of the selection, and an offset toward the centre of mass when the target is
// outside the box. The pathing arm has to cache the flag byte in a local, or the
// two arms are tail-merged.
void __fastcall select_form_calc_offset(select_form_t *pForm, BYTE bArg1) {
  int nFlag = 0;
  int nCount = 0;
  int nMinX;
  int nMinY;
  DWORD dwSumY;
  DWORD dwSumX;
  int nMaxX;
  int nMaxY;

  pForm->wSpread = 0;
  mpoint_t  ptT = pForm->ptTarget;
  dwSumY = 0;
  dwSumX = 0;
  nMinX = 0x7FFFFFFF;
  nMinY = 0x7FFFFFFF;
  nMaxX = 0;
  nMaxY = 0;
  pForm->bFlags = 0;
  pForm->nArea = 0;
  pForm->ptCentre = ptT;
  pForm->ptClamped = ptT;
  // The barrier keeps the four stack initialisations ahead of the pForm stores.
  _ReadWriteBarrier();

  for (CUnit *pUnit = select_get_first(); pUnit != NULL;
       pUnit = select_get_next()) {
    CSprite *pSprite = pUnit->pSprite;
    if (pSprite == NULL)
      continue;
    nFlag = (pUnit->bPathingFlags & 1) != 0 ? 1 : nFlag;
    nCount++;
    int x = PosS(pSprite->wPosX);
    dwSumX += x;
    int y = PosS(pSprite->wPosY);
    dwSumY += y;
    nMinX = x < nMinX ? x : nMinX;
    nMaxX = x > nMaxX ? x : nMaxX;
    nMinY = y < nMinY ? y : nMinY;
    nMaxY = y > nMaxY ? y : nMaxY;
    pForm->nArea +=
        (short)(gUnitBoundBox[pUnit->wType].top + 1 +
                gUnitBoundBox[pUnit->wType].bottom) *
        (short)(gUnitBoundBox[pUnit->wType].right + 1 +
                gUnitBoundBox[pUnit->wType].left);
  }

  if (nCount == 0)
    return;

  int nSpread = (WORD)(int)sqrt((double)pForm->nArea * 2.0);
  pForm->wSpread = (WORD)nSpread;

  if (nFlag != 0 && bArg1 != 0) {
    point16_t ptAvg;
    ptAvg.x = (short)(dwSumX / nCount);
    ptAvg.y = (short)(dwSumY / nCount);
    // Hyperion: through 1.17's point, which these still take (stage E)
    point16_t ptClamped = pForm->ptClamped;
    if (nCount == 1)
      select_get_first()->sub_42C8C0(&ptClamped);
    else
      sub_42C930(reinterpret_cast<intptr_t>(&ptClamped), (WORD)nSpread,
                 *reinterpret_cast<int *>(&ptAvg), (WORD)nSpread);
    sub_49C210(&ptClamped);
    pForm->ptClamped = ptClamped;
  }

  if (nCount < 2)
    return;

  int nTx = pForm->ptTarget.x;
  int nTy;
  if (nTx <= nMaxX && nTx >= nMinX) {
    nTy = pForm->ptTarget.y;
    if (nTy <= nMaxY && nTy >= nMinY) {
      pForm->bFlags = 4;
      pForm->nOffsetX = 0;
      pForm->nOffsetY = 0;
      return;
    }
  }

  if (nFlag != 0) {
    BYTE bFlags = pForm->bFlags | 8;
    pForm->bFlags = bFlags;
    if (nMaxX - nMinX > 0xC0)
      return;
    if (nMaxY - nMinY > 0xC0)
      return;
    pForm->nOffsetX = PosS(pForm->ptCentre.x - dwSumX / nCount);
    pForm->nOffsetY = PosS(pForm->ptCentre.y - dwSumY / nCount);
    pForm->bFlags = bFlags | 3;
    return;
  }

  if (nMaxX - nMinX > 0x100)
    return;
  if (nMaxY - nMinY > 0x100)
    return;
  pForm->nOffsetX = PosS(pForm->ptCentre.x - dwSumX / nCount);
  pForm->nOffsetY = PosS(pForm->ptCentre.y - dwSumY / nCount);
  pForm->bFlags |= 3;
}

// Place this unit inside the formation select_target is building. The walk at
// the end steps the point back toward the reference position until the region
// is one the target's region has a path to.
void CUnit::sub_49C5A0(select_form_t *pForm) {
  pForm->ptUnit = pForm->ptTarget;
  mpoint_t  ptRef = (bPathingFlags & 1) ? pForm->ptClamped : pForm->ptCentre;
  BYTE bFlags = pForm->bFlags;

  if ((bFlags & 4) != 0) {
    int nAt = PosS(pSprite->wPosX);
    int n = ptRef.x;
    OC_STEP_TOWARD(n, nAt);
    pForm->ptUnit.x = (short)n;

    nAt = pSprite->wPosY;
    n = ptRef.y;
    OC_STEP_TOWARD(n, nAt);
    pForm->ptUnit.y = (short)n;
  } else {
    if ((bFlags & 1) != 0)
      pForm->ptUnit.x = PosS(pSprite->wPosX + pForm->nOffsetX);
    if ((bFlags & 2) != 0)
      pForm->ptUnit.y = PosS(pSprite->wPosY + pForm->nOffsetY);
  }

  ClipToMap(&pForm->ptUnit, wType);
  if ((bPathingFlags & 1) == 0)
    return;

  WORD wRegion = GetRegion_xy(ptRef.x, ptRef.y);
  // wType is its own statement so it is loaded before the pushes.
  int nType = wType;
  if (sub_42ED50(nType, pForm->ptUnit.x, pForm->ptUnit.y) &&
      sub_49C270(wRegion, GetRegion_xy(pForm->ptUnit.x, pForm->ptUnit.y)))
    return;

  // The packed point is copied once into a union and read at three widths:
  // the dword store to ptTry and both halves for veclen come from one register.
  union { DWORD dw; point16_t pt; } uTry;
  uTry.pt = pForm->ptUnit;
  mpoint_t  ptTry = uTry.pt;
  int nLen = veclen(uTry.pt.x, ptRef.x, uTry.pt.y, ptRef.y);
  if (nLen == 0)
    nLen = 1;
  int nStep = nLen / 4;
  if (nStep < 2)
    nStep = nLen;
  if (nStep > 16)
    nStep = 16;

  for (int nAt = nStep; nAt <= nLen; nAt += nStep) {
    ptTry.x = PosS((ptRef.x - pForm->ptUnit.x) * nAt / nLen +
                      PosU(pForm->ptUnit.x));
    ptTry.y = PosS((ptRef.y - pForm->ptUnit.y) * nAt / nLen +
                      PosU(pForm->ptUnit.y));
    if (sub_42ED50(wType, ptTry.x, ptTry.y) &&
        sub_49C270(wRegion, GetRegion_xy(ptTry.x, ptTry.y))) {
      pForm->ptUnit = ptTry;
      return;
    }
  }
  pForm->ptUnit = ptRef;
}

// Drop an ally's team circle, then close the gap pUnit leaves in that player's
// selection.
int __fastcall sub_49C870(CUnit *pUnit, int nPlayer) {
  if (pUnit == NULL)
    return 0;
  if (nPlayer >= SELECTION_HUMANS)
    return 0;
  if (sub_49C2B0(nPlayer))
    pUnit->sub_4230E0();
  return select_fixup_units(pUnit, nPlayer);
}

// Replace the new-selection staging list with ppUnits, releasing whatever it
// held, and read each unit back afterwards -- the callee may retarget it.
// Hyperion: the selection's arrays through selectlimits.h's accessors, 255 units in a game
// of Hyperion's selection (stage S); so in every body below.
void __fastcall sub_49CAB0(int nCount, CUnit **ppUnits) {
  CUnit **ppNew = NewUnits();
  for (int i = 0; i < SelectionMax(); i++) {
    CUnit *pOld = ppNew[i];
    if (pOld == NULL)
      break;
    ppNew[i] = NULL;
    sub_422B40(pOld);
  }

  for (int i = 0; i < nCount; i++) {
    ppNew[i] = *ppUnits;
    ppNew[i]->sub_422EB0(i);
    *ppUnits = ppNew[i];
    ppUnits++;
  }
}

// Hyperion: a command that one unit of a selection of several carries out in a game of
// Hyperion's selection (net_cmds.cpp): while it is set, the walk over the
// active player's selection gives that unit alone.
static CUnit *g_pSelectOnly;

void SelectOnlySet(CUnit *pUnit) {
  g_pSelectOnly = pUnit;
}

bool SelectOnlyActive() {
  return g_pSelectOnly != NULL;
}

// Start a walk over the local player's selection.
CUnit * __fastcall select_get_first() {
  if (g_pSelectOnly != NULL) {  // Hyperion: one unit of the selection
    sgbGetUnit = (BYTE)SelectionMax();
    return g_pSelectOnly;
  }
  sgbGetUnit = 0;
  return select_get_next();
}

// Clear both selection arrays at game start.
void select_init() {
  SelectionForGame();  // Hyperion: 12 units or 255 (stage S)
  memset(NewUnits(), 0, SelectionMax() * sizeof(CUnit *));  // Hyperion: 0x30 in 1.17
  memset(SelectedRow(0), 0, SELECTION_HUMANS * SelectionMax() * sizeof(CUnit *));  // Hyperion: 0x180 in 1.17
  net_cmds_reset_select();
}

// Drop a player's selection. An ally's units get their team circle removed one
// by one; anyone else's list is just cleared. The bound test is spelled ahead
// of the ally test, so the inlined copy of sub_49C2B0 loses its own.
void __fastcall sub_49C7D0(DWORD dwHumanID) {
  if ((int)dwHumanID >= SELECTION_HUMANS)
    return;

  if (sub_49C2B0(dwHumanID)) {
    int i = 0;
    CUnit **ppSel = SelectedRow(dwHumanID);
    while (i < SelectionMax()) {
      CUnit *pUnit = *ppSel;
      if (pUnit == NULL)
        break;
      pUnit->sub_4230E0();
      *ppSel = NULL;
      i++;
      ppSel++;
    }
  } else {
    memset(SelectedRow(dwHumanID), 0, SelectionMax() * sizeof(CUnit *));  // Hyperion: 0x30 in 1.17
  }
}

// Seat pUnit at nCount in dwHumanID's selection. Past the first slot the unit
// must be multi-selectable and the active player's own; the first slot takes
// anything with a visible sprite.
int __fastcall select_insert(CUnit *pUnit, DWORD dwHumanID, int nCount) {
  if (pUnit == NULL)
    return 0;
  if ((int)dwHumanID >= SELECTION_HUMANS)
    return 0;

  if (nCount >= SelectionMax())
    return 0;
  if ((pUnit->pSprite->flags.b & 0x20) != 0)
    return 0;
  if (nCount > 0) {
    if (!pUnit->IsMultiSelectable())
      return 0;
    if (pUnit->bPlayer != g_ActiveNationID)
      return 0;
  }

  SelectedRow(dwHumanID)[nCount] = pUnit;
  if (sub_49C2B0(dwHumanID))
    pUnit->CreateTeamCircle();
  return 1;
}

// Commit a resolved unit list as the selection: stage it, pick the unit whose
// selection sound plays, tell the network, then refresh the console.
void __fastcall select_units(int nCount, CUnit **ppUnits, BYTE bPlaySound,
                             int nSend) {
  sub_49CAB0(nCount, ppUnits);

  if (bPlaySound) {
    CUnit *pBest = NULL;
    CUnit **pp = ppUnits;
    for (int i = 0; i < nCount; i++) {
      if (sub_49C140(pBest, *pp))
        pBest = *pp;
      pp++;
    }
    gamesnd_select(pBest);
  }

  if (nSend)
    send_net_cmd_Select(nCount, ppUnits);
  status_change_selection();
}

// Remove pUnit from nPlayer's selection and close the gap, returning what is
// left. MSVC unrolls the search six slots at a time and turns the shift-down
// loop into `rep movsd`, so both must stay written as plain loops.
int __fastcall select_fixup_units(CUnit *pUnit, int nPlayer) {
  int n = 0;
  if (nPlayer >= SELECTION_HUMANS)
    return n;

  CUnit **ppRow = SelectedRow(nPlayer);
  const int nMax = SelectionMax();
  int nFound = nMax;
  for (n = 0; n < nMax; n++) {
    CUnit *p = ppRow[n];
    if (p == NULL)
      break;
    if (p == pUnit)
      nFound = n;
  }

  if (pUnit != NULL && n != 0 && nFound != nMax) {
    n--;
    for (int j = nFound; j < n; j++)
      ppRow[j] = ppRow[j + 1];
    ppRow[n] = NULL;
  }
  return n;
}

// select_get_next's reap: sub_49C8B0's body as a forced inline, so the nested
// select_fixup_units stays a call there as the oracle has it.
static __forceinline void select_reap(CUnit *pUnit) {
  for (int nPlayer = 0; nPlayer < SELECTION_HUMANS; nPlayer++)
    select_fixup_units(pUnit, nPlayer);

  pUnit->RemoveTeamCircle();
  sub_472100(pUnit);
}

// Step the walk to the next live unit in the local player's selection, reaping
// any dead one it passes; a reaped slot re-reads the cursor without advancing.
// The bound is tested once, and the exit condition goes through a flag so the
// loop is not rotated on the null test.
CUnit * __fastcall select_get_next() {
  if (sgbGetUnit >= SelectionMax())
    return NULL;
  CUnit *pUnit;
  for (;;) {
    pUnit = SelectedRow(g_ActiveHumanID)[sgbGetUnit];
    BOOL bLive = pUnit == NULL || !pUnit->IsDead();
    if (bLive)
      break;
    select_reap(pUnit);
  }
  sgbGetUnit++;
  return pUnit;
}

// Take pUnit out of every player's selection, then drop its team circle.
// The oracle has no call here, so select_fixup_units must be defined above
// this body for /Ob2 to splice it in.
void __fastcall sub_49C8B0(CUnit *pUnit) {
  for (int nPlayer = 0; nPlayer < SELECTION_HUMANS; nPlayer++)
    select_fixup_units(pUnit, nPlayer);

  pUnit->RemoveTeamCircle();
  sub_472100(pUnit);
}

WORD get_order_tech_energy_cost(order_id_t bOrderId);  // CUnitInit.cpp

// Hyperion: StarCraft II's smart cast in a game of Hyperion's selection:
// a spell is cast by one selected unit, the nearest to the place that may cast it and has
// the energy, or, when none has, the nearest that may, so that one refusal is heard; NULL when no
// selected unit may cast it, and 1.17's walk refuses it unit by unit.
static CUnit *SmartCaster(int nX, int nY, order_id_t bOrder) {
  const tech_id_t bTech = order_techType[bOrder];
  const WORD wCost = get_order_tech_energy_cost(bOrder);
  CUnit *pBest = NULL, *pAble = NULL;
  long long qBest = 0, qAble = 0;
  for (CUnit *pUnit = select_get_first(); pUnit != NULL; pUnit = select_get_next()) {
    if (TechUseAllowed(bTech, g_ActiveNationID, pUnit) != 1)
      continue;
    const long long dx = PosU(pUnit->pSprite->wPosX) - nX, dy = PosU(pUnit->pSprite->wPosY) - nY;
    const long long q = dx * dx + dy * dy;
    if (pAble == NULL || q < qAble) {
      pAble = pUnit;
      qAble = q;
    }
    if (!pUnit->NeedsMoreEnergy(wCost) && (pBest == NULL || q < qBest)) {
      pBest = pUnit;
      qBest = q;
    }
  }
  return pBest != NULL ? pBest : pAble;
}

// Issue an order at a point or a unit for every selected unit, offset by what
// select_form_calc_offset worked out. The dense order-id switch is the oracle's
// two-level jump table, which a 24-case `return` over 0..0x9B emits. The BYTE
// nOrderID seats the subunit order byte in the dead parameter slot.
void __fastcall select_target(int nX, int nY, CUnit *pTarget, int nTargetType,
                              order_id_t nOrderID, int nQueued) {
  select_form_t form;
  order_id_t bOrder = nOrderID;

  form.ptTarget.x = PosS(nX);
  form.ptTarget.y = PosS(nY);
  select_form_calc_offset(&form, order_canBeObstructed[bOrder]);

  switch (bOrder) {
    case 0x00: case 0x0F: case 0x14: case 0x19: case 0x1D: case 0x1E:
    case 0x1F: case 0x20: case 0x21: case 0x24: case 0x2E: case 0x47:
    case 0x48: case 0x49: case 0x50: case 0x51: case 0x52: case 0x53:
    case 0x55: case 0x56: case 0x57: case 0x58: case 0x59: case 0x9B:
      return;
  }

  // Hyperion: a spell from several goes to one caster (SmartCaster)
  if (SelectionWide() && !SelectOnlyActive() && order_techType[bOrder] != 0x2C) {
    if (CUnit *pCaster = SmartCaster(nX, nY, bOrder)) {
      SelectOnlySet(pCaster);
      select_target(nX, nY, pTarget, nTargetType, nOrderID, nQueued);
      SelectOnlySet(NULL);
      return;
    }
  }

  for (CUnit *pUnit = select_get_first(); pUnit != NULL;
       pUnit = select_get_next()) {
    tech_id_t bTech = order_techType[bOrder];
    int nAllowed;
    if (bTech != 0x2C)
      nAllowed = TechUseAllowed(bTech, g_ActiveNationID, pUnit);
    else
      nAllowed = OrderAllowed(bOrder, g_ActiveNationID, pUnit);
    if (nAllowed != 1) {
      AllowedErr(get_allowed_err());
      continue;
    }

    if (pUnit == pTarget &&
        !((pUnit->dwFlags & 2) != 0 && (bOrder == 0x27 || bOrder == 0x28)) &&
        !((pUnit->wType == 0x2E || pUnit->wType == 0x34) && bOrder == 0x77)) {
      if (!pUnit->IsATransport())
        continue;
      if (bOrder != 0x70)
        continue;
    }

    int nRClick;
    if ((pUnit->dwFlags & 2) != 0 &&
        (bOrder == 0x27 || bOrder == 0x28 || bOrder == 0x43 || bOrder == 0x5F))
      nRClick = 1;
    else
      nRClick = pUnit->GetRClickAction() != 0;
    if (nRClick == 0)
      continue;

    if (pUnit->bOrderId == 0x7D && bOrder != 0)
      continue;

    int nType = pUnit->wType;
    order_id_t bSub = 0x17;  // declared first, or its slot store follows bMain's
    order_id_t bMain = bOrder;

    if (nType == 0x22) {
      if (bOrder == 0x0E || bOrder == 8) {
        bMain = 0xB1;
        goto issue;
      }
    } else if (bOrder == 8) {
      bMain = pTarget != NULL ? gubUnitOrderAttackUnit[nType]
                              : gubUnitOrderAttackTile[nType];
      if (bMain == 0x17)
        continue;
      CUnit *pTurret = pUnit->subunit;
      if (pUnit->IsTurret(pTurret)) {
        int nTurret = pTurret->wType;
        bSub = pTarget != NULL ? gubUnitOrderAttackUnit[nTurret]
                               : gubUnitOrderAttackTile[nTurret];
      }
    }

    if (bMain == 0x27) {
      pUnit->SetRallyTarget_CUnit(pTarget);
      goto done;
    }
    if (bMain == 0x28) {
      pUnit->SetRallyTarget_xy(PosS(nX), PosS(nY));
      goto done;
    }

issue:
    if (pTarget != NULL) {
      int nFlag = (BYTE)nQueued != 0;
      pUnit->sub_42A880(nFlag, bMain, pTarget);
      if (bSub != 0x17)
        pUnit->subunit->sub_42A880(nFlag, bSub, pTarget);
    } else {
      form.ptTarget.x = PosS(nX);
      form.ptTarget.y = PosS(nY);
      if ((WORD)nTargetType == 0xE4)
        pUnit->sub_49C5A0(&form);
      else
        form.ptUnit = form.ptTarget;
      int nFlag = (BYTE)nQueued != 0;
      pUnit->sub_42A850(nFlag, bMain, nTargetType, form.ptUnit.x, form.ptUnit.y);
      if (bSub != 0x17)
        pUnit->subunit->sub_42A850(nFlag, bSub, nTargetType, form.ptUnit.x,
                                   form.ptUnit.y);
    }

done:
    pUnit->bUnion93 |= 2;
  }
}
