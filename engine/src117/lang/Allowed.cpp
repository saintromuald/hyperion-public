// Allowed.cpp -- may this player issue this order, research this tech, use this
// ability, buy this upgrade? Each entry point screens the obvious refusals, then
// hands the dependency tree to CheckAllowed.
//
// The four trees are WORD lists of 0xFFFF-terminated records living next to each
// other in .data; each has an index table in its own arr/*.cpp TU that the four
// *Init bodies fill on first use.
//
// Bodies are in ascending oracle address, 1.17's own order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playertables.h"  // Hyperion: player slots past 11
#include "../h/Allowed.h"
#include "../h/CUnit.h"          // ours: the predicates below and the fields they read
#include "../h/statbtn.h"        // ours: the four Allowed entry points, is_tech_in_progress
#include "../h/net_cmds.h"       // ours: set_allowed_err
#include "../h/saveload.h"       // ours: get_tech_available / get_tech_researched
#include "../gen/globals117.h"
#include "../h/typelimits.h"  // Hyperion: a mod's types
#include "../port/data/port_moddata.h"  // Hyperion: ModTechUseTree, ModTechResearchTree, ModUpgradeTree, ModTreeEdit
#include "../h/mutators.h"  // Hyperion: MutatorResearchForbidden, MutatorUpgradesForbidden

// The four dependency trees, and the four index tables they fill. A tree's
// extent is the distance to the next tree's base, so the last one is bounded by
// DIRECTIONS @0x5026B0 instead; the index extents are each one *Init's own end
// constant.

#ifndef OC_sgUpgradeTree
#define OC_sgUpgradeTree
#ifdef OC_DLL_MODE
#define sgUpgradeTree (reinterpret_cast<WORD *>(OC_VA(0x501958u)))
#else
extern WORD sgUpgradeTree[420] OC_UNALIGNED;
#endif
#endif

#ifndef OC_sgResearchTree
#define OC_sgResearchTree
#ifdef OC_DLL_MODE
#define sgResearchTree (reinterpret_cast<WORD *>(OC_VA(0x501CA0u)))
#else
extern WORD sgResearchTree[160];
#endif
#endif

#ifndef OC_sgTechUseTree
#define OC_sgTechUseTree
#ifdef OC_DLL_MODE
#define sgTechUseTree (reinterpret_cast<WORD *>(OC_VA(0x501DE0u)))
#else
extern WORD sgTechUseTree[344];
#endif
#endif

#ifndef OC_sgOrderTree
#define OC_sgOrderTree
#ifdef OC_DLL_MODE
#define sgOrderTree (reinterpret_cast<WORD *>(OC_VA(0x502090u)))
#else
extern WORD sgOrderTree[660];
#endif
#endif

#ifndef OC_guwOrdDepIndex
#define OC_guwOrdDepIndex
#ifdef OC_DLL_MODE
#define guwOrdDepIndex (reinterpret_cast<WORD *>(OC_VA(0x51BB00u)))
#else
extern WORD guwOrdDepIndex[189] OC_UNALIGNED;
#endif
#endif

#ifndef OC_guwTechDepIndex
#define OC_guwTechDepIndex
#ifdef OC_DLL_MODE
#define guwTechDepIndex (reinterpret_cast<WORD *>(OC_VA(0x51F8C0u)))
#else
extern WORD guwTechDepIndex[44];
#endif
#endif

#ifndef OC_guwTechUseDepIndex
#define OC_guwTechUseDepIndex
#ifdef OC_DLL_MODE
#define guwTechUseDepIndex (reinterpret_cast<WORD *>(OC_VA(0x51F918u)))
#else
extern WORD guwTechUseDepIndex[44] OC_UNALIGNED;
#endif
#endif

#ifndef OC_guwUpgradeDepIndex
#define OC_guwUpgradeDepIndex
#ifdef OC_DLL_MODE
#define guwUpgradeDepIndex (reinterpret_cast<WORD *>(OC_VA(0x524BD0u)))
#else
extern WORD guwUpgradeDepIndex[61];
#endif
#endif

// glCounts_g is a DWORD[4][228][12], spelled here as count.cpp spells it.
// [1] is completed units, [0] existing.
#ifndef OC_GLCOUNTS_G
#define OC_GLCOUNTS_G
#ifdef OC_DLL_MODE
#define glCounts_g (reinterpret_cast<DWORD (*)[228][12]>(OC_VA(0x529604u)))
#else
extern PlayerUnitCountsGrid glCounts_g;
#endif
#endif

// 0x536720, the expansion gate, spelled here as gametype.cpp spells it.
#ifdef OC_DLL_MODE
#define gbIsExpansion (*reinterpret_cast<BYTE *>(OC_VA(0x536720u)))
#else
extern BYTE gbIsExpansion;
#endif

// 0x407570. Walk one record of a dependency tree. Each group is a list of
// requirements joined by 0xFF01 (or); a unit id counts that player's completed
// units, the 0xFF02..0xFF26 opcodes test the unit or the player. Refusals set
// the allowed-error code; 0 is refused and -1 is hidden. The cases are in the
// oracle's order, and 0xFF1C has to stay an early break: inside the switch the
// loop's reloads are hoisted into it and the jump table shifts.
int CheckAllowed(WORD nId, int nPlayer, CUnit *pUnit, const WORD *pTree, int nIndex)
{
  BOOL bGroupFail = 1;
  BOOL bNoUnitMatch = 1;
  BOOL bWrongUnit = 0;
  BOOL bSkipDisabled = 0;
  WORD wOp = pTree[nIndex];
  while (wOp != 0xFFFF) {
    int nCount = 0;
    for (;;) {
      switch (wOp) {
      case 0xFF24:
        if (!gbIsExpansion) {
          set_allowed_err(0x1A);
          return 0;
        }
        nCount++;
        break;
      case 0xFF02: {
        WORD wType = pTree[++nIndex];
        if (glCounts_g[1][wType][nPlayer] != 0)
          bGroupFail = 0;
        if (wType != pUnit->wType) {
          if (bNoUnitMatch)
            bWrongUnit = 1;
        } else {
          bWrongUnit = 0;
          nCount++;
          bNoUnitMatch = 0;
        }
        break;
      }
      case 0xFF03: {
        WORD wType = pTree[++nIndex];
        nCount += glCounts_g[1][wType][nPlayer] + glCounts_g[0][wType][nPlayer];
        break;
      }
      case 0xFF18:
        if (pUnit->IsATransport())
          nCount++;
        break;
      case 0xFF0F:
        if ((WORD)nId == 0x0B && pUnit->wType == 0x67) {
          nCount++;
          break;
        }
        if (!get_tech_researched(nPlayer, nId) && !(glCheatBits & 0x1000))
          break;
        nCount++;
        break;
      case 0xFF1C:
        if (!(gulUnitTypeFlags[pUnit->wType] & 0x40))
          break;
        nCount++;
        break;
      case 0xFF07:
        if (pUnit->IsConstructing() || pUnit->bTechType != 0x2C ||
            pUnit->bUpgradeType != 0x3D) {
          set_allowed_err(5);
          return 0;
        }
        nCount++;
        break;
      case 0xFF08:
        if (pUnit->IsConstructingAddon() || pUnit->IsBuildingProtoss()) {
          set_allowed_err(5);
          return 0;
        }
        nCount++;
        break;
      case 0xFF09:
        if (pUnit->bTechType != 0x2C) {
          set_allowed_err(5);
          return 0;
        }
        nCount++;
        break;
      case 0xFF0A:
        if (pUnit->bUpgradeType != 0x3D) {
          set_allowed_err(5);
          return 0;
        }
        nCount++;
        break;
      case 0xFF05:
        if (!(pUnit->dwFlags & 2)) {
          set_allowed_err(7);
          return 0;
        }
        nCount++;
        break;
      case 0xFF06:
        if (pUnit->dwFlags & 2) {
          set_allowed_err(6);
          return 0;
        }
        nCount++;
        break;
      case 0xFF0D:
        if (pUnit->pUnionD0 != 0) {
          set_allowed_err(8);
          return 0;
        }
        nCount++;
        break;
      case 0xFF11:
        if ((pUnit->dwFlags & 0x10) && pUnit->dwUnion134 == 0) {
          set_allowed_err(5);
          return 0;
        }
        nCount++;
        break;
      case 0xFF26:
        if (!(pUnit->dwFlags & 0x10)) {
          set_allowed_err(5);
          return 0;
        }
        nCount++;
        break;
      case 0xFF12:
        if (pUnit->dwFlags & 2) {
          set_allowed_err(0x13);
          return 0;
        }
        nCount++;
        break;
      case 0xFF13:
        if (!(pUnit->dwFlags & 2)) {
          set_allowed_err(0x13);
          return 0;
        }
        nCount++;
        break;
      case 0xFF14:
        if (!pUnit->CanMove()) {
          set_allowed_err(0x13);
          return 0;
        }
        nCount++;
        break;
      case 0xFF15:
        if (!pUnit->CanAttack()) {
          set_allowed_err(0x13);
          return 0;
        }
        nCount++;
        break;
      case 0xFF1A:
        if (!(gulUnitTypeFlags[pUnit->wType] & 0x10)) {
          set_allowed_err(0x13);
          return 0;
        }
        nCount++;
        break;
      case 0xFF17:
        if (!(gulUnitTypeFlags[pUnit->wType] & 0x20)) {
          set_allowed_err(0x13);
          return 0;
        }
        nCount++;
        break;
      case 0xFF16:
        if (!(gulUnitTypeFlags[pUnit->wType] & 8)) {
          set_allowed_err(0x13);
          return 0;
        }
        nCount++;
        break;
      case 0xFF19:
        if (!(gulUnitTypeFlags[pUnit->wType] & 0x800)) {
          set_allowed_err(0x13);
          return 0;
        }
        nCount++;
        break;
      case 0xFF1B:
        if (pUnit->bSpiderMineCount == 0) {
          set_allowed_err(8);
          return 0;
        }
        nCount++;
        break;
      case 0xFF1D:
        if (!pUnit->GetRClickAction()) {
          set_allowed_err(8);
          return 0;
        }
        nCount++;
        break;
      case 0xFF1E:
        bSkipDisabled = 1;
        nCount++;
        break;
      case 0xFF1F: {
        WORD wLevel;
        switch (get_upgrade_level(nPlayer, nId)) {
        case 1: wLevel = 0xFF20; break;
        case 2: wLevel = 0xFF21; break;
        default: wLevel = 0xFF1F; break;
        }
        while (pTree[nIndex] != wLevel)
          nIndex++;
        nCount++;
        break;
      }
      case 0xFF22:
        set_allowed_err(0x15);
        return -1;
      case 0xFF23:
        set_allowed_err(0x17);
        return 0;
      default:
        nCount += glCounts_g[1][wOp][nPlayer];
        break;
      }
      if (pTree[++nIndex] != 0xFF01)
        break;
      wOp = pTree[++nIndex];
    }
    if (bWrongUnit) {
      set_allowed_err(0x19);
      return 0;
    }
    if (nCount == 0 && bGroupFail) {
      set_allowed_err(8);
      return -1;
    }
    wOp = pTree[nIndex];
    bGroupFail = 1;
  }
  if ((pUnit->dwFlags & 0x40000000) && !bSkipDisabled) {
    set_allowed_err(0x16);
    return 0;
  }
  return 1;
}

// 0x407B10. A building is constructing if it is making an addon or is a protoss
// building still being warped in. GetBuildUnit(0)'s 0xE4 means nothing queued.
BOOL CUnit::IsConstructing()
{
  if (GetBuildUnit(0) != (WORD)0xE4 || IsConstructingAddon() ||
      IsBuildingProtoss())
    return 1;
  return 0;
}

// 0x407B50, CUnit::IsDisabled, is defined inline in Allowed.h.

// 0x407B80. Ghost, and the four hero and campaign variants of it.
BOOL CUnit::IsGhostType()
{
  int nType = wType;
  if (nType == 0x01 || nType == 0x10 || nType == 0x64 || nType == 0x63 ||
      nType == 0x68)
    return 1;
  return 0;
}

// 0x407BB0. Can this player give this order to this unit? Screen the id, the
// owner, the completed flag and the four disable timers, then the handful of
// unit-and-current-order pairs that lock an order out, then the tree.
int OrderAllowed(WORD nOrder, int nPlayer, CUnit *pUnit)
{
  set_allowed_err(0);
  if (!OrderTypeValid((WORD)nOrder)) {  // Hyperion: >= 0xBD, and a mod's orders
    set_allowed_err(0x0F);
    return 0;
  }
  if (guwOrdDepIndex[(WORD)nOrder] == 0xFFFF)
    OrderAllowedInit();

  if (nPlayer != pUnit->bPlayer) {
    set_allowed_err(1);
    return 0;
  }
  if ((WORD)nOrder != 0x27 && (WORD)nOrder != 0x28 && !(pUnit->dwFlags & 1)) {
    set_allowed_err(0x14);
    return 0;
  }
  if (pUnit->IsDisabled()) {
    set_allowed_err(0x0A);
    return 0;
  }
  if ((pUnit->dwFlags & 0x10) && pUnit->wType != 0x67 && (WORD)nOrder != 0x76) {
    set_allowed_err(0x0B);
    return 0;
  }
  if (pUnit->wType == 0x07 && pUnit->bOrderId == 0x21 && (WORD)nOrder != 1) {
    set_allowed_err(8);
    return 0;
  }
  if ((pUnit->IsGhostType() && pUnit->bOrderId == 0x81) ||
      (pUnit->wType == 0x44 && pUnit->bOrderId == 0x6A) ||
      (pUnit->wType == 0x3F && pUnit->bOrderId == 0x6A)) {
    set_allowed_err(8);
    return 0;
  }

  int nIndex = guwOrdDepIndex[(WORD)nOrder];
  if (nIndex == 0) {
    set_allowed_err(0x17);
    return 0;
  }
  return CheckAllowed(nOrder, nPlayer, pUnit, sgOrderTree, nIndex);
}

// 0x407D50. Walk the order tree once, recording where each order's record
// starts, then turn every slot nothing wrote into 0.
void OrderAllowedInit()
{
  int i = 1;
  while (sgOrderTree[i] != 0xFFFF) {
    guwOrdDepIndex[sgOrderTree[i]] = (WORD)(i + 1);
    while (sgOrderTree[i] != 0xFFFF)
      i++;
    i++;
  }

  for (int j = 0; j < 189; j++)
    if (guwOrdDepIndex[j] == 0xFFFF)
      guwOrdDepIndex[j] = 0;
}

// 0x407DC0. The research gate. The cheat bit both waives the availability check
// and forces the already-researched refusal.
int ResearchAllowed(int nResearch, int nPlayer, CUnit *pUnit)
{
  set_allowed_err(0);
  if (!TechTypeValid((WORD)nResearch)) {  // Hyperion: and a mod's past 1.17's rows
    set_allowed_err(0x11);
    return 0;
  }
  if (!TechTypeMod((WORD)nResearch) && guwTechDepIndex[(WORD)nResearch] == 0xFFFF)  // a mod's has no row
    ResearchAllowedInit();
  // Hyperion: the tech mutator's "no research" leaves nothing to research, as a tech the
  // map makes unavailable
  if (MutatorResearchForbidden()) {
    set_allowed_err(2);
    return 0;
  }

  if (nPlayer != pUnit->bPlayer) {
    set_allowed_err(1);
    return 0;
  }
  if (!(pUnit->dwFlags & 1)) {
    set_allowed_err(0x14);
    return 0;
  }
  if (pUnit->IsDisabled()) {
    set_allowed_err(0x0A);
    return 0;
  }
  // Hyperion: a mod's tech, in one of 1.17's unused rows, has its own research
  // requirements and is available to every player; one in progress or researched is
  // refused as 1.17's are (ModTechResearchTree)
  if (const WORD *pModTree = ModTechResearchTree((WORD)nResearch)) {
    if (is_tech_in_progress((tech_id_t)nResearch, nPlayer)) {
      set_allowed_err(0x0C);
      return 0;
    }
    if (get_tech_researched(nPlayer, (WORD)nResearch)) {
      set_allowed_err(0x0D);
      return 0;
    }
    return CheckAllowed(nResearch, nPlayer, pUnit, pModTree, 1);
  }
  if (!get_tech_available(nPlayer, (WORD)nResearch) &&
      !(glCheatBits & 0x1000)) {
    set_allowed_err(2);
    return 0;
  }
  if (is_tech_in_progress((tech_id_t)nResearch, nPlayer)) {
    set_allowed_err(0x0C);
    return 0;
  }
  if (get_tech_researched(nPlayer, (WORD)nResearch) ||
      (glCheatBits & 0x1000)) {
    set_allowed_err(0x0D);
    return 0;
  }

  // Hyperion: one of 1.17's techs a mod's edit gives research requirements has the
  // edit's, after 1.17's other checks (ModTreeEdit)
  if (const WORD *pEditTree = ModTreeEdit(MOD_TREE_TECH_RESEARCH, (WORD)nResearch))
    return CheckAllowed(nResearch, nPlayer, pUnit, pEditTree, 1);
  int nIndex = TechTypeMod((WORD)nResearch) ? 0 : guwTechDepIndex[(WORD)nResearch];  // Hyperion
  if (nIndex == 0) {
    set_allowed_err(0x17);
    return 0;
  }
  return CheckAllowed(nResearch, nPlayer, pUnit, sgResearchTree, nIndex);
}

// 0x407F20. OrderAllowedInit over the research tree.
void ResearchAllowedInit()
{
  int i = 1;
  while (sgResearchTree[i] != 0xFFFF) {
    guwTechDepIndex[sgResearchTree[i]] = (WORD)(i + 1);
    while (sgResearchTree[i] != 0xFFFF)
      i++;
    i++;
  }

  for (int j = 0; j < 44; j++)
    if (guwTechDepIndex[j] == 0xFFFF)
      guwTechDepIndex[j] = 0;
}

// 0x407F90. The ability gate. dwFlags is read once and tested twice, across the
// IsDisabled call, so it sits in a callee-saved register.
int TechUseAllowed(int nTech, int nPlayer, CUnit *pUnit)
{
  set_allowed_err(0);
  if (!TechTypeValid((WORD)nTech)) {  // Hyperion: and a mod's past 1.17's rows
    set_allowed_err(0x11);
    return 0;
  }
  if (!TechTypeMod((WORD)nTech) && guwTechUseDepIndex[(WORD)nTech] == 0xFFFF)  // a mod's has no row
    TechUseAllowedInit();

  if (nPlayer != pUnit->bPlayer) {
    set_allowed_err(1);
    return 0;
  }
  if (!(pUnit->dwFlags & 1)) {
    set_allowed_err(0x14);
    return 0;
  }
  if (pUnit->IsDisabled()) {
    set_allowed_err(0x0A);
    return 0;
  }
  if (pUnit->dwFlags & 0x40000000) {
    set_allowed_err(0x16);
    return 0;
  }
  // Hyperion: a mod's tech, in one of 1.17's unused rows, has its own use
  // requirements and is available to every player (ModTechUseTree)
  if (const WORD *pModTree = ModTechUseTree((WORD)nTech))
    return CheckAllowed(nTech, nPlayer, pUnit, pModTree, 1);
  if (!get_tech_available(nPlayer, (WORD)nTech) && !(glCheatBits & 0x1000)) {
    set_allowed_err(2);
    return 0;
  }
  // Hyperion: and one of 1.17's a mod's edit gives use requirements has the edit's
  // (ModTreeEdit)
  if (const WORD *pEditTree = ModTreeEdit(MOD_TREE_TECH_USE, (WORD)nTech))
    return CheckAllowed(nTech, nPlayer, pUnit, pEditTree, 1);

  int nIndex = TechTypeMod((WORD)nTech) ? 0 : guwTechUseDepIndex[(WORD)nTech];  // Hyperion
  if (nIndex == 0) {
    set_allowed_err(0x17);
    return 0;
  }
  return CheckAllowed(nTech, nPlayer, pUnit, sgTechUseTree, nIndex);
}

// 0x4080D0. OrderAllowedInit over the tech-use tree.
void TechUseAllowedInit()
{
  int i = 1;
  while (sgTechUseTree[i] != 0xFFFF) {
    guwTechUseDepIndex[sgTechUseTree[i]] = (WORD)(i + 1);
    while (sgTechUseTree[i] != 0xFFFF)
      i++;
    i++;
  }

  for (int j = 0; j < 44; j++)
    if (guwTechUseDepIndex[j] == 0xFFFF)
      guwTechUseDepIndex[j] = 0;
}

// 0x408140. The upgrade gate. The level test is the only one of the four that
// compares two calls rather than a field.
int UpgradeAllowed(int nUpgrade, int nPlayer, CUnit *pUnit)
{
  set_allowed_err(0);
  if (!UpgradeTypeValid((WORD)nUpgrade)) {  // Hyperion: and a mod's past 1.17's rows
    set_allowed_err(0x12);
    return 0;
  }
  if (!UpgradeTypeMod((WORD)nUpgrade) && guwUpgradeDepIndex[(WORD)nUpgrade] == 0xFFFF)  // a mod's has no row
    UpgradeAllowedInit();
  // Hyperion: the tech mutator's "no upgrades", as an upgrade at its top level
  if (MutatorUpgradesForbidden()) {
    set_allowed_err(0x0E);
    return 0;
  }

  if (nPlayer != pUnit->bPlayer) {
    set_allowed_err(1);
    return 0;
  }
  if (!(pUnit->dwFlags & 1)) {
    set_allowed_err(0x14);
    return 0;
  }
  if (pUnit->IsDisabled()) {
    set_allowed_err(0x0A);
    return 0;
  }
  if (get_upgrade_level(nPlayer, (WORD)nUpgrade) >=
      get_max_upgrade_level(nPlayer, (WORD)nUpgrade)) {
    set_allowed_err(0x0E);
    return 0;
  }
  if (is_upgrade_in_progress((upgrade_id_t)nUpgrade, nPlayer)) {
    set_allowed_err(0x0C);
    return 0;
  }
  // Hyperion: a mod's upgrade, in one of 1.17's unused rows, has its own requirements
  // (ModUpgradeTree)
  if (const WORD *pModTree = ModUpgradeTree((WORD)nUpgrade))
    return CheckAllowed(nUpgrade, nPlayer, pUnit, pModTree, 1);
  // Hyperion: and one of 1.17's a mod's edit gives requirements has the edit's
  // (ModTreeEdit)
  if (const WORD *pEditTree = ModTreeEdit(MOD_TREE_UPGRADE, (WORD)nUpgrade))
    return CheckAllowed(nUpgrade, nPlayer, pUnit, pEditTree, 1);

  int nIndex = UpgradeTypeMod((WORD)nUpgrade) ? 0 : guwUpgradeDepIndex[(WORD)nUpgrade];  // Hyperion
  if (nIndex == 0) {
    set_allowed_err(0x17);
    return 0;
  }
  return CheckAllowed(nUpgrade, nPlayer, pUnit, sgUpgradeTree, nIndex);
}

// 0x408280. The upgrade tree's index is the only one bounds-checked on the way
// in, so a record naming an id past the table cannot write past it.
void UpgradeAllowedInit()
{
  int i = 1;
  while (sgUpgradeTree[i] != 0xFFFF) {
    if (sgUpgradeTree[i] < 61)
      guwUpgradeDepIndex[sgUpgradeTree[i]] = (WORD)(i + 1);
    while (sgUpgradeTree[i] != 0xFFFF)
      i++;
    i++;
  }

  for (int j = 0; j < 61; j++)
    if (guwUpgradeDepIndex[j] == 0xFFFF)
      guwUpgradeDepIndex[j] = 0;
}
