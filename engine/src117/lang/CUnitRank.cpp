// CUnitRank.cpp -- the rank/title string a unit shows in the status console.
// Bodies are in oracle VA order.
//
// A callee defined here can be spliced into a caller here by /Ob2, which the oracle (WPO
// off) never does across a TU; that is what the __declspec(noinline) markers on some
// declarations are for, and why moving a body between TUs can change the code generated.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/CUnitRank.h"
#include "../h/CUnit.h"
#include "../h/order.h"          // ours: status_get_str
#include "../gen/globals117.h"

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one over the
// block, so adding a name cannot undeclare the ones below it.

// Rank base per unit type, added to the unit's own +0x8E counter. BYTE element
// and unbiased: `movzx eax, word ptr [esi+64h]` then `mov al, byte ptr
// [eax+520C68h]`. Extent 228 is units.dat's row count, as for its neighbours.
#ifndef OC_gubUnitBaseRank
#define OC_gubUnitBaseRank
#ifdef OC_DLL_MODE
#define gubUnitBaseRank (reinterpret_cast<BYTE *>(OC_VA(0x520C68u)))
#else
extern BYTE gubUnitBaseRank[228] OC_UNALIGNED;
#endif
#endif

// WORD element and unbiased off wType: `cmp word ptr [ecx*2+524458h], 0h` with
// ecx holding the type id. The 0x52 arm reads the same array at a folded index
// -- 0x524458 + 0x52*2 is 0x5244FC, not a scalar of its own.
#ifndef OC_guwUnitCustomName
#define OC_guwUnitCustomName
#ifdef OC_DLL_MODE
#define guwUnitCustomName (reinterpret_cast<WORD *>(OC_VA(0x524458u)))
#else
extern WORD guwUnitCustomName[228] OC_UNALIGNED;
#endif
#endif

// The bodies, in oracle VA order.

// 0x431160. The rank string for this unit, or NULL when its type has none.
char *CUnit::GetRankStr()
{
  DWORD dwTypeFlags = gulUnitTypeFlags[wType];

  if ((dwTypeFlags & 0x800) == 0 && wType != 0x0F && wType != 0x0D) {
    if (wType == 0x52) {
      if (guwUnitCustomName[0x52] == 0)
        return status_get_str(0x23C);
    } else {
      if ((wType == 0x0C || wType == 0x1D || wType == 0x1C) &&
          (dwTypeFlags & 0x40) && guwUnitCustomName[wType] != 0)
        return status_get_str(0x23B);

      // +0x8E is inside CUnit's _unmapped_86 run; the oracle adds it to the
      // per-type base at BYTE width before the 16-bit string-id offset.
      switch (GetRace()) {
      case 1:
        return status_get_str((WORD)((BYTE)(gubUnitBaseRank[wType] +
                                            _unmapped_86[8]) + 0x516));
      }
    }
  }

  return NULL;
}
