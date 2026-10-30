#ifndef OC117_ALLOWED_H
#define OC117_ALLOWED_H

#include "platform.h"
#include "CUnit.h"

// src117/lang/Allowed.cpp -- the tech-tree gate. OrderAllowed, ResearchAllowed,
// TechUseAllowed and UpgradeAllowed are declared in statbtn.h; only the names
// below are new.

// 0x407570. The tree walker the four Allowed entry points delegate to.
// `ret 0Ch`: ECX and EDX carry the id and the player, then three stack words.
// The last of those is written as well as read, so it is not const. The id is a
// WORD, as OrderAllowed's is.
int CheckAllowed(WORD nId, int nPlayer, CUnit *pUnit,
                 const WORD *pTree, int nIndex);

// 0x407D50, 0x407F20, 0x4080D0, 0x408280. Each fills its dependency index from
// its tree on first use, then rewrites every 0xFFFF slot to 0.
void OrderAllowedInit();
void ResearchAllowedInit();
void TechUseAllowedInit();
void UpgradeAllowedInit();

// 0x407B50. Lockdown, stasis, maelstrom or the disabled flag: any one of them
// takes the unit out of play. The body is inline here because CUnitCombat.cpp's
// AIPickAttackTarget compiles against it, and seeing that the call writes nothing
// lets the attacker value cross it. gamesnd.cpp must not see the body, so only
// Allowed.cpp and CUnitCombat.cpp include this header; everyone else gets the
// declaration in CUnit.h.
OC_EMIT inline BOOL CUnit::IsDisabled()
{
  if ((dwFlags & 0x400) != 0 || bLockdownTimer != 0 || bStasisTimer != 0 ||
      bParalyzeTimer != 0)
    return 1;
  return 0;
}

#endif
