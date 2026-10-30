// TAI_GuardPost.h -- the guard-post record, its pool and the guard accessors
// src117 code calls. The bodies are in src117/lang/TAI_GuardPost.cpp.
#pragma once

#include "platform.h"
#include "CUnit.h"   // TGuard::PreSave / PostLoad
#include "pos.h"     // Hyperion: upos_t (stage E)
#include "aipools.h" // Hyperion: AI_RECORDS_STORE

// TGuard -- the guard-post record. The stride is 32 (guard_ptr's `shl ecx, 5h`
// against guard_id's `shr ecx, 5h`), and the ids are 1-based: 0x6B68E8 is
// &gGuards_g[-1], the bias guard_ptr folds into its base. The record is
// complete here because the pool's specialization needs it.
struct TGuard {
  /*0x00*/ TGuard *pNext;    // guard_free_unmanned walks `mov eax, dword ptr [eax]`
  /*0x04*/ TGuard *pPrev;    // guard_free_unmanned unlinks through both directions
  /*0x08*/ BYTE    bType;    // `cmp byte ptr [ecx+8h], 1h`
  /*0x09*/ BYTE    bTries;   // `mov cl, byte ptr [eax+9h]`, capped at 100
  /*0x0A*/ WORD    _pad0A;
  /*0x0C*/ CUnit  *pUnit;    // guard_crew_post stores EDX here and EDX's +0x134
  /*0x10*/ WORD    wType;    // `movzx edx, word ptr [eax+10h]`, a unit type
  // Hyperion: the four positions are 4 bytes each on x64 (h/pos.h, stage E)
  /*0x12*/ upos_t  wPosX;    // `mov word ptr [ecx+12h], dx`
  /*0x14*/ upos_t  wPosY;
  /*0x16*/ upos_t  wHomeX;   // guard_crew_post copies +0x16 back into +0x12
  /*0x18*/ upos_t  wHomeY;
  /*0x1A*/ WORD    _pad1A;
  /*0x1C*/ DWORD   dwFlag;   // guard_wake_posts_of_type tests it and stores 1

  // PreSave and PostLoad convert pUnit to and from a unit id. Clear below skips
  // the links, bTries and the pads.
  void PreSave() { pUnit = (CUnit *)(intptr_t)pUnit->GetUnitID(); }
  void PostLoad() { pUnit = CUnit::FromId((DWORD)(DWORD_PTR)pUnit); }

  void Clear() {
    bType = 0;
    pUnit = NULL;
    wType = 0;
    wPosX = 0;
    wPosY = 0;
    wHomeX = 0;
    wHomeY = 0;
    dwFlag = 0;
  }
};
static_assert(offsetof(TGuard, pNext)  == 0x00, "TGuard.pNext  [eax]");
OC_X86_LAYOUT(offsetof(TGuard, pPrev)  == 0x04, "TGuard.pPrev  [eax+4h]");
OC_X86_LAYOUT(offsetof(TGuard, bType)  == 0x08, "TGuard.bType  [eax+8h] BYTE");
OC_X86_LAYOUT(offsetof(TGuard, bTries) == 0x09, "TGuard.bTries [eax+9h] BYTE");
OC_X86_LAYOUT(offsetof(TGuard, pUnit)  == 0x0C, "TGuard.pUnit  [eax+0Ch]");
OC_X86_LAYOUT(offsetof(TGuard, wType)  == 0x10, "TGuard.wType  [eax+10h] WORD");
OC_X86_LAYOUT(offsetof(TGuard, wPosX)  == 0x12, "TGuard.wPosX  [ecx+12h] WORD");
OC_X86_LAYOUT(offsetof(TGuard, wPosY)  == 0x14, "TGuard.wPosY  [ecx+14h] WORD");
OC_X86_LAYOUT(offsetof(TGuard, wHomeX) == 0x16, "TGuard.wHomeX [ecx+16h] WORD");
OC_X86_LAYOUT(offsetof(TGuard, wHomeY) == 0x18, "TGuard.wHomeY [ecx+18h] WORD");
OC_X86_LAYOUT(offsetof(TGuard, dwFlag) == 0x1C, "TGuard.dwFlag [eax+1Ch]");
OC_X86_LAYOUT(sizeof(TGuard) == 0x20, "TGuard stride (guard_ptr x32 / guard_id /32)");

// The pool. tai_guardpost_init hands 0x6B6908 to TSaveNode<TGuard,1000>::Init,
// and the free head sits immediately past the array, at `[esi+7D00h]` = 1000*32.
// The specialization is spelled as the ledger spells it.
template <class T, int N> struct TSaveNode;
template <> struct TSaveNode<TGuard,1000> {
  /*0x0000*/ TGuard  aNodes[AI_RECORDS_STORE];  // Hyperion: 1000 in 1.17
  /*0x7D00*/ TGuard *pFree;

  void Init();
  void PreSave();
  void PostLoad();

  // The save form of a link is a 1-based index. Both conversions keep the
  // null and are written out of `this`, not out of the array's address.
  TGuard *ToIndex(TGuard *p) const {
    if (p == NULL)
      return NULL;

    return (TGuard *)(((DWORD_PTR)p - (DWORD_PTR)this) / sizeof(TGuard) + 1);
  }

  TGuard *FromIndex(TGuard *p) const {
    if (p == NULL)
      return NULL;

    return (TGuard *)((DWORD_PTR)p * sizeof(TGuard) - sizeof(TGuard) + (DWORD_PTR)this);
  }
};

typedef TSaveNode<TGuard, 1000> TGuardPool;
static_assert(offsetof(TGuardPool, aNodes) == 0x0000, "TGuardPool.aNodes");
OC_X86_LAYOUT(offsetof(TGuardPool, pFree) == 0x7D00, "TGuardPool.pFree [esi+7D00h]");
OC_X86_LAYOUT(sizeof(TGuardPool) == 0x7D04, "TGuardPool = TGuard[1000] + head");

// One per player. The stride is 8 and the extent 0x6BE610..0x6BE650 gives the
// count.
struct TPlayerGuards {
  /*0x00*/ TGuardPool *pPool;
  /*0x04*/ TGuard     *pHead;
};
static_assert(offsetof(TPlayerGuards, pPool) == 0x00, "TPlayerGuards.pPool");
OC_X86_LAYOUT(offsetof(TPlayerGuards, pHead) == 0x04, "TPlayerGuards.pHead");
OC_X86_LAYOUT(sizeof(TPlayerGuards) == 8, "TPlayerGuards stride ([ecx*8+...])");

// guard_ptr -- 0x4B25D0, 17 B. `TGuard *`, not int: the value is a biased array
// address, materialised at both exits (`xor eax, eax` on the null path,
// `lea eax, [ecx+6B68E8h]` on the other).
TGuard *guard_ptr(int nId);

// guard_id -- 0x4B2590, 20 B. The inverse. The argument is a pointer, not an
// int: `sub ecx, 6B6908h` subtracts the pool base from it before the shift.
int guard_id(TGuard *pGuard);

// tai_guardpost_init -- 0x4B2640, 171 B. Called by sai_init. Nullary, void.
void tai_guardpost_init();

// sai_read_guard_data -- 0x4B1C10, 340 B / sai_write_guard_data -- 0x4B1D70,
// 617 B. The save codec pair: the file in ECX, and each result consumed as a
// full dword by read_save_ai_data / sai_write_save_data.
int sai_read_guard_data(intptr_t nFile);
int sai_write_guard_data(intptr_t nFile);

// TAI_GuardPost_RemoveUnit -- 0x4B26F0, 45 B. Called by SAI_RemoveUnit and
// aiscript_switch_rescue. `CUnit *` from the call sites, as above.
BOOL TAI_GuardPost_RemoveUnit(CUnit *pUnit);

// 0x4B1A30, 44 B. ECX is the unit; both consuming callers (in TAI_Captain.cpp)
// use the full EAX, and every ret materialises 0 or 1.
BOOL guard_unit_is_soldier(CUnit *pUnit);

// 0x4B25B0, 26 B. Both registers are dereferenced pointers; EAX at the ret is
// an incidental leftover, so void.
void guard_crew_post(TGuard *pGuard, CUnit *pUnit);

// 0x4B25F0, 80 B. ECX and EDX read 32-bit; its one consuming caller uses the
// full EAX and every ret materialises 0 or 1.
BOOL guard_needs_type(int nPlayer, int nType);

// 0x4B2720, 48 B. ECX is a dereferenced pointer; one consuming caller, 0 or 1.
BOOL guard_is_at_post(CUnit *pUnit);

// 0x4B2750, 66 B. ECX read 32-bit, EDX 16-bit.
void guard_wake_posts_of_type(int nPlayer, WORD wType);

// 0x4B27F0, 56 B. `ret 8h` -- two register arguments and two stack words.
void TAI_GuardPost_aiscript_place_guard(int nPlayer, intptr_t nTown, int nType,
                                        int nArg3);
