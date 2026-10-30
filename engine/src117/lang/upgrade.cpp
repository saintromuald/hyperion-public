// upgrade.cpp -- the two enumerators that walk a unit's tech and upgrade
// tables and hand each entry to a caller-supplied callback. Bodies are in
// oracle VA order.
//
// Both tables are file-scope constants; their extents are the walks' own, read
// off the loop bounds.

#include "../h/upgrade.h"
#include "../h/CUnit.h"      // CUnit::wType, CUnit::bPlayer
#include "../h/saveload.h"   // get_tech_researched, get_upgrade_level
#include "../gen/globals117.h"

// 0x4D0076, a Storm import stub, declared as src117/lang/maphdr.cpp declares it.
extern "C" void __stdcall SErrSetLastError(DWORD dwErrCode);

// The tech table: for each unit type, the technologies its panel offers,
// terminated by 44. Twenty-three rows of six WORDs, the first the unit type.
// The walk starts at row 23's second WORD and stops when it reaches row 0's.
struct techrec_t {
  /*0x00*/ WORD wUnit;
  /*0x02*/ WORD wIds[5];
};

static const techrec_t sgTechByUnit_g[23] = {
  { 0, { 0, 44, 0, 0, 0 } },
  { 32, { 0, 44, 0, 0, 0 } },
  { 9, { 6, 7, 2, 44, 0 } },
  { 1, { 1, 10, 44, 0, 0 } },
  { 2, { 3, 44, 0, 0, 0 } },
  { 5, { 5, 44, 0, 0, 0 } },
  { 30, { 5, 44, 0, 0, 0 } },
  { 8, { 9, 44, 0, 0, 0 } },
  { 12, { 8, 44, 0, 0, 0 } },
  { 45, { 12, 18, 13, 17, 44 } },
  { 37, { 11, 44, 0, 0, 0 } },
  { 38, { 11, 44, 0, 0, 0 } },
  { 41, { 11, 44, 0, 0, 0 } },
  { 50, { 11, 44, 0, 0, 0 } },
  { 46, { 11, 14, 15, 16, 44 } },
  { 67, { 23, 19, 20, 44, 0 } },
  { 71, { 21, 22, 44, 0, 0 } },
  { 34, { 34, 24, 30, 44, 0 } },
  { 60, { 25, 44, 0, 0, 0 } },
  { 61, { 28, 44, 0, 0, 0 } },
  { 63, { 27, 29, 31, 44, 0 } },
  { 103, { 32, 44, 0, 0, 0 } },
  { 38, { 32, 44, 0, 0, 0 } },
};

// The upgrade twin, terminated by 61. Twenty-five rows of five WORDs.
struct upgraderec_t {
  /*0x00*/ WORD wUnit;
  /*0x02*/ WORD wIds[4];
};

static const upgraderec_t sgUpgradesByUnit_g[25] = {
  { 0, { 16, 61, 0, 0 } },
  { 2, { 17, 61, 0, 0 } },
  { 9, { 19, 61, 0, 0 } },
  { 1, { 20, 21, 61, 0 } },
  { 8, { 22, 61, 0, 0 } },
  { 12, { 23, 61, 0, 0 } },
  { 42, { 24, 25, 26, 61 } },
  { 37, { 27, 28, 61, 0 } },
  { 38, { 29, 30, 61, 0 } },
  { 45, { 31, 61, 0, 0 } },
  { 46, { 32, 61, 0, 0 } },
  { 66, { 33, 61, 0, 0 } },
  { 65, { 34, 61, 0, 0 } },
  { 83, { 35, 36, 61, 0 } },
  { 69, { 37, 61, 0, 0 } },
  { 84, { 38, 39, 61, 0 } },
  { 67, { 40, 61, 0, 0 } },
  { 70, { 41, 42, 61, 0 } },
  { 72, { 43, 61, 0, 0 } },
  { 71, { 44, 61, 0, 0 } },
  { 3, { 54, 61, 0, 0 } },
  { 39, { 53, 52, 61, 0 } },
  { 34, { 51, 61, 0, 0 } },
  { 63, { 49, 61, 0, 0 } },
  { 60, { 47, 61, 0, 0 } },
};

// Hand every technology the unit's type offers to the callback, together with
// whether the unit's player has researched it, and stop as soon as it answers
// zero. The table is walked from the last row down to the first.
void __stdcall sub_4BBCB0(CUnit *pUnit, PFN_TECH_ENUM pfnEnum, int nCtx)
{
  const WORD *pRowIds;
  const WORD *pId;
  unsigned i;

  if (!((pUnit ? -1 : 0) & (pfnEnum ? -1 : 0))) {
    SErrSetLastError(0x57);
    return;
  }

  pRowIds = sgTechByUnit_g[0].wIds + 23 * 6;
  do {
    pRowIds -= 6;
    if (pRowIds[-1] == pUnit->wType) {
      for (i = 1, pId = pRowIds; i < 6; i++) {
        WORD wId = *pId;
        if (wId == 44)
          break;
        if (!pfnEnum(pUnit, wId, get_tech_researched(pUnit->bPlayer, wId), nCtx))
          return;
        pId++;
      }
    }
  } while (pRowIds != sgTechByUnit_g[0].wIds);
}

// The upgrade twin of sub_4BBCB0: same walk, same guards, get_upgrade_level in
// place of get_tech_researched and 61 as the row terminator.
void __stdcall sub_4BBD40(CUnit *pUnit, PFN_TECH_ENUM pfnEnum, int nCtx)
{
  const WORD *pRowIds;
  const WORD *pId;
  unsigned i;

  if (!((pUnit ? -1 : 0) & (pfnEnum ? -1 : 0))) {
    SErrSetLastError(0x57);
    return;
  }

  pRowIds = sgUpgradesByUnit_g[0].wIds + 25 * 5;
  do {
    pRowIds -= 5;
    if (pRowIds[-1] == pUnit->wType) {
      for (i = 1, pId = pRowIds; i < 5; i++) {
        WORD wId = *pId;
        if (wId == 61)
          break;
        if (!pfnEnum(pUnit, wId, get_upgrade_level(pUnit->bPlayer, wId), nCtx))
          return;
        pId++;
      }
    }
  } while (pRowIds != sgUpgradesByUnit_g[0].wIds);
}
