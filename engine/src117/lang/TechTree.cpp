// TechTree.cpp -- whether a player may build a unit, and the reason when it may
// not: the flattened dependency script, its index and the walk over it.
//
// Bodies are in ascending oracle address, 1.17's own order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playertables.h"  // Hyperion: player slots past 11
#include "../h/TechTree.h"
#include "../h/CUnit.h"          // CUnit and its nine predicates
#include "../h/CUnitSearches.h"  // TTBaseUnitType
#include "../h/net_cmds.h"       // get_allowed_err / set_allowed_err
#include "../h/saveload.h"       // get_tech_researched
#include "../gen/globals117.h"
#include "../h/typelimits.h"  // Hyperion: a mod's types
#include "../port/data/port_moddata.h"  // Hyperion: ModUnitTree, ModTreeEdit

int TTAllowed(WORD wUnitId, int nPlayer, CUnit *pUnit);

// The globals this TU reaches, each spelled once for the standalone build and
// once for the payload.

// 0x50DA88, the flattened dependency script: runs of WORDs, each run closed by
// 0xFFFF and followed by the unit id it belongs to. The extent is an upper
// bound, not a measured size.
#ifdef OC_DLL_MODE
#define sgTree (reinterpret_cast<WORD*>(OC_VA(0x50DA88u)))
#else
extern WORD sgTree[546] OC_UNALIGNED;
#endif

#ifdef OC_DLL_MODE
#define guwUnitDepIndex (reinterpret_cast<WORD*>(OC_VA(0x523A78u)))
#else
extern WORD guwUnitDepIndex[228] OC_UNALIGNED;
#endif

#ifdef OC_DLL_MODE
#define allowed_errcode_g (*reinterpret_cast<DWORD*>(OC_VA(0x6BE654u)))
#else
extern DWORD allowed_errcode_g;
#endif

// 0x536720 is nameless in every source we may read, so the oracle's own
// positional spelling stands.
#ifdef OC_DLL_MODE
#define PlayerUnitsAvailable (reinterpret_cast<BYTE(*)[228]>(OC_VA(0x52655Cu)))
#define glCounts_g (reinterpret_cast<DWORD(*)[228][12]>(OC_VA(0x529604u)))
#else
extern PlayerUnitsAvailableTable PlayerUnitsAvailable;
extern PlayerUnitCountsGrid glCounts_g;
#endif
#ifdef OC_DLL_MODE
#define byte_536720 (*reinterpret_cast<BYTE*>(OC_VA(0x536720u)))
#else
extern BYTE byte_536720;
#endif

// Index sgTree by unit id, then zero the entries no run claimed.
void sub_4B3230() {
  int i;
  unsigned int j;

  for (i = 1; sgTree[i] != 0xFFFF; ++i) {
    guwUnitDepIndex[sgTree[i]] = (WORD)(i + 1);

    while (sgTree[i] != 0xFFFF)
      ++i;
  }

  for (j = 0; UnitTypeValid(j); j = UnitTypeNext(j))  // Hyperion: and a mod's types
    if (guwUnitDepIndex[j] == 0xFFFF)
      guwUnitDepIndex[j] = 0;
}

// Whether nPlayer can build wUnitId with pUnit, leaving the reason in
// allowed_errcode_g. sgTree holds one opcode run per unit type; 0xFF01 joins two
// opcodes into one group, 0xFFFF closes the run, and a group must match at least
// once unless nFlagB was cleared. The first opcode has to be read after the flag
// stores, or FF02's else arm takes nFlagC from nFlagA's register.
int TTAllowed(WORD wUnitId, int nPlayer, CUnit *pUnit) {
  int i;
  int nCount;
  int nFlagA;
  int nFlagB;
  int nFlagC;
  WORD wOp;
  // Hyperion: a mod's unit has its own requirements, in the same form, and is
  // available to every player but where a race names it as its own
  // (port_moddata.h's ModUnitTree and ModUnitForPlayer); one of 1.17's a mod's edit gives
  // requirements has the edit's, and 1.17's other checks (ModTreeEdit)
  const bool bMod = UnitTypeMod(wUnitId);
  const WORD *pEditTree = bMod ? NULL : ModTreeEdit(MOD_TREE_UNIT, wUnitId);
  const WORD *pTree = bMod ? ModUnitTree(wUnitId) : pEditTree ? pEditTree : sgTree;

  allowed_errcode_g = 0;

  if (!UnitTypeValid(wUnitId)) {  // Hyperion: >= 0xE4 in 1.17
    allowed_errcode_g = 0x10;
    return 0;
  }

  if (nPlayer != pUnit->bPlayer) {
    allowed_errcode_g = 1;
    return 0;
  }

  if (!(pUnit->dwFlags & 1)) {
    allowed_errcode_g = 0x14;
    return 0;
  }

  if (pUnit->IsDisabled()) {
    allowed_errcode_g = 0xA;
    return 0;
  }

  if (bMod ? !ModUnitForPlayer(wUnitId, nPlayer) : PlayerUnitsAvailable[nPlayer][wUnitId] == 0) {
    allowed_errcode_g = 2;
    return 0;
  }

  if (UnitTypeQueueable(pUnit->GetBuildUnit(4))) {  // Hyperion: <= 0x69 in 1.17
    allowed_errcode_g = 3;
    return 0;
  }

  if (pUnit->dwFlags & 0x40000000) {
    allowed_errcode_g = 0x16;
    return 0;
  }

  if (!bMod && !pEditTree && guwUnitDepIndex[wUnitId] == 0xFFFF)
    sub_4B3230();

  i = bMod || pEditTree ? (pTree ? 1 : 0) : guwUnitDepIndex[wUnitId];

  if (i == 0) {
    allowed_errcode_g = 0x17;
    return 0;
  }

  nFlagA = 0;
  nFlagC = nFlagB = 1;
  wOp = pTree[i];

  while (wOp != 0xFFFF) {
    nCount = 0;

    for (;;) {
      switch (wOp) {
        case 0xFF25:
          if (get_tech_researched(nPlayer, pTree[++i]) || (glCheatBits & 0x1000)) {
            ++nCount;
            break;
          }

          allowed_errcode_g = 9;
          return -1;

        case 0xFF24:
          if (byte_536720 == 0) {
            allowed_errcode_g = 0x1A;
            return 0;
          }

          ++nCount;
          break;

        case 0xFF04:
          if (glCheatBits & 0x2000) {
            ++i;
            ++nCount;
            break;
          }

          if (pUnit->pUnionC0 == 0 || pUnit->pUnionC0->wType != pTree[++i]) {
            allowed_errcode_g = nFlagA == 0 ? 4 : allowed_errcode_g;
            return -1;
          }

          ++nCount;
          break;

        case 0xFF07:
          if (pUnit->IsConstructing() || pUnit->bTechType != 0x2C ||
              pUnit->bUpgradeType != 0x3D) {
            allowed_errcode_g = 5;
            return 0;
          }

          ++nCount;
          break;

        case 0xFF05:
          if (!(pUnit->dwFlags & 2)) {
            allowed_errcode_g = 7;
            return 0;
          }

          ++nCount;
          break;

        case 0xFF08:
          if (pUnit->IsConstructingAddon() || pUnit->IsBuildingProtoss()) {
            allowed_errcode_g = 5;
            return 0;
          }

          ++nCount;
          break;

        case 0xFF09:
          if (pUnit->bTechType != 0x2C) {
            allowed_errcode_g = 5;
            return 0;
          }

          ++nCount;
          break;

        case 0xFF0A:
          if (pUnit->bUpgradeType != 0x3D) {
            allowed_errcode_g = 5;
            return 0;
          }

          ++nCount;
          break;

        case 0xFF0B:
          if (pUnit->wType == 7 && pUnit->bOrderId == 0x21) {
            allowed_errcode_g = 5;
            return 0;
          }

          ++nCount;
          break;

        case 0xFF0E: {
          BYTE nHangar = 0xFF;
          BYTE nUsed;

          if (pUnit->IsCarrier()) {
            nUsed = (BYTE)(pUnit->bHangarCountBuilding + pUnit->bHangarCount);
            nHangar = (BYTE)(nUsed + pUnit->HangarConstructing());
          } else if (pUnit->IsReaver()) {
            nUsed = pUnit->bHangarCount;
            nHangar = (BYTE)(nUsed + pUnit->HangarConstructing());
          }

          if (nHangar < pUnit->GetHangarCapacity()) {
            ++nCount;
            break;
          }

          allowed_errcode_g = 3;
          return 0;
        }

        case 0xFF10:
          if (pUnit->pUnionD0 != 0) {
            allowed_errcode_g = 3;
            return 0;
          }

          ++nCount;
          break;

        case 0xFF11:
          if (pUnit->dwFlags & 0x10) {
            allowed_errcode_g = 5;
            return 0;
          }

          ++nCount;
          break;

        case 0xFF26:
          if (!(pUnit->dwFlags & 0x10)) {
            allowed_errcode_g = 5;
            return -1;
          }

          ++nCount;
          break;

        case 0xFF0C:
          if (pUnit->pUnionC0 != 0) {
            allowed_errcode_g = nFlagA == 0 ? 4 : allowed_errcode_g;
            return 0;
          }

          ++nCount;
          break;

        case 0xFF22:
          allowed_errcode_g = 0x15;
          return -1;

        case 0xFF23:
          allowed_errcode_g = 0x17;
          return 0;

        case 0xFF02: {
          WORD wReq = pTree[++i];

          nFlagB = glCounts_g[1][wReq][nPlayer] != 0 ? 0 : nFlagB;

          if (wReq != pUnit->wType) {
            if (nFlagC != 0)
              nFlagA = 1;
          } else {
            nFlagA = 0;
            nFlagC = 0;
            ++nCount;
          }

          break;
        }

        case 0xFF03: {
          WORD wReq = pTree[++i];

          nCount += glCounts_g[1][wReq][nPlayer] + glCounts_g[0][wReq][nPlayer];
          break;
        }

        default:
          if (glCheatBits & 0x2000)
            ++nCount;

          nCount += glCounts_g[1][wOp][nPlayer];
          break;
      }

      ++i;

      if (pTree[i] != 0xFF01)
        break;

      wOp = pTree[++i];
    }

    if (nCount == 0 && nFlagB != 0) {
      allowed_errcode_g = 8;
      return -1;
    }

    wOp = pTree[i];
    nFlagB = 1;
  }

  if (nFlagA != 0) {
    allowed_errcode_g = 0x19;
    return -1;
  }

  return 1;
}

// The unit that builds this one, or 228 when nothing does.
// The index must live in an int: a WORD local tests with `test ax,ax` and costs
// the operand-size prefix the oracle does not have.
WORD TTBaseUnitType(WORD wUnitId) {
  int nIdx;

  allowed_errcode_g = 0;

  // Hyperion: a mod's unit, and one of 1.17's a mod's edit gives requirements, have the
  // unit that builds them in their own tree, first; 1.17's index holds its 228 units alone
  //
  const WORD *pOwnTree = UnitTypeMod(wUnitId) ? ModUnitTree(wUnitId) : ModTreeEdit(MOD_TREE_UNIT, wUnitId);
  if (pOwnTree != NULL || wUnitId >= UNIT_TYPES_117)
    return pOwnTree != NULL && pOwnTree[1] == 0xFF02 ? pOwnTree[2] : 0xE4;

  if (guwUnitDepIndex[wUnitId] == 0xFFFF)
    sub_4B3230();

  nIdx = guwUnitDepIndex[wUnitId];

  if (nIdx != 0 && sgTree[nIdx] == 0xFF02)
    return sgTree[nIdx + 1];

  return 0xE4;
}

int get_allowed_err() {
  return allowed_errcode_g;
}

void set_allowed_err(int nErrCode) {
  allowed_errcode_g = nErrCode;
}
